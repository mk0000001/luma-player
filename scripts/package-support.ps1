function Invoke-LumaPackageGit {
    param([string]$ProjectRoot,[string[]]$Arguments)
    $output=@(& git -C $ProjectRoot -c core.quotepath=false @Arguments)
    if($LASTEXITCODE -ne 0) { throw "Package Git inspection failed: $Arguments" }
    return ($output -join "`n")
}

function Get-LumaPackageSource {
    param([string]$ProjectRoot,[switch]$AllowDirtySource)
    $root=[IO.Path]::GetFullPath($ProjectRoot).TrimEnd('\','/')
    $gitRoot=[IO.Path]::GetFullPath((Invoke-LumaPackageGit $root @('rev-parse','--show-toplevel')).Trim()).TrimEnd('\','/')
    if($gitRoot -ne $root) { throw 'Package project must be its own Git repository root.' }
    $commit=(Invoke-LumaPackageGit $root @('rev-parse','--verify','HEAD^{commit}')).Trim()
    $dirty=![string]::IsNullOrEmpty((Invoke-LumaPackageGit $root @('status','--porcelain=v1','--untracked-files=normal')))
    if($dirty -and !$AllowDirtySource) { throw 'Official packages require clean committed source. Commit reviewed changes and rebuild, or explicitly use -AllowDirtySource for development.' }
    return [pscustomobject]@{Commit=$commit;Dirty=$dirty}
}

function Assert-LumaPackageSourceUnchanged {
    param([string]$ProjectRoot,[object]$Source)
    $commit=(Invoke-LumaPackageGit $ProjectRoot @('rev-parse','--verify','HEAD^{commit}')).Trim()
    if($commit -ne $Source.Commit) { throw 'Source commit changed during packaging.' }
    # Newly created output files may be untracked; tracked source must stay at HEAD.
    if(!$Source.Dirty -and ![string]::IsNullOrEmpty((Invoke-LumaPackageGit $ProjectRoot @('status','--porcelain=v1','--untracked-files=no')))) {
        throw 'Tracked source changed during packaging.'
    }
}

function Get-LumaPackagePaths {
    param([string]$ProjectRoot,[string]$Version,[string]$BuildDirectory,[string]$OutputDirectory,[switch]$IncludeCuda)
    $root=[IO.Path]::GetFullPath($ProjectRoot)
    $build=if([IO.Path]::IsPathRooted($BuildDirectory)) { $BuildDirectory } else { Join-Path $root $BuildDirectory }
    $output=if([IO.Path]::IsPathRooted($OutputDirectory)) { $OutputDirectory } else { Join-Path $root $OutputDirectory }
    $flavor=if($IncludeCuda){'windows-x64-cuda'}else{'windows-x64'}
    $package=Join-Path ([IO.Path]::GetFullPath($output)) "LumaPlayer-$Version-$flavor"
    return [pscustomobject]@{BuildRoot=[IO.Path]::GetFullPath($build);Package=$package;Archive="$package.zip";Checksum="$package.zip.sha256";Flavor=$flavor}
}

function Assert-LumaPackageTargetsFree {
    param([object]$Paths)
    foreach($path in $Paths.Package,$Paths.Archive,$Paths.Checksum) {
        if(Test-Path -LiteralPath $path) { throw "Package destination exists; use a fresh output directory: $path" }
    }
}

function Test-LumaPackagePrivateFile {
    param([string]$Path)
    $name=[IO.Path]::GetFileName($Path)
    if($name -match '^(KEYDB\.cfg|HostKeyCertificate\.txt|ProcessingDeviceKeysSimple\.txt)$') { return $true }
    if($name -match '\.npu-profile\.json$') { return $true }
    $privateExtensions=@(
        '.key','.pem','.der','.crt','.cer','.pfx','.p12','.p7b','.p7c',
        '.srt','.vtt','.ass','.ssa','.sup','.sub','.idx','.smi','.sami','.ttml','.dfxp','.sbv','.lrc',
        '.wav','.wave','.pcm','.flac','.alac','.aac','.m4a','.mp3','.ogg','.opus','.wma','.aif','.aiff','.dsf','.dff','.ac3','.eac3','.dts','.dtshd','.mlp','.mka',
        '.m2ts','.mts','.m2t','.ts','.mp4','.mkv','.mov','.avi','.webm','.wmv','.mpg','.mpeg','.vob','.evo','.m4v',
        '.iso','.bdmv','.mpls','.clpi','.bdjo'
    )
    return [IO.Path]::GetExtension($Path).ToLowerInvariant() -in $privateExtensions
}

function Assert-LumaPackagePublicFiles {
    param([string]$Directory)
    foreach($item in Get-ChildItem -LiteralPath $Directory -Recurse -Force) {
        if($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Package cannot contain linked paths: $($item.Name)" }
        if(!$item.PSIsContainer -and (Test-LumaPackagePrivateFile $item.FullName)) { throw "Package contains private playback data: $($item.Name)" }
    }
}

function Copy-LumaPackageTrackedDocuments {
    param([string]$ProjectRoot,[string]$Destination)
    $root=[IO.Path]::GetFullPath($ProjectRoot).TrimEnd('\','/')
    $documents=(Invoke-LumaPackageGit $root @('ls-files','-z','--','docs/')).Split([char[]]@([char]0),[StringSplitOptions]::RemoveEmptyEntries)
    foreach($relative in $documents) {
        if(Test-LumaPackagePrivateFile $relative) { throw "Tracked document is private playback data: $relative" }
        $source=[IO.Path]::GetFullPath((Join-Path $root $relative))
        if(!$source.StartsWith($root+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) { throw 'Tracked document escapes the project directory.' }
        $file=Get-Item -LiteralPath $source
        if($file.PSIsContainer -or ($file.Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw "Tracked document must be a regular file: $relative" }
        $target=Join-Path $Destination $relative
        New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
        Copy-Item -LiteralPath $source -Destination $target
    }
}

function Get-LumaPackageAsrFiles {
    param([string]$ProjectRoot)
    $lock=Get-Content -LiteralPath (Join-Path $ProjectRoot 'dependencies.lock.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    if([string]$lock.model.file -notmatch '^[A-Za-z0-9_.-]+\.bin$' -or [string]$lock.model.sha256 -notmatch '^[0-9a-f]{64}$' -or
       [string]$lock.encoder.archive -notmatch '^[A-Za-z0-9_.-]+\.zip$' -or [string]$lock.encoder.sha256 -notmatch '^[0-9a-f]{64}$') { throw 'Invalid pinned core ASR metadata.' }
    $model=Join-Path $ProjectRoot ".deps/models/$($lock.model.file)"
    if(!(Test-Path -LiteralPath $model -PathType Leaf) -or (Get-FileHash -LiteralPath $model -Algorithm SHA256).Hash -ne $lock.model.sha256) { throw 'Core ASR model is missing or has the wrong SHA-256. Run bootstrap-windows.ps1 without -SkipAiModel.' }
    $files=@([pscustomobject]@{Name=$lock.model.file;Source=$model;Bytes=(Get-Item -LiteralPath $model).Length;Sha256=$lock.model.sha256})
    if($lock.highQualityModel) {
        $quality=$lock.highQualityModel
        if([string]$quality.file -notmatch '^[A-Za-z0-9_.-]+\.bin$' -or [string]$quality.sha256 -notmatch '^[0-9a-f]{64}$' -or [long]$quality.bytes -le 0) { throw 'Invalid pinned quality ASR metadata.' }
        $qualityPath=Join-Path $ProjectRoot ".deps/models/$($quality.file)"
        if(!(Test-Path -LiteralPath $qualityPath -PathType Leaf) -or (Get-Item -LiteralPath $qualityPath).Length -ne [long]$quality.bytes -or (Get-FileHash -LiteralPath $qualityPath -Algorithm SHA256).Hash -ne $quality.sha256) { throw 'Quality ASR model is missing or has the wrong size/SHA-256.' }
        $files += [pscustomobject]@{Name=$quality.file;Source=$qualityPath;Bytes=[long]$quality.bytes;Sha256=$quality.sha256}
    }
    $archive=Join-Path $ProjectRoot ".deps/downloads/$($lock.encoder.archive)"
    if(!(Test-Path -LiteralPath $archive -PathType Leaf) -or (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $lock.encoder.sha256) { throw 'Pinned ASR encoder archive is missing or has the wrong SHA-256.' }
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip=[IO.Compression.ZipFile]::OpenRead($archive)
    try {
        $stem=[IO.Path]::GetFileNameWithoutExtension($lock.model.file)+'-encoder-openvino'
        foreach($name in "$stem.xml","$stem.bin") {
            $entry=$zip.GetEntry($name)
            if(!$entry -or $entry.Length -le 0) { throw "Pinned ASR archive has no encoder: $name" }
            $stream=$entry.Open();$sha=[Security.Cryptography.SHA256]::Create()
            try { $hash=([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-','').ToLowerInvariant() }
            finally { $sha.Dispose();$stream.Dispose() }
            $source=Join-Path $ProjectRoot ".deps/models-openvino/$name"
            if(!(Test-Path -LiteralPath $source -PathType Leaf) -or (Get-Item -LiteralPath $source).Length -ne $entry.Length -or (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash -ne $hash) { throw "Core ASR encoder differs from its pinned archive: $name" }
            $files += [pscustomobject]@{Name=$name;Source=$source;Bytes=$entry.Length;Sha256=$hash}
        }
    } finally { $zip.Dispose() }
    return $files
}

function Assert-LumaPackageAsrFiles {
    param([object[]]$Files,[string]$Directory)
    foreach($file in $Files) {
        $path=Join-Path $Directory $file.Name
        if(!(Test-Path -LiteralPath $path -PathType Leaf) -or (Get-Item -LiteralPath $path).Length -ne $file.Bytes -or (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $file.Sha256) { throw "Staged core ASR model/encoder is missing or corrupt: $($file.Name)" }
    }
}

function Assert-LumaPackageTranslationModel {
    param([string]$Directory,[switch]$IncludeTranslationModel)
    $models=@(Get-ChildItem -LiteralPath $Directory -Recurse -File -Filter '*.gguf')
    if(!$IncludeTranslationModel) { if($models.Count) { throw 'Core package must omit optional translation weights.' };return }
    $spec=(Get-Content -LiteralPath (Join-Path $Directory 'translation-dependencies.lock.json') -Raw -Encoding UTF8 | ConvertFrom-Json).model
    $model=Join-Path $Directory "runtime/translation/models/$($spec.file)"
    if($models.Count -ne 1 -or !(Test-Path -LiteralPath $model -PathType Leaf) -or (Get-Item -LiteralPath $model).Length -ne $spec.bytes -or (Get-FileHash -LiteralPath $model -Algorithm SHA256).Hash -ne $spec.sha256) { throw 'Explicit translation model inclusion requires the pinned file, size and SHA-256.' }
}

function Assert-LumaPackageCoreCudaOmitted {
    param([string]$Directory)
    $root=[IO.Path]::GetFullPath($Directory).TrimEnd('\','/')
    foreach($item in Get-ChildItem -LiteralPath $root -Recurse -Force) {
        $relative=$item.FullName.Substring($root.Length+1).Replace('\','/')
        if($relative -match '^runtime/(?:translation/cuda|cuda)(?:/|$)' -or
           $relative -match '^runtime/ai/(?:ggml-cuda|cudart64_12|cublas64_12|cublasLt64_12)\.dll$' -or
           $relative -match '(?:^|/)(?:\.cuda-downloads|\.asr-cuda-downloads|\.downloads)(?:/|$)' -or
           (!$item.PSIsContainer -and $item.Name -match '\.(?:download|partial)$')) {
            throw "Core package contains optional CUDA binaries or download cache: $relative"
        }
    }
}

function Get-LumaPackageCudaFiles {
    param([string]$ProjectRoot)
    . (Join-Path $PSScriptRoot 'install-asr-cuda.ps1')
    $root=[IO.Path]::GetFullPath($ProjectRoot)
    $asr=Get-LumaAsrCudaSpec -Directory $root
    $asrDirectory=Get-LumaCudaScopedPath -Directory $root -RelativePath '.deps/asr-cuda'
    if(!(Test-LumaAsrCudaFiles -Spec $asr -Directory $asrDirectory)){throw 'Pinned ASR CUDA runtime is missing or corrupt. Run install-asr-cuda.ps1 -Development.'}
    $files=@()
    foreach($file in $asr.files){
        $files += [pscustomobject]@{Path=('runtime/ai/'+$file.name);Source=(Get-LumaCudaScopedPath -Directory $asrDirectory -RelativePath $file.name);Bytes=[long]$file.bytes;Sha256=[string]$file.sha256}
    }
    foreach($notice in @(Get-LumaAsrCudaNotices -Directory $root)){
        $files += [pscustomobject]@{Path=('runtime/ai/asr-cuda-licenses/'+$notice.Name);Source=$notice.Source;Bytes=(Get-Item -LiteralPath $notice.Source).Length;Sha256=$notice.Hash}
    }
    $spec=Get-LumaTranslationCudaSpec -Directory $root -Development
    $translation=Get-LumaCudaScopedPath -Directory $root -RelativePath '.deps/translation/cuda'
    if(!(Test-LumaTranslationCudaInstallation -Spec $spec -Directory $translation)){throw 'Pinned translation CUDA runtime is missing, stale or corrupt. Run install-translation-cuda.ps1 -Development.'}
    # Derive native file pins from the hash-verified release ZIPs. The installation's
    # own manifest cannot establish that an old binary belongs to the pinned release.
    $expected=[Collections.Generic.Dictionary[string,object]]::new([StringComparer]::OrdinalIgnoreCase)
    $seen=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    [long]$expanded=0
    Add-Type -AssemblyName System.IO.Compression
    foreach($asset in $spec.Archives){
        $archive=Get-LumaCudaScopedPath -Directory $root -RelativePath ('.deps/downloads/'+$asset.archive)
        if(!(Test-LumaCudaFile $archive $asset.sha256 $asset.bytes)){throw 'Pinned translation CUDA archive is missing or corrupt; packaging never downloads replacements.'}
        $stream=[IO.File]::OpenRead($archive);$zip=[IO.Compression.ZipArchive]::new($stream,[IO.Compression.ZipArchiveMode]::Read)
        try{
            foreach($entry in $zip.Entries){
                $name=$entry.FullName.Replace('\','/');$null=Get-LumaCudaScopedPath -Directory $translation -RelativePath $name
                if($name.EndsWith('/')){continue}
                if(!$seen.Add($name)){throw 'Duplicate pinned CUDA archive path.'}
                $expanded+=$entry.Length;if($expanded -gt $spec.MaximumExpandedBytes){throw 'Pinned CUDA archive exceeds its extraction limit.'}
                if($name -ne 'llama-server.exe' -and !($name -notmatch '/' -and $name -match '\.dll$') -and $name -notmatch '(^|/)(LICENSE|NOTICE)[^/]*$' -and $name -notmatch '^licenses/'){continue}
                $entryStream=$entry.Open();$sha=[Security.Cryptography.SHA256]::Create()
                try{$hash=([BitConverter]::ToString($sha.ComputeHash($entryStream))).Replace('-','').ToLowerInvariant()}finally{$sha.Dispose();$entryStream.Dispose()}
                $expected[$name]=[pscustomobject]@{Bytes=[long]$entry.Length;Sha256=$hash}
            }
        }finally{$zip.Dispose();$stream.Dispose()}
    }
    foreach($crt in $spec.CrtFiles){$expected[$crt.Name]=[pscustomobject]@{Bytes=[long]$crt.Bytes;Sha256=$crt.Sha256}}
    $expected['licenses/NVIDIA-CUDA-12.4-EULA.html']=[pscustomobject]@{Bytes=(Get-Item -LiteralPath $spec.LicensePath).Length;Sha256=$spec.LicenseSha256}
    foreach($notice in $spec.Notices){$expected['licenses/'+$notice.Path]=[pscustomobject]@{Bytes=(Get-Item -LiteralPath $notice.Source).Length;Sha256=$notice.Sha256}}
    $actual=@(Get-ChildItem -LiteralPath $translation -Recurse -Force -File)
    if($actual.Count -ne $expected.Count+1){throw 'Unexpected file set in the pinned translation CUDA runtime.'}
    foreach($relative in $expected.Keys){
        $pin=$expected[$relative];$path=Get-LumaCudaScopedPath -Directory $translation -RelativePath $relative
        if(!(Test-LumaCudaFile $path $pin.Sha256 $pin.Bytes)){throw "Translation CUDA file differs from its pinned release: $relative"}
        $files += [pscustomobject]@{Path=('runtime/translation/cuda/'+$relative);Source=$path;Bytes=$pin.Bytes;Sha256=$pin.Sha256}
    }
    $manifest=Get-LumaCudaScopedPath -Directory $translation -RelativePath '.luma-cuda-install.json'
    $files += [pscustomobject]@{Path='runtime/translation/cuda/.luma-cuda-install.json';Source=$manifest;Bytes=(Get-Item -LiteralPath $manifest).Length;Sha256=(Get-FileHash -LiteralPath $manifest -Algorithm SHA256).Hash.ToLowerInvariant()}
    $bundled=[Collections.Generic.Dictionary[string,object]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach($file in $files){
        if($file.Path -match '^runtime/(?:ai|translation/cuda)/(?<name>cudart64_12\.dll|cublas64_12\.dll|cublasLt64_12\.dll)$'){$file.Path='runtime/cuda/'+$matches.name}
        if($bundled.ContainsKey($file.Path)){
            $previous=$bundled[$file.Path]
            if($previous.Bytes -ne $file.Bytes -or $previous.Sha256 -ne $file.Sha256){throw 'ASR and translation CUDA require different NVIDIA DLL bytes; they cannot be shared.'}
        }else{$bundled.Add($file.Path,$file)}
    }
    return @($bundled.Values | Sort-Object Path)
}

function Move-LumaPackageCudaSharedRuntime {
    param([object[]]$Files,[string]$Directory)
    . (Join-Path $PSScriptRoot 'install-asr-cuda.ps1')
    $root=[IO.Path]::GetFullPath($Directory)
    $shared=Get-LumaCudaScopedPath -Directory $root -RelativePath 'runtime/cuda'
    if(Test-Path -LiteralPath $shared){throw 'Shared CUDA destination exists; only fresh package staging may be converted.'}
    $moves=@()
    foreach($name in 'cudart64_12.dll','cublas64_12.dll','cublasLt64_12.dll'){
        $pin=@($Files | Where-Object {$_.Path -ceq ('runtime/cuda/'+$name)})
        if($pin.Count -ne 1){throw 'Shared NVIDIA runtime pin is missing or duplicated.'}
        $asr=Get-LumaCudaScopedPath -Directory $root -RelativePath ('runtime/ai/'+$name)
        $translation=Get-LumaCudaScopedPath -Directory $root -RelativePath ('runtime/translation/cuda/'+$name)
        if(!(Test-LumaCudaFile $asr $pin[0].Sha256 $pin[0].Bytes) -or !(Test-LumaCudaFile $translation $pin[0].Sha256 $pin[0].Bytes)){throw 'Both staged NVIDIA DLL copies must match the same pin before deduplication.'}
        $moves += [pscustomobject]@{Asr=$asr;Translation=$translation;Target=(Get-LumaCudaScopedPath -Directory $root -RelativePath ('runtime/cuda/'+$name))}
    }
    # Check the complete classic layout before moving or removing any file.
    foreach($file in $Files | Where-Object {$_.Path -notlike 'runtime/cuda/*'}){
        $path=Get-LumaCudaScopedPath -Directory $root -RelativePath $file.Path
        if(!(Test-LumaCudaFile $path $file.Sha256 $file.Bytes)){throw "Staged CUDA input changed before deduplication: $($file.Path)"}
    }
    New-Item -ItemType Directory -Path $shared | Out-Null
    foreach($move in $moves){[IO.File]::Move($move.Asr,$move.Target);[IO.File]::Delete($move.Translation)}
}

function Assert-LumaPackageCudaFiles {
    param([object[]]$Files,[string]$Directory)
    if(!$Files -or !$Files.Count){throw 'CUDA package requires verified ASR and translation files.'}
    . (Join-Path $PSScriptRoot 'install-asr-cuda.ps1')
    $root=[IO.Path]::GetFullPath($Directory).TrimEnd('\','/')
    $expected=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach($file in $Files){
        if(!$expected.Add($file.Path)){throw 'Duplicate CUDA package pin.'}
        $path=Get-LumaCudaScopedPath -Directory $root -RelativePath $file.Path
        if(!(Test-LumaCudaFile $path $file.Sha256 $file.Bytes)){throw "Staged CUDA file is missing or corrupt: $($file.Path)"}
    }
    foreach($relative in 'runtime/ai/ggml-cuda.dll','runtime/translation/cuda/ggml-cuda.dll','runtime/translation/cuda/llama-server.exe'){
        if(!$expected.Contains($relative)){throw 'CUDA package must include both isolated engines.'}
    }
    foreach($relative in 'runtime/ai/luma-whisper-worker.exe','runtime/ai/whisper.dll','runtime/ai/ggml.dll','runtime/ai/ggml-base.dll','runtime/ai/ggml-cpu-x64.dll','runtime/ai/ggml-vulkan.dll','runtime/translation/vulkan/llama-server.exe','runtime/translation/vulkan/ggml.dll','runtime/translation/vulkan/ggml-base.dll','runtime/translation/vulkan/ggml-cpu-x64.dll','runtime/translation/vulkan/ggml-vulkan.dll'){
        $path=Get-LumaCudaScopedPath -Directory $root -RelativePath $relative
        if(!(Test-Path -LiteralPath $path -PathType Leaf) -or (Get-Item -LiteralPath $path).Length -le 0){throw "CUDA package is missing a CPU/Vulkan fallback: $relative"}
    }
    foreach($item in Get-ChildItem -LiteralPath $root -Recurse -Force){
        $relative=$item.FullName.Substring($root.Length+1).Replace('\','/')
        if($item.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'CUDA package contains linked paths.'}
        if($relative -match '(?:^|/)(?:\.cuda-downloads|\.asr-cuda-downloads|\.downloads)(?:/|$)' -or (!$item.PSIsContainer -and $item.Name -match '\.(?:download|partial)$')){throw "CUDA package contains download cache: $relative"}
        if(!$item.PSIsContainer -and (($relative -match '^runtime/(?:translation/cuda|cuda)/') -or ($item.Name -match '^(?:ggml-cuda|cudart64_12|cublas64_12|cublasLt64_12)\.dll$')) -and !$expected.Contains($relative)){throw "CUDA runtime is outside its verified engine directory: $relative"}
        if(!$item.PSIsContainer -and $item.Name -match '^ggml(?:-[a-z0-9_-]+)?\.dll$' -and $relative -notmatch '^runtime/(?:ai|whisper|translation/(?:cuda|vulkan|openvino))/[^/]+$'){throw "GGML must remain in its own engine directory: $relative"}
    }
}

function New-LumaPackageManifest {
    param([string]$Version,[object]$Source,[object[]]$Files,[switch]$IncludeCuda,[switch]$IncludeTranslationModel)
    $flavor=if($IncludeCuda){'windows-x64-cuda'}else{'windows-x64'}
    $layout=if($IncludeCuda){'shared-nvidia'}else{'none'}
    return [ordered]@{schema=1;version=$Version;sourceCommit=$Source.Commit;sourceDirty=[bool]$Source.Dirty;packageFlavor=$flavor;cudaIncluded=[bool]$IncludeCuda;cudaRuntimeLayout=$layout;translationModelIncluded=[bool]$IncludeTranslationModel;files=@($Files)}
}

function Assert-LumaPackageArchiveSize {
    param([long]$Bytes)
    if($Bytes -le 0 -or $Bytes -ge 2GB){throw "Compressed ZIP is $Bytes bytes; each GitHub release asset must be smaller than 2147483648 bytes."}
}

function Get-LumaPackageManifestFiles {
    param([string]$Directory)
    $root=[IO.Path]::GetFullPath($Directory).TrimEnd('\','/')
    Get-ChildItem -LiteralPath $root -Recurse -File | Sort-Object FullName | ForEach-Object {
        [ordered]@{path=$_.FullName.Substring($root.Length+1).Replace('\','/');bytes=$_.Length;sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}
    }
}
