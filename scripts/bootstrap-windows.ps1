param([switch]$SkipAiModel,[switch]$SkipLocalAi)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$projectRoot = Split-Path -Parent $PSScriptRoot
$dependencyRoot = Join-Path $projectRoot '.deps'
$downloads = Join-Path $dependencyRoot 'downloads'
New-Item -ItemType Directory -Path $downloads -Force | Out-Null
$lock = Get-Content -LiteralPath (Join-Path $projectRoot 'dependencies.lock.json') -Raw | ConvertFrom-Json
function Get-VerifiedFile([string]$Url, [string]$Destination, [string]$ExpectedHash) {
    if (!(Test-Path -LiteralPath $Destination) -or (Get-FileHash -LiteralPath $Destination -Algorithm SHA256).Hash -ne $ExpectedHash) {
        Invoke-WebRequest -Uri $Url -OutFile $Destination -TimeoutSec 240
    }
    if ((Get-FileHash -LiteralPath $Destination -Algorithm SHA256).Hash -ne $ExpectedHash) { throw "SHA-256 mismatch: $Destination" }
}
$sevenZip = Join-Path $dependencyRoot '7zip/7z.exe'
if (!$SkipLocalAi) {
    foreach($tool in @($lock.archiveTool.stub,$lock.archiveTool.installer)) { Get-VerifiedFile $tool.url (Join-Path $downloads $tool.archive) $tool.sha256 }
    if (!(Test-Path -LiteralPath $sevenZip)) {
        & (Join-Path $downloads $lock.archiveTool.stub.archive) x (Join-Path $downloads $lock.archiveTool.installer.archive) "-o$(Join-Path $dependencyRoot '7zip')" -y | Out-Null
        if($LASTEXITCODE -ne 0 -or !(Test-Path -LiteralPath $sevenZip)) { throw 'Cannot extract verified portable 7-Zip' }
    }
}
$artifacts = @($lock.windows)
if (!$SkipLocalAi) { $artifacts += @($lock.native) }
foreach ($dependency in $artifacts) {
    $archive = Join-Path $downloads $dependency.archive
    Get-VerifiedFile $dependency.url $archive $dependency.sha256
    $target = Join-Path $dependencyRoot $dependency.target
    if (!(Test-Path -LiteralPath (Join-Path $target $dependency.marker))) {
        New-Item -ItemType Directory -Path $target -Force | Out-Null
        if ($dependency.extractor -eq '7zip') { & $sevenZip x $archive "-o$target" -y | Out-Null }
        else { & tar -xf $archive -C $target }
        if ($LASTEXITCODE -ne 0) { throw "Cannot extract $($dependency.id)" }
        if (!(Test-Path -LiteralPath (Join-Path $target $dependency.marker))) { throw "Missing extracted marker: $($dependency.id)" }
    }
    Write-Output "Verified $($dependency.id)"
}
$fonts = Join-Path $dependencyRoot 'fonts'
New-Item -ItemType Directory -Path $fonts -Force | Out-Null
foreach($font in $lock.fonts) { Get-VerifiedFile $font.url (Join-Path $fonts $font.file) $font.sha256 }
if (!$SkipLocalAi) {
    $sdkArchive = Join-Path $downloads $lock.shaderCompiler.archive
    Get-VerifiedFile $lock.shaderCompiler.url $sdkArchive $lock.shaderCompiler.sha256
    $sdk = Join-Path $dependencyRoot 'vulkan-sdk-minimal'
    $compiler = Join-Path $sdk 'Bin/glslc.exe'
    if (!(Test-Path -LiteralPath $compiler)) {
        & $sevenZip x $sdkArchive 'Bin/glslc.exe' "-o$sdk" -y | Out-Null
        # The Qt IFW SDK executable contains multiple archives; 7-Zip reports trailing data.
        if ($LASTEXITCODE -notin @(0,1)) { throw 'Vulkan shader compiler extraction failed' }
    }
    if (!(Test-Path -LiteralPath $compiler) -or (Get-FileHash -LiteralPath $compiler -Algorithm SHA256).Hash -ne $lock.shaderCompiler.markerSha256) { throw 'Vulkan shader compiler SHA-256 mismatch' }
}
$vlc = Join-Path $dependencyRoot 'vlc\vlc-3.0.24'
Copy-Item -LiteralPath (Join-Path $dependencyRoot 'aacs\mingw64\bin\libaacs-0.dll') -Destination (Join-Path $vlc 'libaacs.dll') -Force
Copy-Item -LiteralPath (Join-Path $dependencyRoot 'aacs\mingw64\bin\libbdplus-0.dll') -Destination (Join-Path $vlc 'libbdplus.dll') -Force
Copy-Item -LiteralPath (Join-Path $dependencyRoot 'aacs\mingw64\bin\libgcrypt-20.dll'),(Join-Path $dependencyRoot 'aacs\mingw64\bin\libgpg-error-0.dll') -Destination $vlc -Force
if (!$SkipAiModel) {
    $models = Join-Path $dependencyRoot 'models'
    New-Item -ItemType Directory -Path $models -Force | Out-Null
    Get-VerifiedFile $lock.model.url (Join-Path $models $lock.model.file) $lock.model.sha256
    if (!$SkipLocalAi) {
        $encoderArchive = Join-Path $downloads $lock.encoder.archive
        Get-VerifiedFile $lock.encoder.url $encoderArchive $lock.encoder.sha256
        $encoderRoot = Join-Path $dependencyRoot 'models-openvino'
        if (!(Test-Path -LiteralPath (Join-Path $encoderRoot 'ggml-base-encoder-openvino.xml'))) {
            New-Item -ItemType Directory -Path $encoderRoot -Force | Out-Null
            & tar -xf $encoderArchive -C $encoderRoot
            if($LASTEXITCODE -ne 0) { throw 'OpenVINO encoder extraction failed' }
        }
    }
    Write-Output 'Verified multilingual Whisper model'
}
