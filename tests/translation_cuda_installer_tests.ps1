param()
$ErrorActionPreference='Stop'
$cudaProject=Split-Path -Parent $PSScriptRoot
$script:cudaFailures=0
function Assert-Cuda([bool]$Condition,[string]$Message){if(!$Condition){throw $Message}}
function Test-CudaCase([string]$Name,[scriptblock]$Body){try{& $Body;Write-Output "PASS $Name"}catch{$script:cudaFailures++;Write-Output "FAIL $Name : $($_.Exception.Message)"}}
Test-CudaCase 'CUDA is pinned and optional instead of entering the default core download' {
    $lock=Get-Content -LiteralPath (Join-Path $cudaProject 'translation-dependencies.lock.json') -Raw | ConvertFrom-Json
    $assets=@($lock.windows | Where-Object {$_.target -eq 'cuda'})
    Assert-Cuda ($assets.Count -eq 2) 'Expected separate pinned CUDA server and CUDA runtime archives.'
    foreach($asset in $assets){Assert-Cuda ($asset.optional -eq $true) 'CUDA must be optional.';Assert-Cuda ($asset.sha256 -match '^[a-f0-9]{64}$') 'Missing pinned CUDA hash.';Assert-Cuda ($asset.bytes -gt 0) 'Missing pinned CUDA size.'}
}
$installer=Join-Path $cudaProject 'scripts/install-translation-cuda.ps1'
if(Test-Path -LiteralPath $installer){. $installer}
$cudaTestRoot=Join-Path $cudaProject ('.deps/cuda-installer-tests/'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $cudaTestRoot -Force | Out-Null
function New-CudaFixture([string]$Name,[switch]$Traversal){
    $player=Join-Path $cudaTestRoot $Name
    $downloads=Join-Path $player 'runtime/translation/.cuda-downloads'
    New-Item -ItemType Directory -Path $downloads -Force | Out-Null
    $lock=Get-Content -LiteralPath (Join-Path $cudaProject 'translation-dependencies.lock.json') -Raw | ConvertFrom-Json
    Add-Type -AssemblyName System.IO.Compression
    foreach($asset in @($lock.windows | Where-Object {$_.target -eq 'cuda'})){
        $path=Join-Path $downloads $asset.archive
        $stream=[IO.File]::Open($path,[IO.FileMode]::Create)
        $zip=[IO.Compression.ZipArchive]::new($stream,[IO.Compression.ZipArchiveMode]::Create)
        try{
            $names=if($asset.id -eq 'llama-cpp-cuda'){@('llama-server.exe','ggml-cuda.dll','LICENSE')}else{@('cudart64_12.dll','cublas64_12.dll','cublasLt64_12.dll')}
            if($Traversal -and $asset.id -eq 'llama-cpp-cuda'){$names+= '../escaped.dll'}
            foreach($name in $names){$entry=$zip.CreateEntry($name);$writer=[IO.StreamWriter]::new($entry.Open());try{$writer.Write('self-owned fixture '+$name)}finally{$writer.Dispose()}}
        }finally{$zip.Dispose();$stream.Dispose()}
        $asset.sha256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant();$asset.bytes=(Get-Item -LiteralPath $path).Length
    }
    $lock | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $player 'translation-dependencies.lock.json') -Encoding UTF8
    $licenses=Join-Path $player 'licenses/translation';New-Item -ItemType Directory -Path $licenses -Force | Out-Null
    Get-ChildItem -LiteralPath (Join-Path $cudaProject 'LICENSES/translation') | ForEach-Object {Copy-Item -LiteralPath $_.FullName -Destination $licenses -Recurse -Force}
    $crt=Join-Path $player 'runtime/translation/vulkan';New-Item -ItemType Directory -Path $crt -Force | Out-Null
    foreach($name in @('msvcp140.dll','vcruntime140.dll','vcruntime140_1.dll')){[IO.File]::WriteAllText((Join-Path $crt $name),'owned CRT fixture '+$name)}
    return $player
}
try{
    Test-CudaCase 'optional CUDA install carries the parent package VC runtime app-locally' {
        $player=New-CudaFixture 'CRT-copy';$installed=Install-LumaTranslationCuda -Directory $player
        foreach($name in @('msvcp140.dll','vcruntime140.dll','vcruntime140_1.dll')){
            $source=Join-Path $player ('runtime/translation/vulkan/'+$name);$target=Join-Path $installed.RuntimePath $name
            Assert-Cuda (Test-Path -LiteralPath $target) ('Missing portable CRT: '+$name)
            Assert-Cuda ((Get-FileHash -LiteralPath $source).Hash -eq (Get-FileHash -LiteralPath $target).Hash) 'The package CRT changed during installation.'
        }
    }
    Test-CudaCase 'missing bundled CRT fails before downloading optional CUDA' {
        $player=New-CudaFixture 'CRT-missing';$path=Get-LumaCudaScopedPath -Directory $player -RelativePath 'runtime/translation/vulkan/msvcp140.dll';[IO.File]::Delete($path)
        $failed=$false;try{Install-LumaTranslationCuda -Directory $player | Out-Null}catch{$failed=$true}
        Assert-Cuda $failed 'A copied standalone installer must not depend on an unknown system CRT.'
        Assert-Cuda (!(Test-Path -LiteralPath (Join-Path $player 'runtime/translation/cuda'))) 'Missing CRT must fail before installation.'
    }
    Test-CudaCase 'clean PATH child loads its CRT from the installed CUDA folder' {
        $player=New-CudaFixture 'native CRT 한국어'
        $crt=@(Get-LumaCudaCrtFiles -Directory $cudaProject -Development)
        $bundle=Join-Path $player 'runtime/translation/vulkan'
        foreach($file in $crt){Copy-Item -LiteralPath $file.Source -Destination (Join-Path $bundle $file.Name) -Force}
        $installed=Install-LumaTranslationCuda -Directory $player
        $source=Join-Path $cudaTestRoot 'crt-probe.cpp';$binary=Join-Path $installed.RuntimePath 'crt-probe.exe'
        @'
#include <windows.h>
#include <iostream>
#include <stdexcept>
#include <string>
int main(int argc,char **){
    try { if(argc>1)throw std::runtime_error("owned fixture"); } catch(const std::exception &) {}
    wchar_t executable[32768];if(!GetModuleFileNameW(nullptr,executable,32768))return 10;
    const std::wstring path(executable);const auto folder=path.substr(0,path.find_last_of(L"\\/")+1);
    for(const auto name:{L"msvcp140.dll",L"vcruntime140.dll",L"vcruntime140_1.dll"}){
        auto module=GetModuleHandleW(name);if(!module)module=LoadLibraryW(name);if(!module)return 11;
        wchar_t loaded[32768];if(!GetModuleFileNameW(module,loaded,32768))return 12;
        if(_wcsicmp(loaded,(folder+name).c_str())!=0)return 13;
    }
    std::cout<<"app-local VC runtime verified\n";return 0;
}
'@ | Set-Content -LiteralPath $source -Encoding UTF8
        $vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
        $installation=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        $devcmd=Join-Path $installation 'Common7/Tools/VsDevCmd.bat'
        & $env:ComSpec /d /c "`"$devcmd`" -arch=x64 -host_arch=x64 >nul && set" | ForEach-Object {if($_ -match '^([^=]+)=(.*)$'){[Environment]::SetEnvironmentVariable($matches[1],$matches[2],'Process')}}
        & cl.exe /nologo /MD /EHsc /std:c++17 /utf-8 $source "/Fe:$binary" "/Fo:$cudaTestRoot/crt-probe.obj" *> (Join-Path $cudaTestRoot 'crt-probe-build.txt')
        Assert-Cuda ($LASTEXITCODE -eq 0) 'The controlled native CRT probe did not compile.'
        $start=[Diagnostics.ProcessStartInfo]::new();$start.FileName=$binary;$start.Arguments='owned';$start.WorkingDirectory=$cudaTestRoot;$start.UseShellExecute=$false;$start.RedirectStandardOutput=$true;$start.RedirectStandardError=$true;$start.EnvironmentVariables['PATH']=Join-Path $env:SystemRoot 'System32'
        $process=[Diagnostics.Process]::Start($start)
        try{
            if(!$process.WaitForExit(10000)){$process.Kill();throw 'Native local-CRT probe timed out.'}
            Assert-Cuda ($process.ExitCode -eq 0) ('The native child did not use app-local CRT files; exit '+$process.ExitCode)
            Assert-Cuda ($process.StandardOutput.ReadToEnd().Contains('app-local VC runtime verified')) 'Missing native loader proof.'
        }finally{$process.Dispose()}
    }
    Test-CudaCase 'validated two-archive install is app-local and reused without network' {
        Assert-Cuda ([bool](Get-Command Install-LumaTranslationCuda -ErrorAction SilentlyContinue)) 'CUDA installer is not implemented.'
        $player=New-CudaFixture 'player 한국어'
        $installed=Install-LumaTranslationCuda -Directory $player
        Assert-Cuda (!$installed.AlreadyInstalled -and $installed.BytesTransferred -eq 0) 'Cached pinned archives should install without network.'
        Assert-Cuda (Test-Path -LiteralPath (Join-Path $installed.RuntimePath 'llama-server.exe')) 'Missing native CUDA server.'
        Assert-Cuda (Test-Path -LiteralPath (Join-Path $installed.RuntimePath 'licenses/NVIDIA-CUDA-12.4-EULA.html')) 'Raw NVIDIA license was not staged.'
        $second=Install-LumaTranslationCuda -Directory $player
        Assert-Cuda ($second.AlreadyInstalled -and $second.BytesTransferred -eq 0) 'Verified installation should be reused.'
    }
    Test-CudaCase 'corrupt archive preserves the existing installation' {
        Assert-Cuda ([bool](Get-Command Install-LumaTranslationCuda -ErrorAction SilentlyContinue)) 'CUDA installer is not implemented.'
        $player=New-CudaFixture 'corrupt'
        $runtime=Join-Path $player 'runtime/translation/cuda';New-Item -ItemType Directory -Path $runtime -Force | Out-Null
        [IO.File]::WriteAllText((Join-Path $runtime 'keep.txt'),'existing runtime')
        $spec=Get-LumaTranslationCudaSpec -Directory $player
        $archive=Join-Path $player ('runtime/translation/.cuda-downloads/'+$spec.Archives[0].archive)
        [IO.File]::WriteAllText($archive,'bad archive')
        $failed=$false
        try{Expand-LumaTranslationCudaArchives -Spec $spec -ArchiveDirectory (Split-Path -Parent $archive) -Destination (Join-Path $player 'runtime/translation/.cuda-stage-test')}catch{$failed=$true}
        Assert-Cuda $failed 'Corrupt archives must be rejected before extracting.'
        Assert-Cuda ([IO.File]::ReadAllText((Join-Path $runtime 'keep.txt')) -eq 'existing runtime') 'The existing runtime changed.'
    }
    Test-CudaCase 'zip traversal is rejected before writing outside staging' {
        Assert-Cuda ([bool](Get-Command Install-LumaTranslationCuda -ErrorAction SilentlyContinue)) 'CUDA installer is not implemented.'
        $player=New-CudaFixture 'traversal' -Traversal
        $spec=Get-LumaTranslationCudaSpec -Directory $player;$failed=$false
        try{Expand-LumaTranslationCudaArchives -Spec $spec -ArchiveDirectory (Join-Path $player 'runtime/translation/.cuda-downloads') -Destination (Join-Path $player 'runtime/translation/.cuda-stage-test')}catch{$failed=$true}
        Assert-Cuda $failed 'A parent-directory archive entry must fail.'
        Assert-Cuda (!(Test-Path -LiteralPath (Join-Path $player 'runtime/translation/escaped.dll'))) 'The archive escaped staging.'
    }
    Test-CudaCase 'manifest rejects an unpinned host and path escape' {
        Assert-Cuda ([bool](Get-Command Get-LumaTranslationCudaSpec -ErrorAction SilentlyContinue)) 'CUDA installer is not implemented.'
        $player=New-CudaFixture 'bad-lock';$path=Join-Path $player 'translation-dependencies.lock.json'
        $lock=Get-Content -LiteralPath $path -Raw | ConvertFrom-Json
        ($lock.windows | Where-Object {$_.target -eq 'cuda'} | Select-Object -First 1).url='https://github.com.evil.test/asset.zip'
        $lock | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $path -Encoding UTF8
        $failed=$false;try{Get-LumaTranslationCudaSpec -Directory $player | Out-Null}catch{$failed=$true};Assert-Cuda $failed 'An unpinned URL must fail.'
        $failed=$false;try{Get-LumaCudaScopedPath -Directory $player -RelativePath '../escape' | Out-Null}catch{$failed=$true};Assert-Cuda $failed 'An app-local path must not escape its root.'
    }
    Test-CudaCase 'space is checked before downloading or extracting' {
        Assert-Cuda ([bool](Get-Command Assert-LumaCudaDiskSpace -ErrorAction SilentlyContinue)) 'CUDA installer is not implemented.'
        $failed=$false;try{Assert-LumaCudaDiskSpace -Directory $cudaTestRoot -RequiredBytes ([long]::MaxValue)}catch{$failed=$true};Assert-Cuda $failed 'Insufficient space must stop installation.'
    }
    Test-CudaCase 'concurrent installation cannot overwrite an active install' {
        $player=New-CudaFixture 'locked';$translation=Join-Path $player 'runtime/translation'
        $guard=[IO.File]::Open((Join-Path $translation '.cuda-install.lock'),[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
        try{$failed=$false;try{Install-LumaTranslationCuda -Directory $player | Out-Null}catch{$failed=$true};Assert-Cuda $failed 'An active installation lock must be respected.'}finally{$guard.Dispose()}
    }
    Test-CudaCase 'invalid installed native bytes are detected before reuse' {
        $player=New-CudaFixture 'modified';$first=Install-LumaTranslationCuda -Directory $player
        [IO.File]::WriteAllText((Join-Path $first.RuntimePath 'ggml-cuda.dll'),'modified DLL')
        $spec=Get-LumaTranslationCudaSpec -Directory $player
        Assert-Cuda (!(Test-LumaTranslationCudaInstallation -Spec $spec -Directory $first.RuntimePath)) 'Modified installed bytes must not be reused.'
        $second=Install-LumaTranslationCuda -Directory $player
        Assert-Cuda (!$second.AlreadyInstalled -and $second.BytesTransferred -eq 0) 'A verified local archive should repair changed bytes without network.'
    }
}finally{
    $expected=[IO.Path]::GetFullPath((Join-Path $cudaProject '.deps/cuda-installer-tests'))+[IO.Path]::DirectorySeparatorChar
    if(![IO.Path]::GetFullPath($cudaTestRoot).StartsWith($expected,[StringComparison]::OrdinalIgnoreCase)){throw 'Unsafe CUDA installer test cleanup.'}
    Remove-Item -LiteralPath $cudaTestRoot -Recurse -Force
}
if($script:cudaFailures -gt 0){throw "$script:cudaFailures CUDA installer test(s) failed."}
