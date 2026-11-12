$ErrorActionPreference='Stop'
$project=Split-Path -Parent $PSScriptRoot
. (Join-Path $project 'scripts/install-ai-acceleration.ps1')
$root=Join-Path $project ('.deps/ai-acceleration-tests/'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $root -Force | Out-Null
$lockPath=Join-Path $root 'translation-dependencies.lock.json'
Copy-Item -LiteralPath (Join-Path $project 'translation-dependencies.lock.json') -Destination $lockPath
foreach($backend in 'vulkan','openvino'){
    $spec=Get-LumaAccelerationArchive -Directory $root -Backend $backend
    if($spec.target -cne $backend -or $spec.marker -cne 'llama-server.exe'){throw 'Incorrect acceleration archive'}
}
$manifest=Get-Content -LiteralPath $lockPath -Raw | ConvertFrom-Json
($manifest.windows | Where-Object target -EQ 'openvino').url='https://example.invalid/llama.zip'
$manifest | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $lockPath -Encoding UTF8
$rejected=$false
try{Get-LumaAccelerationArchive -Directory $root -Backend openvino | Out-Null}catch{$rejected=$true}
if(!$rejected){throw 'Accepted an unpinned archive URL'}

Add-Type -AssemblyName System.IO.Compression
function New-TestArchive([string]$Name,[string[]]$Entries){
    $path=Join-Path $root $Name
    $file=[IO.File]::Open($path,[IO.FileMode]::CreateNew)
    $zip=[IO.Compression.ZipArchive]::new($file,[IO.Compression.ZipArchiveMode]::Create)
    try{
        foreach($name in $Entries){
            $entry=$zip.CreateEntry($name)
            $stream=$entry.Open()
            try{$stream.WriteByte(65)}finally{$stream.Dispose()}
        }
    }finally{$zip.Dispose();$file.Dispose()}
    [pscustomobject]@{path=$path;sha256=(Get-FileHash -LiteralPath $path).Hash;bytes=(Get-Item -LiteralPath $path).Length}
}
$valid=New-TestArchive 'valid.zip' @('llama-server.exe','ggml-vulkan.dll','licenses/LICENSE-MIT','llama-cli.exe')
$destination=Join-Path $root 'valid'
Expand-LumaAccelerationArchive -Archive $valid.path -Spec $valid -Destination $destination -Backend vulkan
foreach($file in 'llama-server.exe','ggml-vulkan.dll','licenses/LICENSE-MIT'){
    if(!(Test-Path -LiteralPath (Join-Path $destination $file) -PathType Leaf)){throw "Missing staged file: $file"}
}
if(Test-Path -LiteralPath (Join-Path $destination 'llama-cli.exe')){throw 'Unexpected executable was staged'}
$invalid=New-TestArchive 'invalid.zip' @('../escape.dll','llama-server.exe','ggml-vulkan.dll')
$rejected=$false
try{Expand-LumaAccelerationArchive -Archive $invalid.path -Spec $invalid -Destination (Join-Path $root 'invalid') -Backend vulkan}catch{$rejected=$true}
if(!$rejected -or (Test-Path -LiteralPath (Join-Path $root 'escape.dll'))){throw 'Unsafe ZIP entry was accepted'}
if($env:OS -eq 'Windows_NT'){
    $shell=Join-Path ([Environment]::SystemDirectory) 'WindowsPowerShell/v1.0/powershell.exe'
    $missing=Join-Path $root 'explicit-install-root'
    $output=& $shell -NoProfile -NonInteractive -File (Join-Path $project 'scripts/install-ai-acceleration.ps1') -PlayerDirectory $missing -Backend vulkan 2>&1
    if($LASTEXITCODE -eq 0 -or ($output -join ' ') -notmatch 'explicit-install-root'){throw 'Explicit installation directory was lost'}
}
Write-Output 'PASS AI acceleration pins and app-local extraction'
