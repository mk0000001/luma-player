param([switch]$SkipAiModel)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$projectRoot = Split-Path -Parent $PSScriptRoot
$dependencyRoot = Join-Path $projectRoot '.deps'
$downloads = Join-Path $dependencyRoot 'downloads'
New-Item -ItemType Directory -Path $downloads -Force | Out-Null
$lock = Get-Content -LiteralPath (Join-Path $projectRoot 'dependencies.lock.json') -Raw | ConvertFrom-Json
function Get-VerifiedFile([string]$Url, [string]$Destination, [string]$ExpectedHash) {
    if (!(Test-Path -LiteralPath $Destination) -or (Get-FileHash -LiteralPath $Destination -Algorithm SHA256).Hash -ne $ExpectedHash) {
        Invoke-WebRequest -Uri $Url -OutFile $Destination -TimeoutSec 240
    }
    if ((Get-FileHash -LiteralPath $Destination -Algorithm SHA256).Hash -ne $ExpectedHash) { throw "SHA-256 mismatch: $Destination" }
}
foreach ($dependency in $lock.windows) {
    $archive = Join-Path $downloads $dependency.archive
    Get-VerifiedFile $dependency.url $archive $dependency.sha256
    $target = Join-Path $dependencyRoot $dependency.target
    if (!(Test-Path -LiteralPath (Join-Path $target $dependency.marker))) {
        New-Item -ItemType Directory -Path $target -Force | Out-Null
        & tar -xf $archive -C $target
        if ($LASTEXITCODE -ne 0) { throw "Cannot extract $($dependency.id)" }
    }
    Write-Output "Verified $($dependency.id)"
}
$vlc = Join-Path $dependencyRoot 'vlc\vlc-3.0.24'
Copy-Item -LiteralPath (Join-Path $dependencyRoot 'aacs\mingw64\bin\libaacs-0.dll') -Destination (Join-Path $vlc 'libaacs.dll') -Force
Copy-Item -LiteralPath (Join-Path $dependencyRoot 'aacs\mingw64\bin\libbdplus-0.dll') -Destination (Join-Path $vlc 'libbdplus.dll') -Force
Copy-Item -LiteralPath (Join-Path $dependencyRoot 'aacs\mingw64\bin\libgcrypt-20.dll'),(Join-Path $dependencyRoot 'aacs\mingw64\bin\libgpg-error-0.dll') -Destination $vlc -Force
if (!$SkipAiModel) {
    $models = Join-Path $dependencyRoot 'models'
    New-Item -ItemType Directory -Path $models -Force | Out-Null
    Get-VerifiedFile $lock.model.url (Join-Path $models $lock.model.file) $lock.model.sha256
    Write-Output 'Verified multilingual Whisper model'
}
