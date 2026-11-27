"""Extract MT7925 Wi-Fi firmware files from a local MediaTek MTK- container."""

import argparse
import hashlib
import json
import struct
from pathlib import Path

HEADER_BYTES = 16
RECORD_BYTES = 76
FIRMWARE_FILES = {
    "WIFI_MT7925_PATCH_MCU_1_1_hdr.bin",
    "WIFI_RAM_CODE_MT7925_1_1.bin",
}


def read_container(path):
    data = path.read_bytes()
    if len(data) < HEADER_BYTES or data[:4] != b"MTK-":
        raise ValueError("Input is not a supported MTK- firmware container")

    count = struct.unpack_from("<H", data, 4)[0]
    declared_bytes = struct.unpack_from("<I", data, 8)[0]
    table_end = HEADER_BYTES + count * RECORD_BYTES
    if count == 0 or table_end > len(data) or declared_bytes != len(data):
        raise ValueError("Container header or record table is invalid")

    records = {}
    spans = []
    for index in range(count):
        record = HEADER_BYTES + index * RECORD_BYTES
        name = data[record:record + 48].split(b"\0", 1)[0].decode("ascii")
        version = data[record + 48:record + 64].split(b"\0", 1)[0].decode("ascii")
        offset, size, _reserved = struct.unpack_from("<III", data, record + 64)
        if not name or offset < table_end or size == 0 or offset + size > len(data):
            raise ValueError(f"Container record {index} has invalid bounds")
        if name in records:
            raise ValueError(f"Container has duplicate record {name}")
        records[name] = (version, offset, size)
        spans.append((offset, offset + size, name))

    spans.sort()
    for previous, current in zip(spans, spans[1:]):
        if current[0] < previous[1]:
            raise ValueError(f"Container payloads overlap: {previous[2]} and {current[2]}")

    missing = FIRMWARE_FILES - records.keys()
    if missing:
        raise ValueError("Missing MT7925 Wi-Fi firmware: " + ", ".join(sorted(missing)))
    return data, records


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("container", type=Path, help="Local mtkwlan.dat from the vendor driver")
    parser.add_argument("output", type=Path, help="Directory for the two extracted Wi-Fi files")
    args = parser.parse_args()

    data, records = read_container(args.container)
    args.output.mkdir(parents=True, exist_ok=True)
    result = {}
    for name in sorted(FIRMWARE_FILES):
        version, offset, size = records[name]
        payload = data[offset:offset + size]
        (args.output / name).write_bytes(payload)
        result[name] = {
            "version": version,
            "bytes": size,
            "sha256": hashlib.sha256(payload).hexdigest(),
        }
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
