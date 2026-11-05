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
        foreach($name in 'KEYDB.cfg','HostKeyCertificate.txt','ProcessingDeviceKeysSimple.txt') {
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
        foreach($relative in 'runtime/translation/cuda/llama-server.exe','runtime/ai/ggml-cuda.dll','runtime/ai/cudart64_12.dll','runtime/ai/cublas64_12.dll','runtime/ai/cublasLt64_12.dll','runtime/translation/.cuda-downloads/archive.zip','runtime/ai/.asr-cuda-downloads/archive.zip','runtime/ai/partial.download') {
            $directory=Join-Path $packageTestRoot ([guid]::NewGuid().ToString('N'));Write-PackageFixture (Join-Path $directory $relative) 'owned optional CUDA sentinel'
            Expect-PackageFailure { Assert-LumaPackageCoreCudaOmitted -Directory $directory } "Optional CUDA content accepted in core: $relative"
        }
        $directory=Join-Path $packageTestRoot ([guid]::NewGuid().ToString('N'));Write-PackageFixture (Join-Path $directory 'licenses/LICENSES/translation/cuda/NVIDIA-CUDA-12.4-EULA.html') 'owned notice fixture'
        Assert-LumaPackageCoreCudaOmitted -Directory $directory
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
