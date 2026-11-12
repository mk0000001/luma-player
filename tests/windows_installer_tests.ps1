$ErrorActionPreference='Stop'
. "$PSScriptRoot/../scripts/installer-support.ps1"
. "$PSScriptRoot/../scripts/package-support.ps1"
. "$PSScriptRoot/../scripts/bootstrap-support.ps1"
$root=Split-Path -Parent $PSScriptRoot
$a=Get-Content "$root/dependencies.lock.json" -Raw | ConvertFrom-Json
$t=Get-Content "$root/translation-dependencies.lock.json" -Raw | ConvertFrom-Json
$count=0
foreach($spec in @($a.accurateModel,$t.qualityModel)){
 $line=New-LumaInstallerModelEntry -Spec $spec -Destination 'runtime/models' -Component 'accurate'
 if($line -notlike ('*Hash: "'+$spec.sha256+'"*') -or $line -notlike '*external download ignoreversion*'){throw 'missing-native-integrity-enforcement'}
 $count++
 foreach($field in @('url','file','sha256','bytes')){
  $copy=$spec | ConvertTo-Json -Depth 9 | ConvertFrom-Json
  switch($field){url{$copy.url='http://evil.invalid/model'} file{$copy.file='../escape.bin'} sha256{$copy.sha256='invalid'} bytes{$copy.bytes=-1}}
  $rejected=$false;try{New-LumaInstallerModelEntry -Spec $copy -Destination 'runtime/models' -Component 'accurate' | Out-Null}catch{$rejected=$true}
  if(!$rejected){throw "accepted-invalid-$field"};$count++
 }
}
$fixture=Join-Path $root ('.deps/installer-tests/'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $fixture -Force | Out-Null
[IO.File]::WriteAllText((Join-Path $fixture 'manifest.json'),'{}')
$caught=$false
try{& "$root/scripts/build-installer.ps1" -Payload $fixture -Output ($fixture+'/new-output') -Compiler 'never-executed'}catch{$caught=$_.Exception.Message -eq 'Immutable payload manifest hash mismatch'}
if(!$caught -or (Test-Path ($fixture+'/new-output'))){throw 'mutated manifest was not rejected before output creation'}
$count++
$caught=$false
try{& "$root/scripts/build-installer.ps1" -Payload $fixture -Output $fixture -Compiler 'never-executed'}catch{$caught=$_.Exception.Message -like 'Use a new installer output*'}
if(!$caught -or [IO.File]::ReadAllText((Join-Path $fixture 'manifest.json')) -cne '{}'){throw 'existing output not preserved'}
$count++

$first=Join-Path $fixture 'first.txt'
$second=Join-Path $fixture 'second.txt'
[IO.File]::WriteAllText($first,'first')
[IO.File]::WriteAllText($second,'second')
$firstEntry=@{path='first.txt';bytes=5;sha256=(Get-FileHash -LiteralPath $first).Hash}
$secondEntry=@{path='second.txt';bytes=6;sha256=(Get-FileHash -LiteralPath $second).Hash}
$complete=@{files=@($firstEntry,$secondEntry)}
Assert-LumaInstallerPayload -Directory $fixture -Manifest $complete
$count++

$rejected=$false
try{Assert-LumaInstallerPayload -Directory $fixture -Manifest @{files=@($firstEntry,$firstEntry)}}catch{$rejected=$true}
if(!$rejected){throw 'duplicate entries must not stand in for missing manifest coverage'}
$count++

$hidden=Join-Path $fixture 'hidden.txt'
[IO.File]::WriteAllText($hidden,'hidden')
[IO.File]::SetAttributes($hidden,[IO.FileAttributes]::Hidden)
$rejected=$false
try{Assert-LumaInstallerPayload -Directory $fixture -Manifest $complete}catch{$rejected=$true}
if(!$rejected){throw 'hidden payload files must also be listed in the manifest'}
$count++

$target=Join-Path $fixture 'bootstrap-target'
New-Item -ItemType Directory -Path $target | Out-Null
[IO.File]::WriteAllText((Join-Path $target 'marker.txt'),'old cached extraction')
[IO.File]::WriteAllText((Join-Path $target 'old-only.txt'),'no longer shipped')
$pin1='1'*64
$pin2='2'*64
Install-LumaBootstrapExtraction -Target $target -Markers 'marker.txt' -Hashes $pin1 -Extract {
 param($stage)
 [IO.File]::WriteAllText((Join-Path $stage 'marker.txt'),'first archive')
}
if([IO.File]::ReadAllText((Join-Path $target 'marker.txt')) -cne 'first archive' -or (Test-Path -LiteralPath (Join-Path $target 'old-only.txt'))){throw 'obsolete extraction survived a verified bootstrap refresh'}
$count++

Install-LumaBootstrapExtraction -Target $target -Markers 'marker.txt' -Hashes $pin1 -Extract { throw 'matching cache was re-extracted' }
$count++

$rejected=$false
try{
 Install-LumaBootstrapExtraction -Target $target -Markers 'marker.txt' -Hashes $pin2 -Extract { throw 'fixture extraction failed' }
}catch{$rejected=$_.Exception.Message -eq 'fixture extraction failed'}
if(!$rejected -or [IO.File]::ReadAllText((Join-Path $target 'marker.txt')) -cne 'first archive'){throw 'failed extraction did not preserve the previous cache'}
$count++

Install-LumaBootstrapExtraction -Target $target -Markers 'marker.txt' -Hashes $pin2 -ContentDirectory 'archive-root' -Extract {
 param($stage)
 $nested=New-Item -ItemType Directory -Path (Join-Path $stage 'archive-root')
 [IO.File]::WriteAllText((Join-Path $nested.FullName 'marker.txt'),'second archive')
}
if([IO.File]::ReadAllText((Join-Path $target 'marker.txt')) -cne 'second archive'){throw 'a changed archive pin did not refresh the extracted cache'}
$count++
Write-Output "PASS $count installer integrity cases"
