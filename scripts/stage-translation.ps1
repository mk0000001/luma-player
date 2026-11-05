param(
    [Parameter(Mandatory=$true)][string]$RuntimeDirectory,
    [switch]$SkipModel,
    [switch]$SkipOpenVino
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
if (!$SkipModel) {
    $model = Join-Path $translationSource "models\$($translationLock.model.file)"
    if (!(Test-Path -LiteralPath $model) -or (Get-FileHash -LiteralPath $model -Algorithm SHA256).Hash -ne $translationLock.model.sha256) { throw 'Translation model SHA-256 is missing or wrong. Run bootstrap-translation.ps1.' }
    $models = Join-Path $translationTarget 'models'
    New-Item -ItemType Directory -Path $models -Force | Out-Null
    $destination = Join-Path $models $translationLock.model.file
    Copy-Item -LiteralPath $model -Destination $destination -Force
    if ((Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash -ne $translationLock.model.sha256) { throw 'Staged translation model SHA-256 mismatch.' }
}
$licenseTarget = Join-Path $translationTarget 'licenses'
New-Item -ItemType Directory -Path $licenseTarget -Force | Out-Null
Get-ChildItem -LiteralPath (Join-Path $translationProject 'LICENSES\translation') | ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $licenseTarget -Recurse -Force }
Copy-Item -LiteralPath $translationLockPath -Destination (Join-Path $translationTarget 'dependencies.lock.json') -Force
Write-Output "Staged native local translation: $translationTarget"
