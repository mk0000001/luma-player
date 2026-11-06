param()
$ErrorActionPreference = 'Stop'
$packageTestProject = Split-Path -Parent $PSScriptRoot
$packageTestParent = [IO.Path]::GetFullPath((Join-Path $packageTestProject '.deps/package-tests'))
$packageTestRoot = Join-Path $packageTestParent ([guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $packageTestRoot -Force | Out-Null
$support = Join-Path $packageTestProject 'scripts/package-support.ps1'
if (Test-Path -LiteralPath $support) { . $support }
$script:packageFailures = 0
$script:packageCases = 0

function Assert-Package([bool]$Condition,[string]$Message) { if (!$Condition) { throw $Message } }
function Expect-PackageFailure([scriptblock]$Body,[string]$Message) {
    $rejected = $false
    try { & $Body | Out-Null } catch {
        if ($_.Exception -is [Management.Automation.CommandNotFoundException]) { throw }
        $rejected = $true
    }
    Assert-Package $rejected $Message
}
function Test-PackageCase([string]$Name,[scriptblock]$Body) {
    $script:packageCases++
    try { & $Body; Write-Output "PASS $Name" }
    catch { $script:packageFailures++; Write-Output "FAIL $Name : $($_.Exception.Message)" }
}
function Write-PackageFixture([string]$Path,[string]$Contents) {
    New-Item -ItemType Directory -Path (Split-Path -Parent $Path) -Force | Out-Null
    [IO.File]::WriteAllText($Path,$Contents,[Text.UTF8Encoding]::new($false))
}
function Invoke-FixtureGit([string]$Root,[string[]]$Arguments) {
    $output = & git -C $Root @Arguments
    if ($LASTEXITCODE -ne 0) { throw "Fixture git failed: $Arguments" }
    return $output
}
function New-PackageFixtureRepository {
    $root = Join-Path $packageTestRoot ([guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $root | Out-Null
    Invoke-FixtureGit $root @('init','-q') | Out-Null
    Invoke-FixtureGit $root @('config','user.name','Package fixture') | Out-Null
    Invoke-FixtureGit $root @('config','user.email','fixture@example.invalid') | Out-Null
    Invoke-FixtureGit $root @('config','core.autocrlf','false') | Out-Null
    Assert-Package (([IO.Path]::GetFullPath((Invoke-FixtureGit $root @('rev-parse','--show-toplevel')))) -eq $root) 'Fixture Git root escaped its temporary directory.'
    Write-PackageFixture (Join-Path $root '.gitignore') ".deps/`ndist/`n*.pem`n*.vtt`n"
    Write-PackageFixture (Join-Path $root 'VERSION') '0.1.0-alpha.2'
    Write-PackageFixture (Join-Path $root 'docs/공개 문서.md') "소유한 검증 문장`r`nOwned fixture only.`r`n"
    Invoke-FixtureGit $root @('add','--','.gitignore','VERSION','docs') | Out-Null
    Invoke-FixtureGit $root @('commit','-q','-m','Owned package fixture') | Out-Null
    return $root
}
function New-PackageAsrFixture {
    $root = Join-Path $packageTestRoot ([guid]::NewGuid().ToString('N'))
    $models = Join-Path $root '.deps/models'
    $encoders = Join-Path $root '.deps/models-openvino'
    Write-PackageFixture (Join-Path $models 'ggml-base.bin') 'owned ASR model fixture'
    Write-PackageFixture (Join-Path $encoders 'ggml-base-encoder-openvino.xml') '<owned-encoder/>'
    Write-PackageFixture (Join-Path $encoders 'ggml-base-encoder-openvino.bin') 'owned encoder bytes'
    $downloads = Join-Path $root '.deps/downloads'
    New-Item -ItemType Directory -Path $downloads -Force | Out-Null
    $archive = Join-Path $downloads 'encoder.zip'
    [IO.Compression.ZipFile]::CreateFromDirectory($encoders,$archive)
    $lock = @{model=@{file='ggml-base.bin';sha256=(Get-FileHash -LiteralPath (Join-Path $models 'ggml-base.bin')).Hash.ToLower()};encoder=@{archive='encoder.zip';sha256=(Get-FileHash -LiteralPath $archive).Hash.ToLower()}}
    Write-PackageFixture (Join-Path $root 'dependencies.lock.json') ($lock | ConvertTo-Json -Depth 4)
    return $root
}
function New-PackageCudaFixture {
    $root=Join-Path $packageTestRoot ([guid]::NewGuid().ToString('N'))
    $downloads=Join-Path $root '.deps/downloads';New-Item -ItemType Directory -Path $downloads -Force | Out-Null
    $lock=Get-Content -LiteralPath (Join-Path $packageTestProject 'dependencies.lock.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    foreach($file in $lock.asrCuda.files){
        $content=if($file.name -eq 'ggml-cuda.dll'){'owned whisper '+$file.name}else{'owned NVIDIA '+$file.name}
        $path=Join-Path $root ('.deps/asr-cuda/'+$file.name);Write-PackageFixture $path $content
        $file.bytes=(Get-Item -LiteralPath $path).Length;$file.sha256=(Get-FileHash -LiteralPath $path).Hash.ToLowerInvariant()
    }
    Write-PackageFixture (Join-Path $root 'dependencies.lock.json') ($lock | ConvertTo-Json -Depth 14)
    $translation=Get-Content -LiteralPath (Join-Path $packageTestProject 'translation-dependencies.lock.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    foreach($asset in @($translation.windows | Where-Object {$_.target -eq 'cuda'})){
        $archive=Join-Path $downloads $asset.archive;$stream=[IO.File]::Open($archive,[IO.FileMode]::CreateNew);$zip=[IO.Compression.ZipArchive]::new($stream,[IO.Compression.ZipArchiveMode]::Create)
        try{
            $names=if($asset.id -eq 'llama-cpp-cuda'){@('llama-server.exe','ggml-cuda.dll','ggml.dll','ggml-base.dll','ggml-cpu-x64.dll','LICENSE')}else{@('cudart64_12.dll','cublas64_12.dll','cublasLt64_12.dll')}
            foreach($name in $names){$entry=$zip.CreateEntry($name);$writer=[IO.StreamWriter]::new($entry.Open());try{$writer.Write($(if($asset.id -eq 'llama-cpp-cudart'){'owned NVIDIA '+$name}else{'owned llama '+$name}))}finally{$writer.Dispose()}}
        }finally{$zip.Dispose();$stream.Dispose()}
        $asset.bytes=(Get-Item -LiteralPath $archive).Length;$asset.sha256=(Get-FileHash -LiteralPath $archive).Hash.ToLowerInvariant()
    }
    Write-PackageFixture (Join-Path $root 'translation-dependencies.lock.json') ($translation | ConvertTo-Json -Depth 14)
    New-Item -ItemType Directory -Path (Join-Path $root 'LICENSES') -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $packageTestProject 'LICENSES/Whisper-MIT.txt') -Destination (Join-Path $root 'LICENSES')
    Copy-Item -LiteralPath (Join-Path $packageTestProject 'LICENSES/translation') -Destination (Join-Path $root 'LICENSES') -Recurse
    foreach($name in 'msvcp140.dll','vcruntime140.dll','vcruntime140_1.dll'){Write-PackageFixture (Join-Path $root ('runtime/translation/vulkan/'+$name)) ('owned CRT '+$name)}
    . (Join-Path $packageTestProject 'scripts/install-translation-cuda.ps1')
    $spec=Get-LumaTranslationCudaSpec -Directory $root
    Expand-LumaTranslationCudaArchives -Spec $spec -ArchiveDirectory $downloads -Destination (Join-Path $root '.deps/translation/cuda')
    return $root
}
function New-PackageCudaStagedFixture([object[]]$Files){
    $directory=Join-Path $packageTestRoot ([guid]::NewGuid().ToString('N'))
    foreach($file in $Files){$target=Join-Path $directory $file.Path;New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null;Copy-Item -LiteralPath $file.Source -Destination $target}
    foreach($relative in 'runtime/ai/luma-whisper-worker.exe','runtime/ai/whisper.dll','runtime/ai/ggml.dll','runtime/ai/ggml-base.dll','runtime/ai/ggml-cpu-x64.dll','runtime/ai/ggml-vulkan.dll','runtime/translation/vulkan/llama-server.exe','runtime/translation/vulkan/ggml.dll','runtime/translation/vulkan/ggml-base.dll','runtime/translation/vulkan/ggml-cpu-x64.dll','runtime/translation/vulkan/ggml-vulkan.dll'){
        Write-PackageFixture (Join-Path $directory $relative) ('owned fallback '+$relative)
    }
    return $directory
}
function New-PackageCudaClassicFixture([string]$ProjectRoot,[object[]]$Files){
    $directory=New-PackageCudaStagedFixture $Files
    foreach($name in 'cudart64_12.dll','cublas64_12.dll','cublasLt64_12.dll'){
        foreach($relative in ('runtime/ai/'+$name),('runtime/translation/cuda/'+$name)){
            $target=Join-Path $directory $relative;New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
            Copy-Item -LiteralPath (Join-Path $ProjectRoot ('.deps/asr-cuda/'+$name)) -Destination $target -Force
        }
        $shared=Join-Path $directory ('runtime/cuda/'+$name);if(Test-Path -LiteralPath $shared){[IO.File]::Delete($shared)}
    }
    $sharedDirectory=Join-Path $directory 'runtime/cuda';if(Test-Path -LiteralPath $sharedDirectory){[IO.Directory]::Delete($sharedDirectory)}
    return $directory
}

$savedGitEnvironment = @{}
foreach ($name in 'GIT_DIR','GIT_WORK_TREE','GIT_INDEX_FILE') {
    $savedGitEnvironment[$name] = [Environment]::GetEnvironmentVariable($name,'Process')
    Remove-Item -LiteralPath "Env:$name" -ErrorAction SilentlyContinue
}
Add-Type -AssemblyName System.IO.Compression.FileSystem
try {
    Test-PackageCase 'package validation is available without running deployment' {
        Assert-Package ([bool](Get-Command Get-LumaPackageSource -ErrorAction SilentlyContinue)) 'Package source validation is not implemented.'
    }
    Test-PackageCase 'clean source records its actual commit' {
        $root=New-PackageFixtureRepository
        $source=Get-LumaPackageSource -ProjectRoot $root
        Assert-Package (!$source.Dirty) 'A clean fixture was reported dirty.'
        Assert-Package ($source.Commit -eq (Invoke-FixtureGit $root @('rev-parse','HEAD'))) 'Source commit does not match the fixture.'
    }
    Test-PackageCase 'official package rejects tracked changes while development records dirty state' {
        $root=New-PackageFixtureRepository
        Write-PackageFixture (Join-Path $root 'VERSION') '0.1.0-alpha.3'
        Expect-PackageFailure { Get-LumaPackageSource -ProjectRoot $root } 'Official packaging accepted a modified tracked source.'
        $source=Get-LumaPackageSource -ProjectRoot $root -AllowDirtySource
        Assert-Package $source.Dirty 'Development packaging hid dirty source state.'
    }
    Test-PackageCase 'official package rejects untracked source' {
        $root=New-PackageFixtureRepository
        Write-PackageFixture (Join-Path $root 'untracked.cpp') 'owned fixture'
        Expect-PackageFailure { Get-LumaPackageSource -ProjectRoot $root } 'Untracked source was accepted as an official release.'
    }
    Test-PackageCase 'source edits during packaging invalidate a clean snapshot' {
        $root=New-PackageFixtureRepository
        $source=Get-LumaPackageSource -ProjectRoot $root
        Write-PackageFixture (Join-Path $root 'docs/공개 문서.md') 'changed after source snapshot'
        Expect-PackageFailure { Assert-LumaPackageSourceUnchanged -ProjectRoot $root -Source $source } 'A changed source snapshot was accepted.'
    }
    Test-PackageCase 'source commit changes invalidate even development snapshots' {
        $root=New-PackageFixtureRepository
        $source=Get-LumaPackageSource -ProjectRoot $root -AllowDirtySource
        Write-PackageFixture (Join-Path $root 'VERSION') '0.1.0-alpha.3'
        Invoke-FixtureGit $root @('add','VERSION') | Out-Null
        Invoke-FixtureGit $root @('commit','-q','-m','Second fixture commit') | Out-Null
        Expect-PackageFailure { Assert-LumaPackageSourceUnchanged -ProjectRoot $root -Source $source } 'A different source commit was accepted.'
    }
    Test-PackageCase 'only tracked documents are copied and bytes are preserved' {
        $root=New-PackageFixtureRepository
        Write-PackageFixture (Join-Path $root 'docs/private.pem') 'owned dummy private-file sentinel'
        Write-PackageFixture (Join-Path $root 'docs/private.vtt') 'owned dummy caption sentinel'
        Write-PackageFixture (Join-Path $root 'docs/untracked.md') 'untracked draft'
        $destination=Join-Path $packageTestRoot ([guid]::NewGuid().ToString('N'))
        Copy-LumaPackageTrackedDocuments -ProjectRoot $root -Destination $destination
        $files=@(Get-ChildItem -LiteralPath $destination -Recurse -File)
        Assert-Package ($files.Count -eq 1) 'Ignored or untracked documents were copied.'
        Assert-Package ((Get-FileHash -LiteralPath $files[0].FullName).Hash -eq (Get-FileHash -LiteralPath (Join-Path $root 'docs/공개 문서.md')).Hash) 'Tracked document bytes changed during copying.'
    }
    Test-PackageCase 'tracked private captions are rejected before copying' {
        $root=New-PackageFixtureRepository
        Write-PackageFixture (Join-Path $root 'docs/private.vtt') 'owned dummy caption'
        Invoke-FixtureGit $root @('add','-f','docs/private.vtt') | Out-Null
        $destination=Join-Path $packageTestRoot ([guid]::NewGuid().ToString('N'))
        Expect-PackageFailure { Copy-LumaPackageTrackedDocuments -ProjectRoot $root -Destination $destination } 'A tracked subtitle was copied.'
        Assert-Package (!(Test-Path -LiteralPath (Join-Path $destination 'docs/private.vtt'))) 'Private caption bytes were written before rejection.'
    }
    Test-PackageCase 'all supported private caption audio video and key formats are blocked' {
        $extensions='.srt','.vtt','.ass','.ssa','.sup','.sub','.idx','.lrc','.wav','.pcm','.flac','.alac','.aac','.m4a','.mp3','.ac3','.dts','.m2ts','.mts','.ts','.mp4','.mkv','.mov','.avi','.webm','.iso','.bdmv','.mpls','.clpi','.key','.pem','.der','.crt','.cer','.pfx','.p12'
        foreach($extension in $extensions) {
            $directory=Join-Path $packageTestRoot ([guid]::NewGuid().ToString('N'))
            Write-PackageFixture (Join-Path $directory ('dummy'+$extension)) 'owned dummy fixture'
            Expect-PackageFailure { Assert-LumaPackagePublicFiles -Directory $directory } "Private extension accepted: $extension"
        }
        foreach($name in 'KEYDB.cfg','HostKeyCertificate.txt','ProcessingDeviceKeysSimple.txt','ggml-kotoba-whisper-v2.0-q5_0-encoder-openvino.npu-profile.json') {
            $directory=Join-Path $packageTestRoot ([guid]::NewGuid().ToString('N'))
            Write-PackageFixture (Join-Path $directory $name) 'owned dummy fixture'
            Expect-PackageFailure { Assert-LumaPackagePublicFiles -Directory $directory } "Private name accepted: $name"
        }
        $directory=Join-Path $packageTestRoot ([guid]::NewGuid().ToString('N'))
        Write-PackageFixture (Join-Path $directory 'LICENSE.txt') 'owned notice'
        Write-PackageFixture (Join-Path $directory 'logo.png') 'owned artwork fixture'
        Assert-LumaPackagePublicFiles -Directory $directory
    }
    Test-PackageCase 'parent-relative output produces correct manifest paths' {
        $root=New-PackageFixtureRepository
        $paths=Get-LumaPackagePaths -ProjectRoot $root -Version '0.1.0-alpha.2' -BuildDirectory './build' -OutputDirectory '../release-review'
        Assert-Package ($paths.Package -eq (Join-Path $packageTestRoot 'release-review/LumaPlayer-0.1.0-alpha.2-windows-x64')) 'Package path was not canonicalized.'
        Write-PackageFixture (Join-Path $paths.Package 'runtime/mpv/libmpv-2.dll') 'owned DLL fixture'
        $manifest=@(Get-LumaPackageManifestFiles -Directory $paths.Package)
        Assert-Package ($manifest.Count -eq 1 -and $manifest[0].path -eq 'runtime/mpv/libmpv-2.dll') 'Manifest truncated a parent-relative path.'
        Assert-Package ($manifest[0].bytes -eq 17) 'Manifest file size is wrong.'
    }
    Test-PackageCase 'existing folder archive and checksum are never replaced' {
        foreach($kind in 'Package','Archive','Checksum') {
            $root=New-PackageFixtureRepository
            $paths=Get-LumaPackagePaths -ProjectRoot $root -Version '0.1.0-alpha.2' -BuildDirectory 'build' -OutputDirectory 'dist'
            if($kind -eq 'Package') { New-Item -ItemType Directory -Path $paths.Package -Force | Out-Null }
            else { Write-PackageFixture $paths.$kind 'previous release sentinel' }
            Expect-PackageFailure { Assert-LumaPackageTargetsFree -Paths $paths } "Existing $kind target was accepted."
            if($kind -ne 'Package') { Assert-Package ([IO.File]::ReadAllText($paths.$kind) -eq 'previous release sentinel') 'A prior release was changed.' }
        }
    }
    Test-PackageCase 'ASR model and both encoders must match pinned bytes' {
        $root=New-PackageAsrFixture
        $files=@(Get-LumaPackageAsrFiles -ProjectRoot $root)
        Assert-Package ($files.Count -eq 3) 'Core ASR contract omitted an encoder.'
        $staged=Join-Path $root 'runtime/models'
        New-Item -ItemType Directory -Path $staged -Force | Out-Null
        foreach($file in $files) { Copy-Item -LiteralPath $file.Source -Destination (Join-Path $staged $file.Name) }
        Assert-LumaPackageAsrFiles -Files $files -Directory $staged
        Write-PackageFixture (Join-Path $staged 'ggml-base.bin') 'corrupt staged model'
        Expect-PackageFailure { Assert-LumaPackageAsrFiles -Files $files -Directory $staged } 'A corrupt staged ASR model was accepted.'
    }
    Test-PackageCase 'missing or corrupt ASR inputs cannot silently produce a core package' {
        foreach($name in 'ggml-base.bin','ggml-base-encoder-openvino.xml','ggml-base-encoder-openvino.bin','encoder.zip') {
            $root=New-PackageAsrFixture
            $directory=if($name -eq 'ggml-base.bin') { '.deps/models' } elseif($name -eq 'encoder.zip') { '.deps/downloads' } else { '.deps/models-openvino' }
            $inputPath=Join-Path (Join-Path $root $directory) $name
            Write-PackageFixture $inputPath 'corrupt fixture bytes'
            Expect-PackageFailure { Get-LumaPackageAsrFiles -ProjectRoot $root } "Corrupt ASR input accepted: $name"
            Remove-Item -LiteralPath $inputPath
            Expect-PackageFailure { Get-LumaPackageAsrFiles -ProjectRoot $root } "Missing ASR input accepted: $name"
        }
    }
    Test-PackageCase 'package entrypoint rejects invalid inputs before deployment or output creation' {
        foreach($scenario in 'dirty','archive','model') {
            $root=New-PackageFixtureRepository
            $asr=New-PackageAsrFixture
            Copy-Item -LiteralPath (Join-Path $asr '.deps') -Destination (Join-Path $root '.deps') -Recurse
            Copy-Item -LiteralPath (Join-Path $asr 'dependencies.lock.json') -Destination $root
            New-Item -ItemType Directory -Path (Join-Path $root 'scripts') | Out-Null
            foreach($file in 'package-windows.ps1','package-support.ps1') { Copy-Item -LiteralPath (Join-Path $packageTestProject "scripts/$file") -Destination (Join-Path $root 'scripts') }
            Invoke-FixtureGit $root @('add','--','dependencies.lock.json','scripts') | Out-Null
            Invoke-FixtureGit $root @('commit','-q','-m','Package entrypoint fixture') | Out-Null
            $target=Join-Path $root 'dist/LumaPlayer-0.1.0-alpha.2-windows-x64'
            if($scenario -eq 'dirty') { Write-PackageFixture (Join-Path $root 'docs/공개 문서.md') 'dirty fixture';$reason='clean committed source' }
            elseif($scenario -eq 'archive') { Write-PackageFixture "$target.zip" 'previous archive';$reason='destination exists' }
            else { Remove-Item -LiteralPath (Join-Path $root '.deps/models/ggml-base.bin');$reason='Core ASR model' }
            $message=''
            try { & (Join-Path $root 'scripts/package-windows.ps1') | Out-Null } catch { $message=$_.Exception.Message }
            Assert-Package ($message.Contains($reason)) "Package entrypoint did not reject $scenario at its preflight boundary: $message"
            Assert-Package (!(Test-Path -LiteralPath $target)) 'Package entrypoint created output before its validation completed.'
            if($scenario -eq 'archive') { Assert-Package ([IO.File]::ReadAllText("$target.zip") -eq 'previous archive') 'Package entrypoint changed an existing ZIP.' }
        }
    }
    Test-PackageCase 'translation weights are absent by default and verified when explicitly included' {
        $directory=Join-Path $packageTestRoot ([guid]::NewGuid().ToString('N'))
        $model=Join-Path $directory 'runtime/translation/models/fixture.gguf'
        Write-PackageFixture $model 'owned optional model'
        $spec=@{model=@{file='fixture.gguf';bytes=20;sha256=(Get-FileHash -LiteralPath $model).Hash.ToLower()}}
        Write-PackageFixture (Join-Path $directory 'translation-dependencies.lock.json') ($spec | ConvertTo-Json -Depth 3)
        Expect-PackageFailure { Assert-LumaPackageTranslationModel -Directory $directory } 'Default core silently included translation weights.'
        Assert-LumaPackageTranslationModel -Directory $directory -IncludeTranslationModel
        Write-PackageFixture $model 'corrupt optional model'
        Expect-PackageFailure { Assert-LumaPackageTranslationModel -Directory $directory -IncludeTranslationModel } 'Explicit model inclusion skipped hash verification.'
        Remove-Item -LiteralPath $model
        Assert-LumaPackageTranslationModel -Directory $directory
    }
    Test-PackageCase 'core package excludes optional CUDA binaries and download caches' {
        foreach($relative in 'runtime/translation/cuda/llama-server.exe','runtime/ai/ggml-cuda.dll','runtime/ai/cudart64_12.dll','runtime/ai/cublas64_12.dll','runtime/ai/cublasLt64_12.dll','runtime/cuda/cudart64_12.dll','runtime/translation/.cuda-downloads/archive.zip','runtime/ai/.asr-cuda-downloads/archive.zip','runtime/ai/partial.download') {
            $directory=Join-Path $packageTestRoot ([guid]::NewGuid().ToString('N'));Write-PackageFixture (Join-Path $directory $relative) 'owned optional CUDA sentinel'
            Expect-PackageFailure { Assert-LumaPackageCoreCudaOmitted -Directory $directory } "Optional CUDA content accepted in core: $relative"
        }
        $directory=Join-Path $packageTestRoot ([guid]::NewGuid().ToString('N'));Write-PackageFixture (Join-Path $directory 'licenses/LICENSES/translation/cuda/NVIDIA-CUDA-12.4-EULA.html') 'owned notice fixture'
        Assert-LumaPackageCoreCudaOmitted -Directory $directory
    }
    Test-PackageCase 'CUDA flavor has separate destinations and truthful manifest metadata' {
        $root=Join-Path $packageTestRoot ([guid]::NewGuid().ToString('N'))
        $core=Get-LumaPackagePaths -ProjectRoot $root -Version '0.1.0-alpha.4' -BuildDirectory build -OutputDirectory dist
        $cuda=Get-LumaPackagePaths -ProjectRoot $root -Version '0.1.0-alpha.4' -BuildDirectory build -OutputDirectory dist -IncludeCuda
        Assert-Package ($core.Package -ne $cuda.Package -and $cuda.Package.EndsWith('-windows-x64-cuda')) 'CUDA packaging collides with the core release.'
        $source=[pscustomobject]@{Commit=('a'*40);Dirty=$false}
        $coreManifest=New-LumaPackageManifest -Version '0.1.0-alpha.4' -Source $source -Files @()
        $cudaManifest=New-LumaPackageManifest -Version '0.1.0-alpha.4' -Source $source -Files @() -IncludeCuda
        Assert-Package ($coreManifest.cudaIncluded -is [bool] -and !$coreManifest.cudaIncluded -and $coreManifest.packageFlavor -ceq 'windows-x64') 'Core manifest does not describe omitted CUDA.'
        Assert-Package ($cudaManifest.cudaIncluded -is [bool] -and $cudaManifest.cudaIncluded -and $cudaManifest.packageFlavor -ceq 'windows-x64-cuda' -and !$cudaManifest.translationModelIncluded) 'CUDA manifest implies missing runtimes or bundled model weights.'
    }
    Test-PackageCase 'CUDA source pins keep Whisper and llama GGML and original notices isolated' {
        $root=New-PackageCudaFixture;$files=@(Get-LumaPackageCudaFiles -ProjectRoot $root)
        $asr=@($files | Where-Object {$_.Path -ceq 'runtime/ai/ggml-cuda.dll'});$translation=@($files | Where-Object {$_.Path -ceq 'runtime/translation/cuda/ggml-cuda.dll'})
        Assert-Package ($asr.Count -eq 1 -and $translation.Count -eq 1 -and $asr[0].Sha256 -ne $translation[0].Sha256) 'Different GGML builds were mixed.'
        Assert-Package (@($files | Where-Object {$_.Path -ceq 'runtime/ai/asr-cuda-licenses/NVIDIA-CUDA-12.4-EULA.html'}).Count -eq 1) 'ASR raw license was omitted.'
        $staged=New-PackageCudaStagedFixture $files;Assert-LumaPackageCudaFiles -Files $files -Directory $staged
    }
    Test-PackageCase 'only byte-identical NVIDIA DLLs are shared while GGML and provenance stay isolated' {
        $root=New-PackageCudaFixture;$files=@(Get-LumaPackageCudaFiles -ProjectRoot $root)
        $shared=@($files | Where-Object {$_.Path -like 'runtime/cuda/*'})
        Assert-Package ($shared.Count -eq 3 -and @($files | Where-Object {$_.Path -match '^runtime/(?:ai|translation/cuda)/(?:cudart64_12|cublas64_12|cublasLt64_12)\.dll$'}).Count -eq 0) 'NVIDIA dependencies remain duplicated in the manifest.'
        $staged=New-PackageCudaClassicFixture -ProjectRoot $root -Files $files
        $manifest=Join-Path $staged 'runtime/translation/cuda/.luma-cuda-install.json';$before=(Get-FileHash -LiteralPath $manifest).Hash
        Move-LumaPackageCudaSharedRuntime -Files $files -Directory $staged
        Assert-LumaPackageCudaFiles -Files $files -Directory $staged
        Assert-Package ((Get-FileHash -LiteralPath $manifest).Hash -eq $before) 'Original verified installation provenance was rewritten.'
        Assert-Package (@(Get-ChildItem -LiteralPath (Join-Path $staged 'runtime/cuda') -File).Count -eq 3) 'Shared CUDA folder contains more than the identical NVIDIA DLLs.'
    }
    Test-PackageCase 'shared-layout conversion rejects incomplete inputs before moving any DLL' {
        $root=New-PackageCudaFixture;$files=@(Get-LumaPackageCudaFiles -ProjectRoot $root);$staged=New-PackageCudaClassicFixture -ProjectRoot $root -Files $files
        Write-PackageFixture (Join-Path $staged 'runtime/translation/cuda/cublasLt64_12.dll') 'unexpected duplicate'
        Expect-PackageFailure {Move-LumaPackageCudaSharedRuntime -Files $files -Directory $staged} 'Nonidentical source copies were deduplicated.'
        Assert-Package (!(Test-Path -LiteralPath (Join-Path $staged 'runtime/cuda'))) 'Conversion created shared output before validating every input.'
        Assert-Package (Test-Path -LiteralPath (Join-Path $staged 'runtime/ai/cudart64_12.dll')) 'Conversion moved a valid input before detecting a later corrupt DLL.'
    }
    Test-PackageCase 'missing corrupt and stale CUDA installations fail before packaging output exists' {
        foreach($scenario in 'missing-asr','corrupt-asr','missing-translation','stale-translation'){
            $root=New-PackageCudaFixture
            if($scenario -eq 'missing-asr'){Remove-Item -LiteralPath (Join-Path $root '.deps/asr-cuda/ggml-cuda.dll')}
            elseif($scenario -eq 'corrupt-asr'){Write-PackageFixture (Join-Path $root '.deps/asr-cuda/ggml-cuda.dll') 'stale whisper plugin'}
            elseif($scenario -eq 'missing-translation'){Remove-Item -LiteralPath (Join-Path $root '.deps/translation/cuda/llama-server.exe')}
            else{$path=Join-Path $root '.deps/translation/cuda/.luma-cuda-install.json';$manifest=Get-Content -LiteralPath $path -Raw | ConvertFrom-Json;$manifest.archives[0].sha256='0'*64;Write-PackageFixture $path ($manifest | ConvertTo-Json -Depth 8)}
            Expect-PackageFailure {Get-LumaPackageCudaFiles -ProjectRoot $root} "CUDA source accepted $scenario"
            Assert-Package (!(Test-Path -LiteralPath (Join-Path $root 'dist'))) 'CUDA preflight wrote a package output.'
        }
    }
    Test-PackageCase 'rewritten installation hashes cannot hide a wrong pinned CUDA binary' {
        $root=New-PackageCudaFixture;$runtime=Join-Path $root '.deps/translation/cuda';$path=Join-Path $runtime 'ggml-cuda.dll'
        Write-PackageFixture $path 'old plugin with a forged current installation manifest'
        $manifestPath=Join-Path $runtime '.luma-cuda-install.json';$manifest=Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
        $file=$manifest.files | Where-Object {$_.path -ceq 'ggml-cuda.dll'};$file.bytes=(Get-Item -LiteralPath $path).Length;$file.sha256=(Get-FileHash -LiteralPath $path).Hash.ToLowerInvariant()
        Write-PackageFixture $manifestPath ($manifest | ConvertTo-Json -Depth 8)
        Expect-PackageFailure {Get-LumaPackageCudaFiles -ProjectRoot $root} 'Self-consistent stale GGML was accepted without checking pinned ZIP contents.'
    }
    Test-PackageCase 'CUDA pinned archives and raw licenses cannot be missing or corrupt' {
        foreach($scenario in 'archive','license'){
            $root=New-PackageCudaFixture
            $path=if($scenario -eq 'archive'){Join-Path $root '.deps/downloads/llama-b12125-bin-win-cuda-12.4-x64.zip'}else{Join-Path $root 'LICENSES/translation/cuda/NVIDIA-CUDA-12.4-EULA.html'}
            Write-PackageFixture $path 'corrupt fixture'
            Expect-PackageFailure {Get-LumaPackageCudaFiles -ProjectRoot $root} "CUDA packaging accepted corrupt $scenario"
        }
    }
    Test-PackageCase 'staged CUDA rejects changed files misplaced plugins missing fallbacks and caches' {
        $root=New-PackageCudaFixture;$files=@(Get-LumaPackageCudaFiles -ProjectRoot $root)
        foreach($scenario in 'plugin','license','misplaced','misplaced-base','cpu','vulkan','cache'){
            $staged=New-PackageCudaStagedFixture $files
            if($scenario -eq 'plugin'){Write-PackageFixture (Join-Path $staged 'runtime/ai/ggml-cuda.dll') 'wrong staged plugin'}
            elseif($scenario -eq 'license'){Write-PackageFixture (Join-Path $staged 'runtime/translation/cuda/licenses/NVIDIA-CUDA-12.4-EULA.html') 'changed license'}
            elseif($scenario -eq 'misplaced'){Write-PackageFixture (Join-Path $staged 'ggml-cuda.dll') 'unscoped CUDA plugin'}
            elseif($scenario -eq 'misplaced-base'){Write-PackageFixture (Join-Path $staged 'runtime/cuda/ggml-base.dll') 'unscoped GGML base'}
            elseif($scenario -eq 'cpu'){Remove-Item -LiteralPath (Join-Path $staged 'runtime/ai/ggml-cpu-x64.dll')}
            elseif($scenario -eq 'vulkan'){Remove-Item -LiteralPath (Join-Path $staged 'runtime/translation/vulkan/ggml-vulkan.dll')}
            else{Write-PackageFixture (Join-Path $staged 'runtime/translation/.cuda-downloads/cache.zip') 'download cache'}
            Expect-PackageFailure {Assert-LumaPackageCudaFiles -Files $files -Directory $staged} "CUDA staged validation accepted $scenario"
        }
    }
    Test-PackageCase 'CUDA package entrypoint rejects missing runtime before deployment' {
        $root=New-PackageFixtureRepository;$asr=New-PackageAsrFixture
        Copy-Item -LiteralPath (Join-Path $asr '.deps') -Destination (Join-Path $root '.deps') -Recurse
        Copy-Item -LiteralPath (Join-Path $asr 'dependencies.lock.json') -Destination $root
        New-Item -ItemType Directory -Path (Join-Path $root 'scripts') | Out-Null
        foreach($file in 'package-windows.ps1','package-support.ps1','install-asr-cuda.ps1','install-translation-cuda.ps1'){Copy-Item -LiteralPath (Join-Path $packageTestProject ('scripts/'+$file)) -Destination (Join-Path $root 'scripts')}
        Invoke-FixtureGit $root @('add','--','dependencies.lock.json','scripts') | Out-Null;Invoke-FixtureGit $root @('commit','-q','-m','CUDA packaging fixture') | Out-Null
        $message='';try{& (Join-Path $root 'scripts/package-windows.ps1') -IncludeCuda | Out-Null}catch{$message=$_.Exception.Message}
        Assert-Package ($message.Contains('CUDA') -and !$message.Contains('parameter cannot be found')) "CUDA preflight was not reached: $message"
        Assert-Package (!(Test-Path -LiteralPath (Join-Path $root 'dist'))) 'Missing CUDA was discovered only after deployment started.'
    }
    Test-PackageCase 'actual compressed archive bytes obey the two GiB asset boundary for every flavor' {
        Assert-LumaPackageArchiveSize -Bytes (2GB-1)
        Expect-PackageFailure {Assert-LumaPackageArchiveSize -Bytes 2GB} 'A two-GiB ZIP was accepted.'
        Expect-PackageFailure {Assert-LumaPackageArchiveSize -Bytes 0} 'An empty ZIP was accepted.'
    }
    Test-PackageCase 'quality ASR model is pinned and cannot be silently omitted' {
        $root=New-PackageAsrFixture;$quality=Join-Path $root '.deps/models/owned-quality.bin';Write-PackageFixture $quality 'owned quality ASR fixture'
        $lockPath=Join-Path $root 'dependencies.lock.json';$lock=Get-Content -LiteralPath $lockPath -Raw | ConvertFrom-Json
        $lock | Add-Member -NotePropertyName highQualityModel -NotePropertyValue @{file='owned-quality.bin';bytes=(Get-Item -LiteralPath $quality).Length;sha256=(Get-FileHash -LiteralPath $quality).Hash.ToLower()}
        Write-PackageFixture $lockPath ($lock | ConvertTo-Json -Depth 5)
        Assert-Package (@(Get-LumaPackageAsrFiles -ProjectRoot $root).Count -eq 4) 'Quality ASR model was omitted from core contract.'
        Write-PackageFixture $quality 'corrupt quality fixture';Expect-PackageFailure { Get-LumaPackageAsrFiles -ProjectRoot $root } 'Corrupt quality model accepted.'
        Remove-Item -LiteralPath $quality;Expect-PackageFailure { Get-LumaPackageAsrFiles -ProjectRoot $root } 'Missing quality model accepted.'
    }
    Test-PackageCase 'committed upstream license bytes survive checkout normalization rules' {
        $root=New-PackageFixtureRepository
        Copy-Item -LiteralPath (Join-Path $packageTestProject '.gitattributes') -Destination (Join-Path $root '.gitattributes')
        $license=Join-Path $root 'LICENSES/owned-fixture.txt'
        Write-PackageFixture $license "owned original notice`r`nsecond original line`r`n"
        $workingHash=Invoke-FixtureGit $root @('hash-object','--no-filters','--','LICENSES/owned-fixture.txt')
        Invoke-FixtureGit $root @('add','--','.gitattributes','LICENSES/owned-fixture.txt') | Out-Null
        $indexHash=Invoke-FixtureGit $root @('rev-parse',':LICENSES/owned-fixture.txt')
        Assert-Package ($workingHash -eq $indexHash) 'Git normalized original upstream license bytes.'
    }
} finally {
    foreach($name in $savedGitEnvironment.Keys) {
        if($null -eq $savedGitEnvironment[$name]) { Remove-Item -LiteralPath "Env:$name" -ErrorAction SilentlyContinue }
        else { [Environment]::SetEnvironmentVariable($name,$savedGitEnvironment[$name],'Process') }
    }
    $resolvedRoot=[IO.Path]::GetFullPath($packageTestRoot)
    if(!$resolvedRoot.StartsWith($packageTestParent+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) { throw 'Refusing package fixture cleanup outside its verified directory.' }
    Remove-Item -LiteralPath $resolvedRoot -Recurse -Force
}
Write-Output ("Package checks: {0} passed, {1} failed." -f ($script:packageCases-$script:packageFailures),$script:packageFailures)
if($script:packageFailures -gt 0) { throw "$script:packageFailures package test(s) failed." }
