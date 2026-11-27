# LuoboDrop

> **Experimental MT7925 port:** This source includes an MT7925 adaptation, but only compilation and firmware-independent checks have passed. Windows rejected the local test-signing certificate, so the driver has not been loaded or validated on MT7925 hardware. It is not a usable driver release. The tested setup and benchmark below refer to the original MT7922 development build, not this port. The source package excludes driver binaries, test certificates, and firmware images.

LuoboDrop is an experimental Windows 11 project for exchanging files with an iPad through native AirDrop while the Windows PC remains associated with a Wi-Fi access point. It uses a custom WiFiCx driver for a single MediaTek MT7922 adapter, an Apple Wireless Direct Link (AWDL) implementation, and a Windows desktop panel.

## Community source preview

LuoboDrop is available free of charge as source for research and contributions. This preview uses a WDK-based implementation for the ordinary Wi-Fi data rings. It builds locally and passes synthetic ring tests. Hardware validation of this preview is pending.

Reviews and reproducible reports about TX/RX ring ownership, queue cancellation and restart, or Wi-Fi and AWDL coexistence are welcome. See [Build and recovery](BUILD.md) and [Known issues](KNOWN_ISSUES.md).

## Tested setup

The reference machine was a ROG Strix G16 G614FR with an MT7922 and Windows 11 Home x64 build 26300.9974. The paired iPad's model and iPadOS version were not recorded in the benchmark evidence. Other adapters, iPhones, Macs, Windows builds and channel plans require verification.

On November 23, 2026, each direction transferred a 57,393,124-byte video over a shared 5 GHz channel 149 with 80 MHz bandwidth.

| Direction | File body time | Average file body rate | Wi-Fi probes during test |
| --- | ---: | ---: | --- |
| Windows to iPad | 1.495 s | 38.40 MB/s | Gateway 6/6, HTTPS 6/6 |
| iPad to Windows | 1.494 s | 38.43 MB/s | Gateway 39/40, HTTPS 40/40 |

Rates use decimal MB and exclude discovery, acceptance and final confirmation. The results refer to the installed development package numbered 0.70.0.84. This source snapshot requires its own benchmark before inheriting those results.

## Documentation

- [Build and recovery](BUILD.md)
- [Architecture](ARCHITECTURE.md)
- [Known issues](KNOWN_ISSUES.md)
- [Third-party notices](THIRD_PARTY_NOTICES.md)
- [Security](SECURITY.md)
- [简体中文](README_zh-CN.md)

## License

LuoboDrop project code used in the combined driver and transfer tools is licensed under [GNU GPL version 3 or later](LICENSE) (SPDX: `GPL-3.0-or-later`). Third-party files retain their notices and applicable terms. See [Third-party notices](THIRD_PARTY_NOTICES.md).

LuoboDrop is an independent project and has no affiliation with Apple, MediaTek, ASUS or Microsoft. AirDrop is a trademark of Apple Inc.
