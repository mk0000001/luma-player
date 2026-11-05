[CmdletBinding()]
param([string]$PlayerDirectory,[switch]$Development)

function Get-LumaCudaScopedPath {
    param([string]$Directory,[string]$RelativePath)
    $root=[IO.Path]::GetFullPath($Directory).TrimEnd([IO.Path]::DirectorySeparatorChar)
    if([IO.Path]::IsPathRooted($RelativePath)){throw 'CUDA path must be app-local.'}
    $path=[IO.Path]::GetFullPath((Join-Path $root $RelativePath))
    if(!$path.StartsWith($root+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){throw 'CUDA path escapes the player directory.'}
    $check=$path
    while($check -and $check.Length -ge $root.Length){
        if((Test-Path -LiteralPath $check) -and ((Get-Item -LiteralPath $check -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)){throw 'CUDA installation does not follow filesystem links.'}
        if($check -eq $root){break};$check=Split-Path -Parent $check
    }
    return $path
}
function Get-LumaCudaCrtFiles {
    param([string]$Directory,[switch]$Development)
    $candidates=@((Get-LumaCudaScopedPath -Directory $Directory -RelativePath 'runtime/translation/vulkan'),[IO.Path]::GetFullPath($Directory))
    if($Development){
        $vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
        if(Test-Path -LiteralPath $vswhere){
            $installation=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
            if($installation){$redist=Get-Item -Path (Join-Path $installation 'VC/Redist/MSVC/*/x64/Microsoft.VC143.CRT') -ErrorAction SilentlyContinue | Sort-Object FullName -Descending | Select-Object -First 1;if($redist){$candidates+=$redist.FullName}}
        }
    }
    $required=@('msvcp140.dll','vcruntime140.dll','vcruntime140_1.dll')
    foreach($candidate in $candidates){
        $complete=$true;foreach($name in $required){if(!(Test-Path -LiteralPath (Join-Path $candidate $name) -PathType Leaf)){$complete=$false;break}}
        if(!$complete){continue}
        $files=@(Get-ChildItem -LiteralPath $candidate -File | Where-Object {$_.Name -match '^(msvcp140(?:_[a-z0-9_]+)?|vcruntime140(?:_[a-z0-9_]+)?|concrt140|vccorlib140|vcamp140)\.dll$'})
        foreach($file in $files){
            if($file.Length -le 0 -or ($file.Attributes -band [IO.FileAttributes]::ReparsePoint)){throw 'Invalid bundled VC runtime file.'}
            [pscustomobject]@{Name=$file.Name;Source=$file.FullName;Bytes=$file.Length;Sha256=(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}
        }
        return
    }
    throw '앱에 포함된 VC 런타임이 없습니다. 전체 플레이어 폴더에서 설치하세요 / The bundled VC runtime is missing; install from the complete player folder.'
}
function Get-LumaTranslationCudaSpec {
    param([string]$Directory,[switch]$Development)
    $candidates=@((Join-Path $Directory 'translation-dependencies.lock.json'),(Join-Path $Directory 'licenses/translation-dependencies.lock.json'))
    $path=$candidates | Where-Object {Test-Path -LiteralPath $_ -PathType Leaf} | Select-Object -First 1
    if(!$path){throw 'CUDA 잠금 파일이 없습니다 / Missing pinned translation manifest.'}
    $lock=Get-Content -LiteralPath $path -Raw -Encoding UTF8 | ConvertFrom-Json
    $archives=@($lock.windows | Where-Object {$_.target -eq 'cuda'})
    if($lock.llamaCppRelease -cne 'b12125' -or $lock.cuda.version -cne '12.4' -or $archives.Count -ne 2){throw 'Invalid pinned CUDA version or archive count.'}
    $expected=@{'llama-cpp-cuda'='llama-b12125-bin-win-cuda-12.4-x64.zip';'llama-cpp-cudart'='cudart-llama-bin-win-cuda-12.4-x64.zip'}
    $seen=@{}
    foreach($archive in $archives){
        if(!$expected.ContainsKey([string]$archive.id) -or $seen.ContainsKey([string]$archive.id) -or $archive.archive -cne $expected[$archive.id] -or $archive.optional -ne $true -or $archive.sha256 -notmatch '^[a-f0-9]{64}$' -or [long]$archive.bytes -le 0 -or [long]$archive.bytes -gt 2GB){throw 'Invalid optional CUDA archive metadata.'}
        $uri=[uri]$archive.url
        if(!$uri.IsAbsoluteUri -or $uri.Scheme -ne 'https' -or $uri.Host -ne 'github.com' -or !$uri.IsDefaultPort -or $uri.UserInfo -or $uri.Query -or $uri.Fragment -or $uri.AbsolutePath -cne ('/ggml-org/llama.cpp/releases/download/b12125/'+$archive.archive)){throw 'Invalid immutable CUDA archive URL.'}
        $seen[[string]$archive.id]=$true
    }
    if($lock.cuda.licenseFile -cne 'cuda/NVIDIA-CUDA-12.4-EULA.html' -or $lock.cuda.licenseSha256 -notmatch '^[a-f0-9]{64}$' -or [long]$lock.cuda.maximumExpandedBytes -le 0 -or [long]$lock.cuda.maximumExpandedBytes -gt 4GB){throw 'Invalid CUDA license or extraction limit.'}
    $licenseCandidates=@((Join-Path $Directory ('licenses/translation/'+$lock.cuda.licenseFile)),(Join-Path $Directory ('runtime/translation/licenses/'+$lock.cuda.licenseFile)))
    $license=$licenseCandidates | Where-Object {Test-Path -LiteralPath $_ -PathType Leaf} | Select-Object -First 1
    if(!$license -or (Get-FileHash -LiteralPath $license -Algorithm SHA256).Hash -ne $lock.cuda.licenseSha256){throw 'CUDA 원문 라이선스가 없거나 변경되었습니다 / Raw NVIDIA CUDA license is missing or changed.'}
    $notices=@()
    foreach($notice in @($lock.licenses | Where-Object {$_.file -ne $lock.cuda.licenseFile})){
        if($notice.sha256 -notmatch '^[a-f0-9]{64}$'){throw 'Invalid translation notice hash.'}
        $relative=[string]$notice.file
        if($relative -notmatch '^[A-Za-z0-9_./-]+$' -or @($relative.Split('/') | Where-Object {$_ -eq '.' -or $_ -eq '..' -or $_ -eq ''}).Count -gt 0){throw 'Invalid translation notice path.'}
        $noticeCandidates=@((Get-LumaCudaScopedPath -Directory $Directory -RelativePath ('licenses/translation/'+$relative)),(Get-LumaCudaScopedPath -Directory $Directory -RelativePath ('runtime/translation/licenses/'+$relative)))
        $source=$noticeCandidates | Where-Object {Test-Path -LiteralPath $_ -PathType Leaf} | Select-Object -First 1
        if(!$source -or (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash -ne $notice.sha256){throw "Raw translation notice is missing or changed: $relative"}
        $notices += [pscustomobject]@{Path=$relative;Source=$source;Sha256=$notice.sha256}
    }
    $crt=@(Get-LumaCudaCrtFiles -Directory $Directory -Development:$Development)
    return [pscustomobject]@{Archives=$archives;MaximumExpandedBytes=[long]$lock.cuda.maximumExpandedBytes;LicensePath=$license;LicenseSha256=[string]$lock.cuda.licenseSha256;Notices=$notices;CrtFiles=$crt}
}
function Test-LumaCudaFile {
    param([string]$Path,[string]$Hash,[long]$Bytes)
    return (Test-Path -LiteralPath $Path -PathType Leaf) -and (Get-Item -LiteralPath $Path).Length -eq $Bytes -and (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash -eq $Hash
}
function Assert-LumaCudaDiskSpace {
    param([string]$Directory,[long]$RequiredBytes)
    $drive=[IO.DriveInfo]::new([IO.Path]::GetPathRoot([IO.Path]::GetFullPath($Directory)))
    if($drive.AvailableFreeSpace -lt $RequiredBytes){throw ('CUDA 설치 공간이 부족합니다 / Insufficient CUDA installation space. Required {0:N2} GiB; available {1:N2} GiB.' -f ($RequiredBytes/1GB),($drive.AvailableFreeSpace/1GB))}
}
function Receive-LumaCudaFile {
    param([uri]$Uri,[string]$Destination,[long]$ExpectedBytes)
    Add-Type -AssemblyName System.Net.Http
    $handler=[Net.Http.HttpClientHandler]::new();$handler.AllowAutoRedirect=$true;$handler.MaxAutomaticRedirections=10
    if($handler.PSObject.Properties.Name -contains 'SslProtocols'){$handler.SslProtocols=[Security.Authentication.SslProtocols]::Tls12}
    $client=[Net.Http.HttpClient]::new($handler);$client.Timeout=[Threading.Timeout]::InfiniteTimeSpan
    $cancel=[Threading.CancellationTokenSource]::new();$cancel.CancelAfter([TimeSpan]::FromMinutes(60))
    $response=$null;$downloadStream=$null;$fileStream=$null
    try{
        $response=$client.GetAsync($Uri,[Net.Http.HttpCompletionOption]::ResponseHeadersRead,$cancel.Token).GetAwaiter().GetResult();$null=$response.EnsureSuccessStatusCode()
        $length=$response.Content.Headers.ContentLength
        if($null -ne $length -and [long]$length -ne $ExpectedBytes){throw 'CUDA response size differs from the pinned size.'}
        $downloadStream=$response.Content.ReadAsStreamAsync().GetAwaiter().GetResult()
        $fileStream=[IO.FileStream]::new($Destination,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None,1MB,[IO.FileOptions]::SequentialScan)
        $buffer=New-Object byte[] 1MB;[long]$received=0;[long]$nextProgress=0
        while(($read=$downloadStream.ReadAsync($buffer,0,$buffer.Length,$cancel.Token).GetAwaiter().GetResult()) -gt 0){
            $received+=$read;if($received -gt $ExpectedBytes){throw 'CUDA download exceeds the pinned size.'};$fileStream.Write($buffer,0,$read)
            if($received -ge $nextProgress){Write-Progress -Activity '선택 CUDA 번역 / Optional CUDA translation' -Status ('{0:N1} / {1:N1} MiB' -f ($received/1MB),($ExpectedBytes/1MB)) -PercentComplete ([Math]::Min(100,[int](100.0*$received/$ExpectedBytes)));$nextProgress=$received+16MB}
        }
        if($received -ne $ExpectedBytes){throw 'CUDA download is incomplete.'};$fileStream.Flush($true);return $received
    }finally{
        Write-Progress -Activity '선택 CUDA 번역 / Optional CUDA translation' -Completed
        if($fileStream){$fileStream.Dispose()};if($downloadStream){$downloadStream.Dispose()};if($response){$response.Dispose()};$client.Dispose();$cancel.Dispose()
    }
}
function Expand-LumaTranslationCudaArchives {
    param($Spec,[string]$ArchiveDirectory,[string]$Destination)
    Add-Type -AssemblyName System.IO.Compression
    $root=[IO.Path]::GetFullPath($Destination)
    New-Item -ItemType Directory -Path $root -Force | Out-Null
    $seen=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase);[long]$expanded=0
    foreach($asset in $Spec.Archives){
        $archive=Join-Path $ArchiveDirectory $asset.archive
        if(!(Test-LumaCudaFile $archive $asset.sha256 $asset.bytes)){throw 'CUDA archive size or SHA-256 mismatch; existing runtime was preserved.'}
        $stream=[IO.File]::OpenRead($archive);$zip=[IO.Compression.ZipArchive]::new($stream,[IO.Compression.ZipArchiveMode]::Read)
        try{
            foreach($entry in $zip.Entries){
                $name=$entry.FullName.Replace('\','/');$parts=$name.TrimEnd('/').Split('/')
                if(!$name -or $name.StartsWith('/') -or $name.Contains(':') -or @($parts | Where-Object {$_ -eq '..' -or $_ -eq '.' -or $_ -eq ''}).Count -gt 0 -or (($entry.ExternalAttributes -shr 16) -band 0xf000) -eq 0xa000 -or ($entry.ExternalAttributes -band 0x400)) {throw 'Unsafe CUDA archive entry.'}
                $path=Get-LumaCudaScopedPath -Directory $root -RelativePath ($name.Replace('/',[IO.Path]::DirectorySeparatorChar))
                if($name.EndsWith('/')){New-Item -ItemType Directory -Path $path -Force | Out-Null;continue}
                if(!$seen.Add($name)){throw 'Duplicate CUDA archive entry.'}
                $expanded+=$entry.Length;if($expanded -gt $Spec.MaximumExpandedBytes){throw 'CUDA archive exceeds the pinned extraction limit.'}
                # Keep runtime DLLs, the server, and unmodified upstream notices.
                if($name -ne 'llama-server.exe' -and !($name -notmatch '/' -and $name -match '\.dll$') -and $name -notmatch '(^|/)(LICENSE|NOTICE)[^/]*$' -and $name -notmatch '^licenses/'){continue}
                New-Item -ItemType Directory -Path (Split-Path -Parent $path) -Force | Out-Null
                $entryStream=$entry.Open();$fileStream=[IO.File]::Open($path,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write)
                try{$entryStream.CopyTo($fileStream)}finally{$fileStream.Dispose();$entryStream.Dispose()}
            }
        }finally{$zip.Dispose();$stream.Dispose()}
    }
    foreach($name in @('llama-server.exe','ggml-cuda.dll','cudart64_12.dll','cublas64_12.dll','cublasLt64_12.dll')){if(!(Test-Path -LiteralPath (Join-Path $root $name) -PathType Leaf)){throw "Missing CUDA runtime dependency: $name"}}
    foreach($crt in $Spec.CrtFiles){
        if(!(Test-LumaCudaFile $crt.Source $crt.Sha256 $crt.Bytes)){throw 'The bundled VC runtime changed during CUDA installation.'}
        Copy-Item -LiteralPath $crt.Source -Destination (Join-Path $root $crt.Name) -Force
    }
    $licenses=Join-Path $root 'licenses';New-Item -ItemType Directory -Path $licenses -Force | Out-Null
    Copy-Item -LiteralPath $Spec.LicensePath -Destination (Join-Path $licenses 'NVIDIA-CUDA-12.4-EULA.html') -Force
    foreach($notice in $Spec.Notices){$path=Get-LumaCudaScopedPath -Directory $licenses -RelativePath $notice.Path;New-Item -ItemType Directory -Path (Split-Path -Parent $path) -Force | Out-Null;Copy-Item -LiteralPath $notice.Source -Destination $path -Force}
    $files=@(Get-ChildItem -LiteralPath $root -Recurse -File | ForEach-Object {[pscustomobject]@{path=$_.FullName.Substring($root.TrimEnd('\').Length+1).Replace('\','/');bytes=$_.Length;sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}})
    [pscustomobject]@{archives=@($Spec.Archives | Select-Object archive,sha256,bytes);files=$files} | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $root '.luma-cuda-install.json') -Encoding UTF8
}
function Test-LumaTranslationCudaInstallation {
    param($Spec,[string]$Directory)
    try{
        $path=Join-Path $Directory '.luma-cuda-install.json';if(!(Test-Path -LiteralPath $path -PathType Leaf)){return $false}
        $manifest=Get-Content -LiteralPath $path -Raw -Encoding UTF8 | ConvertFrom-Json
        if(@($manifest.archives).Count -ne $Spec.Archives.Count -or @($manifest.files).Count -lt 6){return $false}
        foreach($asset in $Spec.Archives){$match=@($manifest.archives | Where-Object {$_.archive -ceq $asset.archive -and $_.sha256 -eq $asset.sha256 -and [long]$_.bytes -eq [long]$asset.bytes});if($match.Count -ne 1){return $false}}
        $names=@($manifest.files | ForEach-Object {$_.path})
        foreach($name in @('llama-server.exe','ggml-cuda.dll','cudart64_12.dll','cublas64_12.dll','cublasLt64_12.dll','licenses/NVIDIA-CUDA-12.4-EULA.html')){if($names -cnotcontains $name){return $false}}
        foreach($file in $manifest.files){if($file.sha256 -notmatch '^[a-f0-9]{64}$' -or [long]$file.bytes -le 0 -or !(Test-LumaCudaFile (Get-LumaCudaScopedPath -Directory $Directory -RelativePath $file.path) $file.sha256 $file.bytes)){return $false}}
        $actual=@(Get-ChildItem -LiteralPath $Directory -Recurse -File -Force | Where-Object {$_.Name -ne '.luma-cuda-install.json'})
        if($actual.Count -ne @($manifest.files).Count){return $false}
        foreach($notice in $Spec.Notices){if($names -cnotcontains ('licenses/'+$notice.Path) -or (Get-FileHash -LiteralPath (Get-LumaCudaScopedPath -Directory $Directory -RelativePath ('licenses/'+$notice.Path)) -Algorithm SHA256).Hash -ne $notice.Sha256){return $false}}
        foreach($crt in $Spec.CrtFiles){if(!(Test-LumaCudaFile (Join-Path $Directory $crt.Name) $crt.Sha256 $crt.Bytes)){return $false}}
        return (Get-FileHash -LiteralPath (Join-Path $Directory 'licenses/NVIDIA-CUDA-12.4-EULA.html') -Algorithm SHA256).Hash -eq $Spec.LicenseSha256
    }catch{return $false}
}
function Install-LumaTranslationCuda {
    param([string]$Directory,[switch]$Development)
    $ErrorActionPreference='Stop'
    if($env:OS -ne 'Windows_NT' -or $env:PROCESSOR_ARCHITECTURE -eq 'ARM64' -or $env:PROCESSOR_ARCHITEW6432 -eq 'ARM64'){throw 'This optional CUDA 12.4 runtime requires Windows x64.'}
    $root=[IO.Path]::GetFullPath($Directory);$spec=Get-LumaTranslationCudaSpec -Directory $root -Development:$Development
    $base=if($Development){'.deps/translation'}else{'runtime/translation'}
    $translation=Get-LumaCudaScopedPath -Directory $root -RelativePath $base
    $target=Get-LumaCudaScopedPath -Directory $translation -RelativePath 'cuda'
    $downloads=Get-LumaCudaScopedPath -Directory $root -RelativePath $(if($Development){'.deps/downloads'}else{'runtime/translation/.cuda-downloads'})
    New-Item -ItemType Directory -Path $translation,$downloads -Force | Out-Null
    $lock=$null;$stage=$null;$backup=$null
    try{
        try{$lock=[IO.File]::Open((Join-Path $translation '.cuda-install.lock'),[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)}catch{throw '다른 CUDA 설치가 진행 중이거나 폴더에 쓸 수 없습니다 / Another CUDA installation is active, or the folder is not writable.'}
        if(Test-LumaTranslationCudaInstallation -Spec $spec -Directory $target){return [pscustomobject]@{AlreadyInstalled=$true;BytesTransferred=0;RuntimePath=$target}}
        [long]$archiveBytes=($spec.Archives | Measure-Object -Property bytes -Sum).Sum
        Assert-LumaCudaDiskSpace -Directory $translation -RequiredBytes ($archiveBytes+$spec.MaximumExpandedBytes+64MB)
        [long]$transferred=0
        foreach($asset in $spec.Archives){
            $archive=Get-LumaCudaScopedPath -Directory $downloads -RelativePath $asset.archive
            if(!(Test-LumaCudaFile $archive $asset.sha256 $asset.bytes)){
                $temporary=Get-LumaCudaScopedPath -Directory $downloads -RelativePath ($asset.archive+'.download')
                if(Test-Path -LiteralPath $temporary -PathType Leaf){Remove-Item -LiteralPath $temporary -Force}
                $transferred+=Receive-LumaCudaFile -Uri $asset.url -Destination $temporary -ExpectedBytes $asset.bytes
                if(!(Test-LumaCudaFile $temporary $asset.sha256 $asset.bytes)){throw 'CUDA download size or SHA-256 mismatch.'}
                if(Test-Path -LiteralPath $archive -PathType Leaf){[IO.File]::Replace($temporary,$archive,[NullString]::Value)}else{[IO.File]::Move($temporary,$archive)}
            }
        }
        $stage=Get-LumaCudaScopedPath -Directory $translation -RelativePath ('.cuda-stage-'+[guid]::NewGuid().ToString('N'))
        Expand-LumaTranslationCudaArchives -Spec $spec -ArchiveDirectory $downloads -Destination $stage
        if(!(Test-LumaTranslationCudaInstallation -Spec $spec -Directory $stage)){throw 'CUDA staging validation failed.'}
        if(Test-Path -LiteralPath $target){$backup=Get-LumaCudaScopedPath -Directory $translation -RelativePath ('.cuda-previous-'+[guid]::NewGuid().ToString('N'));[IO.Directory]::Move($target,$backup)}
        try{[IO.Directory]::Move($stage,$target);$stage=$null}catch{if($backup -and !(Test-Path -LiteralPath $target)){[IO.Directory]::Move($backup,$target);$backup=$null};throw}
        return [pscustomobject]@{AlreadyInstalled=$false;BytesTransferred=$transferred;RuntimePath=$target}
    }finally{
        try{
            foreach($cleanup in @($stage,$backup)){
                if($cleanup -and (Test-Path -LiteralPath $cleanup)){
                    $verified=Get-LumaCudaScopedPath -Directory $translation -RelativePath ([IO.Path]::GetFileName($cleanup))
                    if($verified -ne $cleanup -or [IO.Path]::GetFileName($verified) -notmatch '^\.cuda-(stage|previous)-[a-f0-9]{32}$'){throw 'Unsafe CUDA staging cleanup target.'}
                    Remove-Item -LiteralPath $verified -Recurse -Force
                }
            }
        }finally{if($lock){$lock.Dispose()}}
    }
}
if($MyInvocation.InvocationName -ne '.'){
    $ErrorActionPreference='Stop'
    if([string]::IsNullOrWhiteSpace($PlayerDirectory)){$PlayerDirectory=Split-Path -Parent $PSScriptRoot}
    $result=Install-LumaTranslationCuda -Directory $PlayerDirectory -Development:$Development
    Write-Output '선택 CUDA 번역 런타임을 검증했습니다. 드라이버와 시스템 설정은 변경하지 않았습니다.'
    Write-Output 'The optional CUDA translation runtime is verified. Drivers and system settings were not changed.'
    Write-Output $result.RuntimePath
}
