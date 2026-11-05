param([string]$Version,[string]$OutputDirectory='dist',[string]$BuildDirectory='build',[switch]$IncludeTranslationModel,[switch]$AllowDirtySource)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'package-support.ps1')
if (!$Version) { $Version=(Get-Content -LiteralPath (Join-Path $projectRoot 'VERSION') -Raw).Trim() }
if ($Version -notmatch '^\d+\.\d+\.\d+(-[A-Za-z0-9.]+)?$') { throw 'Invalid semantic version' }
if ((Get-Content -LiteralPath (Join-Path $projectRoot 'VERSION') -Raw).Trim() -ne $Version) { throw 'Package version differs from VERSION' }
$source=Get-LumaPackageSource -ProjectRoot $projectRoot -AllowDirtySource:$AllowDirtySource
$paths=Get-LumaPackagePaths -ProjectRoot $projectRoot -Version $Version -BuildDirectory $BuildDirectory -OutputDirectory $OutputDirectory
Assert-LumaPackageTargetsFree -Paths $paths
$asrFiles=@(Get-LumaPackageAsrFiles -ProjectRoot $projectRoot)
$executable=Join-Path $paths.BuildRoot 'LumaPlayer.exe'
if(!(Test-Path -LiteralPath $executable) -or [Diagnostics.FileVersionInfo]::GetVersionInfo($executable).ProductVersion -ne $Version) { throw 'Executable product version differs from VERSION; rebuild the selected build directory.' }
$package=$paths.Package
New-Item -ItemType Directory -Path $package | Out-Null
Copy-Item -LiteralPath $executable,(Join-Path $projectRoot 'README.md'),(Join-Path $projectRoot 'CHANGELOG.md'),(Join-Path $projectRoot 'LICENSE'),(Join-Path $projectRoot 'THIRD_PARTY_NOTICES.md'),(Join-Path $projectRoot 'translation-dependencies.lock.json') -Destination $package
Copy-LumaPackageTrackedDocuments -ProjectRoot $projectRoot -Destination $package
New-Item -ItemType Directory -Path (Join-Path $package 'tools') -Force | Out-Null
foreach($name in 'install-translation.ps1','install-translation-cuda.ps1','install-asr-cuda.ps1') { Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination (Join-Path $package 'tools') }
Copy-Item -LiteralPath (Join-Path $package 'docs/INSTALL-TRANSLATION.md') -Destination (Join-Path $package 'tools/README-translation.md')
& (Join-Path $projectRoot '.deps\qt\bin\windeployqt.exe') --release --no-translations --no-opengl-sw --no-system-d3d-compiler (Join-Path $package 'LumaPlayer.exe')
if ($LASTEXITCODE -ne 0) { throw 'Qt deployment failed' }
& (Join-Path $PSScriptRoot 'stage-runtime.ps1') -Destination $package -SkipTranslationModel:(!$IncludeTranslationModel)
Assert-LumaPackagePublicFiles -Directory $package
Assert-LumaPackageCoreCudaOmitted -Directory $package
Assert-LumaPackageAsrFiles -Files $asrFiles -Directory (Join-Path $package 'runtime/models')
Assert-LumaPackageTranslationModel -Directory $package -IncludeTranslationModel:$IncludeTranslationModel
Assert-LumaPackageSourceUnchanged -ProjectRoot $projectRoot -Source $source
$manifestFiles=@(Get-LumaPackageManifestFiles -Directory $package)
$manifest=[ordered]@{schema=1;version=$Version;sourceCommit=$source.Commit;sourceDirty=$source.Dirty;translationModelIncluded=[bool]$IncludeTranslationModel;files=$manifestFiles}
[IO.File]::WriteAllText((Join-Path $package 'manifest.json'),($manifest | ConvertTo-Json -Depth 6),[Text.UTF8Encoding]::new($false))
$zip=$paths.Archive
if((Test-Path -LiteralPath $zip) -or (Test-Path -LiteralPath $paths.Checksum)) { throw 'Archive or checksum appeared during packaging; existing outputs were preserved.' }
$temporaryZip="$zip.$([guid]::NewGuid().ToString('N')).tmp.zip"
try {
    & tar -a -cf $temporaryZip -C (Split-Path -Parent $package) (Split-Path -Leaf $package)
    if ($LASTEXITCODE -ne 0) { throw 'ZIP creation failed' }
    if (!$IncludeTranslationModel -and (Get-Item -LiteralPath $temporaryZip).Length -ge 2GB) { throw 'Core ZIP exceeds the GitHub per-asset limit.' }
    # Publish without replacing an archive that appeared after preflight.
    [IO.File]::Move($temporaryZip,$zip)
} finally { if(Test-Path -LiteralPath $temporaryZip) { Remove-Item -LiteralPath $temporaryZip } }
$hash = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLower()
$checksum=[IO.File]::Open($paths.Checksum,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
try { $bytes=[Text.UTF8Encoding]::new($false).GetBytes("$hash  $(Split-Path -Leaf $zip)`n");$checksum.Write($bytes,0,$bytes.Length) }
finally { $checksum.Dispose() }
Write-Output $zip
