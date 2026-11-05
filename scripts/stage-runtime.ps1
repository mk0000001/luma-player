param([string]$Destination = 'build')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
if (![IO.Path]::IsPathRooted($Destination)) { $Destination = Join-Path $projectRoot $Destination }
$runtime = Join-Path $Destination 'runtime'
foreach ($folder in 'vlc','mpv','whisper','models','tools') { New-Item -ItemType Directory -Path (Join-Path $runtime $folder) -Force | Out-Null }
$vlc = Join-Path $projectRoot '.deps\vlc\vlc-3.0.24'
Copy-Item -Path "$vlc\*.dll","$vlc\vlc.exe","$vlc\COPYING*","$vlc\AUTHORS*" -Destination (Join-Path $runtime 'vlc') -Force
Copy-Item -LiteralPath (Join-Path $vlc 'plugins') -Destination (Join-Path $runtime 'vlc') -Recurse -Force
Copy-Item -LiteralPath (Join-Path $projectRoot '.deps\mpv\libmpv-2.dll') -Destination (Join-Path $runtime 'mpv') -Force
$whisper = Join-Path $projectRoot '.deps\whisper\Release'
Copy-Item -Path "$whisper\*.dll","$whisper\whisper-cli.exe" -Destination (Join-Path $runtime 'whisper') -Force
$ffmpeg = Join-Path $projectRoot '.deps\ffmpeg\ffmpeg-9.0.2-essentials_build'
Copy-Item -LiteralPath (Join-Path $ffmpeg 'bin\ffmpeg.exe'),(Join-Path $ffmpeg 'LICENSE') -Destination (Join-Path $runtime 'tools') -Force
$model = Join-Path $projectRoot '.deps\models\ggml-base.bin'
if (Test-Path -LiteralPath $model) { Copy-Item -LiteralPath $model -Destination (Join-Path $runtime 'models') -Force }
$licenses = Join-Path $Destination 'licenses'
New-Item -ItemType Directory -Path $licenses -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $projectRoot 'LICENSE'),(Join-Path $projectRoot 'THIRD_PARTY_NOTICES.md'),(Join-Path $projectRoot 'dependencies.lock.json') -Destination $licenses -Force
Copy-Item -LiteralPath (Join-Path $projectRoot 'LICENSES') -Destination $licenses -Recurse -Force
