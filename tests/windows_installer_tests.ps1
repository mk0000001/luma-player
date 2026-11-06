$ErrorActionPreference='Stop'
. "$PSScriptRoot/../scripts/installer-support.ps1"
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
Write-Output "PASS $count installer integrity cases"
