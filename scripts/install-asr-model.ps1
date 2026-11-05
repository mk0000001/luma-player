[CmdletBinding()]
param([string]$PlayerDirectory,[string]$Profile='japanese')
$asrModelDirectoryArgument=$PlayerDirectory
$asrModelProfileArgument=$Profile
$asrModelDirectInvocation=$MyInvocation.InvocationName -ne '.'
$asrModelSharedInstaller=Join-Path $PSScriptRoot 'install-translation.ps1'
if(!(Test-Path -LiteralPath $asrModelSharedInstaller -PathType Leaf)){throw '공유 설치 도구가 없습니다. tools 폴더를 함께 유지하세요 / Missing shared installer; keep install-translation.ps1 beside this tool.'}
# Preserve this script's arguments: the shared script has its own parameter defaults.
. $asrModelSharedInstaller

function Get-LumaAsrModelSpec {
 param([string]$Directory,[string]$Profile='japanese')
 $ErrorActionPreference='Stop'
 try{
  if($Profile -ne 'japanese'){throw '지원하지 않는 음성 인식 프로필입니다 / Unsupported ASR model profile.'}
  $root=[IO.Path]::GetFullPath($Directory)
  $candidates=@((Join-Path $root 'dependencies.lock.json'),(Join-Path $root 'licenses/dependencies.lock.json'))
  $lock=$candidates | Where-Object {Test-Path -LiteralPath $_ -PathType Leaf} | Select-Object -First 1
  if(!$lock){throw '고정 의존성 파일이 없습니다 / Missing dependencies.lock.json.'}
  $manifest=Get-Content -LiteralPath $lock -Raw -Encoding UTF8 | ConvertFrom-Json
  $model=$manifest.japaneseModel
  if(!$model){throw '일본어 모델 프로필이 없습니다 / Missing Japanese ASR model profile.'}
  if([string]$model.file -notmatch '^ggml-[A-Za-z0-9_.-]+\.bin$' -or [string]$model.id -notmatch '^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$' -or [string]$model.revision -notmatch '^[a-f0-9]{40}$' -or [string]$model.sha256 -notmatch '^[a-f0-9]{64}$'){
   throw '모델 고정 정보가 잘못되었습니다 / Invalid pinned ASR model metadata.'
  }
  [long]$size=$model.bytes
  if($size -le 0 -or $size -gt 2GB -or $model.language -cne 'ja' -or !($model.optionalAtRuntime -is [bool]) -or !$model.optionalAtRuntime){throw '모델 크기 또는 일본어 선택 정보가 잘못되었습니다 / Invalid ASR size or optional Japanese profile.'}
  $uri=[uri]$model.url
  $expected='/'+$model.id+'/resolve/'+$model.revision+'/'+$model.file
  if(!$uri.IsAbsoluteUri -or $uri.Scheme -ne 'https' -or $uri.Host -ne 'huggingface.co' -or !$uri.IsDefaultPort -or $uri.UserInfo -or $uri.Query -or $uri.Fragment -or $uri.AbsolutePath -cne $expected){throw '고정 공식 모델 주소가 잘못되었습니다 / Invalid immutable Hugging Face ASR model URL.'}
  if($model.license -cne 'Apache-2.0' -or [string]$model.licenseFile -notmatch '^[A-Za-z0-9][A-Za-z0-9_.-]*\.txt$' -or [string]$model.licenseSha256 -notmatch '^[a-f0-9]{64}$'){throw '모델 라이선스 고정 정보가 잘못되었습니다 / Invalid pinned ASR license metadata.'}
  $licenseCandidates=@((Join-Path $root ('LICENSES/'+$model.licenseFile)),(Join-Path $root ('licenses/LICENSES/'+$model.licenseFile)),(Join-Path $root ('licenses/'+$model.licenseFile)))
  $license=$licenseCandidates | Where-Object {Test-Path -LiteralPath $_ -PathType Leaf} | Select-Object -First 1
  if(!$license -or (Get-FileHash -LiteralPath $license -Algorithm SHA256).Hash -ne $model.licenseSha256){throw '라이선스 원문이 없거나 변경되었습니다 / Raw ASR license is missing or changed.'}
  return $model
 }catch{throw "음성 인식 모델 정보 확인 실패 / ASR model metadata failed: $($_.Exception.Message)"}
}

function Install-LumaAsrModel {
 param([string]$Directory,[string]$Profile='japanese')
 $ErrorActionPreference='Stop';$guard=$null
 try{
  $root=[IO.Path]::GetFullPath($Directory);$model=Get-LumaAsrModelSpec -Directory $root -Profile $Profile
  $models=Join-Path $root 'runtime/models';New-Item -ItemType Directory -Path $models -Force | Out-Null
  $destination=Join-Path $models $model.file;$temporary=$destination+'.download'
  try{$guard=[IO.File]::Open(($destination+'.install.lock'),[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)}
  catch{throw '다른 모델 설치가 진행 중이거나 폴더에 쓸 수 없습니다 / Another ASR installation is active, or the model folder is not writable.'}
  if(Test-LumaTranslationModelFile $destination $model.sha256 $model.bytes){return [pscustomobject]@{AlreadyInstalled=$true;BytesTransferred=0;ModelPath=$destination}}
  [long]$transferred=0
  if(!(Test-LumaTranslationModelFile $temporary $model.sha256 $model.bytes)){
   if(Test-Path -LiteralPath $temporary -PathType Leaf){Remove-Item -LiteralPath $temporary -Force}
   Assert-LumaTranslationDiskSpace -Directory $models -RequiredBytes ([long]$model.bytes+64MB)
   Write-Host '선택 일본어 음성 인식 모델을 다운로드합니다 / Downloading the optional Japanese ASR model.'
   $transferred=Receive-LumaTranslationModelFile -Uri $model.url -TemporaryPath $temporary -ExpectedBytes $model.bytes
  }
  Complete-LumaTranslationModelFile $temporary $destination $model.sha256 $model.bytes
  return [pscustomobject]@{AlreadyInstalled=$false;BytesTransferred=$transferred;ModelPath=$destination}
 }catch{throw "음성 인식 모델 설치 실패 / ASR model installation failed: $($_.Exception.Message)"}
 finally{if($guard){$guard.Dispose()}}
}

if($asrModelDirectInvocation){
 if([string]::IsNullOrWhiteSpace($asrModelDirectoryArgument)){$asrModelDirectoryArgument=Split-Path -Parent $PSScriptRoot}
 $result=Install-LumaAsrModel -Directory $asrModelDirectoryArgument -Profile $asrModelProfileArgument
 Write-Output $(if($result.AlreadyInstalled){'기존 일본어 모델을 검증했습니다. 다운로드를 생략합니다 / Existing Japanese model verified; no download needed.'}else{'일본어 음성 인식 모델을 설치했습니다 / Japanese ASR model installed.'})
 Write-Output $result.ModelPath
}
