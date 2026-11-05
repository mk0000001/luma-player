[CmdletBinding()]
param([string]$PlayerDirectory,[switch]$Development)
# Reuse the bounded, hash-checked app-local transport. No translation model is required.
. (Join-Path $PSScriptRoot 'install-translation-cuda.ps1') -PlayerDirectory $PlayerDirectory -Development:$Development

function Get-LumaAsrCudaSpec {
    param([string]$Directory)
    $candidates=@((Join-Path $Directory 'dependencies.lock.json'),(Join-Path $Directory 'licenses/dependencies.lock.json'))
    $path=$candidates | Where-Object {Test-Path -LiteralPath $_ -PathType Leaf} | Select-Object -First 1
    if(!$path){throw 'Missing pinned ASR dependency manifest.'}
    $spec=(Get-Content -LiteralPath $path -Raw -Encoding UTF8 | ConvertFrom-Json).asrCuda
    if(!$spec -or !$spec.optional -or $spec.release -cne 'b5454' -or $spec.archive -cne 'whisper-bin-win-cuda-12.4.0-x64.zip' -or $spec.url -cne 'https://github.com/ggml-org/whisper.cpp/releases/download/b5454/whisper-bin-win-cuda-12.4.0-x64.zip' -or $spec.sha256 -notmatch '^[a-f0-9]{64}$' -or [long]$spec.bytes -le 0 -or [long]$spec.bytes -gt 2GB){throw 'Invalid pinned whisper b5454 CUDA metadata.'}
    $names=@('cudart64_12.dll','cublas64_12.dll','cublasLt64_12.dll','ggml-cuda.dll')
    if(@($spec.files).Count -ne $names.Count){throw 'Invalid ASR CUDA addon list.'}
    foreach($name in $names){$files=@($spec.files | Where-Object {$_.name -ceq $name});if($files.Count -ne 1 -or $files[0].sha256 -notmatch '^[a-f0-9]{64}$' -or [long]$files[0].bytes -le 0 -or [long]$files[0].bytes -gt 2GB){throw 'Invalid ASR CUDA addon pin.'}}
    return $spec
}
function Test-LumaAsrCudaFiles {
    param($Spec,[string]$Directory)
    foreach($file in $Spec.files){if(!(Test-LumaCudaFile (Get-LumaCudaScopedPath -Directory $Directory -RelativePath $file.name) $file.sha256 $file.bytes)){return $false}}
    return $true
}
function Get-LumaAsrCudaNotices {
    param([string]$Directory)
    $notices=@(
        @{path='Whisper-MIT.txt';hash='94f29bbed6a22c35b992c5c6ebf0e7c92f13b836b90f36f461c9cf2f0f1d010d'},
        @{path='translation/cuda/NVIDIA-CUDA-12.4-EULA.html';hash='182734e0cee7c6217b911ef88fc92c4a1f487d1003c24dd9dab1597a399fb7c5'}
    )
    foreach($notice in $notices){
        $candidates=@((Join-Path $Directory ('LICENSES/'+$notice.path)),(Join-Path $Directory ('licenses/LICENSES/'+$notice.path)),(Join-Path $Directory ('licenses/'+$notice.path)))
        $source=$candidates | Where-Object {Test-Path -LiteralPath $_ -PathType Leaf} | Select-Object -First 1
        if(!$source -or (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash -ne $notice.hash){throw 'Raw ASR CUDA license is missing or changed.'}
        [pscustomobject]@{Source=$source;Name=[IO.Path]::GetFileName($source);Hash=$notice.hash}
    }
}
function Copy-LumaAsrCudaAddon {
    param($Spec,[string]$Source,[string]$Destination,[object[]]$Notices)
    if(!(Test-LumaAsrCudaFiles -Spec $Spec -Directory $Source)){throw 'ASR CUDA addon size or SHA-256 mismatch.'}
    foreach($notice in $Notices){if((Get-FileHash -LiteralPath $notice.Source -Algorithm SHA256).Hash -ne $notice.Hash){throw 'ASR CUDA license changed.'}}
    New-Item -ItemType Directory -Path $Destination -Force | Out-Null
    foreach($file in $Spec.files){
        $target=Get-LumaCudaScopedPath -Directory $Destination -RelativePath $file.name
        if((Test-Path -LiteralPath $target) -and !(Test-LumaCudaFile $target $file.sha256 $file.bytes)){throw "Refusing to replace an unexpected ASR DLL: $($file.name)"}
    }
    # Publish the backend last. Existing own Whisper/GGML DLLs are never replaced.
    foreach($name in @('cudart64_12.dll','cublas64_12.dll','cublasLt64_12.dll','ggml-cuda.dll')){
        $target=Get-LumaCudaScopedPath -Directory $Destination -RelativePath $name
        if(!(Test-Path -LiteralPath $target)){
            $temporary=Get-LumaCudaScopedPath -Directory $Destination -RelativePath ('.asr-cuda-'+[guid]::NewGuid().ToString('N')+'.tmp')
            try{Copy-Item -LiteralPath (Join-Path $Source $name) -Destination $temporary;[IO.File]::Move($temporary,$target)}finally{if(Test-Path -LiteralPath $temporary){[IO.File]::Delete($temporary)}}
        }
    }
    $licenses=Get-LumaCudaScopedPath -Directory $Destination -RelativePath 'asr-cuda-licenses';New-Item -ItemType Directory -Path $licenses -Force | Out-Null
    foreach($notice in $Notices){if((Get-FileHash -LiteralPath $notice.Source -Algorithm SHA256).Hash -ne $notice.Hash){throw 'ASR CUDA license changed.'};Copy-Item -LiteralPath $notice.Source -Destination (Join-Path $licenses $notice.Name) -Force}
}
function Install-LumaAsrCuda {
    param([string]$Directory,[switch]$Development)
    $ErrorActionPreference='Stop'
    if($env:OS -ne 'Windows_NT' -or $env:PROCESSOR_ARCHITECTURE -eq 'ARM64' -or $env:PROCESSOR_ARCHITEW6432 -eq 'ARM64'){throw 'This optional ASR CUDA runtime requires Windows x64.'}
    $root=[IO.Path]::GetFullPath($Directory);$spec=Get-LumaAsrCudaSpec -Directory $root;$notices=@(Get-LumaAsrCudaNotices -Directory $root)
    $target=Get-LumaCudaScopedPath -Directory $root -RelativePath $(if($Development){'.deps/asr-cuda'}else{'runtime/ai'})
    $downloads=Get-LumaCudaScopedPath -Directory $root -RelativePath $(if($Development){'.deps/downloads'}else{'runtime/ai/.cuda-downloads'})
    New-Item -ItemType Directory -Path $target,$downloads -Force | Out-Null
    $guard=$null;$stage=$null
    try{
        $guard=[IO.File]::Open((Join-Path $target '.asr-cuda-install.lock'),[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
        if(Test-LumaAsrCudaFiles -Spec $spec -Directory $target){Copy-LumaAsrCudaAddon -Spec $spec -Source $target -Destination $target -Notices $notices;return [pscustomobject]@{AlreadyInstalled=$true;RuntimePath=$target;BytesTransferred=0}}
        [long]$expanded=($spec.files | Measure-Object -Property bytes -Sum).Sum
        Assert-LumaCudaDiskSpace -Directory $target -RequiredBytes ([long]$spec.bytes+2*$expanded+64MB)
        $archive=Get-LumaCudaScopedPath -Directory $downloads -RelativePath $spec.archive;[long]$transferred=0
        if(!(Test-LumaCudaFile $archive $spec.sha256 $spec.bytes)){
            $temporary=Get-LumaCudaScopedPath -Directory $downloads -RelativePath ($spec.archive+'.'+[guid]::NewGuid().ToString('N')+'.download')
            try{
                $transferred=Receive-LumaCudaFile -Uri $spec.url -Destination $temporary -ExpectedBytes $spec.bytes
                if(!(Test-LumaCudaFile $temporary $spec.sha256 $spec.bytes)){throw 'Pinned ASR CUDA ZIP checksum mismatch.'}
                if(Test-Path -LiteralPath $archive){throw 'An unexpected cached ASR CUDA archive exists; it was preserved.'};[IO.File]::Move($temporary,$archive)
            }finally{if(Test-Path -LiteralPath $temporary){[IO.File]::Delete($temporary)}}
        }
        $stage=Get-LumaCudaScopedPath -Directory $target -RelativePath ('.asr-cuda-stage-'+[guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Path $stage -Force | Out-Null
        Add-Type -AssemblyName System.IO.Compression
        $stream=[IO.File]::OpenRead($archive);$zip=[IO.Compression.ZipArchive]::new($stream,[IO.Compression.ZipArchiveMode]::Read)
        try{foreach($file in $spec.files){$entry=$zip.GetEntry('Release/'+$file.name);if(!$entry -or $entry.Length -ne $file.bytes){throw 'Pinned ASR addon is missing or truncated.'};$entryStream=$entry.Open();$output=[IO.File]::Open((Join-Path $stage $file.name),[IO.FileMode]::CreateNew,[IO.FileAccess]::Write);try{$entryStream.CopyTo($output)}finally{$output.Dispose();$entryStream.Dispose()}}}finally{$zip.Dispose();$stream.Dispose()}
        Copy-LumaAsrCudaAddon -Spec $spec -Source $stage -Destination $target -Notices $notices
        return [pscustomobject]@{AlreadyInstalled=$false;RuntimePath=$target;BytesTransferred=$transferred}
    }finally{
        try{if($stage -and (Test-Path -LiteralPath $stage)){$verified=Get-LumaCudaScopedPath -Directory $target -RelativePath ([IO.Path]::GetFileName($stage));if($verified -ne $stage -or [IO.Path]::GetFileName($stage) -notmatch '^\.asr-cuda-stage-[a-f0-9]{32}$'){throw 'Unsafe ASR CUDA staging cleanup.'};Remove-Item -LiteralPath $verified -Recurse -Force}}finally{if($guard){$guard.Dispose()}}
    }
}
if($MyInvocation.InvocationName -ne '.'){
    if([string]::IsNullOrWhiteSpace($PlayerDirectory)){$PlayerDirectory=Split-Path -Parent $PSScriptRoot}
    $result=Install-LumaAsrCuda -Directory $PlayerDirectory -Development:$Development
    Write-Output '선택 ASR CUDA 플러그인을 검증했습니다. 자체 Whisper/GGML 기본 DLL과 드라이버는 유지했습니다.'
    Write-Output 'The optional ASR CUDA plugin is verified. Own Whisper/GGML base DLLs and drivers were preserved.'
    Write-Output $result.RuntimePath
}
