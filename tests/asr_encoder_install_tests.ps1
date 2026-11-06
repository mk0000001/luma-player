$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '../scripts/install-asr-encoder.ps1')
$encoderTestRoot=Join-Path ([IO.Path]::GetTempPath()) ('luma-encoder-install-tests-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $encoderTestRoot | Out-Null
$stem=Join-Path $encoderTestRoot 'encoder'
$source=[pscustomobject]@{sha256='ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad';bytes=3}
$info=[ordered]@{format='luma-whisper-encoder-v1';sourceModel='kotoba-whisper-v2.0-q5_0';sourceModelSha256=$source.sha256;sourceModelBytes=3;sourceQuantization='Q5_0';weightsPrecision='FP16';inputName='mel';inputShape=@(1,128,3000);outputName='output_features';outputShape=@(1,1500,1280);encoderLayers=32;decoderLayers=2;heads=20;mels=128;state=1280;audioContext=1500;sourceTensorCount=487;geluApproximation='TANH';whisperSource='b5454';encoderXmlBytes=3;encoderBinBytes=3;encoderXmlSha256=$source.sha256;encoderBinSha256='cb8379ac2098aa165029e3938a51da0bcecfc008fd6795f401178647f96c5b34'}
$utf8=[Text.UTF8Encoding]::new($false)
$info.openvinoBuildNumber='fixture-runtime'
function Write-EncoderFixture {
 [IO.File]::WriteAllText(($stem+'.xml'),'abc',$utf8);[IO.File]::WriteAllText(($stem+'.bin'),'def',$utf8)
 [IO.File]::WriteAllText(($stem+'.json'),($info | ConvertTo-Json -Depth 4),$utf8)
}
function Expect-EncoderIntegrity([bool]$Expected,[string]$Case){
 if((Test-LumaEncoderFiles -Stem $stem -Source $source) -ne $Expected){throw "FAIL $Case"};Write-Output "PASS $Case"
}
function Assert-EncoderCase([bool]$Condition,[string]$Case){
 if(!$Condition){throw "FAIL $Case"};Write-Output "PASS $Case"
}
function New-CalibrationWorkerFixture {
 param([string]$Directory)
 New-Item -ItemType Directory -Path $Directory -Force | Out-Null
 $program=Join-Path $Directory 'luma-whisper-worker.exe';$code=Join-Path $Directory 'fixture.cs'
 $fixtureSource=@'
using System;
using System.IO;
using System.Threading;
class CalibrationWorkerFixture {
 static int Main(string[] args) {
  string mode=Environment.GetEnvironmentVariable("LUMA_ENCODER_FIXTURE") ?? "fast";
  if(Array.IndexOf(args,"--verify")>=0) {Console.WriteLine("LUMA_ENCODER_VERIFIED=fixture");return 0;}
  if(Array.IndexOf(args,"--device")<0 || Array.IndexOf(args,"NPU")<0) return 81;
  Console.WriteLine("LUMA_READY"); Console.Out.Flush();
  if(Array.IndexOf(args,"--server")<0) return 0;
  string line; int index=0;
  while((line=Console.ReadLine())!=null) {
   string[] fields=line.Split('\t'); if(fields.Length!=4 || !File.Exists(fields[0])) return 82;
   if(mode=="crash" && index==1) return 22;
   if(mode=="timeout") Thread.Sleep(10000);
   if(mode=="slow-wall" && index==1) Thread.Sleep(3101);
   if(mode=="chatty") for(int j=0;j<100;++j) {Console.WriteLine("LUMA_STATUS=busy");Console.Out.Flush();Thread.Sleep(20);}
   string ms=mode=="slow" ? "3001" : mode=="nan" ? "NaN" : "1200";
   File.WriteAllText(fields[1]+".srt","");
   Console.WriteLine("LUMA_AUDIO_MS=16000");
   Console.WriteLine("LUMA_INFERENCE_MS="+ms);
   Console.WriteLine("LUMA_ENCODER_MS=1000");
   Console.WriteLine("LUMA_ENCODER_CALLS="+(mode=="zero" ? "0" : "1"));
   Console.WriteLine("LUMA_BACKEND="+(mode=="wrong-device" ? "GPU" : "NPU"));
   Console.WriteLine("LUMA_DEVICE="+(mode=="wrong-device" ? "GPU.0" : "NPU"));
   Console.WriteLine("LUMA_DONE="+fields[1]);Console.Out.Flush();++index;
  }
  return 0;
 }
}
'@
 [IO.File]::WriteAllText($code,$fixtureSource,$utf8)
 $compiler=Join-Path ([Environment]::GetFolderPath([Environment+SpecialFolder]::Windows)) 'Microsoft.NET/Framework64/v4.0.30319/csc.exe'
 & $compiler /nologo /target:exe ("/out:"+$program) $code
 if($LASTEXITCODE){throw 'Cannot compile the small calibration process fixture.'}
 foreach($name in @('whisper.dll','ggml.dll','ggml-base.dll','ggml-cpu-x64.dll','openvino.dll','openvino_intel_npu_plugin.dll','openvino_ir_frontend.dll','tbb12.dll')){
  [IO.File]::WriteAllText((Join-Path $Directory $name),'abc',$utf8)
 }
 return $program
}
try{
 Write-EncoderFixture;Expect-EncoderIntegrity $true 'valid metadata and actual independent SHA-256 fixtures'
 $info.openvinoBuildNumber='';Write-EncoderFixture;Expect-EncoderIntegrity $false 'missing required OpenVINO metadata'
 $info.openvinoBuildNumber='fixture-runtime';Write-EncoderFixture
 [IO.File]::WriteAllText(($stem+'.bin'),'xyz',$utf8);Expect-EncoderIntegrity $false 'same-length BIN corruption'
 Write-EncoderFixture;[IO.File]::WriteAllText(($stem+'.xml'),'abz',$utf8);Expect-EncoderIntegrity $false 'same-length XML corruption'
 Write-EncoderFixture;$info.sourceModelSha256='0'*64;[IO.File]::WriteAllText(($stem+'.json'),($info | ConvertTo-Json -Depth 4),$utf8);Expect-EncoderIntegrity $false 'other source model identity'
 $info.sourceModelSha256=$source.sha256;Write-EncoderFixture;$info.inputShape=@(1,80,3000);[IO.File]::WriteAllText(($stem+'.json'),($info | ConvertTo-Json -Depth 4),$utf8);Expect-EncoderIntegrity $false 'incompatible mel shape'
 $info.inputShape=@(1,128,3000);Write-EncoderFixture;[IO.File]::Delete($stem+'.json');Expect-EncoderIntegrity $false 'partial pair has no published metadata'
 Write-EncoderFixture;[IO.File]::WriteAllText(($stem+'.json'),(' '*65537),$utf8);Expect-EncoderIntegrity $false 'bounded metadata read'
 Write-EncoderFixture
 $waveFixture=Join-Path $encoderTestRoot 'owned-check.wav';New-LumaNpuCalibrationWave -Path $waveFixture
 $waveBytes=[IO.File]::ReadAllBytes($waveFixture)
 Assert-EncoderCase ($waveBytes.Length -eq 512044 -and [Text.Encoding]::ASCII.GetString($waveBytes,0,4) -ceq 'RIFF' -and [BitConverter]::ToInt16($waveBytes,20) -eq 1 -and [BitConverter]::ToInt16($waveBytes,22) -eq 1 -and [BitConverter]::ToInt32($waveBytes,24) -eq 16000 -and [BitConverter]::ToInt16($waveBytes,34) -eq 16 -and [BitConverter]::ToInt32($waveBytes,40) -eq 512000) 'owned calibration WAV is sixteen seconds of mono PCM16 at 16 kHz'
 Assert-EncoderCase (@($waveBytes[44..107] | Where-Object {$_ -ne 0}).Count -gt 0) 'owned calibration WAV contains a non-silent signal'
 $modelPath=Join-Path $encoderTestRoot 'model.bin';[IO.File]::WriteAllText($modelPath,'abc',$utf8)
 $fixtureWorker=New-CalibrationWorkerFixture (Join-Path $encoderTestRoot 'calibration/ai')
 # The hardware boundary is replaced; process framing, metrics, profile writes,
 # identity file hashing and publication remain production implementations.
 function Get-LumaNpuDriverIdentity {return [pscustomobject]@{pnpDeviceId='PCI\VEN_8086&DEV_7D1D\FIXTURE';driverKey='{f01a9d53-3ff6-48d2-9f97-c8a7004be10c}\0000';driverVersion='32.0.100.5540';driverDate='9-1-2026';infPath='oem42.inf';matchingDeviceId='pci\ven_8086&dev_7d1d'}}
 function Get-LumaNpuCacheDirectory {return (Join-Path $encoderTestRoot 'calibration/cache')}
 $env:LUMA_ENCODER_FIXTURE='fast'
 $calibration=Initialize-LumaNpuEncoder -Worker $fixtureWorker -Model $modelPath -Encoder ($stem+'.xml')
 Assert-EncoderCase ($null -ne $calibration -and $calibration.InferenceVerified -and $calibration.NpuRecommended) 'four genuine fast requests qualify automatic NPU'
 Assert-EncoderCase (Test-Path -LiteralPath ($stem+'.npu-profile.json') -PathType Leaf) 'calibration publishes its local sidecar'
 $profile=Get-Content -LiteralPath ($stem+'.npu-profile.json') -Raw -Encoding UTF8 | ConvertFrom-Json
 Assert-EncoderCase ($profile.runs.Count -eq 4 -and $profile.inferenceVerified -and $profile.eligible -and $profile.warmMaxInferenceMs -eq 1200) 'profile records four actual requests and independent timing values'
 foreach($case in @(@('slow',$true,'too-slow'),@('slow-wall',$true,'too-slow'),@('zero',$false,'invalid-measurement'),@('wrong-device',$false,'invalid-measurement'),@('nan',$false,'invalid-measurement'),@('crash',$false,'worker-failed'),@('timeout',$false,'timeout'),@('chatty',$false,'timeout'))){
  $env:LUMA_ENCODER_FIXTURE=$case[0]
  $limit=if($case[0] -eq 'slow-wall'){6}else{1}
  $result=Initialize-LumaNpuEncoder -Worker $fixtureWorker -Model $modelPath -Encoder ($stem+'.xml') -TimeoutSeconds $limit
  Assert-EncoderCase ($result.InferenceVerified -eq $case[1] -and !$result.NpuRecommended -and $result.NpuProfileReason -eq $case[2]) ("reject "+$case[0]+" while preserving whether actual inference completed")
 }
 $env:LUMA_ENCODER_FIXTURE='fast'
 $old='previous-profile';[IO.File]::WriteAllText(($stem+'.npu-profile.json'),$old,$utf8)
 $lock=[IO.File]::Open(($stem+'.npu-profile.json'),[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
 try{
  $failed=$false;try{$null=Initialize-LumaNpuEncoder -Worker $fixtureWorker -Model $modelPath -Encoder ($stem+'.xml')}catch{$failed=$true}
  Assert-EncoderCase ($failed -and [IO.File]::ReadAllText($stem+'.npu-profile.json') -ceq $old) 'publication failure preserves previous profile byte for byte'
 }finally{$lock.Dispose()}
 Assert-EncoderCase (@(Get-ChildItem -LiteralPath $encoderTestRoot -Filter '.npu-calibration-*' -Directory).Count -eq 0) 'calibration removes only its bounded temporary work directories'
 $installRoot=Join-Path $encoderTestRoot 'installed';$installAi=Join-Path $installRoot 'runtime/ai';$installModels=Join-Path $installRoot 'runtime/models'
 New-Item -ItemType Directory -Path $installAi,$installModels,(Join-Path $installRoot 'LICENSES') -Force | Out-Null
 Get-ChildItem -LiteralPath (Split-Path -Parent $fixtureWorker) -File | Where-Object {$_.Extension -in @('.exe','.dll')} | Copy-Item -Destination $installAi
 Copy-Item -LiteralPath $fixtureWorker -Destination (Join-Path $installAi 'luma-whisper-encoder.exe')
 [IO.File]::WriteAllText((Join-Path $installModels 'ggml-fixture.bin'),'abc',$utf8)
 [IO.File]::WriteAllText((Join-Path $installRoot 'LICENSES/fixture.txt'),'abc',$utf8)
 $pin=[ordered]@{file='ggml-fixture.bin';id='fixture/whisper';revision=('1'*40);sha256=$source.sha256;bytes=3;language='ja';optionalAtRuntime=$true;url=('https://huggingface.co/fixture/whisper/resolve/'+('1'*40)+'/ggml-fixture.bin');license='Apache-2.0';licenseFile='fixture.txt';licenseSha256=$source.sha256}
 [IO.File]::WriteAllText((Join-Path $installRoot 'dependencies.lock.json'),(@{japaneseModel=$pin}|ConvertTo-Json -Depth 4),$utf8)
 $installStem=Join-Path $installModels 'ggml-fixture-encoder-openvino'
 [IO.File]::WriteAllText(($installStem+'.xml'),'abc',$utf8);[IO.File]::WriteAllText(($installStem+'.bin'),'def',$utf8)
 [IO.File]::WriteAllText(($installStem+'.json'),($info|ConvertTo-Json -Depth 4),$utf8)
 $install=Install-LumaAsrEncoder -Directory $installRoot -PrepareNpu
 Assert-EncoderCase (@($install).Count -eq 1 -and $install.AlreadyInstalled -eq $true -and $install.NpuPrepared -eq $true -and $install.InferenceVerified -eq $true -and $install.NpuRecommended -eq $true) 'installer returns one measured qualification result instead of a requested preparation flag'
 Remove-Item -LiteralPath (Join-Path (Split-Path -Parent $fixtureWorker) 'tbb12.dll')
 $incomplete=Initialize-LumaNpuEncoder -Worker $fixtureWorker -Model $modelPath -Encoder ($stem+'.xml')
 Assert-EncoderCase (!$incomplete.NpuPrepared -and !$incomplete.InferenceVerified -and !$incomplete.NpuRecommended -and $incomplete.NpuProfileReason -eq 'runtime-incomplete') 'runtime dependencies outside the worker directory cannot qualify NPU'
}finally{
 Remove-Item Env:LUMA_ENCODER_FIXTURE -ErrorAction SilentlyContinue
 $resolved=[IO.Path]::GetFullPath($encoderTestRoot);$tempPrefix=[IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\','/')+[IO.Path]::DirectorySeparatorChar
 if(!$resolved.StartsWith($tempPrefix,[StringComparison]::OrdinalIgnoreCase) -or [IO.Path]::GetFileName($resolved) -notmatch '^luma-encoder-install-tests-[a-f0-9]{32}$'){throw 'Unsafe encoder fixture cleanup.'}
 Remove-Item -LiteralPath $resolved -Recurse -Force
}
