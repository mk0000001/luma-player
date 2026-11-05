[CmdletBinding()]
param([string]$PlayerDirectory)

function Get-LumaTranslationModelSpec {
    param([string]$Directory)
    $ErrorActionPreference = 'Stop'
    $candidates = @((Join-Path $Directory 'translation-dependencies.lock.json'),(Join-Path $Directory 'licenses\translation-dependencies.lock.json'))
    $lockPath = $candidates | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
    if (!$lockPath) { throw '번역 잠금 파일이 없습니다 / Missing translation-dependencies.lock.json in the player folder or licenses folder.' }
    $model = (Get-Content -LiteralPath $lockPath -Raw -Encoding UTF8 | ConvertFrom-Json).model
    if (!$model -or [string]$model.file -notmatch '^[A-Za-z0-9_.-]+\.gguf$' -or [string]$model.sha256 -notmatch '^[0-9a-f]{64}$' -or [string]$model.revision -notmatch '^[0-9a-f]{40}$' -or [string]$model.id -notmatch '^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$') {
        throw '번역 모델 잠금 정보가 잘못되었습니다 / Invalid pinned translation model metadata.'
    }
    $size = [long]$model.bytes
    if ($size -le 0 -or $size -gt 100GB) { throw '모델 파일 크기가 잘못되었습니다 / Invalid pinned model size.' }
    $uri = [uri]$model.url
    $expectedPath = '/' + $model.id + '/resolve/' + $model.revision + '/' + $model.file
    if (!$uri.IsAbsoluteUri -or $uri.Scheme -ne 'https' -or $uri.Host -ne 'huggingface.co' -or !$uri.IsDefaultPort -or $uri.UserInfo -or $uri.Query -or $uri.Fragment -or $uri.AbsolutePath -cne $expectedPath) {
        throw '고정 Hugging Face 모델 URL이 잘못되었습니다 / Invalid immutable Hugging Face model URL.'
    }
    return $model
}
function Test-LumaTranslationModelFile {
    param([string]$Path,[string]$ExpectedHash,[long]$ExpectedBytes)
    if (!(Test-Path -LiteralPath $Path -PathType Leaf) -or (Get-Item -LiteralPath $Path).Length -ne $ExpectedBytes) { return $false }
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash -eq $ExpectedHash
}
function Complete-LumaTranslationModelFile {
    param([string]$TemporaryPath,[string]$Destination,[string]$ExpectedHash,[long]$ExpectedBytes)
    $ErrorActionPreference = 'Stop'
    if (!(Test-LumaTranslationModelFile $TemporaryPath $ExpectedHash $ExpectedBytes)) {
        throw '다운로드 파일의 크기 또는 SHA-256이 일치하지 않습니다. 기존 모델은 유지합니다 / Download size or SHA-256 mismatch; the existing model was preserved.'
    }
    # Both paths are in the same model directory. Publish only verified bytes.
    if (Test-Path -LiteralPath $Destination -PathType Leaf) { [IO.File]::Replace($TemporaryPath,$Destination,[NullString]::Value) }
    else { [IO.File]::Move($TemporaryPath,$Destination) }
}
function Assert-LumaTranslationDiskSpace {
    param([string]$Directory,[long]$RequiredBytes)
    try {
        $drive = [IO.DriveInfo]::new([IO.Path]::GetPathRoot([IO.Path]::GetFullPath($Directory)))
        $available = $drive.AvailableFreeSpace
    } catch {
        Write-Verbose 'The filesystem did not report available space; write errors will still stop installation.'
        return
    }
    if ($available -lt $RequiredBytes) {
        throw ('모델 설치 공간이 부족합니다 / Insufficient free space. Required: {0:N2} GiB; available: {1:N2} GiB.' -f ($RequiredBytes/1GB),($available/1GB))
    }
}
function Receive-LumaTranslationModelFile {
    param([uri]$Uri,[string]$TemporaryPath,[long]$ExpectedBytes)
    $ErrorActionPreference = 'Stop'
    Add-Type -AssemblyName System.Net.Http
    $handler = [Net.Http.HttpClientHandler]::new()
    $handler.AllowAutoRedirect = $true
    $handler.MaxAutomaticRedirections = 10
    if ($handler.PSObject.Properties.Name -contains 'SslProtocols') { $handler.SslProtocols = [Security.Authentication.SslProtocols]::Tls12 }
    $client = [Net.Http.HttpClient]::new($handler)
    $client.Timeout = [Threading.Timeout]::InfiniteTimeSpan
    $cancel = [Threading.CancellationTokenSource]::new()
    $cancel.CancelAfter([TimeSpan]::FromMinutes(60))
    $response=$null; $downloadStream=$null; $fileStream=$null
    try {
        $response = $client.GetAsync($Uri,[Net.Http.HttpCompletionOption]::ResponseHeadersRead,$cancel.Token).GetAwaiter().GetResult()
        $null = $response.EnsureSuccessStatusCode()
        $length = $response.Content.Headers.ContentLength
        if ($null -ne $length -and [long]$length -ne $ExpectedBytes) { throw '서버 파일 크기가 잠금 정보와 다릅니다 / Server file size differs from the pinned size.' }
        $downloadStream = $response.Content.ReadAsStreamAsync().GetAwaiter().GetResult()
        $fileStream = [IO.FileStream]::new($TemporaryPath,[IO.FileMode]::Create,[IO.FileAccess]::Write,[IO.FileShare]::None,1MB,[IO.FileOptions]::SequentialScan)
        $buffer = New-Object byte[] 1MB
        [long]$received=0; [long]$nextProgress=0
        while (($read=$downloadStream.ReadAsync($buffer,0,$buffer.Length,$cancel.Token).GetAwaiter().GetResult()) -gt 0) {
            $received += $read
            if ($received -gt $ExpectedBytes) { throw '서버 파일이 예상 크기를 초과했습니다 / Download exceeds the pinned file size.' }
            $fileStream.Write($buffer,0,$read)
            if ($received -ge $nextProgress) {
                Write-Progress -Activity '한국어 번역 모델 / Korean translation model' -Status ('{0:N1} / {1:N1} MiB' -f ($received/1MB),($ExpectedBytes/1MB)) -PercentComplete ([Math]::Min(100,[int](100.0*$received/$ExpectedBytes)))
                $nextProgress = $received + 16MB
            }
        }
        if ($received -ne $ExpectedBytes) { throw '모델 다운로드가 완료되지 않았습니다 / Model download was incomplete.' }
        $fileStream.Flush($true)
        return $received
    } finally {
        Write-Progress -Activity '한국어 번역 모델 / Korean translation model' -Completed
        if ($fileStream) { $fileStream.Dispose() }
        if ($downloadStream) { $downloadStream.Dispose() }
        if ($response) { $response.Dispose() }
        $client.Dispose(); $cancel.Dispose()
    }
}
function Install-LumaTranslationModel {
    param([string]$Directory)
    $ErrorActionPreference = 'Stop'
    $directoryPath = [IO.Path]::GetFullPath($Directory)
    $model = Get-LumaTranslationModelSpec -Directory $directoryPath
    $models = Join-Path $directoryPath 'runtime\translation\models'
    New-Item -ItemType Directory -Path $models -Force | Out-Null
    $destination = Join-Path $models $model.file
    $temporary = "$destination.download"
    $installerLock=$null
    try {
        try { $installerLock=[IO.File]::Open("$destination.install.lock",[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None) }
        catch { throw '다른 모델 설치가 진행 중이거나 폴더에 쓸 수 없습니다 / Another installation is active, or the model folder is not writable.' }
        if (Test-LumaTranslationModelFile $destination $model.sha256 $model.bytes) {
            return [pscustomobject]@{ AlreadyInstalled=$true; BytesTransferred=0; ModelPath=$destination }
        }
        [long]$transferred=0
        if (!(Test-LumaTranslationModelFile $temporary $model.sha256 $model.bytes)) {
            # A partial file from an interrupted run is never published or trusted.
            if (Test-Path -LiteralPath $temporary -PathType Leaf) { Remove-Item -LiteralPath $temporary -Force }
            Assert-LumaTranslationDiskSpace -Directory $models -RequiredBytes ([long]$model.bytes+64MB)
            Write-Host ('선택 번역 모델 {0:N2} GB를 다운로드합니다. 완료 후에는 오프라인으로 사용할 수 있습니다.' -f ([long]$model.bytes/1e9))
            Write-Host ('Downloading the optional {0:N2} GB translation model for offline use.' -f ([long]$model.bytes/1e9))
            $transferred=Receive-LumaTranslationModelFile -Uri $model.url -TemporaryPath $temporary -ExpectedBytes $model.bytes
        }
        Complete-LumaTranslationModelFile $temporary $destination $model.sha256 $model.bytes
        return [pscustomobject]@{ AlreadyInstalled=$false; BytesTransferred=$transferred; ModelPath=$destination }
    } finally { if ($installerLock) { $installerLock.Dispose() } }
}
if ($MyInvocation.InvocationName -ne '.') {
    $ErrorActionPreference = 'Stop'
    try {
        # Windows PowerShell 5.1 may not initialize PSScriptRoot during parameter
        # default evaluation. Resolve it after entering the script body.
        if ([string]::IsNullOrWhiteSpace($PlayerDirectory)) { $PlayerDirectory=Split-Path -Parent $PSScriptRoot }
        $result=Install-LumaTranslationModel -Directory $PlayerDirectory
        if ($result.AlreadyInstalled) {
            Write-Output '기존 번역 모델의 SHA-256을 확인했습니다. 다운로드를 생략합니다.'
            Write-Output 'The existing translation model is verified; no download was needed.'
        } else {
            Write-Output '한국어 번역 모델 설치를 완료했습니다.'
            Write-Output 'The Korean translation model is installed.'
        }
        Write-Output $result.ModelPath
    } catch { throw "모델 설치 실패 / Model installation failed: $($_.Exception.Message)" }
}
