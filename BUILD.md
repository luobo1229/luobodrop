# Build and recovery

## Development tools

- Windows x64, Visual Studio C++ Build Tools with x64 MSVC and the Windows SDK. The reference toolset used MSVC 14.51.36231 and SDK 10.0.26100.0.
- [Microsoft WDK x64 10.0.26100.6584](https://www.nuget.org/packages/microsoft.windows.wdk.x64/10.0.26100.6584), extracted outside the repository. Use its `c` directory as `-WdkRoot`. The WDK supplies KMDF 1.33, WiFiCx 1.2, NetAdapterCx 2.5 and WLAN 2.0 headers and libraries.
- Windows PowerShell 5.1, Python 3.14.5 for the validated tooling environment, and an OpenSSL command available to the transfer process. The Python minimum is unverified.
- .NET 6 SDK for the current WPF project. .NET 6 is out of support and the project needs a supported target before broad distribution. [Microsoft .NET lifecycle](https://dotnet.microsoft.com/en-us/platform/support/policy/dotnet-core)

## Build from source

The WiFiCx build uses the NetAdapterCx APIs provided by the Windows SDK and WDK.

Run these in a Windows PowerShell session with an execution policy appropriate for trusted local scripts. Replace the WDK path with your extraction directory.

```powershell
.\scripts\build.ps1 -WifiCx -WdkRoot 'D:\dev-deps\wdk\c'
.\scripts\build.ps1 -TestNetRings -WdkRoot 'D:\dev-deps\wdk\c'
.\scripts\build.ps1 -QueryOnly
.\scripts\build.ps1 -WinAwdlMergeOnly
dotnet build .\desktop\AirDropDesktop.csproj -c Release -o .\build\desktop\app
python -B -m unittest discover -s tests -p 'test_*.py'
python -B -m unittest discover -s tools/winawdl/airdrop/tests -p 'test_*.py'
```

The driver build creates an unsigned SYS in `build/wificx-x64/`. Firmware and certificates are unnecessary for compilation. The standalone ring tests run without Wi-Fi hardware or firmware. The query executable and TCP merger DLL go under `build/x64/`. The older native diagnostic switches depend on private reference inputs and are outside this candidate's supported build.

### Experimental MT7925 station build

The MT7925 WiFiCx package matches only `PCI\VEN_14C3&DEV_7925&SUBSYS_60001A3B&REV_00`. It uses the two MT7925 firmware files extracted from the backed-up vendor package; `prepare-firmware.ps1` verifies their pinned SHA-256 values before packaging. The current port exposes a restricted CN 5 GHz channel profile. Scanning uses the Connac3 Unified MCU TLVs and completion event. It does not expose 2.4 GHz or 6 GHz, and the extracted firmware has no separate CLC regulatory region.

```powershell
.\scripts\prepare-firmware.ps1 -Chip MT7925 -FirmwareDirectory 'D:\dev-deps\mt7925'
.\scripts\build.ps1 -WifiCx -WdkRoot 'D:\dev-deps\wdk\c'
.\scripts\package-test.ps1 -WifiCx -Chip MT7925 -WdkRoot 'D:\dev-deps\wdk\c'
```

Use this profile only in the configured CN region until a region-matched regulatory data path is available. Keep the vendor driver package exported and recoverable as described below.

## Local runtime and pack inputs

1. Obtain [Wintun 0.14.1](https://www.wintun.net/) separately. Verify the ZIP SHA-256 `07c256185d6ee3652e09fa55c0b673e2624b565e02c4b9091c79ca7d2f24ef51` and place its AMD64 DLL under `tools/wintun/bin/amd64/`.
2. Download the two inputs below from the pinned Linux Firmware commit. The preparation script checks both SHA-256 hashes before producing the local driver image and CLC. Keep the inputs and outputs outside Git. [MediaTek firmware terms](https://gitlab.com/kernel-firmware/linux-firmware/-/raw/2452612955cd7fd31f4cca30f129b9b9959c8da5/LICENSES/LICENCE.mediatek)

   | Firmware input | Version | SHA-256 |
   | --- | --- | --- |
   | [WIFI_MT7922_patch_mcu_1_1_hdr.bin](https://gitlab.com/kernel-firmware/linux-firmware/-/raw/2452612955cd7fd31f4cca30f129b9b9959c8da5/mediatek/WIFI_MT7922_patch_mcu_1_1_hdr.bin) | 20261027203145a | 5356256c3a361ce6a06165f67cb01b55aecaac0e2f8d2d9cbde6312c4cf1a91c |
   | [WIFI_RAM_CODE_MT7922_1.bin](https://gitlab.com/kernel-firmware/linux-firmware/-/raw/2452612955cd7fd31f4cca30f129b9b9959c8da5/mediatek/WIFI_RAM_CODE_MT7922_1.bin) | 20261027203227 | ae4dab06cc1849682e254fe97183ed093c51382baca9b3f8a00fd6809ab71884 |

3. For an authorized test lab only, package the built driver using a local code-signing certificate. This creates local build output and does not install the package. A usable development package also requires the signing policy supported by the test machine.
4. For a machine already configured with a compatible test package, set the `LUOBODROP_PEER` environment variable to the peer's AWDL address in the shell used to launch `airdrop.cmd`. The command-line transfer script also accepts `-SharedAdvertPeer`. Keep the peer address in local configuration.

```powershell
.\scripts\prepare-firmware.ps1 -FirmwareDirectory 'D:\dev-deps\mt7922'
.\scripts\package-test.ps1 -WifiCx -WdkRoot 'D:\dev-deps\wdk\c'
```

The snapshot includes no test key or signed driver. Secure Boot can constrain Windows test-signing configuration. Keep testing and any boot-policy changes on machines designated for driver development. The private reference package does not establish compatibility with default Secure Boot settings. [Microsoft test-signing documentation](https://learn.microsoft.com/en-us/windows-hardware/drivers/install/the-testsigning-boot-configuration-option)

## Recover the vendor driver

Before any installation in a test lab, use Device Manager to record the current driver and use `pnputil /export-driver` with the current published INF to save the vendor package to local storage. Keep an alternative network connection or offline copy accessible.

To recover, stop LuoboDrop, select **Roll Back Driver** for the adapter in Device Manager if available, or point **Update driver → Browse my computer** to the backed-up vendor INF. Reboot if requested, verify the vendor and version, reconnect to Wi-Fi, and verify traffic. Remove the test package only after checking its published INF and that the vendor package is active. PnPUtil installation follows driver ranking, so importing the vendor INF alone may not change the active driver. The original research installer was specific to its lab machine and is excluded from this candidate. [PnPUtil reference](https://learn.microsoft.com/en-us/windows-hardware/drivers/devtest/pnputil-command-syntax)
