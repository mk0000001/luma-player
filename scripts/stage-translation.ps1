param(
    [Parameter(Mandatory=$true)][string]$RuntimeDirectory,
    [switch]$SkipModel,
    [switch]$SkipOpenVino,
    [switch]$IncludeCuda,
    [ValidateSet('fast','quality','all')][string]$ModelProfile='fast'
)
$ErrorActionPreference = 'Stop'
$translationProject = Split-Path -Parent $PSScriptRoot
$translationSource = Join-Path $translationProject '.deps\translation'
$translationTarget = Join-Path ([IO.Path]::GetFullPath($RuntimeDirectory)) 'translation'
if ([IO.Path]::GetFullPath($translationSource).TrimEnd('\') -eq [IO.Path]::GetFullPath($translationTarget).TrimEnd('\')) { throw 'Translation staging source and target must differ.' }
$translationLockPath = Join-Path $translationProject 'translation-dependencies.lock.json'
$translationLock = Get-Content -LiteralPath $translationLockPath -Raw | ConvertFrom-Json
New-Item -ItemType Directory -Path $translationTarget -Force | Out-Null
foreach ($dependency in $translationLock.windows) {
    if ($dependency.target -eq 'cuda') { continue }
    if ($SkipOpenVino -and $dependency.target -eq 'openvino') { continue }
    $source = Join-Path $translationSource $dependency.target
    if (!(Test-Path -LiteralPath (Join-Path $source $dependency.marker))) { throw "Run bootstrap-translation.ps1 first: $($dependency.id)" }
    $target = Join-Path $translationTarget $dependency.target
    New-Item -ItemType Directory -Path $target -Force | Out-Null
    # Keep every native backend dependency in its own directory, separate from ASR.
    Get-ChildItem -LiteralPath $source -File | Where-Object { $_.Extension -eq '.dll' -or $_.Name -eq 'llama-server.exe' -or $_.Name -like 'LICENSE*' } | ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination $target -Force
    }
}
if ($IncludeCuda) {
    . (Join-Path $PSScriptRoot 'install-translation-cuda.ps1')
    $cudaSpec=Get-LumaTranslationCudaSpec -Directory $translationProject -Development
    $cudaSource=Join-Path $translationSource 'cuda'
    if (!(Test-LumaTranslationCudaInstallation -Spec $cudaSpec -Directory $cudaSource)) { throw 'Run install-translation-cuda.ps1 -Development first.' }
    $cudaTarget=Join-Path $translationTarget 'cuda'
    New-Item -ItemType Directory -Path $cudaTarget -Force | Out-Null
    Get-ChildItem -LiteralPath $cudaSource -Force | ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $cudaTarget -Recurse -Force }
    if (!(Test-LumaTranslationCudaInstallation -Spec $cudaSpec -Directory $cudaTarget)) { throw 'Staged CUDA runtime verification failed.' }
}
if (!$SkipModel) {
    if ($ModelProfile -ne 'fast' -and !$translationLock.qualityModel) { throw 'The requested quality translation model is absent from the lock.' }
    $selectedModels = if ($ModelProfile -eq 'quality') { @($translationLock.qualityModel) } elseif ($ModelProfile -eq 'all') { @($translationLock.model,$translationLock.qualityModel) } else { @($translationLock.model) }
    $models = Join-Path $translationTarget 'models'
    New-Item -ItemType Directory -Path $models -Force | Out-Null
    foreach ($spec in $selectedModels) {
        $model = Join-Path $translationSource "models\$($spec.file)"
        if (!(Test-Path -LiteralPath $model) -or (Get-Item -LiteralPath $model).Length -ne $spec.bytes -or (Get-FileHash -LiteralPath $model -Algorithm SHA256).Hash -ne $spec.sha256) { throw "Translation model size/SHA-256 is missing or wrong: $($spec.file). Run bootstrap-translation.ps1 with the selected model profile." }
        $destination = Join-Path $models $spec.file
        Copy-Item -LiteralPath $model -Destination $destination -Force
        if ((Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash -ne $spec.sha256) { throw 'Staged translation model SHA-256 mismatch.' }
    }
}
$licenseTarget = Join-Path $translationTarget 'licenses'
New-Item -ItemType Directory -Path $licenseTarget -Force | Out-Null
Get-ChildItem -LiteralPath (Join-Path $translationProject 'LICENSES\translation') | ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $licenseTarget -Recurse -Force }
Copy-Item -LiteralPath $translationLockPath -Destination (Join-Path $translationTarget 'dependencies.lock.json') -Force
Write-Output "Staged native local translation: $translationTarget"
