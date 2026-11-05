param([switch]$SkipRecognition)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$installation) { throw 'Visual Studio C++ Build Tools are required.' }
$devcmd = Join-Path $installation 'Common7/Tools/VsDevCmd.bat'
& $env:ComSpec /d /c "`"$devcmd`" -arch=x64 -host_arch=x64 >nul && set" | ForEach-Object {
    if ($_ -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($matches[1],$matches[2],'Process') }
}
$env:VSLANG = '1033'
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
$OutputEncoding = [Console]::OutputEncoding
& chcp.com 65001 | Out-Null
$mecabSource = Join-Path $projectRoot '.deps/mecab-source/mecab-61b90ba6e669dc2d7d533d4a80d206f3b31d52b1/mecab'
$mecabBuild = Join-Path $projectRoot '.deps/build-mecab'
& cmake -S (Join-Path $projectRoot 'native/mecab') -B $mecabBuild -G Ninja "-DCMAKE_CXX_COMPILER=$((Get-Command cl).Source)" -DCMAKE_BUILD_TYPE=Release "-DMECAB_SOURCE=$mecabSource"
if ($LASTEXITCODE) { throw 'MeCab configuration failed' }
& cmake --build $mecabBuild --parallel 6
if ($LASTEXITCODE) { throw 'MeCab build failed' }
$dictionarySource = Join-Path (Split-Path $mecabSource -Parent) 'mecab-ipadic'
$dictionary = Join-Path $projectRoot '.deps/ipadic'
if (!(Test-Path -LiteralPath (Join-Path $dictionary 'sys.dic'))) {
    New-Item -ItemType Directory -Path $dictionary -Force | Out-Null
    & (Join-Path $mecabBuild 'mecab-dict-index.exe') -d $dictionarySource -o $dictionary -f EUC-JP -t UTF-8
    if ($LASTEXITCODE) { throw 'UTF-8 IPADIC compilation failed' }
    Copy-Item -LiteralPath (Join-Path $dictionarySource 'dicrc') -Destination $dictionary -Force
}
if (!$SkipRecognition) {
    $minimalSdk = Join-Path $projectRoot '.deps/vulkan-sdk-minimal'
    if (Test-Path -LiteralPath (Join-Path $minimalSdk 'Bin/glslc.exe')) {
        $env:VULKAN_SDK = $minimalSdk
        New-Item -ItemType Directory -Path (Join-Path $minimalSdk 'Include'),(Join-Path $minimalSdk 'Lib') -Force | Out-Null
        Copy-Item -Path (Join-Path $projectRoot '.deps/vulkan-headers/Vulkan-Headers-vulkan-sdk-1.4.350.0/include/*') -Destination (Join-Path $minimalSdk 'Include') -Recurse -Force
        $spirv = Join-Path $projectRoot '.deps/spirv-headers/SPIRV-Headers-vulkan-sdk-1.4.350.0'
        & cmake -S $spirv -B (Join-Path $projectRoot '.deps/build-spirv-headers') -G Ninja "-DCMAKE_INSTALL_PREFIX=$minimalSdk" -DSPIRV_HEADERS_SKIP_EXAMPLES=ON
        if ($LASTEXITCODE) { throw 'SPIRV headers configuration failed' }
        & cmake --install (Join-Path $projectRoot '.deps/build-spirv-headers')
        if ($LASTEXITCODE) { throw 'SPIRV headers installation failed' }
        $exports = Join-Path $projectRoot '.deps/vulkan-loader/Vulkan-Loader-vulkan-sdk-1.4.350.0/loader/vulkan-1.def'
        & lib.exe /nologo "/def:$exports" /machine:x64 "/out:$minimalSdk/Lib/vulkan-1.lib"
        if ($LASTEXITCODE) { throw 'Vulkan import library generation failed' }
    }
    if (!$env:VULKAN_SDK -and (Test-Path -LiteralPath 'C:/VulkanSDK/Bin/glslc.exe')) { $env:VULKAN_SDK = 'C:/VulkanSDK' }
    if (!$env:VULKAN_SDK) { throw 'Vulkan SDK is required. Run bootstrap-windows.ps1 or set VULKAN_SDK.' }
    $openVino = Join-Path $projectRoot '.deps/openvino/w_openvino_toolkit_windows_2024.1.0.15008.f4afc983258_x86_64/runtime/cmake'
    $workerBuild = Join-Path $projectRoot '.deps/build-ai'
    & cmake -S (Join-Path $projectRoot 'native/ai_worker') -B $workerBuild -G Ninja "-DCMAKE_CXX_COMPILER=$((Get-Command cl).Source)" "-DCMAKE_C_COMPILER=$((Get-Command cl).Source)" "-DCMAKE_PREFIX_PATH=$env:VULKAN_SDK" "-DVulkan_INCLUDE_DIR=$env:VULKAN_SDK/Include" "-DVulkan_LIBRARY=$env:VULKAN_SDK/Lib/vulkan-1.lib" "-DVulkan_GLSLC_EXECUTABLE=$env:VULKAN_SDK/Bin/glslc.exe" -DCMAKE_BUILD_TYPE=Release "-DOpenVINO_DIR=$openVino" "-DWHISPER_SOURCE=$projectRoot/.deps/whisper-source/whisper.cpp-b5454"
    if ($LASTEXITCODE) { throw 'Local AI worker configuration failed' }
    & cmake --build $workerBuild --parallel 6
    if ($LASTEXITCODE) { throw 'Local AI worker build failed' }
}
