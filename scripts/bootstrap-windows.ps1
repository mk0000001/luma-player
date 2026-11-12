param([switch]$SkipAiModel,[switch]$SkipLocalAi)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
. (Join-Path $PSScriptRoot 'bootstrap-support.ps1')
$projectRoot = Split-Path -Parent $PSScriptRoot
$dependencyRoot = Join-Path $projectRoot '.deps'
$downloads = Join-Path $dependencyRoot 'downloads'
New-Item -ItemType Directory -Path $downloads -Force | Out-Null
$lock = Get-Content -LiteralPath (Join-Path $projectRoot 'dependencies.lock.json') -Raw | ConvertFrom-Json
$sevenZip = Join-Path $dependencyRoot '7zip/7z.exe'
if (!$SkipLocalAi) {
    $tools = @($lock.archiveTool.stub,$lock.archiveTool.installer)
    foreach ($tool in $tools) { Get-LumaBootstrapFile $tool.url (Join-Path $downloads $tool.archive) $tool.sha256 240 }
    Install-LumaBootstrapExtraction -Target (Join-Path $dependencyRoot '7zip') -Markers '7z.exe' -Hashes $tools.sha256 -Extract {
        param($stage)
        & (Join-Path $downloads $lock.archiveTool.stub.archive) x (Join-Path $downloads $lock.archiveTool.installer.archive) "-o$stage" -y | Out-Null
        if ($LASTEXITCODE -ne 0) { throw 'Cannot extract verified portable 7-Zip' }
    }
}
$artifacts = @($lock.windows)
if (!$SkipLocalAi) { $artifacts += @($lock.native) }
foreach ($group in $artifacts | Group-Object target) {
    $dependencies = $group.Group
    foreach ($dependency in $dependencies) {
        Get-LumaBootstrapFile $dependency.url (Join-Path $downloads $dependency.archive) $dependency.sha256 240
    }
    $extraction = @{ Target=(Join-Path $dependencyRoot $group.Name); Markers=$dependencies.marker; Hashes=$dependencies.sha256 }
    # Go's archive contains its own go directory. Keep the existing toolchain path.
    if ($group.Name -eq '.') {
        if ($dependencies.Count -ne 1 -or $dependencies[0].id -cne 'go') { throw 'Unexpected archive layout in the Windows dependency lock.' }
        $extraction.Target = Join-Path $dependencyRoot 'go'
        $extraction.Markers = $dependencies[0].marker.Substring('go/'.Length)
        $extraction.ContentDirectory = 'go'
    }
    Install-LumaBootstrapExtraction @extraction -Extract {
        param($stage)
        foreach ($dependency in $dependencies) {
            $archive = Join-Path $downloads $dependency.archive
            if ($dependency.extractor -eq '7zip') { & $sevenZip x $archive "-o$stage" -y | Out-Null }
            else { & tar -xf $archive -C $stage }
            if ($LASTEXITCODE -ne 0) { throw "Cannot extract $($dependency.id)" }
        }
    }
    foreach ($dependency in $dependencies) { Write-Output "Verified $($dependency.id)" }
}
$fonts = Join-Path $dependencyRoot 'fonts'
New-Item -ItemType Directory -Path $fonts -Force | Out-Null
foreach($font in $lock.fonts) { Get-LumaBootstrapFile $font.url (Join-Path $fonts $font.file) $font.sha256 240 }
if (!$SkipLocalAi) {
    $sdkArchive = Join-Path $downloads $lock.shaderCompiler.archive
    Get-LumaBootstrapFile $lock.shaderCompiler.url $sdkArchive $lock.shaderCompiler.sha256 240
    $sdk = Join-Path $dependencyRoot 'vulkan-sdk-minimal'
    $compiler = Join-Path $sdk 'Bin/glslc.exe'
    Install-LumaBootstrapExtraction -Target $sdk -Markers 'Bin/glslc.exe' -Hashes $lock.shaderCompiler.sha256 -Extract {
        param($stage)
        & $sevenZip x $sdkArchive 'Bin/glslc.exe' "-o$stage" -y | Out-Null
        # The Qt IFW SDK executable contains multiple archives; 7-Zip reports trailing data.
        if ($LASTEXITCODE -notin @(0,1)) { throw 'Vulkan shader compiler extraction failed' }
        if ((Get-FileHash -LiteralPath (Join-Path $stage 'Bin/glslc.exe') -Algorithm SHA256).Hash -ne $lock.shaderCompiler.markerSha256) { throw 'Vulkan shader compiler SHA-256 mismatch' }
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
    Get-LumaBootstrapFile $lock.model.url (Join-Path $models $lock.model.file) $lock.model.sha256 240
    if($lock.highQualityModel) { Get-LumaBootstrapFile $lock.highQualityModel.url (Join-Path $models $lock.highQualityModel.file) $lock.highQualityModel.sha256 240 }
    if (!$SkipLocalAi) {
        $encoderArchive = Join-Path $downloads $lock.encoder.archive
        Get-LumaBootstrapFile $lock.encoder.url $encoderArchive $lock.encoder.sha256 240
        $encoderRoot = Join-Path $dependencyRoot 'models-openvino'
        Install-LumaBootstrapExtraction -Target $encoderRoot -Markers 'ggml-base-encoder-openvino.xml','ggml-base-encoder-openvino.bin' -Hashes $lock.encoder.sha256 -Extract {
            param($stage)
            & tar -xf $encoderArchive -C $stage
            if($LASTEXITCODE -ne 0) { throw 'OpenVINO encoder extraction failed' }
        }
    }
    Write-Output 'Verified multilingual Whisper model'
}
