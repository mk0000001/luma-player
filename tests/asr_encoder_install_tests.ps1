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
}finally{
 $resolved=[IO.Path]::GetFullPath($encoderTestRoot);$tempPrefix=[IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\','/')+[IO.Path]::DirectorySeparatorChar
 if(!$resolved.StartsWith($tempPrefix,[StringComparison]::OrdinalIgnoreCase) -or [IO.Path]::GetFileName($resolved) -notmatch '^luma-encoder-install-tests-[a-f0-9]{32}$'){throw 'Unsafe encoder fixture cleanup.'}
 Remove-Item -LiteralPath $resolved -Recurse -Force
}
