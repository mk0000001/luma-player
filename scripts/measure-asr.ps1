[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$Worker,
    [Parameter(Mandatory=$true)][string]$Model,
    [Parameter(Mandatory=$true)][string]$Wave,
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [string]$Device='NPU', [string]$Encoder, [string]$Language='ja', [string]$CacheDirectory,
    [ValidateSet('auto','fast','accurate')][string]$Quality='auto',
    [ValidateRange(1,16)][int]$Threads=6,
    [ValidateRange(2,20)][int]$Requests=4,
    [ValidateRange(1,900)][int]$TimeoutSeconds=300,
    [ValidateRange(1,60)][int]$ReadyTimeoutSeconds=60
)
# 인식 내용 없이 첫 추론과 이후 추론의 장치·시간·실제 인코더 호출 수를 기록합니다.
# Record device/timing/actual encoder calls, separating first inference from later requests.
$ErrorActionPreference='Stop'
function Quote-NativeArgument([string]$Value){
    if($Value -notmatch '[\s"]' -and $Value.Length){return $Value}
    return '"'+[regex]::Replace([regex]::Replace($Value,'(\\*)"','$1$1\"'),'(\\+)$','$1$1')+'"'
}
foreach($path in @($Worker,$Model,$Wave)){if(!(Test-Path -LiteralPath $path -PathType Leaf)){throw "Missing input: $path"}}
if($Encoder -and !(Test-Path -LiteralPath $Encoder -PathType Leaf)){throw 'Missing encoder XML.'}
$root=[IO.Path]::GetFullPath($OutputDirectory);New-Item -ItemType Directory -Path $root -Force | Out-Null
if(Test-Path -LiteralPath (Join-Path $root 'metrics.json')){throw 'Choose a new output directory; existing measurements are preserved.'}
$arguments=@('--device',$Device,'-m',[IO.Path]::GetFullPath($Model),'--quality',$Quality,'--server','--warmup')
if($Encoder){
    $cache=if($CacheDirectory){[IO.Path]::GetFullPath($CacheDirectory)}else{Join-Path $root 'cache'}
    $arguments+=@('--encoder',[IO.Path]::GetFullPath($Encoder),'--cache',$cache)
}
$start=[Diagnostics.ProcessStartInfo]::new();$start.FileName=[IO.Path]::GetFullPath($Worker)
$start.Arguments=($arguments | ForEach-Object {Quote-NativeArgument $_}) -join ' '
$start.WorkingDirectory=Split-Path -Parent $start.FileName
$start.UseShellExecute=$false;$start.CreateNoWindow=$true
$start.RedirectStandardInput=$true;$start.RedirectStandardOutput=$true;$start.RedirectStandardError=$true
$start.StandardOutputEncoding=[Text.UTF8Encoding]::new($false);$start.StandardErrorEncoding=$start.StandardOutputEncoding
$process=[Diagnostics.Process]::new();$process.StartInfo=$start;$started=$false;$stderrTask=$null
$watch=[Diagnostics.Stopwatch]::StartNew();$stdout=[Collections.Generic.List[string]]::new();$runs=[Collections.Generic.List[object]]::new()
function Read-WorkerLine {
    param([Diagnostics.Stopwatch]$Phase,[int]$LimitSeconds)
    $remaining=[int][Math]::Floor($LimitSeconds*1000-$Phase.Elapsed.TotalMilliseconds)
    if($remaining -le 0){throw 'ASR worker response timed out.'}
    $pending=$process.StandardOutput.ReadLineAsync()
    if(!$pending.Wait($remaining)){throw 'ASR worker response timed out.'}
    $line=$pending.Result;if($null -eq $line){throw 'ASR worker exited before a completed request.'}
    $stdout.Add($line);return $line
}
try{
    $started=$process.Start();if(!$started){throw 'Cannot start ASR worker.'}
    $stderrTask=$process.StandardError.ReadToEndAsync()
    do{$line=Read-WorkerLine -Phase $watch -LimitSeconds $ReadyTimeoutSeconds}while($line -cne 'LUMA_READY')
    $readyMs=$watch.Elapsed.TotalMilliseconds
    for($i=0;$i -lt $Requests;$i++){
        $prefix=Join-Path $root ('request-'+$i);$request=[string]::Join("`t",@([IO.Path]::GetFullPath($Wave),$prefix,$Language,[string]$Threads))
        $requestWatch=[Diagnostics.Stopwatch]::StartNew();$process.StandardInput.WriteLine($request);$process.StandardInput.Flush()
        $metrics=@{}
        do{
            $line=Read-WorkerLine -Phase $requestWatch -LimitSeconds $TimeoutSeconds
            if($line -match '^LUMA_(AUDIO_MS|INFERENCE_MS|ENCODER_MS|ENCODER_CALLS|BACKEND|DEVICE)=(.+)$'){
                if($metrics.ContainsKey($matches[1])){throw 'Duplicate per-request metric.'}
                $metrics[$matches[1]]=$matches[2]
            }
            if($line -match '^LUMA_ABORTED='){throw 'ASR benchmark request was aborted.'}
        }while($line -cne ('LUMA_DONE='+$prefix))
        $requestWatch.Stop()
        foreach($key in @('AUDIO_MS','INFERENCE_MS','ENCODER_MS','ENCODER_CALLS','BACKEND','DEVICE')){if(!$metrics.ContainsKey($key)){throw "Missing per-request metric: $key"}}
        if(!(Test-Path -LiteralPath ($prefix+'.srt'))){throw 'Completed request lacks its subtitle file.'}
        $runs.Add([pscustomobject]@{index=$i;firstInference=($i -eq 0);requestWallMs=$requestWatch.Elapsed.TotalMilliseconds;audioMs=[double]::Parse($metrics.AUDIO_MS,[Globalization.CultureInfo]::InvariantCulture);inferenceMs=[double]::Parse($metrics.INFERENCE_MS,[Globalization.CultureInfo]::InvariantCulture);encoderTotalMs=[double]::Parse($metrics.ENCODER_MS,[Globalization.CultureInfo]::InvariantCulture);encoderCalls=[int]$metrics.ENCODER_CALLS;backend=$metrics.BACKEND;device=$metrics.DEVICE})
    }
    $process.StandardInput.Close()
    if(!$process.WaitForExit(30000)){throw 'ASR worker did not exit after closing its request stream.'}
    $stderr=$stderrTask.GetAwaiter().GetResult()
    if($process.ExitCode -ne 0){throw "ASR worker exit code $($process.ExitCode)."}
    for($i=0;$i -lt $Requests;$i++){
        foreach($key in @('requestWallMs','encoderTotalMs','inferenceMs','audioMs')){
            $value=[double]$runs[$i].$key
            if([double]::IsNaN($value) -or [double]::IsInfinity($value) -or $value -le 0){throw 'Non-finite or non-positive inference timing.'}
        }
        if($runs[$i].encoderCalls -le 0){throw 'Non-positive raw encoder count.'}
        if($Device -match '^NPU(?:\.[0-9]+)?$' -and ($runs[$i].backend -cne 'NPU' -or $runs[$i].device -cnotmatch '^NPU(?:\.[0-9]+)?$')){throw 'Requested NPU inference was not acknowledged by the actual backend/device markers.'}
    }
    $result=[pscustomobject]@{schema=1;requestedDevice=$Device;threads=$Threads;language=$Language;readyMs=$readyMs;readyMeans='load-and-compile-only; first real inference is recorded separately';workerSha256=(Get-FileHash -LiteralPath $Worker -Algorithm SHA256).Hash.ToLowerInvariant();modelSha256=(Get-FileHash -LiteralPath $Model -Algorithm SHA256).Hash.ToLowerInvariant();waveSha256=(Get-FileHash -LiteralPath $Wave -Algorithm SHA256).Hash.ToLowerInvariant();encoderSha256=$(if($Encoder){(Get-FileHash -LiteralPath $Encoder -Algorithm SHA256).Hash.ToLowerInvariant()}else{$null});runs=@($runs.ToArray())}
    $result | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $root 'metrics.json') -Encoding UTF8
    Write-Output ($result | ConvertTo-Json -Depth 6)
}finally{
    if($started -and !$process.HasExited){$process.Kill();$process.WaitForExit()}
    if($stderrTask){[IO.File]::WriteAllText((Join-Path $root 'worker.stderr.txt'),$stderrTask.GetAwaiter().GetResult(),[Text.UTF8Encoding]::new($false))}
    [IO.File]::WriteAllLines((Join-Path $root 'worker.stdout.txt'),$stdout,[Text.UTF8Encoding]::new($false));$process.Dispose()
}
