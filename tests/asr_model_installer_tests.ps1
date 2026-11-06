param([string]$ExistingModel)
$ErrorActionPreference='Stop'
$asrProject=Split-Path -Parent $PSScriptRoot
. (Join-Path $asrProject 'scripts/install-asr-model.ps1')
$asrTestsParent=[IO.Path]::GetFullPath((Join-Path $asrProject '.deps/asr-model-installer-tests'))
$asrTestsRoot=Join-Path $asrTestsParent ([guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $asrTestsRoot -Force | Out-Null
$script:asrFailures=0;$script:asrTransportCalls=0;$script:asrTransportMode='valid'
$script:asrPayload=[Text.Encoding]::UTF8.GetBytes('Owned synthetic ASR model bytes')
function Assert-Asr([bool]$Condition,[string]$Message){if(!$Condition){throw $Message}}
function Test-AsrCase([string]$Name,[scriptblock]$Body){try{& $Body;Write-Output "PASS $Name"}catch{$script:asrFailures++;Write-Output "FAIL $Name : $($_.Exception.Message)"}}
function New-AsrFixture([string]$Name,[switch]$Packaged){
 $directory=Join-Path $asrTestsRoot $Name;New-Item -ItemType Directory -Path $directory -Force | Out-Null
 $base=if($Packaged){Join-Path $directory 'licenses'}else{$directory}
 $licenses=Join-Path $base 'LICENSES';New-Item -ItemType Directory -Path $licenses -Force | Out-Null
 $license=Join-Path $licenses 'Kotoba-Whisper-Apache-2.0.txt';[IO.File]::WriteAllText($license,'Owned license fixture',[Text.UTF8Encoding]::new($false))
 $temporary=Join-Path $directory 'fixture.bin';[IO.File]::WriteAllBytes($temporary,$script:asrPayload)
 $revision='e3a0cf6a62b95911703cfb97d819292e058f12c3'
 $spec=[pscustomobject]@{id='kotoba-tech/kotoba-whisper-v2.0-ggml';revision=$revision;file='ggml-owned-japanese.bin';url="https://huggingface.co/kotoba-tech/kotoba-whisper-v2.0-ggml/resolve/$revision/ggml-owned-japanese.bin";sha256=(Get-FileHash $temporary -Algorithm SHA256).Hash.ToLowerInvariant();bytes=$script:asrPayload.Length;license='Apache-2.0';licenseFile='Kotoba-Whisper-Apache-2.0.txt';licenseSha256=(Get-FileHash $license -Algorithm SHA256).Hash.ToLowerInvariant();language='ja';optionalAtRuntime=$true}
 $manifest=[pscustomobject]@{schema=2;japaneseModel=$spec};$lock=Join-Path $base 'dependencies.lock.json'
 [IO.File]::WriteAllText($lock,($manifest|ConvertTo-Json -Depth 8),[Text.UTF8Encoding]::new($false))
 return [pscustomobject]@{Directory=$directory;Manifest=$manifest;Lock=$lock;License=$license;Destination=(Join-Path $directory ('runtime/models/'+$spec.file))}
}
function Save-AsrFixture($Fixture){[IO.File]::WriteAllText($Fixture.Lock,($Fixture.Manifest|ConvertTo-Json -Depth 8),[Text.UTF8Encoding]::new($false))}
function Receive-LumaTranslationModelFile {
 param([uri]$Uri,[string]$TemporaryPath,[long]$ExpectedBytes)
 $script:asrTransportCalls++
 if($script:asrTransportMode -eq 'fail'){throw 'Owned transport failure'}
 $bytes=if($script:asrTransportMode -eq 'corrupt'){[Text.Encoding]::UTF8.GetBytes('Wrong synthetic ASR model byte')}elseif($script:asrTransportMode -eq 'short'){[byte[]]@(1,2,3)}else{$script:asrPayload}
 [IO.File]::WriteAllBytes($TemporaryPath,$bytes);return $bytes.Length
}
try{
 Test-AsrCase 'accurate full model has its own MIT profile and destination' {
  $fixture=New-AsrFixture 'accurate-profile'
  $model=$fixture.Manifest.japaneseModel | ConvertTo-Json | ConvertFrom-Json
  $model.id='ggerganov/whisper.cpp';$model.file='ggml-owned-accurate.bin';$model.license='MIT';$model.language='multilingual'
  $model.url='https://huggingface.co/ggerganov/whisper.cpp/resolve/'+$model.revision+'/'+$model.file
  $fixture.Manifest | Add-Member -NotePropertyName accurateModel -NotePropertyValue $model;Save-AsrFixture $fixture
  $spec=Get-LumaAsrModelSpec -Directory $fixture.Directory -Profile accurate
  Assert-Asr ($spec.file -eq 'ggml-owned-accurate.bin' -and $spec.license -eq 'MIT') 'Accurate profile must use its own pinned full model.'
  $script:asrTransportMode='valid';$script:asrTransportCalls=0;$installed=Install-LumaAsrModel -Directory $fixture.Directory -Profile accurate
  Assert-Asr ($script:asrTransportCalls -eq 1 -and (Test-Path -LiteralPath $installed.ModelPath)) 'Accurate profile must install verified bytes.'
  Assert-Asr ((Split-Path -Leaf $installed.ModelPath) -eq 'ggml-owned-accurate.bin') 'Accurate profile silently installed another model.'
 }
 Test-AsrCase 'accurate profile never falls back to Japanese model or license' {
  $fixture=New-AsrFixture 'accurate-missing';$failed=$false
  try{Get-LumaAsrModelSpec -Directory $fixture.Directory -Profile accurate|Out-Null}catch{$failed=$true}
  Assert-Asr $failed 'Missing accurate profile must not fall back.'
  $model=$fixture.Manifest.japaneseModel | ConvertTo-Json | ConvertFrom-Json
  $fixture.Manifest | Add-Member -NotePropertyName accurateModel -NotePropertyValue $model;Save-AsrFixture $fixture;$failed=$false
  try{Get-LumaAsrModelSpec -Directory $fixture.Directory -Profile accurate|Out-Null}catch{$failed=$true}
  Assert-Asr $failed 'Accurate profile must reject the wrong license and language.'
 }
 Test-AsrCase 'missing lock fails with bilingual error' {
  $message='';try{Get-LumaAsrModelSpec -Directory $asrTestsRoot|Out-Null}catch{$message=$_.Exception.Message}
  Assert-Asr ($message -match 'Missing' -and $message -match '/') 'Missing pinned metadata must be explained in Korean and English.'
 }
 Test-AsrCase 'Japanese profile reads source and packaged metadata' {
  foreach($packaged in @($false,$true)){$fixture=New-AsrFixture ('metadata-'+$packaged) -Packaged:$packaged;$spec=Get-LumaAsrModelSpec $fixture.Directory;Assert-Asr ($spec.file -eq 'ggml-owned-japanese.bin') 'The Japanese model was not selected.'}
 }
 Test-AsrCase 'missing Japanese profile cannot fall back to Turbo' {
  $fixture=New-AsrFixture 'missing-profile';$fixture.Manifest.PSObject.Properties.Remove('japaneseModel');Save-AsrFixture $fixture
  $failed=$false;try{Get-LumaAsrModelSpec $fixture.Directory|Out-Null}catch{$failed=$true};Assert-Asr $failed 'An absent requested profile must fail.'
 }
 Test-AsrCase 'invalid pins and external URLs fail before download' {
  $invalid=@(@('file','../escape.bin'),@('revision','main'),@('sha256','0'),@('bytes',0),@('bytes',2147483649),@('url','http://huggingface.co/model'),@('url','https://example.invalid/model'),@('url','https://huggingface.co/model?x=1'),@('licenseFile','../LICENSE'),@('licenseSha256','bad'),@('language','en'),@('optionalAtRuntime',$false))
  $index=0;foreach($change in $invalid){$fixture=New-AsrFixture ('invalid-'+$index);$index++;$fixture.Manifest.japaneseModel.($change[0])=$change[1];Save-AsrFixture $fixture;$failed=$false;try{Get-LumaAsrModelSpec $fixture.Directory|Out-Null}catch{$failed=$true};Assert-Asr $failed ('Invalid metadata accepted: '+$change[0])}
 }
 Test-AsrCase 'missing and modified raw license both fail' {
  $fixture=New-AsrFixture 'license';[IO.File]::WriteAllText($fixture.License,'Modified license');$failed=$false;try{Get-LumaAsrModelSpec $fixture.Directory|Out-Null}catch{$failed=$true};Assert-Asr $failed 'Changed license accepted.'
  Remove-Item -LiteralPath $fixture.License;$failed=$false;try{Get-LumaAsrModelSpec $fixture.Directory|Out-Null}catch{$failed=$true};Assert-Asr $failed 'Missing license accepted.'
 }
 Test-AsrCase 'verified download publishes to runtime models' {
  $fixture=New-AsrFixture 'publish';$script:asrTransportMode='valid';$script:asrTransportCalls=0;$result=Install-LumaAsrModel $fixture.Directory
  Assert-Asr (!$result.AlreadyInstalled -and $script:asrTransportCalls -eq 1) 'A new model must use one bounded download.'
  Assert-Asr ((Get-FileHash $fixture.Destination -Algorithm SHA256).Hash.ToLowerInvariant() -eq $fixture.Manifest.japaneseModel.sha256) 'Published model hash differs.'
 }
 Test-AsrCase 'corrupt truncated and failed transfers preserve current file' {
  foreach($mode in @('corrupt','short','fail')){$fixture=New-AsrFixture ('preserve-'+$mode);New-Item -ItemType Directory -Path (Split-Path $fixture.Destination) -Force|Out-Null;[IO.File]::WriteAllText($fixture.Destination,'Keep old bytes');$script:asrTransportMode=$mode;$script:asrTransportCalls=0;$failed=$false;try{Install-LumaAsrModel $fixture.Directory|Out-Null}catch{$failed=$true};Assert-Asr ($failed -and $script:asrTransportCalls -eq 1) 'The test must reach the transfer and reject its result.';Assert-Asr ([IO.File]::ReadAllText($fixture.Destination) -eq 'Keep old bytes') 'Unverified bytes replaced the current file.'}
 }
 Test-AsrCase 'valid current and completed temporary files need no network' {
  foreach($cached in @($false,$true)){$fixture=New-AsrFixture ('reuse-'+$cached);New-Item -ItemType Directory -Path (Split-Path $fixture.Destination) -Force|Out-Null;$target=if($cached){$fixture.Destination+'.download'}else{$fixture.Destination};[IO.File]::WriteAllBytes($target,$script:asrPayload);$script:asrTransportMode='fail';$script:asrTransportCalls=0;$result=Install-LumaAsrModel $fixture.Directory;Assert-Asr ($script:asrTransportCalls -eq 0) 'Verified existing bytes must not download.';Assert-Asr (Test-Path -LiteralPath $fixture.Destination) 'Verified model was not installed.';Assert-Asr ($result.AlreadyInstalled -eq !$cached) 'Existing/current cache result is wrong.'}
 }
 Test-AsrCase 'concurrent installer cannot overwrite model' {
  $fixture=New-AsrFixture 'concurrent';New-Item -ItemType Directory -Path (Split-Path $fixture.Destination) -Force|Out-Null;$guard=[IO.File]::Open(($fixture.Destination+'.install.lock'),[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
  try{$failed=$false;try{Install-LumaAsrModel $fixture.Directory|Out-Null}catch{$failed=$true};Assert-Asr $failed 'A second installer ignored the existing exclusive lock.'}finally{$guard.Dispose()}
 }
 Test-AsrCase 'packaged default works in Windows PowerShell 5.1' {
  $fixture=New-AsrFixture 'packaged 日本語 한글' -Packaged;New-Item -ItemType Directory -Path (Split-Path $fixture.Destination) -Force|Out-Null;[IO.File]::WriteAllBytes($fixture.Destination,$script:asrPayload)
  $tools=Join-Path $fixture.Directory 'tools';New-Item -ItemType Directory -Path $tools -Force|Out-Null
  foreach($name in @('install-asr-model.ps1','install-translation.ps1')){Copy-Item -LiteralPath (Join-Path $asrProject ('scripts/'+$name)) -Destination $tools}
  $ps=Join-Path $env:SystemRoot 'System32/WindowsPowerShell/v1.0/powershell.exe';$copied=Join-Path $tools 'install-asr-model.ps1'
  & $ps -NoProfile -ExecutionPolicy Bypass -File $copied | Out-Host;Assert-Asr ($LASTEXITCODE -eq 0) 'Packaged default failed in Windows PowerShell 5.1.'
 }
 Test-AsrCase 'explicit player directory survives shared helper defaults' {
  $fixture=New-AsrFixture 'explicit target 日本語';New-Item -ItemType Directory -Path (Split-Path $fixture.Destination) -Force|Out-Null;[IO.File]::WriteAllBytes($fixture.Destination,$script:asrPayload)
  $tools=Join-Path $asrTestsRoot 'separate tools';New-Item -ItemType Directory -Path $tools -Force|Out-Null
  foreach($name in @('install-asr-model.ps1','install-translation.ps1')){Copy-Item -LiteralPath (Join-Path $asrProject ('scripts/'+$name)) -Destination $tools}
  $ps=Join-Path $env:SystemRoot 'System32/WindowsPowerShell/v1.0/powershell.exe'
  & $ps -NoProfile -ExecutionPolicy Bypass -File (Join-Path $tools 'install-asr-model.ps1') -PlayerDirectory $fixture.Directory -Profile japanese | Out-Host
  Assert-Asr ($LASTEXITCODE -eq 0) 'The explicit destination/profile was replaced by shared script defaults.'
  Assert-Asr (!(Test-Path -LiteralPath (Join-Path $asrTestsRoot 'runtime/models'))) 'The installer wrote into the script parent instead of the explicit player folder.'
  $failed=$false;try{Get-LumaAsrModelSpec $fixture.Directory -Profile invalid|Out-Null}catch{$failed=$_.Exception.Message -match 'Unsupported'};Assert-Asr $failed 'An unknown profile silently selected Japanese.'
 }
 if(!$ExistingModel){$ExistingModel=Join-Path $asrProject '.deps/models/ggml-kotoba-whisper-v2.0-q5_0.bin'}
 if((Test-Path -LiteralPath $ExistingModel) -and (Get-Content (Join-Path $asrProject 'dependencies.lock.json') -Raw | ConvertFrom-Json).japaneseModel){
  Test-AsrCase 'actual pinned Japanese model is reused unchanged' {
   $directory=Join-Path $asrTestsRoot 'real reuse';New-Item -ItemType Directory -Path $directory -Force|Out-Null;Copy-Item -LiteralPath (Join-Path $asrProject 'dependencies.lock.json') -Destination $directory;Copy-Item -LiteralPath (Join-Path $asrProject 'LICENSES') -Destination $directory -Recurse
   $spec=Get-LumaAsrModelSpec $directory;$models=Join-Path $directory 'runtime/models';New-Item -ItemType Directory -Path $models -Force|Out-Null;$target=Join-Path $models $spec.file;New-Item -ItemType HardLink -Path $target -Target ([IO.Path]::GetFullPath($ExistingModel))|Out-Null
   $before=(Get-Item $target).LastWriteTimeUtc;$script:asrTransportCalls=0;$result=Install-LumaAsrModel $directory
   Assert-Asr ($result.AlreadyInstalled -and $result.BytesTransferred -eq 0 -and $script:asrTransportCalls -eq 0) 'Real pinned model was not reused.';Assert-Asr ((Get-Item $target).LastWriteTimeUtc -eq $before) 'Valid model was rewritten.'
  }
 }
}finally{
 $checked=[IO.Path]::GetFullPath($asrTestsRoot)
 if(!$checked.StartsWith($asrTestsParent+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){throw 'Unsafe ASR test cleanup path'}
 Remove-Item -LiteralPath $checked -Recurse -Force
}
if($script:asrFailures){throw "$script:asrFailures ASR model installer test(s) failed."}
