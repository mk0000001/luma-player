param([string]$Destination = 'build',[switch]$SkipLocalAi,[switch]$SkipTranslation,[switch]$SkipTranslationModel,[switch]$IncludeTranslationOpenVino,[switch]$IncludeAsrCuda,[switch]$IncludeCuda)
$ErrorActionPreference = 'Stop'
if ($IncludeCuda) {
    if ($SkipLocalAi -or $SkipTranslation) { throw 'IncludeCuda requires both local ASR and translation runtimes.' }
    $IncludeAsrCuda=$true
}
if ($SkipLocalAi -and $IncludeAsrCuda) { throw 'IncludeAsrCuda requires the own local ASR runtime.' }
$projectRoot = Split-Path -Parent $PSScriptRoot
$dependencyLock = Get-Content -LiteralPath (Join-Path $projectRoot 'dependencies.lock.json') -Raw -Encoding UTF8 | ConvertFrom-Json
if (![IO.Path]::IsPathRooted($Destination)) { $Destination = Join-Path $projectRoot $Destination }
$runtime = Join-Path $Destination 'runtime'
foreach ($folder in 'vlc','mpv','whisper','models','tools') { New-Item -ItemType Directory -Path (Join-Path $runtime $folder) -Force | Out-Null }
New-Item -ItemType Directory -Path (Join-Path $runtime 'fonts') -Force | Out-Null
Copy-Item -Path (Join-Path $projectRoot '.deps/fonts/*.ttf'),(Join-Path $projectRoot '.deps/fonts/*.otf') -Destination (Join-Path $runtime 'fonts') -Force
$vlc = Join-Path $projectRoot '.deps\vlc\vlc-3.0.24'
Copy-Item -Path "$vlc\*.dll","$vlc\vlc.exe","$vlc\COPYING*","$vlc\AUTHORS*" -Destination (Join-Path $runtime 'vlc') -Force
Copy-Item -LiteralPath (Join-Path $vlc 'plugins') -Destination (Join-Path $runtime 'vlc') -Recurse -Force
# libbluray's Windows loader searches the host EXE directory explicitly.
foreach ($name in 'libaacs.dll','libbdplus.dll','libgcrypt-20.dll','libgpg-error-0.dll') {
    Copy-Item -LiteralPath (Join-Path $vlc $name) -Destination (Join-Path $Destination $name) -Force
}
Copy-Item -LiteralPath (Join-Path $projectRoot '.deps\mpv\libmpv-2.dll') -Destination (Join-Path $runtime 'mpv') -Force
$whisper = Join-Path $projectRoot '.deps\whisper\Release'
Copy-Item -Path "$whisper\*.dll","$whisper\whisper-cli.exe" -Destination (Join-Path $runtime 'whisper') -Force
$ffmpeg = Join-Path $projectRoot '.deps\ffmpeg-full\ffmpeg-9.0.2-full_build'
Copy-Item -LiteralPath (Join-Path $ffmpeg 'bin\ffmpeg.exe'),(Join-Path $ffmpeg 'LICENSE'),(Join-Path $ffmpeg 'README.txt') -Destination (Join-Path $runtime 'tools') -Force
foreach ($name in 'libaacs.dll','libbdplus.dll','libgcrypt-20.dll','libgpg-error-0.dll') {
    Copy-Item -LiteralPath (Join-Path $vlc $name) -Destination (Join-Path $runtime 'tools' $name) -Force
}
$model = Join-Path $projectRoot '.deps\models\ggml-base.bin'
if (Test-Path -LiteralPath $model) { Copy-Item -LiteralPath $model -Destination (Join-Path $runtime 'models') -Force }
$highQualityModel=$dependencyLock.highQualityModel
if ($highQualityModel) {
    if ($highQualityModel.file -notmatch '^[A-Za-z0-9_.-]+\.bin$' -or $highQualityModel.sha256 -notmatch '^[a-f0-9]{64}$') { throw 'Invalid pinned high-quality ASR model metadata.' }
    $highQualityPath=Join-Path $projectRoot ('.deps/models/'+$highQualityModel.file)
    if (Test-Path -LiteralPath $highQualityPath -PathType Leaf) {
        if ((Get-Item -LiteralPath $highQualityPath).Length -ne [long]$highQualityModel.bytes -or (Get-FileHash -LiteralPath $highQualityPath -Algorithm SHA256).Hash -ne $highQualityModel.sha256) { throw 'High-quality ASR model size or SHA-256 mismatch.' }
        Copy-Item -LiteralPath $highQualityPath -Destination (Join-Path $runtime 'models') -Force
    }
}
if (!$SkipLocalAi) {
    foreach($folder in 'reading','reading/ipadic','ai') { New-Item -ItemType Directory -Path (Join-Path $runtime $folder) -Force | Out-Null }
    $nativeAi = Join-Path $projectRoot '.deps/build-ai/bin'
    $mecab = Join-Path $projectRoot '.deps/build-mecab/mecab.dll'
    if (!(Test-Path -LiteralPath (Join-Path $nativeAi 'luma-whisper-worker.exe')) -or !(Test-Path -LiteralPath (Join-Path $nativeAi 'luma-whisper-encoder.exe')) -or !(Test-Path -LiteralPath $mecab)) { throw 'Build native AI first: scripts/build-local-ai.ps1' }
    Copy-Item -LiteralPath $mecab -Destination (Join-Path $runtime 'reading') -Force
    Copy-Item -Path (Join-Path $projectRoot '.deps/ipadic/*') -Destination (Join-Path $runtime 'reading/ipadic') -Force
    [IO.File]::WriteAllText((Join-Path $runtime 'reading/mecabrc'),'', [Text.UTF8Encoding]::new($false))
    Copy-Item -Path "$nativeAi/*.dll","$nativeAi/luma-whisper-worker.exe","$nativeAi/luma-whisper-encoder.exe" -Destination (Join-Path $runtime 'ai') -Force
    $openVinoDependency = @($dependencyLock.native | Where-Object id -EQ 'openvino')
    if ($openVinoDependency.Count -ne 1) { throw 'Exactly one pinned OpenVINO SDK is required.' }
    $openVinoConfig = Join-Path $projectRoot ('.deps/'+$openVinoDependency[0].target+'/'+$openVinoDependency[0].marker)
    if (!(Test-Path -LiteralPath $openVinoConfig -PathType Leaf)) { throw 'Run bootstrap-windows.ps1 to install the pinned OpenVINO SDK.' }
    $openVino = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $openVinoConfig))
    foreach ($name in 'openvino.dll','openvino_c.dll','openvino_ir_frontend.dll','openvino_intel_npu_plugin.dll','openvino_intel_npu_compiler.dll','openvino_intel_npu_compiler_loader.dll','openvino_intel_npu_vm_runtime.dll') {
        if (!(Test-Path -LiteralPath (Join-Path $openVino ('runtime/bin/intel64/Release/'+$name)) -PathType Leaf)) { throw "Pinned OpenVINO runtime is incomplete: $name" }
    }
    # Preserve the SDK's flat NPU plugin/compiler/loader/VM layout in the ASR folder.
    Copy-Item -Path "$openVino/runtime/bin/intel64/Release/*.dll" -Destination (Join-Path $runtime 'ai') -Force
    Get-ChildItem -LiteralPath "$openVino/runtime/3rdparty/tbb/bin" -Filter '*.dll' | Where-Object Name -NotMatch '_debug' | ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $runtime 'ai') -Force }
    foreach($name in 'ggml-base-encoder-openvino.xml','ggml-base-encoder-openvino.bin') {
        $encoder = Join-Path $projectRoot ".deps/models-openvino/$name"
        if(Test-Path -LiteralPath $encoder) { Copy-Item -LiteralPath $encoder -Destination (Join-Path $runtime 'models') -Force }
    }
    if($IncludeAsrCuda) {
        . (Join-Path $PSScriptRoot 'install-asr-cuda.ps1')
        $asrCudaSpec=Get-LumaAsrCudaSpec -Directory $projectRoot
        Copy-LumaAsrCudaAddon -Spec $asrCudaSpec -Source (Join-Path $projectRoot '.deps/asr-cuda') -Destination (Join-Path $runtime 'ai') -Notices @(Get-LumaAsrCudaNotices -Directory $projectRoot)
    }
}
if (!$SkipTranslation) {
    $translationArgs=@{ RuntimeDirectory=$runtime;SkipModel=$SkipTranslationModel;SkipOpenVino=(!$IncludeTranslationOpenVino);IncludeCuda=$IncludeCuda }
    & (Join-Path $PSScriptRoot 'stage-translation.ps1') @translationArgs
}
# Child EXEs do not inherit the parent's DLL search directories.
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$visualStudio=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$crt=Get-Item -Path (Join-Path $visualStudio 'VC/Redist/MSVC/*/x64/Microsoft.VC143.CRT') -ErrorAction SilentlyContinue | Sort-Object FullName -Descending | Select-Object -First 1
if(!$crt) { throw 'Microsoft VC143 redistributable runtime files are required for the portable package.' }
$crtDestinations=@($Destination,(Join-Path $runtime 'whisper'))
if(!$SkipLocalAi) { $crtDestinations+=Join-Path $runtime 'ai' }
if(!$SkipTranslation) { $crtDestinations+=Join-Path $runtime 'translation/vulkan';if($IncludeTranslationOpenVino) { $crtDestinations+=Join-Path $runtime 'translation/openvino' } }
foreach($directory in $crtDestinations) { Copy-Item -Path (Join-Path $crt.FullName '*.dll') -Destination $directory -Force }
$licenses = Join-Path $Destination 'licenses'
New-Item -ItemType Directory -Path $licenses -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $projectRoot 'LICENSE'),(Join-Path $projectRoot 'THIRD_PARTY_NOTICES.md'),(Join-Path $projectRoot 'dependencies.lock.json') -Destination $licenses -Force
Copy-Item -LiteralPath (Join-Path $projectRoot 'LICENSES') -Destination $licenses -Recurse -Force
if (!$SkipLocalAi) {
    # Runtime binaries use Intel's Distribution License, with separate third-party terms.
    # Copy the pinned SDK originals after repository notices so no stale SDK notice wins.
    $openVinoLicenses = Join-Path $licenses 'LICENSES/OpenVINO'
    New-Item -ItemType Directory -Path $openVinoLicenses -Force | Out-Null
    Copy-Item -Path (Join-Path $openVino 'docs/licensing/*') -Destination $openVinoLicenses -Force
    Copy-Item -LiteralPath (Join-Path $openVino 'runtime/3rdparty/tbb/TBB-LICENSE') -Destination $openVinoLicenses -Force
}
