"""Inspect backed-up MT7922 or MT7925 Connac2 firmware without hardware access."""

import argparse
import hashlib
import json
from pathlib import Path
from struct import Struct

from firmware_boot import FW_FEATURE_NON_DL, PCI_DMA_PAGE_BYTES, pack_driver_image, scatter_packets

# On-disk layout documented by mt76_connac_mcu.h.
PATCH_HEADER = Struct(">16s4sI4xHH12xI48x")
PATCH_REGION = Struct(">7I36x")
RAM_TRAILER = Struct("<5B2x10s15sI")
RAM_REGION = Struct("<16x2I2B14x")
FIRMWARE_NAMES = {
    "mt7922": ("WIFI_MT7922_patch_mcu_1_1_hdr.bin", "WIFI_RAM_CODE_MT7922_1.bin"),
    "mt7925": ("WIFI_MT7925_PATCH_MCU_1_1_hdr.bin", "WIFI_RAM_CODE_MT7925_1_1.bin"),
}

# mt7921.h CLC container and mt7921_clc_rule, little endian.
FW_TYPE_CLC = 2
CLC_ENTRY = Struct("<IBBBB8x")
CLC_RULE = Struct("<2s2sH")


def ascii_field(field):
    return field.rstrip(b"\x00\r\n ").decode("ascii")


def inspect_rom(data):
    if len(data) < PATCH_HEADER.size:
        raise ValueError("ROM patch header is truncated")
    date, platform, hw_sw_version, _, _, count = PATCH_HEADER.unpack_from(data)
    table_end = PATCH_HEADER.size + PATCH_REGION.size * count
    if not count or table_end > len(data):
        raise ValueError("ROM patch section table is truncated or empty")

    regions = []
    for index in range(count):
        entry = PATCH_HEADER.size + PATCH_REGION.size * index
        kind, offset, size, address, length, security, alignment = PATCH_REGION.unpack_from(data, entry)
        if (kind & 0xFFFF) != 2:
            raise ValueError(f"Unsupported ROM patch section type {kind:#x}")
        if offset < table_end or offset + size > len(data) or length + alignment > size:
            raise ValueError(f"ROM patch section {index} exceeds the image bounds")
        regions.append({
            "offset": offset, "length": length, "target": f"0x{address:08x}",
            "security": f"0x{security:08x}", "alignment_bytes": alignment,
        })
    return {
        "file_bytes": len(data), "build": ascii_field(date), "platform": ascii_field(platform),
        "hw_sw_version": f"0x{hw_sw_version:08x}", "regions": regions,
    }


def inspect_ram(data):
    if len(data) < RAM_TRAILER.size:
        raise ValueError("RAM image trailer is truncated")
    trailer_offset = len(data) - RAM_TRAILER.size
    _, _, count, version, flags, firmware, date, _ = RAM_TRAILER.unpack_from(data, trailer_offset)
    table_offset = trailer_offset - RAM_REGION.size * count
    if version != 2 or not count or table_offset < 0:
        raise ValueError("Unsupported RAM image format or truncated region table")

    offset = 0
    regions = []
    for index in range(count):
        address, length, features, region_type = RAM_REGION.unpack_from(
            data, table_offset + RAM_REGION.size * index
        )
        if offset + length > table_offset:
            raise ValueError(f"RAM region {index} overlaps the region table")
        regions.append({
            "offset": offset, "length": length, "target": f"0x{address:08x}",
            "features": f"0x{features:02x}", "type": region_type,
        })
        offset += length
    return {
        "file_bytes": len(data), "build": ascii_field(date), "firmware_version": ascii_field(firmware),
        "format_flags": flags, "pre_table_metadata_bytes": table_offset - offset,
        "regions": regions,
    }


def describe_scatter(data, regions, max_payload, is_patch):
    for region in regions:
        if not is_patch and int(region["features"], 16) & FW_FEATURE_NON_DL:
            region["scatter"] = None
            continue
        packets = scatter_packets(data, region["offset"], region["length"], max_payload)
        sizes = [len(packet) for packet in packets]
        region["scatter"] = {
            "packet_count": len(sizes), "payload_bytes": sum(sizes),
            "last_packet_bytes": sizes[-1],
        }


def find_clc_region(ram, ram_info):
    for region in ram_info["regions"]:
        if (int(region["features"], 16) & FW_FEATURE_NON_DL) and region["type"] == FW_TYPE_CLC:
            return ram[region["offset"]:region["offset"] + region["length"]]
    raise ValueError("RAM image contains no non-downloadable CLC region")


def validate_clc(data):
    entries = []
    offset = 0
    while offset + CLC_ENTRY.size <= len(data):
        length, index, version, countries, entry_type = CLC_ENTRY.unpack_from(data, offset)
        if length < CLC_ENTRY.size or offset + length > len(data):
            raise ValueError(f"CLC entry at {offset} exceeds the region")
        position = offset + CLC_ENTRY.size
        end = offset + length
        for _ in range(countries):
            if position + CLC_RULE.size > end:
                raise ValueError(f"CLC entry at {offset} has a truncated rule table")
            _, _, rule_bytes = CLC_RULE.unpack_from(data, position)
            if position + CLC_RULE.size + rule_bytes > end:
                raise ValueError(f"CLC rule at {position} exceeds its entry")
            position += CLC_RULE.size + rule_bytes
        entries.append({
            "offset": offset, "bytes": length, "index": index,
            "version": version, "countries": countries, "type": entry_type,
        })
        offset += length
    if not entries:
        raise ValueError("CLC region has no entries")
    return entries


def read_firmware(backup, name):
    matches = list(backup.rglob(name))
    if len(matches) != 1:
        raise ValueError(f"Expected one {name} under {backup}, found {len(matches)}")
    return matches[0].read_bytes()


def main():
    project = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--chip", choices=FIRMWARE_NAMES, default="mt7922")
    parser.add_argument(
        "backup", type=Path, nargs="?", default=project / "backup" / "mediatek-3.5.2.1376"
    )
    parser.add_argument("--pack-image", type=Path, help="Save the local driver boot payload")
    parser.add_argument("--pack-clc", type=Path, help="Save the firmware CLC region for SET_CLC")
    args = parser.parse_args()
    try:
        rom_name, ram_name = FIRMWARE_NAMES[args.chip]
        rom = read_firmware(args.backup, rom_name)
        ram = read_firmware(args.backup, ram_name)
        rom_info = inspect_rom(rom)
        ram_info = inspect_ram(ram)
        describe_scatter(rom, rom_info["regions"], PCI_DMA_PAGE_BYTES,
                         is_patch=True)
        describe_scatter(ram, ram_info["regions"], PCI_DMA_PAGE_BYTES, is_patch=False)
        result = {"rom_patch": rom_info, "ram_image": ram_info}
        if args.pack_image is not None:
            image = pack_driver_image(rom, rom_info, ram, ram_info)
            args.pack_image.write_bytes(image)
            result["driver_image"] = {
                "path": str(args.pack_image), "bytes": len(image),
                "sha256": hashlib.sha256(image).hexdigest(),
            }
        if args.pack_clc is not None:
            clc = find_clc_region(ram, ram_info)
            entries = validate_clc(clc)
            args.pack_clc.write_bytes(clc)
            result["clc_image"] = {
                "path": str(args.pack_clc), "bytes": len(clc),
                "sha256": hashlib.sha256(clc).hexdigest(), "entries": entries,
            }
    except (OSError, ValueError) as error:
        parser.exit(1, f"Firmware inspection failed: {error}\n")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
