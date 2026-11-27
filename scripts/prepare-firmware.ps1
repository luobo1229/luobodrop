param(
    [ValidateSet('MT7922', 'MT7925')][string]$Chip = 'MT7922',
    [Parameter(Mandatory)][string]$FirmwareDirectory
)

$ErrorActionPreference = 'Stop'
$inputDirectory = (Resolve-Path -LiteralPath $FirmwareDirectory).ProviderPath
$firmware = if ($Chip -eq 'MT7925') {
    @{
        'WIFI_MT7925_PATCH_MCU_1_1_hdr.bin' = '0F4FD0B6C61E0A6E6293044F055DD50639251B5A26BEAA78ECA77CBF6049B340'
        'WIFI_RAM_CODE_MT7925_1_1.bin' = 'F70174E32F020044E4E8B072C3A1D71826A30C0B92C593AC4E77F94CEA274D23'
    }
} else {
    @{
        'WIFI_MT7922_patch_mcu_1_1_hdr.bin' = '5356256C3A361CE6A06165F67CB01B55AECAAC0E2F8D2D9CBDE6312C4CF1A91C'
        'WIFI_RAM_CODE_MT7922_1.bin' = 'AE4DAB06CC1849682E254FE97183ED093C51382BACA9B3F8A00FD6809AB71884'
    }
}
foreach ($name in $firmware.Keys) {
    $file = Join-Path $inputDirectory $name
    if ((Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash -ne $firmware[$name]) {
        throw "Unexpected SHA-256 for $name. Use the firmware revision in BUILD.md."
    }
}

$output = Join-Path (Split-Path -Parent $PSScriptRoot) 'build\wificx-x64'
[void][IO.Directory]::CreateDirectory($output)
if ($Chip -eq 'MT7925') {
    & python -B (Join-Path $PSScriptRoot 'inspect-firmware.py') --chip mt7925 $inputDirectory `
        --pack-image (Join-Path $output 'mt7925-reference-image.bin')
} else {
    & python -B (Join-Path $PSScriptRoot 'inspect-firmware.py') $inputDirectory `
        --pack-image (Join-Path $output 'mt7922-reference-image.bin') `
        --pack-clc (Join-Path $output 'mt7922-wificx-clc.bin')
}
if ($LASTEXITCODE -ne 0) {
    throw "Firmware preparation failed with exit code $LASTEXITCODE."
}
