param()
$ErrorActionPreference='Stop'
$asrProject=Split-Path -Parent $PSScriptRoot
. (Join-Path $asrProject 'scripts/install-asr-cuda.ps1')
$asrRoot=Join-Path $asrProject ('.deps/asr-cuda-installer-tests/'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $asrRoot -Force | Out-Null
$script:asrFailures=0
function Assert-Asr([bool]$Condition,[string]$Message){if(!$Condition){throw $Message}}
function Test-AsrCase([string]$Name,[scriptblock]$Body){try{& $Body;Write-Output "PASS $Name"}catch{$script:asrFailures++;Write-Output "FAIL $Name : $($_.Exception.Message)"}}
function New-AsrFixture([string]$Name){
    $player=Join-Path $asrRoot $Name;$downloads=Join-Path $player 'runtime/ai/.cuda-downloads'
    New-Item -ItemType Directory -Path $downloads -Force | Out-Null
    $lock=Get-Content -LiteralPath (Join-Path $asrProject 'dependencies.lock.json') -Raw | ConvertFrom-Json
    $source=Join-Path $player 'owned-addon';New-Item -ItemType Directory -Path $source -Force | Out-Null
    Add-Type -AssemblyName System.IO.Compression
    $archive=Join-Path $downloads $lock.asrCuda.archive;$stream=[IO.File]::Open($archive,[IO.FileMode]::Create);$zip=[IO.Compression.ZipArchive]::new($stream,[IO.Compression.ZipArchiveMode]::Create)
    try{foreach($file in $lock.asrCuda.files){
        $bytes=[Text.Encoding]::UTF8.GetBytes('self-owned '+$file.name);$path=Join-Path $source $file.name;[IO.File]::WriteAllBytes($path,$bytes)
        $file.bytes=$bytes.Length;$file.sha256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
        $entry=$zip.CreateEntry('Release/'+$file.name);$entryStream=$entry.Open();try{$entryStream.Write($bytes,0,$bytes.Length)}finally{$entryStream.Dispose()}
    }}finally{$zip.Dispose();$stream.Dispose()}
    $lock.asrCuda.bytes=(Get-Item -LiteralPath $archive).Length;$lock.asrCuda.sha256=(Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
    $lock | ConvertTo-Json -Depth 14 | Set-Content -LiteralPath (Join-Path $player 'dependencies.lock.json') -Encoding UTF8
    $licenses=Join-Path $player 'LICENSES';New-Item -ItemType Directory -Path (Join-Path $licenses 'translation/cuda') -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $asrProject 'LICENSES/Whisper-MIT.txt') -Destination $licenses
    Copy-Item -LiteralPath (Join-Path $asrProject 'LICENSES/translation/cuda/NVIDIA-CUDA-12.4-EULA.html') -Destination (Join-Path $licenses 'translation/cuda')
    return $player
}
try{
    Test-AsrCase 'ASR addon is pinned to whisper b5454 and contains no base DLL replacements' {
        $spec=Get-LumaAsrCudaSpec -Directory $asrProject
        Assert-Asr ($spec.release -eq 'b5454' -and $spec.files.Count -eq 4) 'Wrong addon source or file count.'
        foreach($name in @('whisper.dll','ggml.dll','ggml-base.dll')){Assert-Asr ($spec.files.name -notcontains $name) 'An own base DLL must not be replaced.'}
    }
    Test-AsrCase 'two installs reuse verified local bytes and preserve own base libraries' {
        $player=New-AsrFixture 'player';$target=Join-Path $player 'runtime/ai'
        foreach($name in @('whisper.dll','ggml-base.dll','ggml.dll')){[IO.File]::WriteAllText((Join-Path $target $name),'own-base')}
        $first=Install-LumaAsrCuda -Directory $player;Assert-Asr (!$first.AlreadyInstalled -and $first.BytesTransferred -eq 0) 'The fixture ZIP should install offline.'
        $second=Install-LumaAsrCuda -Directory $player;Assert-Asr ($second.AlreadyInstalled -and $second.BytesTransferred -eq 0) 'Verified addon was not reused.'
        foreach($name in @('whisper.dll','ggml-base.dll','ggml.dll')){Assert-Asr ([IO.File]::ReadAllText((Join-Path $target $name)) -eq 'own-base') 'An own base DLL changed.'}
        Assert-Asr (Test-Path -LiteralPath (Join-Path $target 'asr-cuda-licenses/NVIDIA-CUDA-12.4-EULA.html')) 'Raw NVIDIA license was not staged.'
    }
    Test-AsrCase 'incorrect addon bytes are rejected before publishing any DLL' {
        $player=New-AsrFixture 'corrupt';$spec=Get-LumaAsrCudaSpec -Directory $player;$source=Join-Path $player 'owned-addon';$target=Join-Path $player 'output'
        [IO.File]::WriteAllText((Join-Path $source 'ggml-cuda.dll'),'wrong source')
        $failed=$false;try{Copy-LumaAsrCudaAddon -Spec $spec -Source $source -Destination $target -Notices @(Get-LumaAsrCudaNotices -Directory $player)}catch{$failed=$true}
        Assert-Asr $failed 'Corrupt addon bytes must fail.';Assert-Asr (!(Test-Path -LiteralPath (Join-Path $target 'cudart64_12.dll'))) 'Unverified bytes were published.'
    }
    Test-AsrCase 'unexpected existing CUDA plugin is preserved instead of mixing versions' {
        $player=New-AsrFixture 'existing';$spec=Get-LumaAsrCudaSpec -Directory $player;$target=Join-Path $player 'output';New-Item -ItemType Directory -Path $target -Force | Out-Null
        [IO.File]::WriteAllText((Join-Path $target 'ggml-cuda.dll'),'unexpected plugin')
        $failed=$false;try{Copy-LumaAsrCudaAddon -Spec $spec -Source (Join-Path $player 'owned-addon') -Destination $target -Notices @(Get-LumaAsrCudaNotices -Directory $player)}catch{$failed=$true}
        Assert-Asr $failed 'An unexpected plugin must not be overwritten.';Assert-Asr ([IO.File]::ReadAllText((Join-Path $target 'ggml-cuda.dll')) -eq 'unexpected plugin') 'The old plugin changed.'
    }
    Test-AsrCase 'manifest cannot request a DLL outside the fixed addon list' {
        $player=New-AsrFixture 'bad-lock';$path=Join-Path $player 'dependencies.lock.json';$lock=Get-Content -LiteralPath $path -Raw | ConvertFrom-Json
        $lock.asrCuda.files[0].name='../ggml-base.dll';$lock | ConvertTo-Json -Depth 14 | Set-Content -LiteralPath $path -Encoding UTF8
        $failed=$false;try{Get-LumaAsrCudaSpec -Directory $player | Out-Null}catch{$failed=$true};Assert-Asr $failed 'An escaping addon path must fail.'
    }
}finally{
    $parent=[IO.Path]::GetFullPath((Join-Path $asrProject '.deps/asr-cuda-installer-tests'))+[IO.Path]::DirectorySeparatorChar
    if(![IO.Path]::GetFullPath($asrRoot).StartsWith($parent,[StringComparison]::OrdinalIgnoreCase)){throw 'Unsafe ASR test cleanup.'};Remove-Item -LiteralPath $asrRoot -Recurse -Force
}
if($script:asrFailures){throw "$script:asrFailures ASR CUDA installer test(s) failed."}
