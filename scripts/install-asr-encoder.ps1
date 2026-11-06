[CmdletBinding()]
param([string]$PlayerDirectory,[switch]$Development,[switch]$VerifyOnly,[switch]$PrepareNpu)
$encoderInstallDirectory=$PlayerDirectory;$encoderInstallDevelopment=$Development;$encoderInstallVerifyOnly=$VerifyOnly
$encoderInstallDirect=$MyInvocation.InvocationName -ne '.'
$encoderInstallPrepareNpu=$PrepareNpu
. (Join-Path $PSScriptRoot 'install-asr-model.ps1')

function Invoke-LumaEncoderTool {
 param([string]$Program,[string[]]$Arguments,[int]$TimeoutSeconds=600)
 $quoted=@($Arguments | ForEach-Object {if($_ -notmatch '[\s"]' -and $_.Length){$_}else{'"'+[regex]::Replace([regex]::Replace($_,'(\\*)"','$1$1\"'),'(\\+)$','$1$1')+'"'}})
 $start=[Diagnostics.ProcessStartInfo]::new();$start.FileName=$Program;$start.Arguments=$quoted -join ' '
 $start.WorkingDirectory=Split-Path -Parent $Program
 $start.UseShellExecute=$false;$start.CreateNoWindow=$true;$start.RedirectStandardInput=$true;$start.RedirectStandardOutput=$true;$start.RedirectStandardError=$true
 $start.StandardOutputEncoding=[Text.UTF8Encoding]::new($false);$start.StandardErrorEncoding=$start.StandardOutputEncoding
 $process=[Diagnostics.Process]::new();$process.StartInfo=$start;$started=$false
 try{
  $started=$process.Start();if(!$started){throw 'Cannot start native encoder tool.'}
  $process.StandardInput.Close();$stdout=$process.StandardOutput.ReadToEndAsync();$stderr=$process.StandardError.ReadToEndAsync()
  $watch=[Diagnostics.Stopwatch]::StartNew()
  while(!$process.WaitForExit(30000)){
   if($watch.Elapsed.TotalSeconds -ge $TimeoutSeconds){throw 'Native encoder preparation timed out.'}
   Write-Host '인코더를 준비하는 중… / Preparing encoder…'
  }
  $text=$stdout.GetAwaiter().GetResult();$errorText=$stderr.GetAwaiter().GetResult()
  if($process.ExitCode -ne 0){throw ('Native encoder tool failed: '+$errorText.Substring([Math]::Max(0,$errorText.Length-2000)))}
  return $text
 }finally{if($started -and !$process.HasExited){$process.Kill();$process.WaitForExit()};$process.Dispose()}
}
function Confirm-LumaEncoderIdentity {
 param([string]$Tool,[string]$Model,[string]$Encoder)
 $proof=Invoke-LumaEncoderTool -Program $Tool -Arguments @('--verify','--model',$Model,'--encoder',$Encoder)
 if($proof -notmatch '(?m)^LUMA_ENCODER_VERIFIED='){throw 'Native encoder identity verification was not acknowledged.'}
}
function Initialize-LumaNpuEncoder {
 param([string]$Worker,[string]$Model,[string]$Encoder)
 # Qt6 CacheLocation for the normal-integrity Windows app, organization and
 # application both LumaPlayer. Driver/runtime-specific blobs stay local.
 $local=[Environment]::GetFolderPath([Environment+SpecialFolder]::LocalApplicationData)
 if(!$local){throw 'Cannot resolve the app-local NPU cache.'}
 $cache=Join-Path $local 'LumaPlayer/LumaPlayer/cache/openvino'
 New-Item -ItemType Directory -Path $cache -Force | Out-Null
 $proof=Invoke-LumaEncoderTool -Program $Worker -Arguments @('--device','NPU','-m',$Model,'--encoder',$Encoder,'--cache',$cache,'--warmup') -TimeoutSeconds 1800
 if($proof -notmatch '(?m)^LUMA_READY\r?$'){throw 'NPU encoder preparation was not acknowledged.'}
 Write-Host 'NPU 컴파일 캐시를 준비했습니다. 실제 추론은 재생 후 확인합니다 / NPU compile cache prepared; actual inference is confirmed during playback.'
}
function Test-LumaEncoderFiles {
 param([string]$Stem,$Source)
 try{
  $metadata=Get-Item -LiteralPath ($Stem+'.json') -ErrorAction Stop
  if($metadata.Length -gt 65536){return $false}
  $info=Get-Content -LiteralPath $metadata.FullName -Raw -Encoding UTF8 | ConvertFrom-Json
  if($info.format -cne 'luma-whisper-encoder-v1' -or $info.sourceModel -cne 'kotoba-whisper-v2.0-q5_0' -or $info.sourceModelSha256 -cne $Source.sha256 -or [long]$info.sourceModelBytes -ne [long]$Source.bytes -or $info.sourceQuantization -cne 'Q5_0' -or $info.weightsPrecision -cne 'FP16'){return $false}
  if($info.inputName -cne 'mel' -or (@($info.inputShape) -join ',') -cne '1,128,3000' -or $info.outputName -cne 'output_features' -or (@($info.outputShape) -join ',') -cne '1,1500,1280'){return $false}
  if($info.encoderLayers -ne 32 -or $info.decoderLayers -ne 2 -or $info.heads -ne 20 -or $info.mels -ne 128 -or $info.state -ne 1280 -or $info.audioContext -ne 1500 -or $info.sourceTensorCount -ne 487 -or $info.geluApproximation -cne 'TANH' -or $info.whisperSource -cne 'b5454'){return $false}
  if([string]::IsNullOrWhiteSpace($info.openvinoBuildNumber)){return $false}
  foreach($kind in @('Xml','Bin')){
   $hash=[string]$info.('encoder'+$kind+'Sha256');[long]$size=$info.('encoder'+$kind+'Bytes')
   if($hash -notmatch '^[a-f0-9]{64}$' -or $size -le 0 -or $size -gt 4GB -or !(Test-LumaTranslationModelFile ($Stem+'.'+$kind.ToLowerInvariant()) $hash $size)){return $false}
  }
  return $true
 }catch{return $false}
}
function Install-LumaAsrEncoder {
 param([string]$Directory,[switch]$Development,[switch]$VerifyOnly,[switch]$PrepareNpu)
 $ErrorActionPreference='Stop';$root=[IO.Path]::GetFullPath($Directory)
 $spec=Get-LumaAsrModelSpec -Directory $root
 $models=Join-Path $root $(if($Development){'.deps/models'}else{'runtime/models'})
 $model=Join-Path $models $spec.file
 if(!(Test-LumaTranslationModelFile $model $spec.sha256 $spec.bytes)){throw '먼저 고정 Kotoba 모델을 설치하세요 / Install the pinned Kotoba model first.'}
 $name=[IO.Path]::GetFileNameWithoutExtension($spec.file)+'-encoder-openvino'
 $stem=Join-Path $models $name;$guard=$null;$stage=$null
 $tool=Join-Path $root $(if($Development){'.deps/build-ai/bin/luma-whisper-encoder.exe'}else{'runtime/ai/luma-whisper-encoder.exe'})
 $worker=Join-Path (Split-Path -Parent $tool) 'luma-whisper-worker.exe'
 if(!(Test-Path -LiteralPath $tool -PathType Leaf)){throw 'C++ 인코더 변환 도구가 없습니다. 최신 패키지를 사용하세요 / Missing native encoder converter; use the latest package.'}
 try{
  $guard=[IO.File]::Open(($stem+'.install.lock'),[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
  if(Test-LumaEncoderFiles -Stem $stem -Source $spec){
   Confirm-LumaEncoderIdentity -Tool $tool -Model $model -Encoder ($stem+'.xml')
   if($PrepareNpu){Initialize-LumaNpuEncoder -Worker $worker -Model $model -Encoder ($stem+'.xml')}
   return [pscustomobject]@{AlreadyInstalled=$true;EncoderPath=$stem+'.xml';NpuPrepared=[bool]$PrepareNpu;InferenceVerified=$false}
  }
  if($VerifyOnly){throw '인코더 정체성/무결성 검증 실패 / Encoder identity/integrity verification failed.'}
  foreach($extension in @('.xml','.bin','.json')){if(Test-Path -LiteralPath ($stem+$extension)){throw '기존 인코더를 보존했습니다. 새 폴더에서 변환하세요 / Existing encoder preserved; use a fresh model folder.'}}
  Assert-LumaTranslationDiskSpace -Directory $models -RequiredBytes 2GB
  $stage=Join-Path $models ('.encoder-stage-'+[guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Path $stage | Out-Null
  $temporary=Join-Path $stage $name
  $null=Invoke-LumaEncoderTool -Program $tool -Arguments @('--model',$model,'--output',($temporary+'.xml'))
  if(!(Test-LumaEncoderFiles -Stem $temporary -Source $spec)){throw '변환 결과 무결성 검사 실패 / Converted artifact integrity check failed.'}
  Confirm-LumaEncoderIdentity -Tool $tool -Model $model -Encoder ($temporary+'.xml')
  # Publish the metadata last. The player cannot select a partial Kotoba pair.
  $published=[Collections.Generic.List[string]]::new()
  try{
   foreach($extension in @('.bin','.xml','.json')){[IO.File]::Move(($temporary+$extension),($stem+$extension));$published.Add($stem+$extension)}
  }catch{foreach($path in $published){[IO.File]::Delete($path)};throw}
  if($PrepareNpu){Initialize-LumaNpuEncoder -Worker $worker -Model $model -Encoder ($stem+'.xml')}
  return [pscustomobject]@{AlreadyInstalled=$false;EncoderPath=$stem+'.xml';NpuPrepared=[bool]$PrepareNpu;InferenceVerified=$false}
 }finally{
  if($stage -and (Test-Path -LiteralPath $stage)){
   $resolved=[IO.Path]::GetFullPath($stage);$parent=[IO.Path]::GetFullPath($models).TrimEnd('\')+'\'
   if(!$resolved.StartsWith($parent,[StringComparison]::OrdinalIgnoreCase) -or [IO.Path]::GetFileName($resolved) -notmatch '^\.encoder-stage-[a-f0-9]{32}$'){throw 'Unsafe encoder staging cleanup.'}
   Remove-Item -LiteralPath $resolved -Recurse -Force
  }
  if($guard){$guard.Dispose()}
 }
}
if($encoderInstallDirect){
 if([string]::IsNullOrWhiteSpace($encoderInstallDirectory)){$encoderInstallDirectory=Split-Path -Parent $PSScriptRoot}
 $result=Install-LumaAsrEncoder -Directory $encoderInstallDirectory -Development:$encoderInstallDevelopment -VerifyOnly:$encoderInstallVerifyOnly -PrepareNpu:$encoderInstallPrepareNpu
 Write-Output 'Kotoba 인코더 파일을 검증했습니다. NPU 사용 여부는 실제 추론 후 확인합니다.'
 Write-Output 'Kotoba encoder files are verified. NPU execution is confirmed only after actual inference.'
 Write-Output $result.EncoderPath
}
