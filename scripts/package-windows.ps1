param([string]$Version='0.1.0-alpha.1',[string]$OutputDirectory='dist')
$ErrorActionPreference = 'Stop'
if ($Version -notmatch '^\d+\.\d+\.\d+(-[A-Za-z0-9.]+)?$') { throw 'Invalid semantic version' }
$projectRoot = Split-Path -Parent $PSScriptRoot
if ((Get-Content -LiteralPath (Join-Path $projectRoot 'VERSION') -Raw).Trim() -ne $Version) { throw 'Package version differs from VERSION' }
$outputRoot = if ([IO.Path]::IsPathRooted($OutputDirectory)) { $OutputDirectory } else { Join-Path $projectRoot $OutputDirectory }
$package = Join-Path $outputRoot "LumaPlayer-$Version-windows-x64"
if (Test-Path -LiteralPath $package) { throw 'Package destination exists; use a fresh output directory.' }
New-Item -ItemType Directory -Path $package -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $projectRoot 'build\LumaPlayer.exe'),(Join-Path $projectRoot 'README.md'),(Join-Path $projectRoot 'CHANGELOG.md'),(Join-Path $projectRoot 'LICENSE'),(Join-Path $projectRoot 'THIRD_PARTY_NOTICES.md') -Destination $package
Copy-Item -LiteralPath (Join-Path $projectRoot 'docs') -Destination (Join-Path $package 'docs') -Recurse
& (Join-Path $projectRoot '.deps\qt\bin\windeployqt.exe') --release --no-translations --no-opengl-sw --no-system-d3d-compiler (Join-Path $package 'LumaPlayer.exe')
if ($LASTEXITCODE -ne 0) { throw 'Qt deployment failed' }
& (Join-Path $PSScriptRoot 'stage-runtime.ps1') -Destination $package
$forbidden = @(Get-ChildItem -LiteralPath $package -Recurse -File | Where-Object { $_.Name -match '^(KEYDB\.cfg|HostKeyCertificate\.txt|ProcessingDeviceKeysSimple\.txt)$' -or $_.Extension -in '.srt','.wav','.pcm','.m2ts','.mp4','.mkv' })
if ($forbidden.Count) { throw 'Package contains private playback data' }
$zip = "$package.zip"
& tar -a -cf $zip -C (Split-Path -Parent $package) (Split-Path -Leaf $package)
if ($LASTEXITCODE -ne 0) { throw 'ZIP creation failed' }
$hash = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLower()
[IO.File]::WriteAllText("$zip.sha256", "$hash  $(Split-Path -Leaf $zip)`n",[Text.UTF8Encoding]::new($false))
Write-Output $zip
