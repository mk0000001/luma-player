param([switch]$SkipModel, [switch]$SkipOpenVino)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$translationProject = Split-Path -Parent $PSScriptRoot
$translationRoot = Join-Path $translationProject '.deps\translation'
$translationDownloads = Join-Path $translationProject '.deps\downloads'
$translationLock = Get-Content -LiteralPath (Join-Path $translationProject 'translation-dependencies.lock.json') -Raw | ConvertFrom-Json
New-Item -ItemType Directory -Path $translationRoot,$translationDownloads -Force | Out-Null
function Get-TranslationFile([string]$Url,[string]$Destination,[string]$Hash) {
    if ((Test-Path -LiteralPath $Destination) -and (Get-FileHash -LiteralPath $Destination -Algorithm SHA256).Hash -eq $Hash) { return }
    $partial = "$Destination.part"
    Invoke-WebRequest -Uri $Url -OutFile $partial -TimeoutSec 1800
    if ((Get-FileHash -LiteralPath $partial -Algorithm SHA256).Hash -ne $Hash) { throw "Translation SHA-256 mismatch: $Destination" }
    Move-Item -LiteralPath $partial -Destination $Destination -Force
}
foreach ($dependency in $translationLock.windows) {
    if ($SkipOpenVino -and $dependency.target -eq 'openvino') { continue }
    $archive = Join-Path $translationDownloads $dependency.archive
    Get-TranslationFile $dependency.url $archive $dependency.sha256
    $target = Join-Path $translationRoot $dependency.target
    New-Item -ItemType Directory -Path $target -Force | Out-Null
    $stamp = Join-Path $target '.luma-translation-archive.sha256'
    if (!(Test-Path -LiteralPath (Join-Path $target $dependency.marker)) -or !(Test-Path -LiteralPath $stamp) -or (Get-Content -LiteralPath $stamp -Raw).Trim() -ne $dependency.sha256) {
        & tar -xf $archive -C $target
        if ($LASTEXITCODE -ne 0) { throw "Translation archive extraction failed: $($dependency.id)" }
        Set-Content -LiteralPath $stamp -Value $dependency.sha256 -NoNewline
    }
    Write-Output "Verified local translation runtime: $($dependency.id)"
}
foreach ($license in $translationLock.licenses) {
    $path = Join-Path $translationProject "LICENSES\translation\$($license.file)"
    if (!(Test-Path -LiteralPath $path) -or (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $license.sha256) { throw "Translation license missing or changed: $($license.file)" }
}
if (!$SkipModel) {
    Write-Output 'Verifying optional Korean translation model (2,497,280,256 bytes; about 2.33 GiB).'
    $models = Join-Path $translationRoot 'models'
    New-Item -ItemType Directory -Path $models -Force | Out-Null
    Get-TranslationFile $translationLock.model.url (Join-Path $models $translationLock.model.file) $translationLock.model.sha256
    Write-Output "Verified local translation model: $($translationLock.model.file)"
}
