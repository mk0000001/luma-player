#requires -Version 7.2
[CmdletBinding(DefaultParameterSetName = 'Sign')]
param(
    [Parameter(Mandatory, ParameterSetName = 'Initialize')][switch]$Initialize,
    [Parameter(Mandatory, ParameterSetName = 'Sign')][string]$Version,
    [Parameter(Mandatory, ParameterSetName = 'Sign')][string]$InstallerPath,
    [Parameter(Mandatory, ParameterSetName = 'Sign')][string]$OutputDirectory
)

$ErrorActionPreference = 'Stop'
if (!$IsWindows) { throw 'Windows current-user DPAPI is required.' }

# Public coordinates also pinned by UpdateManager. The private key stays in DPAPI.
$pinnedPublicCoordinates = '030d278213ed871b4be26a45ebf038a93ce8af23bc55a7d0933855b4c7e1e73eb5bea934a2ad48dfd6a4fdae5254849c10299ea1a15a4e35fa01d3a7a003a96f'
$repository = 'mk0000001/luma-player'
$maximumInstallerBytes = 2147483648L
$profileDirectory = [Environment]::GetFolderPath([Environment+SpecialFolder]::LocalApplicationData)
if (!$profileDirectory) { throw 'The current user has no LocalAppData directory.' }
$keyDirectory = [IO.Path]::GetFullPath((Join-Path $profileDirectory 'LumaPlayerReleaseSigning'))
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if ($keyDirectory.StartsWith($projectRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'The signing key must be outside the repository.'
}
$keyPath = Join-Path $keyDirectory 'update-signing-key.dpapi'
$entropy = [Text.Encoding]::UTF8.GetBytes('LumaPlayer update signing key v1')
$scope = [Security.Cryptography.DataProtectionScope]::CurrentUser

function Open-SigningKey {
    if (Test-Path -LiteralPath $keyDirectory) {
        $directory = Get-Item -LiteralPath $keyDirectory -Force
        if (!$directory.PSIsContainer -or ($directory.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw 'The signing key directory must be a local directory.'
        }
    }
    if (Test-Path -LiteralPath $keyPath) {
        $file = Get-Item -LiteralPath $keyPath -Force
        if ($file.PSIsContainer -or ($file.Attributes -band [IO.FileAttributes]::ReparsePoint) -or $file.Length -le 0 -or $file.Length -gt 4096) {
            throw 'Invalid protected signing key file.'
        }
        $clear = [Security.Cryptography.ProtectedData]::Unprotect([IO.File]::ReadAllBytes($keyPath), $entropy, $scope)
        try {
            return [Security.Cryptography.CngKey]::Import($clear, [Security.Cryptography.CngKeyBlobFormat]::EccPrivateBlob)
        } finally {
            [Array]::Clear($clear, 0, $clear.Length)
        }
    }
    if (!$Initialize) { throw 'Initialize the release signing key first.' }

    $null = [IO.Directory]::CreateDirectory($keyDirectory)
    $parameters = [Security.Cryptography.CngKeyCreationParameters]::new()
    $parameters.ExportPolicy = [Security.Cryptography.CngExportPolicies]::AllowPlaintextExport
    $newKey = [Security.Cryptography.CngKey]::Create([Security.Cryptography.CngAlgorithm]::ECDsaP256, [NullString]::Value, $parameters)
    try {
        if (!$newKey.IsEphemeral) { throw 'Expected an ephemeral CNG key.' }
        $clear = $newKey.Export([Security.Cryptography.CngKeyBlobFormat]::EccPrivateBlob)
        try {
            $protected = [Security.Cryptography.ProtectedData]::Protect($clear, $entropy, $scope)
        } finally {
            [Array]::Clear($clear, 0, $clear.Length)
        }
        $file = [IO.File]::Open($keyPath, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
        try {
            $file.Write($protected, 0, $protected.Length)
            $file.Flush($true)
        } finally {
            $file.Dispose()
        }
        return $newKey
    } catch {
        $newKey.Dispose()
        throw
    }
}

$key = Open-SigningKey
try {
    $public = $key.Export([Security.Cryptography.CngKeyBlobFormat]::EccPublicBlob)
    if ($public.Length -ne 72 -or [BitConverter]::ToUInt32($public, 0) -ne 0x31534345 -or [BitConverter]::ToUInt32($public, 4) -ne 32) {
        throw 'The signing key must use ECDSA P-256.'
    }
    $x = [BitConverter]::ToString($public, 8, 32).Replace('-', '').ToLowerInvariant()
    $y = [BitConverter]::ToString($public, 40, 32).Replace('-', '').ToLowerInvariant()
    if ($Initialize) {
        [ordered]@{ publicX = $x; publicY = $y; keyFile = $keyPath } | ConvertTo-Json
        return
    }
    if (($x + $y) -cne $pinnedPublicCoordinates) { throw 'The protected key differs from the update key pinned by LumaPlayer.' }

    $semver = '\A(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)(?:-(?<pre>[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*))?(?:\+[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*)?\z'
    if ($Version.Length -gt 128 -or $Version -cnotmatch $semver) { throw 'Invalid SemVer release version.' }
    if ($Matches['pre']) {
        foreach ($identifier in ($Matches['pre'] -split '\.')) {
            if ($identifier -cmatch '\A0[0-9]+\z') { throw 'Invalid numeric SemVer prerelease identifier.' }
        }
    }
    $installer = Get-Item -LiteralPath $InstallerPath -Force
    $installerPattern = '\ALumaPlayer-' + [regex]::Escape($Version) + '-setup\.[1-9][0-9]{0,5}-windows-x64-cuda\.exe\z'
    if ($installer.PSIsContainer -or ($installer.Attributes -band [IO.FileAttributes]::ReparsePoint) -or $installer.Name -cnotmatch $installerPattern) {
        throw 'Select the Windows x64 CUDA installer for the exact release version.'
    }

    $installerStream = [IO.File]::Open($installer.FullName, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
    try {
        $size = $installerStream.Length
        if ($size -lt 128 -or $size -gt $maximumInstallerBytes) { throw 'Installer size is outside the supported range.' }
        $versionInfo = [Diagnostics.FileVersionInfo]::GetVersionInfo($installer.FullName)
        if (([string]$versionInfo.ProductName).TrimEnd(' ') -cne 'LumaPlayer' -or
            ([string]$versionInfo.FileDescription).TrimEnd(' ') -cne 'LumaPlayer Setup' -or
            ([string]$versionInfo.ProductVersion).TrimEnd(' ') -cne $Version -or
            ([string]$versionInfo.FileVersion).TrimEnd(' ') -cne $Version) {
            throw 'The Windows installer version resource does not match the release.'
        }
        $reader = [IO.BinaryReader]::new($installerStream, [Text.Encoding]::UTF8, $true)
        try {
            if ($reader.ReadUInt16() -ne 0x5a4d) { throw 'Installer is not a Windows PE file.' }
            $installerStream.Position = 60
            $peOffset = $reader.ReadUInt32()
            if ($peOffset -lt 64 -or $peOffset -gt $size - 24) { throw 'Invalid Windows PE header.' }
            $installerStream.Position = $peOffset
            if ($reader.ReadUInt32() -ne 0x00004550 -or $reader.ReadUInt16() -notin @(0x014c, 0x8664)) {
                throw 'Invalid Windows installer executable.'
            }
        } finally {
            $reader.Dispose()
        }
        $installerStream.Position = 0
        $hash = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($installerStream)).ToLowerInvariant()
        $manifest = [ordered]@{
            schema = 1
            repository = $repository
            platform = 'windows-x64'
            version = $Version
            file = $installer.Name
            bytes = $size
            sha256 = $hash
        }
        $data = [Text.UTF8Encoding]::new($false, $true).GetBytes(($manifest | ConvertTo-Json -Compress))
        $signer = [Security.Cryptography.ECDsaCng]::new($key)
        try {
            $algorithm = [Security.Cryptography.HashAlgorithmName]::SHA256
            $format = [Security.Cryptography.DSASignatureFormat]::IeeeP1363FixedFieldConcatenation
            $signature = $signer.SignData($data, $algorithm, $format)
            if ($signature.Length -ne 64 -or !$signer.VerifyData($data, $signature, $algorithm, $format)) {
                throw 'Signature self-verification failed.'
            }
        } finally {
            $signer.Dispose()
        }
    } finally {
        $installerStream.Dispose()
    }

    $destination = [IO.Path]::GetFullPath($OutputDirectory)
    $null = [IO.Directory]::CreateDirectory($destination)
    $manifestPath = Join-Path $destination 'luma-update.json'
    $signaturePath = Join-Path $destination 'luma-update.sig'
    $manifestFile = $null
    $signatureFile = $null
    $completed = $false
    try {
        $manifestFile = [IO.File]::Open($manifestPath, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
        $signatureFile = [IO.File]::Open($signaturePath, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
        $manifestFile.Write($data, 0, $data.Length)
        $signatureFile.Write($signature, 0, $signature.Length)
        $manifestFile.Flush($true)
        $signatureFile.Flush($true)
        $completed = $true
    } finally {
        if ($signatureFile) { $signatureFile.Dispose() }
        if ($manifestFile) { $manifestFile.Dispose() }
        if (!$completed) {
            if ($signatureFile) { [IO.File]::Delete($signaturePath) }
            if ($manifestFile) { [IO.File]::Delete($manifestPath) }
        }
    }
    [ordered]@{ manifest = $manifestPath; signature = $signaturePath; version = $Version; bytes = $size; sha256 = $hash } | ConvertTo-Json
} finally {
    $key.Dispose()
}
