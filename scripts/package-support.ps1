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
    param([string]$ProjectRoot,[string]$Version,[string]$BuildDirectory,[string]$OutputDirectory)
    $root=[IO.Path]::GetFullPath($ProjectRoot)
    $build=if([IO.Path]::IsPathRooted($BuildDirectory)) { $BuildDirectory } else { Join-Path $root $BuildDirectory }
    $output=if([IO.Path]::IsPathRooted($OutputDirectory)) { $OutputDirectory } else { Join-Path $root $OutputDirectory }
    $package=Join-Path ([IO.Path]::GetFullPath($output)) "LumaPlayer-$Version-windows-x64"
    return [pscustomobject]@{BuildRoot=[IO.Path]::GetFullPath($build);Package=$package;Archive="$package.zip";Checksum="$package.zip.sha256"}
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
    $documents=(Invoke-LumaPackageGit $root @('ls-files','-z','--','docs/')).Split([char]0,[StringSplitOptions]::RemoveEmptyEntries)
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
        if($relative -match '^runtime/translation/cuda(?:/|$)' -or
           $relative -match '^runtime/ai/(?:ggml-cuda|cudart64_12|cublas64_12|cublasLt64_12)\.dll$' -or
           $relative -match '(?:^|/)(?:\.cuda-downloads|\.asr-cuda-downloads|\.downloads)(?:/|$)' -or
           (!$item.PSIsContainer -and $item.Name -match '\.(?:download|partial)$')) {
            throw "Core package contains optional CUDA binaries or download cache: $relative"
        }
    }
}

function Get-LumaPackageManifestFiles {
    param([string]$Directory)
    $root=[IO.Path]::GetFullPath($Directory).TrimEnd('\','/')
    Get-ChildItem -LiteralPath $root -Recurse -File | Sort-Object FullName | ForEach-Object {
        [ordered]@{path=$_.FullName.Substring($root.Length+1).Replace('\','/');bytes=$_.Length;sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}
    }
}
