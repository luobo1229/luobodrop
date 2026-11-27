# Known issues

- The MT7925 port has passed compilation and firmware-independent checks only. Windows rejected the local test-signing certificate during PnP installation, and no MT7925 hardware runtime test has succeeded. Its current regulatory profile is limited to CN 5 GHz; 2.4 GHz and 6 GHz are not exposed. Do not treat this port as a ready-to-install driver.
- The community source preview has passed compilation and synthetic NetRing tests. Hardware benchmark results in the README refer to the original development build on the ROG and MT7922. Automatic device discovery across multiple receivers and the Windows Explorer extension are outside this candidate.
- Older builds could reconnect to the access point while IP traffic remained stalled. The station radio toggle change was separately tested on the reference machine. Recovery and reconnect behavior still need validation on the prepared build.
- AirDrop requests can be delivered to iPad Notification Center. If a request does not appear while a transfer is waiting for acceptance, check for the current request there.
- The snapshot driver is experimental kernel code. The private development package uses test signing. The public driver-signing and recovery paths have not been accepted for delivery.
- The desktop currently targets .NET 6. The default app icon is used while the LuoboDrop artwork is reviewed.
- Optional connectivity tests send probes to the gateway and to www.baidu.com. Raw transfer logs, captures and the lab network configuration belong outside the repository.
- Benchmark results refer to the original development package. The prepared snapshot needs a fresh benchmark before using those figures as its results.
