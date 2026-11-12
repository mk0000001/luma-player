[CmdletBinding()]
param(
    [string]$ToolchainRoot = (Join-Path $PSScriptRoot '../.deps/android-toolchain'),
    [string[]]$Tasks = @(':app:testReleaseUnitTest', ':app:lintRelease', ':app:assembleRelease'),
    [switch]$SignLocalBeta
)

$ErrorActionPreference = 'Stop'
$ToolchainRoot = [IO.Path]::GetFullPath($ToolchainRoot)
$downloadRoot = Join-Path $ToolchainRoot 'downloads'
$sdk = Join-Path $ToolchainRoot 'sdk'
$jdk = Join-Path $ToolchainRoot 'jdk-17.0.21+9'
$gradle = Join-Path $ToolchainRoot 'gradle-8.13'
$buildRoot = Join-Path $ToolchainRoot 'build'
$tempRoot = Join-Path $ToolchainRoot 'temp'
$platform = Join-Path $sdk 'platforms/android-36'
$buildTools = Join-Path $sdk 'build-tools/36.0.0'

function Install-Archive {
    param([string]$Url, [string]$Sha256, [string]$Target, [string]$EntryPoint)

    $marker = Join-Path $Target '.luma-archive-sha256'
    if ((Test-Path -LiteralPath (Join-Path $Target $EntryPoint)) -and
        (Test-Path -LiteralPath $marker) -and (Get-Content -LiteralPath $marker -Raw).Trim() -eq $Sha256) {
        return
    }

    $archive = Join-Path $downloadRoot ([IO.Path]::GetFileName($Url))
    if (-not (Test-Path -LiteralPath $archive) -or
        (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $Sha256) {
        Write-Host "Downloading $([IO.Path]::GetFileName($Url))"
        & curl.exe --fail --location --silent --show-error --retry 2 --output "$archive.part" $Url
        if ($LASTEXITCODE -ne 0) { throw "Download failed: $Url" }
        if ((Get-FileHash -LiteralPath "$archive.part" -Algorithm SHA256).Hash -ne $Sha256) {
            throw "Archive SHA-256 mismatch: $Url"
        }
        Move-Item -LiteralPath "$archive.part" -Destination $archive -Force
    }

    New-Item -ItemType Directory -Path $Target -Force | Out-Null
    & tar.exe -xf $archive -C $Target --strip-components 1
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath (Join-Path $Target $EntryPoint))) {
        throw "Archive extraction failed: $archive"
    }
    Set-Content -LiteralPath $marker -Value $Sha256 -Encoding ascii
}

function Sign-LocalBetaApk {
    $release = Join-Path $buildRoot 'app/outputs/apk/release'
    $metadata = Get-Content -LiteralPath (Join-Path $release 'output-metadata.json') -Raw | ConvertFrom-Json
    $variant = $metadata.elements[0]
    $unsigned = Join-Path $release $variant.outputFile
    $apk = Join-Path $release "LumaPlayer-Android-$($variant.versionName).apk"
    $signer = Join-Path $buildTools 'apksigner.bat'
    $signingRoot = Join-Path $env:LOCALAPPDATA 'LumaPlayer/AndroidSigning'
    $keystore = Join-Path $signingRoot 'beta-local.p12'
    $passwordFile = Join-Path $signingRoot 'beta-local.dpapi'
    New-Item -ItemType Directory -Path $signingRoot -Force | Out-Null

    if (Test-Path -LiteralPath $passwordFile) {
        $securePassword = Get-Content -LiteralPath $passwordFile -Raw | ConvertTo-SecureString
        $env:LUMA_LOCAL_BETA_PASSWORD = [Net.NetworkCredential]::new('', $securePassword).Password
    } else {
        if (Test-Path -LiteralPath $keystore) { throw 'Local beta signing password is unavailable.' }
        $passwordBytes = [byte[]]::new(32)
        $random = [Security.Cryptography.RandomNumberGenerator]::Create()
        try { $random.GetBytes($passwordBytes) } finally { $random.Dispose() }
        $env:LUMA_LOCAL_BETA_PASSWORD = [Convert]::ToBase64String($passwordBytes)
        $securePassword = ConvertTo-SecureString -String $env:LUMA_LOCAL_BETA_PASSWORD -AsPlainText -Force
        ConvertFrom-SecureString -SecureString $securePassword |
            Set-Content -LiteralPath $passwordFile -Encoding ascii -NoNewline
    }

    if (-not (Test-Path -LiteralPath $keystore)) {
        & (Join-Path $jdk 'bin/keytool.exe') -genkeypair -noprompt -keystore $keystore `
            -storetype PKCS12 -storepass:env LUMA_LOCAL_BETA_PASSWORD -alias lumaplayer-beta `
            -keyalg RSA -keysize 3072 -validity 10000 -dname 'CN=LumaPlayer Android Local Beta'
        if ($LASTEXITCODE -ne 0) { throw 'Local beta signing key creation failed.' }
    }

    & $signer sign --ks $keystore --ks-key-alias lumaplayer-beta `
        --ks-pass env:LUMA_LOCAL_BETA_PASSWORD --out $apk $unsigned
    if ($LASTEXITCODE -ne 0) { throw 'APK signing failed.' }
    & $signer verify --verbose --print-certs $apk
    if ($LASTEXITCODE -ne 0) { throw 'APK signature verification failed.' }
    Get-FileHash -LiteralPath $apk -Algorithm SHA256 | Format-List Hash, Path
}

if ($SignLocalBeta -and ':app:assembleRelease' -notin $Tasks) {
    $Tasks += ':app:assembleRelease'
}

$environment = @{
    JAVA_HOME = $jdk
    ANDROID_HOME = $sdk
    ANDROID_SDK_ROOT = $sdk
    GRADLE_USER_HOME = (Join-Path $ToolchainRoot 'gradle-cache')
    TEMP = $tempRoot
    TMP = $tempRoot
}
$originalEnvironment = @{}
foreach ($name in @($environment.Keys) + 'LUMA_LOCAL_BETA_PASSWORD') {
    $originalEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
}

try {
    New-Item -ItemType Directory -Path $downloadRoot, $tempRoot -Force | Out-Null
    foreach ($name in $environment.Keys) {
        [Environment]::SetEnvironmentVariable($name, $environment[$name], 'Process')
    }

    Install-Archive `
        'https://github.com/adoptium/temurin17-binaries/releases/download/jdk-17.0.21%2B9/OpenJDK17U-jdk_x64_windows_hotspot_17.0.21_9.zip' `
        '98518ecf554465057ee5509462631deeee374bc368c12a411d225cd29381f84b' $jdk 'bin/java.exe'
    Install-Archive 'https://downloads.gradle.org/distributions/gradle-8.13-bin.zip' `
        '20f1b1176237254a6fc204d8434196fa11a4cfb387567519c61556e8710aed78' $gradle 'bin/gradle.bat'
    Install-Archive 'https://dl.google.com/android/repository/platform-36_r02.zip' `
        '37607369a28c5b640b3a7998868d45898ebcb777565a0e85f9acf36f29631d2e' $platform 'android.jar'
    Install-Archive 'https://dl.google.com/android/repository/build-tools_r36_windows.zip' `
        'aa1095cb14d83e483818a748a2c06faaeb8e601561b06a356a119a1b2ca280d3' $buildTools 'apksigner.bat'
    Install-Archive 'https://dl.google.com/android/repository/platform-tools_r37.0.3-win.zip' `
        'b5d2015f5d5b7a24a2ac646a245f20f63fb501e4174c61863c74bed1df6de4bd' `
        (Join-Path $sdk 'platform-tools') 'adb.exe'

    & (Join-Path $gradle 'bin/gradle.bat') --project-dir $PSScriptRoot `
        --project-cache-dir (Join-Path $ToolchainRoot 'project-cache') --console=plain `
        --no-daemon "-PlumaBuildRoot=$buildRoot" @Tasks
    if ($LASTEXITCODE -ne 0) { throw "Android Gradle tasks failed: $LASTEXITCODE" }

    if ($SignLocalBeta) { Sign-LocalBetaApk }
} finally {
    foreach ($name in $originalEnvironment.Keys) {
        [Environment]::SetEnvironmentVariable($name, $originalEnvironment[$name], 'Process')
    }
}
