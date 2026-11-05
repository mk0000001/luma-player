param([Parameter(Mandatory=$true)][string]$Worker,[string]$Model,[string]$OwnedWave,[string]$Language='en',[string]$Device='CUDA0')
$ErrorActionPreference='Stop'
$asrTestRoot=Join-Path (Split-Path -Parent $PSScriptRoot) 'build-translation-devices/asr-tests'
New-Item -ItemType Directory -Path $asrTestRoot -Force | Out-Null
& $Worker --list-devices 1>(Join-Path $asrTestRoot 'devices.json') 2>(Join-Path $asrTestRoot 'devices.stderr.txt')
if($LASTEXITCODE -ne 0){throw 'The ASR worker could not enumerate devices.'}
$devices=(Get-Content -LiteralPath (Join-Path $asrTestRoot 'devices.json') -Raw | ConvertFrom-Json).devices
$cuda=@($devices | Where-Object {$_.id -eq 'CUDA0'})
if($cuda.Count -ne 1 -or $cuda[0].backend -ne 'CUDA'){throw 'CUDA0 must be identified as the CUDA backend, independently of Vulkan.'}
Write-Output 'PASS own worker dynamically loads b5454 CUDA0 and reports CUDA identity'
if($Model -and $OwnedWave){
    $output=Join-Path $asrTestRoot 'owned-result'
    $watch=[Diagnostics.Stopwatch]::StartNew()
    & $Worker --device $Device -m $Model -f $OwnedWave -of $output -l $Language -t 6 1>(Join-Path $asrTestRoot 'inference.stdout.txt') 2>(Join-Path $asrTestRoot 'inference.stderr.txt')
    $watch.Stop()
    if($LASTEXITCODE -ne 0){throw 'Owned-wave CUDA ASR inference failed.'}
    $proof=Get-Content -LiteralPath (Join-Path $asrTestRoot 'inference.stdout.txt') -Raw
    if($proof -notmatch '(?m)^LUMA_ACCELERATOR=CUDA GPU\r?$' -or $proof -notmatch '(?m)^LUMA_DEVICE=CUDA0\r?$' -or $proof -notmatch '(?m)^LUMA_BACKEND=GPU\r?$' -or !(Test-Path -LiteralPath ($output+'.srt'))){throw 'The ASR result lacks actual CUDA device/inference proof.'}
    Write-Output 'PASS completed owned-wave CUDA ASR with actual backend proof and an output file'
    if((Get-Item -LiteralPath ($output+'.srt')).Length -eq 0){throw 'Owned speech produced no subtitle cues.'}
    [pscustomobject]@{requestedDevice=$Device;device='CUDA0';backend='CUDA';elapsedMs=$watch.ElapsedMilliseconds;modelSha256=(Get-FileHash -LiteralPath $Model -Algorithm SHA256).Hash;waveSha256=(Get-FileHash -LiteralPath $OwnedWave -Algorithm SHA256).Hash;language=$Language} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $asrTestRoot 'metrics.json') -Encoding UTF8
    Write-Output ('Owned-wave process elapsed: '+$watch.ElapsedMilliseconds+' ms')
}
