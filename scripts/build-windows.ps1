param([string]$Configuration = 'Release', [switch]$CoreOnly)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $installation) { throw 'Visual Studio 2022 C++ Build Tools are required.' }
$devcmd = Join-Path $installation 'Common7\Tools\VsDevCmd.bat'
& $env:ComSpec /d /c "`"$devcmd`" -arch=x64 -host_arch=x64 >nul && set" | ForEach-Object {
    if ($_ -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process') }
}
$qtRoot = Join-Path $projectRoot '.deps\qt'
$env:PATH = "$qtRoot\bin;$env:PATH"
$appFlag = if ($CoreOnly) { 'OFF' } else { 'ON' }
& cmake -S $projectRoot -B (Join-Path $projectRoot 'build') -G Ninja "-DCMAKE_CXX_COMPILER=$((Get-Command cl).Source)" "-DCMAKE_PREFIX_PATH=$qtRoot" "-DCMAKE_BUILD_TYPE=$Configuration" "-DLUMA_BUILD_APP=$appFlag"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& cmake --build (Join-Path $projectRoot 'build') --parallel 6
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& ctest --test-dir (Join-Path $projectRoot 'build') --output-on-failure
exit $LASTEXITCODE
