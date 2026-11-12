[CmdletBinding()]
param([string]$PlayerDirectory,[ValidateSet('cuda','vulkan','openvino')][string]$Backend)

. (Join-Path $PSScriptRoot 'install-asr-cuda.ps1') -PlayerDirectory $PlayerDirectory

function Get-LumaAccelerationArchive {
    param([string]$Directory,[ValidateSet('vulkan','openvino')][string]$Backend)
    $Backend=$Backend.ToLowerInvariant()
    $lockPath=Get-LumaCudaScopedPath -Directory $Directory -RelativePath 'translation-dependencies.lock.json'
    $lock=Get-Content -LiteralPath $lockPath -Raw -Encoding UTF8 | ConvertFrom-Json
    $archive=@($lock.windows | Where-Object target -CEQ $Backend)
    $name=if($Backend -eq 'vulkan'){'llama-b12125-bin-win-vulkan-x64.zip'}else{'llama-b12125-bin-win-openvino-2026.4.1-x64.zip'}
    $url='https://github.com/ggml-org/llama.cpp/releases/download/b12125/'+$name
    if($lock.llamaCppRelease -cne 'b12125' -or $archive.Count -ne 1 -or
       $archive[0].id -cne ('llama-cpp-'+$Backend) -or $archive[0].archive -cne $name -or
       $archive[0].url -cne $url -or $archive[0].marker -cne 'llama-server.exe' -or
       $archive[0].sha256 -cnotmatch '^[a-f0-9]{64}$' -or [long]$archive[0].bytes -le 0 -or [long]$archive[0].bytes -gt 512MB){
        throw 'Invalid pinned acceleration archive metadata.'
    }
    return $archive[0]
}

function Expand-LumaAccelerationArchive {
    param([string]$Archive,$Spec,[string]$Destination,[ValidateSet('vulkan','openvino')][string]$Backend)
    if(!(Test-LumaCudaFile $Archive $Spec.sha256 $Spec.bytes)){throw 'Acceleration archive size or SHA-256 mismatch.'}
    Add-Type -AssemblyName System.IO.Compression
    New-Item -ItemType Directory -Path $Destination | Out-Null
    $file=[IO.File]::OpenRead($Archive)
    $zip=$null
    $names=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    [long]$expanded=0
    try{
        $zip=[IO.Compression.ZipArchive]::new($file,[IO.Compression.ZipArchiveMode]::Read)
        foreach($entry in $zip.Entries){
            $name=$entry.FullName.Replace('\','/')
            if($name -notmatch '^[A-Za-z0-9_. /-]+$' -or $name.StartsWith('/') -or
               @($name.TrimEnd('/').Split('/') | Where-Object {$_ -in '.','..','' -or $_.EndsWith('.') -or $_.EndsWith(' ')}).Count -gt 0 -or
               (($entry.ExternalAttributes -shr 16) -band 0xf000) -eq 0xa000 -or ($entry.ExternalAttributes -band 0x400)){
                throw 'Unsafe acceleration archive entry.'
            }
            if(!$names.Add($name.TrimEnd('/'))){throw 'Duplicate acceleration archive entry.'}
            $expanded+=$entry.Length
            if($expanded -gt 1GB){throw 'Acceleration archive exceeds the extraction limit.'}
            if($name.EndsWith('/')){continue}
            if($name -ne 'llama-server.exe' -and $name -notmatch '^[^/]+\.dll$' -and
               $name -notmatch '(^|/)(LICENSE|NOTICE)[^/]*$' -and $name -notmatch '^licenses/'){continue}
            $path=Get-LumaCudaScopedPath -Directory $Destination -RelativePath $name
            New-Item -ItemType Directory -Path (Split-Path -Parent $path) -Force | Out-Null
            $source=$entry.Open()
            try{
                $output=[IO.File]::Open($path,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write)
                try{$source.CopyTo($output)}finally{$output.Dispose()}
            }finally{$source.Dispose()}
        }
    }finally{
        if($zip){$zip.Dispose()}
        $file.Dispose()
    }
    foreach($name in 'llama-server.exe',('ggml-'+$Backend+'.dll')){
        if(!(Test-Path -LiteralPath (Join-Path $Destination $name) -PathType Leaf)){throw "Missing acceleration runtime file: $name"}
    }
}

function Remove-LumaAccelerationStage {
    param([string]$Base,[string]$Path)
    $name=[IO.Path]::GetFileName($Path)
    $checked=Get-LumaCudaScopedPath -Directory $Base -RelativePath $name
    if($checked -cne $Path -or $name -cnotmatch '^\.(vulkan|openvino)-(stage|previous)$'){throw 'Unsafe acceleration cleanup target.'}
    if(Test-Path -LiteralPath $checked -PathType Container){[IO.Directory]::Delete($checked,$true)}
}

function Install-LumaTranslationAcceleration {
    param([string]$Directory,[ValidateSet('vulkan','openvino')][string]$Backend)
    $spec=Get-LumaAccelerationArchive -Directory $Directory -Backend $Backend
    $base=Get-LumaCudaScopedPath -Directory $Directory -RelativePath 'runtime/translation'
    New-Item -ItemType Directory -Path $base -Force | Out-Null
    $target=Get-LumaCudaScopedPath -Directory $base -RelativePath $Backend
    $backup=Get-LumaCudaScopedPath -Directory $base -RelativePath ('.'+$Backend+'-previous')
    $lockPath=Get-LumaCudaScopedPath -Directory $base -RelativePath ('.'+$Backend+'-install.lock')
    $stage=Get-LumaCudaScopedPath -Directory $base -RelativePath ('.'+$Backend+'-stage')
    $lock=[IO.File]::Open($lockPath,[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::Write,[IO.FileShare]::None)
    $partial=$null
    try{
        if((Test-Path -LiteralPath $backup) -and !(Test-Path -LiteralPath $target)){[IO.Directory]::Move($backup,$target)}
        Remove-LumaAccelerationStage -Base $base -Path $stage
        $downloads=Get-LumaCudaScopedPath -Directory $base -RelativePath '.backend-downloads'
        New-Item -ItemType Directory -Path $downloads -Force | Out-Null
        $archive=Get-LumaCudaScopedPath -Directory $downloads -RelativePath $spec.archive
        Assert-LumaCudaDiskSpace -Directory $base -RequiredBytes ([long]$spec.bytes+1GB)
        if(!(Test-LumaCudaFile $archive $spec.sha256 $spec.bytes)){
            $partial=Get-LumaCudaScopedPath -Directory $downloads -RelativePath ($spec.archive+'.download')
            if(Test-Path -LiteralPath $partial -PathType Leaf){[IO.File]::Delete($partial)}
            Receive-LumaCudaFile -Uri $spec.url -Destination $partial -ExpectedBytes $spec.bytes | Out-Null
            if(!(Test-LumaCudaFile $partial $spec.sha256 $spec.bytes)){throw 'Acceleration download size or SHA-256 mismatch.'}
            if(Test-Path -LiteralPath $archive -PathType Leaf){[IO.File]::Replace($partial,$archive,[NullString]::Value)}else{[IO.File]::Move($partial,$archive)}
            $partial=$null
        }
        Expand-LumaAccelerationArchive -Archive $archive -Spec $spec -Destination $stage -Backend $Backend
        $manifestPath=Get-LumaCudaScopedPath -Directory $Directory -RelativePath 'manifest.json'
        $manifest=if(Test-Path -LiteralPath $manifestPath -PathType Leaf){
            Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
        }
        foreach($crt in @(Get-LumaCudaCrtFiles -Directory $Directory -PreferPlayerRoot)){
            if(!(Test-LumaCudaFile $crt.Source $crt.Sha256 $crt.Bytes)){throw 'The bundled VC runtime changed during installation.'}
            if($manifest){
                $relative=$crt.Source.Substring([IO.Path]::GetFullPath($Directory).TrimEnd('\','/').Length+1).Replace('\','/')
                $pin=@($manifest.files | Where-Object path -CEQ $relative)
                if($pin.Count -ne 1 -or $pin[0].sha256 -ine $crt.Sha256 -or [long]$pin[0].bytes -ne $crt.Bytes){throw 'The bundled VC runtime does not match the package manifest.'}
            }
            Copy-Item -LiteralPath $crt.Source -Destination (Join-Path $stage $crt.Name) -Force
        }
        # Never delete an active backend until its replacement has been verified and staged.
        Remove-LumaAccelerationStage -Base $base -Path $backup
        if(Test-Path -LiteralPath $target){[IO.Directory]::Move($target,$backup)}
        try{[IO.Directory]::Move($stage,$target);$stage=$null}
        catch{if((Test-Path -LiteralPath $backup) -and !(Test-Path -LiteralPath $target)){[IO.Directory]::Move($backup,$target)};throw}
        Remove-LumaAccelerationStage -Base $base -Path $backup
    }finally{
        try{
            if($partial -and (Test-Path -LiteralPath $partial -PathType Leaf)){[IO.File]::Delete($partial)}
            if($stage){Remove-LumaAccelerationStage -Base $base -Path $stage}
        }finally{$lock.Dispose()}
    }
}

function Test-LumaSharedCudaAcceleration {
    param([string]$Directory)
    try{
        $path=Get-LumaCudaScopedPath -Directory $Directory -RelativePath 'manifest.json'
        $manifest=Get-Content -LiteralPath $path -Raw -Encoding UTF8 | ConvertFrom-Json
        if($manifest.cudaRuntimeLayout -cne 'shared-nvidia'){return $false}
        $files=@($manifest.files | Where-Object {$_.path -match '^runtime/(?:cuda/|translation/cuda/|ai/ggml-cuda\.dll$)'})
        foreach($required in 'runtime/ai/ggml-cuda.dll','runtime/cuda/cudart64_12.dll','runtime/cuda/cublas64_12.dll','runtime/cuda/cublasLt64_12.dll','runtime/translation/cuda/llama-server.exe','runtime/translation/cuda/ggml-cuda.dll'){
            if(@($files | Where-Object path -CEQ $required).Count -ne 1){return $false}
        }
        foreach($entry in $files){
            if(!(Test-LumaCudaFile (Get-LumaCudaScopedPath -Directory $Directory -RelativePath $entry.path) $entry.sha256 $entry.bytes)){return $false}
        }
        return $true
    }catch{return $false}
}

if($MyInvocation.InvocationName -ne '.'){
    $ErrorActionPreference='Stop'
    if($env:OS -ne 'Windows_NT' -or $env:PROCESSOR_ARCHITECTURE -eq 'ARM64' -or $env:PROCESSOR_ARCHITEW6432 -eq 'ARM64'){throw 'AI acceleration packages require Windows x64.'}
    if(!$Backend){throw 'Select an AI acceleration backend.'}
    $Backend=$Backend.ToLowerInvariant()
    if([string]::IsNullOrWhiteSpace($PlayerDirectory)){$PlayerDirectory=Split-Path -Parent $PSScriptRoot}
    $directory=[IO.Path]::GetFullPath($PlayerDirectory)
    if($Backend -eq 'cuda'){
        if(!(Test-LumaSharedCudaAcceleration -Directory $directory)){
            Install-LumaAsrCuda -Directory $directory | Out-Null
            Install-LumaTranslationCuda -Directory $directory | Out-Null
        }
    }else{
        Install-LumaTranslationAcceleration -Directory $directory -Backend $Backend
    }
    Write-Output 'App-local AI acceleration files verified. Restart LumaPlayer to use the installed backend.'
}
