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
function Get-LumaNpuCacheDirectory {
 # Qt6 CacheLocation for the normal-integrity Windows app, organization and
 # application both LumaPlayer. Driver/runtime-specific blobs stay local.
 $local=[Environment]::GetFolderPath([Environment+SpecialFolder]::LocalApplicationData)
 if(!$local){throw 'Cannot resolve the app-local NPU cache.'}
 return (Join-Path $local 'LumaPlayer/LumaPlayer/cache/openvino')
}
function Get-LumaNpuDriverIdentity {
 $devices=@(Get-CimInstance Win32_PnPSignedDriver -ErrorAction Stop | Where-Object {
  $_.DeviceClass -eq 'ComputeAccelerator' -or $_.DeviceName -match 'Intel.*AI Boost|Intel.*NPU'
 })
 if($devices.Count -ne 1){throw 'A unique local NPU driver identity is unavailable.'}
 $pnp=[string]$devices[0].DeviceID
 if($pnp.Length -gt 512 -or $pnp -notmatch '^PCI\\[A-Za-z0-9_&-]+\\[A-Za-z0-9_&-]+$'){throw 'Invalid NPU PnP identity.'}
 $enum=Get-ItemProperty -LiteralPath ('Registry::HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Enum\'+$pnp) -ErrorAction Stop
 $key=[string]$enum.Driver
 if($key -notmatch '^\{[a-fA-F0-9]{8}-[a-fA-F0-9]{4}-[a-fA-F0-9]{4}-[a-fA-F0-9]{4}-[a-fA-F0-9]{12}\}\\[0-9]{4}$'){throw 'Invalid NPU driver mapping.'}
 $driver=Get-ItemProperty -LiteralPath ('Registry::HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Control\Class\'+$key) -ErrorAction Stop
 return [pscustomobject]@{pnpDeviceId=$pnp;driverKey=$key;driverVersion=[string]$driver.DriverVersion;driverDate=[string]$driver.DriverDate;infPath=[string]$driver.InfPath;matchingDeviceId=[string]$driver.MatchingDeviceId}
}
function New-LumaNpuCalibrationWave {
 param([string]$Path)
 # An owned deterministic signal exercises the fixed-shape encoder. It is not
 # speech and cannot measure transcription accuracy or real dialogue cost.
 $stream=[IO.File]::Open($Path,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
 $writer=[IO.BinaryWriter]::new($stream)
 try{
  $samples=16000*16;$bytes=$samples*2
  $writer.Write([Text.Encoding]::ASCII.GetBytes('RIFF'));$writer.Write([int](36+$bytes));$writer.Write([Text.Encoding]::ASCII.GetBytes('WAVEfmt '))
  $writer.Write([int]16);$writer.Write([int16]1);$writer.Write([int16]1);$writer.Write([int]16000);$writer.Write([int]32000);$writer.Write([int16]2);$writer.Write([int16]16)
  $writer.Write([Text.Encoding]::ASCII.GetBytes('data'));$writer.Write([int]$bytes)
  for($i=0;$i -lt $samples;$i++){
   $envelope=if(($i%16000) -lt 12000){1.0}else{0.25}
   $sample=[int16][Math]::Round($envelope*(5000*[Math]::Sin(2*[Math]::PI*223*$i/16000)+2000*[Math]::Sin(2*[Math]::PI*997*$i/16000)))
   $writer.Write($sample)
  }
 }finally{$writer.Dispose();$stream.Dispose()}
}
function Get-LumaNpuFileIdentity {
 param([string]$Path,[string]$RelativePath)
 $file=Get-Item -LiteralPath $Path -ErrorAction Stop
 if($file.PSIsContainer -or ($file.Attributes -band [IO.FileAttributes]::ReparsePoint) -or $file.Length -le 0){throw 'A calibration dependency is not a regular non-empty file.'}
 return [pscustomobject]@{relativePath=$RelativePath;sha256=(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant();bytes=[long]$file.Length;mtimeMs=([DateTimeOffset]$file.LastWriteTimeUtc).ToUnixTimeMilliseconds()}
}
function Get-LumaNpuRuntimeIdentity {
 param([string]$Directory)
 $files=@(Get-ChildItem -LiteralPath $Directory -File | Where-Object { $_.Name -match '^(?:whisper|ggml|ggml-base|ggml-cpu-[a-zA-Z0-9_-]+|openvino|openvino_ir_frontend|openvino_intel_npu_[a-zA-Z0-9_-]+|tbb12|tbbmalloc|tbbmalloc_proxy)\.dll$' } | Sort-Object Name)
 foreach($name in @('whisper.dll','ggml.dll','ggml-base.dll','openvino.dll','openvino_ir_frontend.dll','openvino_intel_npu_plugin.dll','tbb12.dll')){
  if($files.Name -notcontains $name){throw 'NPU runtime dependencies must reside beside the worker.'}
 }
 if(!(@($files | Where-Object {$_.Name -match '^ggml-cpu-'}).Count)){throw 'Missing native CPU decoder dependency.'}
 return @($files | ForEach-Object {Get-LumaNpuFileIdentity -Path $_.FullName -RelativePath $_.Name})
}
function Write-LumaNpuProfile {
 param([string]$Path,$Profile)
 $destination=[IO.Path]::GetFullPath($Path);$temporary=$destination+'.tmp-'+[guid]::NewGuid().ToString('N')
 $json=$Profile | ConvertTo-Json -Depth 8
 $encoding=[Text.UTF8Encoding]::new($false)
 if($encoding.GetByteCount($json) -gt 65536){throw 'NPU calibration profile exceeds the bounded format.'}
 try{
  [IO.File]::WriteAllText($temporary,$json,$encoding)
  if([IO.File]::Exists($destination)){[IO.File]::Replace($temporary,$destination,[System.Management.Automation.Language.NullString]::Value)}else{[IO.File]::Move($temporary,$destination)}
 }finally{if([IO.File]::Exists($temporary)){[IO.File]::Delete($temporary)}}
}
function Initialize-LumaNpuEncoder {
 param([string]$Worker,[string]$Model,[string]$Encoder,[ValidateRange(1,60)][int]$TimeoutSeconds=60)
 $profilePath=[IO.Path]::ChangeExtension($Encoder,'.npu-profile.json');$measurement=$null;$prepared=$false;$verified=$false;$eligible=$false;$reason='preparation-failed'
 $stem=Join-Path ([IO.Path]::GetDirectoryName($Encoder)) ([IO.Path]::GetFileNameWithoutExtension($Encoder))
 $manifest=Get-Content -LiteralPath ($stem+'.json') -Raw -Encoding UTF8 | ConvertFrom-Json
 $sourceFile=Get-Item -LiteralPath $Model -ErrorAction Stop;$xml=Get-Item -LiteralPath $Encoder -ErrorAction Stop;$bin=Get-Item -LiteralPath ($stem+'.bin') -ErrorAction Stop
 $workerIdentity=Get-LumaNpuFileIdentity -Path $Worker -RelativePath ([IO.Path]::GetFileName($Worker))
 $runtime=@();$device=$null;$runs=@();$readyMs=0.0;$firstMs=0.0;$warmMax=0.0;$wallMax=0.0
 $work=Join-Path $xml.DirectoryName ('.npu-calibration-'+[guid]::NewGuid().ToString('N'))
 try{
  try{$runtime=@(Get-LumaNpuRuntimeIdentity -Directory (Split-Path -Parent $Worker));$device=Get-LumaNpuDriverIdentity}catch{$reason='runtime-incomplete';throw}
  $cache=Get-LumaNpuCacheDirectory;New-Item -ItemType Directory -Path $cache -Force | Out-Null
  $proof=Invoke-LumaEncoderTool -Program $Worker -Arguments @('--device','NPU','-m',$Model,'--encoder',$Encoder,'--cache',$cache,'--warmup') -TimeoutSeconds 1800
  if($proof -notmatch '(?m)^LUMA_READY\r?$'){throw 'NPU cache preparation was not acknowledged.'}
  $prepared=$true
  New-Item -ItemType Directory -Path $work | Out-Null
  $wave=Join-Path $work 'owned-signal.wav';New-LumaNpuCalibrationWave -Path $wave
  $reason='worker-failed'
  $json=& (Join-Path $PSScriptRoot 'measure-asr.ps1') -Worker $Worker -Model $Model -Wave $wave -OutputDirectory (Join-Path $work 'measurement') -Device NPU -Encoder $Encoder -Language ja -Threads 6 -Requests 4 -CacheDirectory $cache -ReadyTimeoutSeconds 60 -TimeoutSeconds $TimeoutSeconds
  $measurement=$json | ConvertFrom-Json;$runs=@($measurement.runs);$readyMs=[double]$measurement.readyMs
  $reason='invalid-measurement'
  if($runs.Count -ne 4 -or [double]::IsNaN($readyMs) -or [double]::IsInfinity($readyMs) -or $readyMs -le 0 -or $readyMs -gt 60000){throw 'Invalid calibration readiness or request count.'}
  for($i=0;$i -lt 4;$i++){
   $run=$runs[$i]
   if($run.index -ne $i -or $run.backend -cne 'NPU' -or $run.device -cnotmatch '^NPU(?:\.[0-9]+)?$' -or $run.encoderCalls -le 0 -or $run.audioMs -ne 16000){throw 'Invalid calibration inference proof.'}
   foreach($field in @('requestWallMs','encoderTotalMs','inferenceMs')){$number=[double]$run.$field;if([double]::IsNaN($number) -or [double]::IsInfinity($number) -or $number -le 0){throw 'Invalid calibration inference timing.'}}
  }
  $verified=$true;$firstMs=[double]$runs[0].inferenceMs
  $warmMax=[double](($runs | Select-Object -Skip 1 | Measure-Object inferenceMs -Maximum).Maximum)
  $wallMax=[double](($runs | Select-Object -Skip 1 | Measure-Object requestWallMs -Maximum).Maximum)
  $eligible=$warmMax -le 3000 -and $wallMax -le 3000;$reason=if($eligible){'qualified'}else{'too-slow'}
 }catch{
  if($_.Exception.Message -match 'timed out|timeout'){$reason='timeout'}
  elseif($_.Exception.Message -match 'per-request metric|Non-finite|Non-positive|actual backend/device markers|subtitle file'){$reason='invalid-measurement'}
 }finally{
  if(Test-Path -LiteralPath $work){
   $resolved=[IO.Path]::GetFullPath($work);$parent=[IO.Path]::GetFullPath($xml.DirectoryName).TrimEnd('\','/')+[IO.Path]::DirectorySeparatorChar
   if(!$resolved.StartsWith($parent,[StringComparison]::OrdinalIgnoreCase) -or [IO.Path]::GetFileName($resolved) -notmatch '^\.npu-calibration-[a-f0-9]{32}$' -or ((Get-Item -LiteralPath $resolved).Attributes -band [IO.FileAttributes]::ReparsePoint)){throw 'Unsafe NPU calibration cleanup.'}
   Remove-Item -LiteralPath $resolved -Recurse -Force
  }
 }
 $profile=[ordered]@{format='luma-npu-profile-v1';policy='synthetic-16s-4requests-maxwarm3000-v1';eligible=[bool]$eligible;inferenceVerified=[bool]$verified;reason=$reason;testedAtUtc=[DateTime]::UtcNow.ToString('o');stimulus='deterministic-pcm-v1';audioMs=16000;requestCount=4;warmRequestCount=3;readyMs=$readyMs;warmMaxInferenceMs=$warmMax;warmMaxWallMs=$wallMax;firstInferenceMs=$firstMs;workerThreads=6;language='ja';source=[ordered]@{sha256=$manifest.sourceModelSha256;bytes=[long]$sourceFile.Length;mtimeMs=([DateTimeOffset]$sourceFile.LastWriteTimeUtc).ToUnixTimeMilliseconds()};encoder=[ordered]@{xmlSha256=$manifest.encoderXmlSha256;xmlBytes=[long]$xml.Length;xmlMtimeMs=([DateTimeOffset]$xml.LastWriteTimeUtc).ToUnixTimeMilliseconds();binSha256=$manifest.encoderBinSha256;binBytes=[long]$bin.Length;binMtimeMs=([DateTimeOffset]$bin.LastWriteTimeUtc).ToUnixTimeMilliseconds()};worker=$workerIdentity;runtimeFiles=@($runtime);device=$device;runs=@($runs)}
 Write-LumaNpuProfile -Path $profilePath -Profile $profile
 Write-Host $(if($eligible){'NPU 반복 추론과 준비 시간 검사를 통과했습니다 / NPU repeated inference and timing qualification passed.'}else{'자동 음성 인식은 GPU/CPU를 사용합니다 / Automatic ASR will use GPU/CPU: '+$reason})
 Write-Host '합성 입력 시험은 실제 음성 정확도나 대화 처리 시간을 보장하지 않습니다 / Synthetic input does not measure speech accuracy or guarantee dialogue latency.'
 return [pscustomobject]@{NpuPrepared=$prepared;InferenceVerified=$verified;NpuRecommended=$eligible;NpuProfilePath=$profilePath;NpuProfileReason=$reason}
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
   $calibration=$null;if($PrepareNpu){$calibration=Initialize-LumaNpuEncoder -Worker $worker -Model $model -Encoder ($stem+'.xml')}
   return (New-LumaEncoderInstallResult -AlreadyInstalled $true -EncoderPath ($stem+'.xml') -Calibration $calibration)
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
  $calibration=$null;if($PrepareNpu){$calibration=Initialize-LumaNpuEncoder -Worker $worker -Model $model -Encoder ($stem+'.xml')}
  return (New-LumaEncoderInstallResult -AlreadyInstalled $false -EncoderPath ($stem+'.xml') -Calibration $calibration)
 }finally{
  if($stage -and (Test-Path -LiteralPath $stage)){
   $resolved=[IO.Path]::GetFullPath($stage);$parent=[IO.Path]::GetFullPath($models).TrimEnd('\')+'\'
   if(!$resolved.StartsWith($parent,[StringComparison]::OrdinalIgnoreCase) -or [IO.Path]::GetFileName($resolved) -notmatch '^\.encoder-stage-[a-f0-9]{32}$'){throw 'Unsafe encoder staging cleanup.'}
   Remove-Item -LiteralPath $resolved -Recurse -Force
  }
  if($guard){$guard.Dispose()}
 }
}
function New-LumaEncoderInstallResult {
 param([bool]$AlreadyInstalled,[string]$EncoderPath,$Calibration)
 return [pscustomobject]@{AlreadyInstalled=$AlreadyInstalled;EncoderPath=$EncoderPath;NpuPrepared=($null -ne $Calibration -and $Calibration.NpuPrepared);InferenceVerified=($null -ne $Calibration -and $Calibration.InferenceVerified);NpuRecommended=($null -ne $Calibration -and $Calibration.NpuRecommended);NpuProfilePath=$(if($Calibration){$Calibration.NpuProfilePath}else{[IO.Path]::ChangeExtension($EncoderPath,'.npu-profile.json')});NpuProfileReason=$(if($Calibration){$Calibration.NpuProfileReason}else{'not-calibrated'})}
}
if($encoderInstallDirect){
 if([string]::IsNullOrWhiteSpace($encoderInstallDirectory)){$encoderInstallDirectory=Split-Path -Parent $PSScriptRoot}
 $result=Install-LumaAsrEncoder -Directory $encoderInstallDirectory -Development:$encoderInstallDevelopment -VerifyOnly:$encoderInstallVerifyOnly -PrepareNpu:$encoderInstallPrepareNpu
 Write-Output 'Kotoba 인코더 파일을 검증했습니다. 자동 NPU 선택은 이 기기의 반복 추론 검증 결과를 따릅니다.'
 Write-Output 'Kotoba encoder files are verified. Automatic NPU selection follows this machine''s repeated-inference qualification.'
 Write-Output $result.EncoderPath
}
