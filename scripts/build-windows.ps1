param([string]$Configuration = 'Release', [switch]$CoreOnly, [string]$BuildDirectory = 'build', [string[]]$Targets, [switch]$SkipTests)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$buildRoot = if ([IO.Path]::IsPathRooted($BuildDirectory)) { $BuildDirectory } else { Join-Path $projectRoot $BuildDirectory }
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $installation) { throw 'Visual Studio 2022 C++ Build Tools are required.' }
$devcmd = Join-Path $installation 'Common7\Tools\VsDevCmd.bat'
& $env:ComSpec /d /c "`"$devcmd`" -arch=x64 -host_arch=x64 >nul && set" | ForEach-Object {
    if ($_ -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process') }
}
$env:VSLANG = '1033'
# CMake/Ninja must see the same encoding as localized cl /showIncludes.
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
$OutputEncoding = [Console]::OutputEncoding
& chcp.com 65001 | Out-Null
$qtRoot = Join-Path $projectRoot '.deps\qt'
$env:PATH = "$qtRoot\bin;$env:PATH"
$appFlag = if ($CoreOnly) { 'OFF' } else { 'ON' }
$compilerState = Get-ChildItem (Join-Path $buildRoot 'CMakeFiles/*/CMakeCXXCompiler.cmake') -ErrorAction SilentlyContinue | Select-Object -First 1
$resetDependencies = $compilerState -and (Get-Content -LiteralPath $compilerState.FullName -Raw) -notmatch 'CMAKE_CXX_CL_SHOWINCLUDES_PREFIX "(Note: including file: |참고: 포함 파일: )'
$configureArgs = @('-S',$projectRoot,'-B',$buildRoot,'-G','Ninja',"-DCMAKE_CXX_COMPILER=$((Get-Command cl).Source)","-DCMAKE_PREFIX_PATH=$qtRoot","-DCMAKE_BUILD_TYPE=$Configuration","-DLUMA_BUILD_APP=$appFlag")
if ($resetDependencies) { $configureArgs += '--fresh' }
& cmake @configureArgs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
$buildArgs = @('--build',$buildRoot,'--parallel','6')
if ($Targets) { $buildArgs += @('--target') + $Targets }
if ($resetDependencies) { $buildArgs += '--clean-first' }
& cmake @buildArgs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
if ($SkipTests) { exit 0 }
& ctest --test-dir $buildRoot --output-on-failure
$testExit=$LASTEXITCODE
if($testExit -ne 0 -and (Test-Path -LiteralPath (Join-Path $buildRoot 'ui-test-results.txt'))) { Get-Content -LiteralPath (Join-Path $buildRoot 'ui-test-results.txt') -Tail 80 }
exit $testExit
