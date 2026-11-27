param(
    [Parameter(Mandatory)][string]$FirmwareDirectory
)

$ErrorActionPreference = 'Stop'
$inputDirectory = (Resolve-Path -LiteralPath $FirmwareDirectory).ProviderPath
$required = @{
    'WIFI_MT7925_PATCH_MCU_1_1_hdr.bin' = '0F4FD0B6C61E0A6E6293044F055DD50639251B5A26BEAA78ECA77CBF6049B340'
    'WIFI_RAM_CODE_MT7925_1_1.bin' = 'F70174E32F020044E4E8B072C3A1D71826A30C0B92C593AC4E77F94CEA274D23'
}
foreach ($name in $required.Keys) {
    $file = Join-Path $inputDirectory $name
    if ((Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash -ne $required[$name]) {
        throw "Unexpected SHA-256 for $name. This port profile is pinned to the backed-up 2025-12 vendor firmware."
    }
}

$output = Join-Path (Split-Path -Parent $PSScriptRoot) 'build\wificx-x64'
[void][IO.Directory]::CreateDirectory($output)
$image = Join-Path $output 'mt7925-reference-image.bin'
& python -B (Join-Path $PSScriptRoot 'inspect-firmware.py') $inputDirectory `
    --chip mt7925 --pack-image $image
if ($LASTEXITCODE -ne 0) {
    throw 'MT7925 firmware inspection or image packing failed.'
}

$expectedImageHash = '5AE764D361592815237E6B688387B8F4FFD614443938F142AB44CE5416C1161B'
if ((Get-FileHash -LiteralPath $image -Algorithm SHA256).Hash -ne $expectedImageHash) {
    Remove-Item -LiteralPath $image -Force
    throw 'Packed MT7925 image does not match the accepted firmware profile.'
}
Write-Host "Prepared local MT7925 boot image: $image"
Write-Host 'This image is not an installable driver and does not establish hardware support.'
