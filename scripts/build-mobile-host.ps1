param([string]$Destination = 'build/runtime/tools')

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$toolchain = Join-Path $projectRoot '.deps/go'
$compiler = Join-Path $toolchain 'bin/go.exe'
if (!(Test-Path -LiteralPath $compiler -PathType Leaf)) {
    throw 'Run bootstrap-windows.ps1 to install the verified Go toolchain.'
}
$version = & $compiler version
if ($LASTEXITCODE -ne 0 -or $version -ne 'go version go1.26.11 windows/amd64') {
    throw 'The mobile host requires the pinned Go 1.26.11 Windows x64 toolchain.'
}
if (![IO.Path]::IsPathRooted($Destination)) { $Destination = Join-Path $projectRoot $Destination }
$Destination = [IO.Path]::GetFullPath($Destination)
New-Item -ItemType Directory -Path $Destination -Force | Out-Null
$temporary = Join-Path $Destination ('lumaplayer-mobile-host.' + [guid]::NewGuid().ToString('N') + '.tmp.exe')
$environment = @{
    GOTOOLCHAIN = 'local'
    GOWORK = 'off'
    GOPROXY = 'off'
    CGO_ENABLED = '0'
    GOOS = 'windows'
    GOARCH = 'amd64'
    GOFLAGS = ''
}
$saved = @{}
foreach ($name in $environment.Keys) {
    $saved[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
    [Environment]::SetEnvironmentVariable($name, $environment[$name], 'Process')
}
Push-Location -LiteralPath (Join-Path $projectRoot 'mobile-host')
try {
    & $compiler build -trimpath -buildvcs=false '-ldflags=-H=windowsgui' -o $temporary .
    if ($LASTEXITCODE -ne 0) { throw 'Mobile host build failed.' }
    Move-Item -LiteralPath $temporary -Destination (Join-Path $Destination 'lumaplayer-mobile-host.exe') -Force
    Copy-Item -LiteralPath (Join-Path $toolchain 'LICENSE') -Destination (Join-Path $Destination 'Go-LICENSE.txt') -Force
    Copy-Item -LiteralPath (Join-Path $toolchain 'PATENTS') -Destination (Join-Path $Destination 'Go-PATENTS.txt') -Force
} finally {
    Pop-Location
    foreach ($name in $saved.Keys) { [Environment]::SetEnvironmentVariable($name, $saved[$name], 'Process') }
    if (Test-Path -LiteralPath $temporary -PathType Leaf) { Remove-Item -LiteralPath $temporary }
}
