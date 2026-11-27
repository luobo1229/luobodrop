param(
    [switch]$KernelMode,
    [switch]$WifiCx,
    [ValidateSet('MT7922', 'MT7925')][string]$Chip = 'MT7922',
    [string]$PackageDirectory,
    [string]$WdkRoot,
    [ValidatePattern('^[A-Fa-f0-9]{40}$')][string]$SigningCertificateThumbprint
)

$ErrorActionPreference = 'Stop'
if ($WifiCx -and $KernelMode) {
    throw 'Choose either -KernelMode or -WifiCx.'
}
if ($Chip -eq 'MT7925' -and !$WifiCx) {
    throw 'The MT7925 package is available only for the WiFiCx station driver.'
}

$root = Split-Path -Parent $PSScriptRoot
$kit = if ($WdkRoot) { (Resolve-Path -LiteralPath $WdkRoot).ProviderPath }
       else { Join-Path $root 'tools\wdk-10.0.26100.6584\c' }
$sdk = Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Windows Kits\10'
$signTool = Join-Path $sdk 'bin\10.0.26100.0\x64\signtool.exe'
$target = if ($WifiCx) { 'build\wificx-x64' } elseif ($KernelMode) { 'build\x64' } else { 'build\umdf-x64' }
$chipPrefix = $Chip.ToLowerInvariant()
$buildDriverFile = if ($WifiCx) { 'mt7922-wificx.sys' } elseif ($KernelMode) { 'mt7922-probe.sys' } else { 'mt7922-probe.dll' }
$driverFile = if ($WifiCx) { "$chipPrefix-wificx.sys" } else { $buildDriverFile }
$packageName = if ($WifiCx) { "$chipPrefix-wificx" } elseif ($KernelMode) { 'mt7922-probe' } else { 'mt7922-umdf' }
$build = Join-Path $root $target
$package = if ($PackageDirectory) {
    $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($PackageDirectory)
} else {
    Join-Path $build $(if ($WifiCx -and $Chip -eq 'MT7925') { 'package-mt7925' } else { 'package' })
}
[void][IO.Directory]::CreateDirectory($package)
$inf = Join-Path $package "$packageName.inf"
$driver = Join-Path $package $driverFile
$catalog = Join-Path $package "$packageName.cat"
$publicCertificate = Join-Path $package "$chipPrefix-test.cer"
Copy-Item -LiteralPath (Join-Path $root "driver\$packageName.inf") -Destination $inf -Force
Copy-Item -LiteralPath (Join-Path $build $buildDriverFile) -Destination $driver -Force
if ($WifiCx) {
    if ($Chip -eq 'MT7925') {
        $firmwareSource = Join-Path $build 'mt7925-reference-image.bin'
        $expectedFirmwareHash = '5AE764D361592815237E6B688387B8F4FFD614443938F142AB44CE5416C1161B'
        if ((Get-FileHash -LiteralPath $firmwareSource -Algorithm SHA256).Hash -ne $expectedFirmwareHash) {
            throw 'The MT7925 package requires the pinned vendor firmware image.'
        }
        Copy-Item -LiteralPath $firmwareSource -Destination (Join-Path $package 'mt7925-wificx-fw.bin') -Force
    } else {
        Copy-Item -LiteralPath (Join-Path $build 'mt7922-reference-image.bin') `
            -Destination (Join-Path $package 'mt7922-wificx-fw.bin') -Force
        Copy-Item -LiteralPath (Join-Path $build 'mt7922-wificx-clc.bin') `
            -Destination (Join-Path $package 'mt7922-wificx-clc.bin') -Force
    }
}

$ephemeralSignature = !$SigningCertificateThumbprint
$temporaryPfx = $null
$temporaryPfxPassword = $null
$signingKey = $null
if ($ephemeralSignature) {
    # CertEnroll may be unavailable in restricted build shells. Create the short-lived
    # code-signing certificate in memory and export an encrypted PFX only for SignTool.
    $signingKey = [System.Security.Cryptography.RSA]::Create(2048)
    $request = [System.Security.Cryptography.X509Certificates.CertificateRequest]::new(
        "CN=$Chip WiFiCx Local Test", $signingKey,
        [System.Security.Cryptography.HashAlgorithmName]::SHA256,
        [System.Security.Cryptography.RSASignaturePadding]::Pkcs1)
    $request.CertificateExtensions.Add(
        [System.Security.Cryptography.X509Certificates.X509BasicConstraintsExtension]::new($false, $false, 0, $true))
    $request.CertificateExtensions.Add(
        [System.Security.Cryptography.X509Certificates.X509KeyUsageExtension]::new(
            [System.Security.Cryptography.X509Certificates.X509KeyUsageFlags]::DigitalSignature, $true))
    $codeSigningOids = [System.Security.Cryptography.OidCollection]::new()
    [void]$codeSigningOids.Add([System.Security.Cryptography.Oid]::new('1.3.6.1.5.5.7.3.3'))
    $request.CertificateExtensions.Add(
        [System.Security.Cryptography.X509Certificates.X509EnhancedKeyUsageExtension]::new($codeSigningOids, $true))
    $signature = $request.CreateSelfSigned([DateTimeOffset]::UtcNow.AddMinutes(-5), [DateTimeOffset]::UtcNow.AddDays(1))
    $temporaryPfxPassword = [Guid]::NewGuid().ToString('N')
    $temporaryPfx = Join-Path $package ".${chipPrefix}-temporary-signing-$([Guid]::NewGuid().ToString('N')).pfx"
    [IO.File]::WriteAllBytes($temporaryPfx,
        $signature.Export([System.Security.Cryptography.X509Certificates.X509ContentType]::Pfx, $temporaryPfxPassword))
} else {
    $signature = Get-Item -LiteralPath "Cert:\CurrentUser\My\$SigningCertificateThumbprint"
    $now = Get-Date
    if (!$signature.HasPrivateKey -or $now -lt $signature.NotBefore -or $now -ge $signature.NotAfter) {
        throw 'The selected signing certificate must be current and have an accessible private key.'
    }
}
try {
    if ($ephemeralSignature) {
        & $signTool sign /fd SHA256 /f $temporaryPfx /p $temporaryPfxPassword $driver
    } else {
        & $signTool sign /fd SHA256 /sha1 $signature.Thumbprint $driver
    }
    if ($LASTEXITCODE -ne 0) { throw 'Driver binary could not be signed.' }

    & (Join-Path $kit 'tools\10.0.26100.0\x64\infverif.exe') /w $inf
    if ($LASTEXITCODE -ne 0) { throw 'INF verification failed.' }

    & (Join-Path $kit 'bin\10.0.26100.0\x86\Inf2Cat.exe') "/driver:$package" /os:10_GE_X64 /uselocaltime
    if ($LASTEXITCODE -ne 0) { throw 'Catalog generation failed.' }

    if ($ephemeralSignature) {
        & $signTool sign /fd SHA256 /f $temporaryPfx /p $temporaryPfxPassword $catalog
    } else {
        & $signTool sign /fd SHA256 /sha1 $signature.Thumbprint $catalog
    }
    if ($LASTEXITCODE -ne 0) { throw 'Driver catalog could not be signed.' }

    if ($ephemeralSignature) {
        [IO.File]::WriteAllBytes($publicCertificate,
            $signature.Export([System.Security.Cryptography.X509Certificates.X509ContentType]::Cert))
    } else {
        Export-Certificate -Cert $signature -FilePath $publicCertificate -Force | Out-Null
    }
    foreach ($file in $driver, $catalog) {
        $signedFile = Get-AuthenticodeSignature -LiteralPath $file
        if ($signedFile.SignerCertificate.Thumbprint -ne $signature.Thumbprint) {
            throw "Expected test signature missing: $file"
        }
    }
    Write-Output "Signed package: $package"
}
finally {
    if ($ephemeralSignature) {
        if ($temporaryPfx -and (Test-Path -LiteralPath $temporaryPfx)) {
            Remove-Item -LiteralPath $temporaryPfx -Force -ErrorAction Stop
        }
        if ($signature) { $signature.Dispose() }
        if ($signingKey) { $signingKey.Dispose() }
    }
}
