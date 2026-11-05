param([string]$ExistingModel,[switch]$RunDownloadTest)
$ErrorActionPreference = 'Stop'
$installerProject = Split-Path -Parent $PSScriptRoot
. (Join-Path $installerProject 'scripts\install-translation.ps1')
$installerTestsRoot = [IO.Path]::GetFullPath((Join-Path $installerProject ('.deps\translation-installer-tests\' + [guid]::NewGuid().ToString('N'))))
$installerTestsParent = [IO.Path]::GetFullPath((Join-Path $installerProject '.deps\translation-installer-tests'))
New-Item -ItemType Directory -Path $installerTestsRoot -Force | Out-Null
$script:installerFailures = 0
function Assert-Installer([bool]$Condition,[string]$Message) { if (!$Condition) { throw $Message } }
function Test-InstallerCase([string]$Name,[scriptblock]$Body) {
    try { & $Body; Write-Output "PASS $Name" }
    catch { $script:installerFailures++; Write-Output "FAIL $Name : $($_.Exception.Message)" }
}
try {
    Test-InstallerCase 'corrupt download never replaces existing file' {
        $temporary = Join-Path $installerTestsRoot 'bad.download'
        $destination = Join-Path $installerTestsRoot 'old.gguf'
        [IO.File]::WriteAllText($temporary,'corrupt')
        [IO.File]::WriteAllText($destination,'original model')
        $failed = $false
        try { Complete-LumaTranslationModelFile $temporary $destination ('0'*64) 7 } catch { $failed=$true }
        Assert-Installer $failed 'A wrong SHA-256 must fail.'
        Assert-Installer ([IO.File]::ReadAllText($destination) -eq 'original model') 'The existing file changed before verification.'
    }
    Test-InstallerCase 'verified temporary file replaces invalid model' {
        $temporary = Join-Path $installerTestsRoot 'verified.download'
        $destination = Join-Path $installerTestsRoot 'replace.gguf'
        [IO.File]::WriteAllText($temporary,'verified model bytes')
        [IO.File]::WriteAllText($destination,'invalid old model')
        $hash = (Get-FileHash -LiteralPath $temporary -Algorithm SHA256).Hash
        Complete-LumaTranslationModelFile $temporary $destination $hash 20
        Assert-Installer ([IO.File]::ReadAllText($destination) -eq 'verified model bytes') 'Verified bytes were not installed.'
        Assert-Installer (!(Test-Path -LiteralPath $temporary)) 'Temporary file was not renamed.'
    }
    Test-InstallerCase 'truncated download never replaces existing file' {
        $temporary=Join-Path $installerTestsRoot 'short.download'
        $destination=Join-Path $installerTestsRoot 'keep.gguf'
        [IO.File]::WriteAllText($temporary,'short')
        [IO.File]::WriteAllText($destination,'original')
        $hash=(Get-FileHash -LiteralPath $temporary -Algorithm SHA256).Hash
        $failed=$false
        try { Complete-LumaTranslationModelFile $temporary $destination $hash 20 } catch { $failed=$true }
        Assert-Installer $failed 'A correct hash with the wrong pinned size must fail.'
        Assert-Installer ([IO.File]::ReadAllText($destination) -eq 'original') 'A truncated download replaced the existing file.'
    }
    Test-InstallerCase 'lock rejects model filename escaping player directory' {
        $directory = Join-Path $installerTestsRoot 'unsafe-lock'
        New-Item -ItemType Directory -Path $directory -Force | Out-Null
        $lock = Get-Content -LiteralPath (Join-Path $installerProject 'translation-dependencies.lock.json') -Raw | ConvertFrom-Json
        $lock.model.file='../escape.gguf'
        $lock | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $directory 'translation-dependencies.lock.json') -Encoding UTF8
        $failed=$false
        try { Get-LumaTranslationModelSpec -Directory $directory | Out-Null } catch { $failed=$true }
        Assert-Installer $failed 'A lock filename containing a parent directory must fail.'
    }
    Test-InstallerCase 'lock can be found in the packaged licenses folder' {
        $directory=Join-Path $installerTestsRoot 'licenses-fallback'
        $licenses=Join-Path $directory 'licenses'
        New-Item -ItemType Directory -Path $licenses -Force | Out-Null
        Copy-Item -LiteralPath (Join-Path $installerProject 'translation-dependencies.lock.json') -Destination $licenses
        $model=Get-LumaTranslationModelSpec -Directory $directory
        Assert-Installer ($model.file -eq 'Qwen3-4B-Q4_K_M.gguf') 'The licenses-folder manifest was not found.'
    }
    Test-InstallerCase 'quality profile selects dedicated model and default remains fast' {
        $directory=Join-Path $installerTestsRoot 'profile-selection'
        New-Item -ItemType Directory -Path $directory -Force | Out-Null
        $lock=Get-Content -LiteralPath (Join-Path $installerProject 'translation-dependencies.lock.json') -Raw | ConvertFrom-Json
        $quality=[pscustomobject]@{id='tencent/Hy-MT2-7B-GGUF';revision='ab8472660ac61fac25f1af43fac2599d52a8a775';file='Hy-MT2-7B-Q4_K_M.gguf';url='https://huggingface.co/tencent/Hy-MT2-7B-GGUF/resolve/ab8472660ac61fac25f1af43fac2599d52a8a775/Hy-MT2-7B-Q4_K_M.gguf';sha256='9f96256500f3fc1ab4d64336b58f52a949a95ad7516b0c229476eef782f9f77b';bytes=4624648896}
        $lock | Add-Member -MemberType NoteProperty -Name qualityModel -Value $quality -Force
        $lock | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $directory 'translation-dependencies.lock.json') -Encoding UTF8
        $selected=Get-LumaTranslationModelSpec -Directory $directory -Profile quality
        Assert-Installer ($selected.file -eq $quality.file) 'Quality must select the dedicated translation model.'
        $default=Get-LumaTranslationModelSpec -Directory $directory
        Assert-Installer ($default.file -eq $lock.model.file) 'Existing default fast model must remain unchanged.'
    }
    Test-InstallerCase 'missing quality profile fails instead of silently choosing fast' {
        $directory=Join-Path $installerTestsRoot 'missing-quality'
        New-Item -ItemType Directory -Path $directory -Force | Out-Null
        $lock=Get-Content -LiteralPath (Join-Path $installerProject 'translation-dependencies.lock.json') -Raw | ConvertFrom-Json
        $lock.PSObject.Properties.Remove('qualityModel')
        $lock | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $directory 'translation-dependencies.lock.json') -Encoding UTF8
        $failed=$false
        try { Get-LumaTranslationModelSpec -Directory $directory -Profile quality | Out-Null } catch { $failed=$true }
        Assert-Installer $failed 'An unavailable requested quality profile must fail.'
    }
    if (!$ExistingModel) { $ExistingModel=Join-Path $installerProject '.deps\translation\models\Qwen3-4B-Q4_K_M.gguf' }
    if (Test-Path -LiteralPath $ExistingModel) {
        Test-InstallerCase 'valid real model is reused without download' {
            $directory = Join-Path $installerTestsRoot 'player package 한국어'
            $models = Join-Path $directory 'runtime\translation\models'
            New-Item -ItemType Directory -Path $models -Force | Out-Null
            Copy-Item -LiteralPath (Join-Path $installerProject 'translation-dependencies.lock.json') -Destination $directory
            $target = Join-Path $models 'Qwen3-4B-Q4_K_M.gguf'
            New-Item -ItemType HardLink -Path $target -Target ([IO.Path]::GetFullPath($ExistingModel)) | Out-Null
            $before=(Get-Item -LiteralPath $target).LastWriteTimeUtc
            $result=Install-LumaTranslationModel -Directory $directory
            Assert-Installer $result.AlreadyInstalled 'Existing valid model was not reused.'
            Assert-Installer ($result.BytesTransferred -eq 0) 'Existing model unexpectedly downloaded data.'
            Assert-Installer ((Get-Item -LiteralPath $target).LastWriteTimeUtc -eq $before) 'Existing model was overwritten.'
        }
        Test-InstallerCase 'packaged tools default works in Windows PowerShell 5.1' {
            $directory=Join-Path $installerTestsRoot 'player package 한국어'
            $tools=Join-Path $directory 'tools'
            New-Item -ItemType Directory -Path $tools -Force | Out-Null
            $copied=Join-Path $tools 'install-translation.ps1'
            Copy-Item -LiteralPath (Join-Path $installerProject 'scripts\install-translation.ps1') -Destination $copied
            $windowsPowerShell=Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
            & $windowsPowerShell -NoProfile -ExecutionPolicy Bypass -File $copied | Out-Host
            Assert-Installer ($LASTEXITCODE -eq 0) 'Packaged default player directory failed in Windows PowerShell.'
            $target=Join-Path $directory 'runtime\translation\models\Qwen3-4B-Q4_K_M.gguf'
            Assert-Installer (Test-Path -LiteralPath $target) 'The packaged model path disappeared.'
        }
    } else { Write-Output 'SKIP valid real model reuse: pass -ExistingModel to a downloaded pinned model.' }
    $qualityModel=Join-Path $installerProject '.deps\translation\models\Hy-MT2-7B-Q4_K_M.gguf'
    if (Test-Path -LiteralPath $qualityModel) {
        Test-InstallerCase 'valid dedicated quality model is reused without downloading fast model' {
            $directory=Join-Path $installerTestsRoot 'quality player 한국어'
            $models=Join-Path $directory 'runtime\translation\models'
            New-Item -ItemType Directory -Path $models -Force | Out-Null
            Copy-Item -LiteralPath (Join-Path $installerProject 'translation-dependencies.lock.json') -Destination $directory
            $target=Join-Path $models 'Hy-MT2-7B-Q4_K_M.gguf'
            New-Item -ItemType HardLink -Path $target -Target ([IO.Path]::GetFullPath($qualityModel)) | Out-Null
            $result=Install-LumaTranslationModel -Directory $directory -Profile quality
            Assert-Installer $result.AlreadyInstalled 'Existing quality model was not reused.'
            Assert-Installer ($result.BytesTransferred -eq 0) 'Quality reuse unexpectedly downloaded data.'
            Assert-Installer (!(Test-Path (Join-Path $models 'Qwen3-4B-Q4_K_M.gguf'))) 'Quality installation downloaded the fast model.'
        }
        Test-InstallerCase 'packaged quality selection works in Windows PowerShell 5.1' {
            $directory=Join-Path $installerTestsRoot 'quality player 한국어'
            $tools=Join-Path $directory 'tools'
            New-Item -ItemType Directory -Path $tools -Force | Out-Null
            $copied=Join-Path $tools 'install-translation.ps1'
            Copy-Item -LiteralPath (Join-Path $installerProject 'scripts\install-translation.ps1') -Destination $copied
            $windowsPowerShell=Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
            & $windowsPowerShell -NoProfile -ExecutionPolicy Bypass -File $copied -Profile quality | Out-Host
            Assert-Installer ($LASTEXITCODE -eq 0) 'Packaged quality installer failed in Windows PowerShell.'
        }
    } else { Write-Output 'SKIP dedicated quality model reuse: pinned local model is absent.' }
    if ($RunDownloadTest) {
        Test-InstallerCase 'HttpClient streams a small pinned upstream file and verifies before rename' {
            $reference=Join-Path $installerProject 'LICENSES\translation\Qwen3-4B-Apache-2.0.txt'
            $temporary=Join-Path $installerTestsRoot 'transport.download'
            $destination=Join-Path $installerTestsRoot 'transport-verified.txt'
            $size=(Get-Item -LiteralPath $reference).Length
            $hash=(Get-FileHash -LiteralPath $reference -Algorithm SHA256).Hash
            $received=Receive-LumaTranslationModelFile -Uri 'https://huggingface.co/Qwen/Qwen3-4B-GGUF/resolve/bc640142c66e1fdd12af0bd68f40445458f3869b/LICENSE' -TemporaryPath $temporary -ExpectedBytes $size
            Assert-Installer ($received -eq $size) 'The streamed size is wrong.'
            Complete-LumaTranslationModelFile $temporary $destination $hash $size
            Assert-Installer ((Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash -eq $hash) 'The streamed file hash is wrong.'
        }
    }
}
finally {
    if (!$installerTestsRoot.StartsWith($installerTestsParent+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) { throw 'Unsafe installer test cleanup target.' }
    Remove-Item -LiteralPath $installerTestsRoot -Recurse -Force
}
if ($script:installerFailures -gt 0) { throw "$script:installerFailures translation installer test(s) failed." }
