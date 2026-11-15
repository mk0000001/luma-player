#ifndef Payload
 #error Payload must be verified by build-installer.ps1
#endif
#ifndef InstallerFlavor
 #define InstallerFlavor "windows-x64"
#endif
[Setup]
AppId={{C6A16658-7E17-4F16-B49F-AC61297DD921}
AppName=LumaPlayer
AppVersion={#PlayerVersion}
AppVerName=LumaPlayer {#PlayerVersion}
VersionInfoProductName=LumaPlayer
VersionInfoProductTextVersion={#PlayerVersion}
VersionInfoTextVersion={#PlayerVersion}
VersionInfoDescription=LumaPlayer Setup
AppPublisher=LumaPlayer contributors
AppPublisherURL=https://github.com/mk0000001/luma-player
DefaultDirName={localappdata}\Programs\LumaPlayer
DefaultGroupName=LumaPlayer
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputDir={#Output}
OutputBaseFilename=LumaPlayer-{#PlayerVersion}-setup.1-{#InstallerFlavor}
Compression=lzma2/ultra64
LZMAUseSeparateProcess=yes
LZMANumBlockThreads=4
#ifdef IncludeFullModels
DiskSpanning=yes
DiskSliceSize=2100000000
#endif
ArchiveExtraction=full
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\LumaPlayer.exe
CloseApplications=no
RestartApplications=no
SetupMutex=LumaPlayer.Setup.C6A16658
DisableProgramGroupPage=yes
SetupLogging=no
UsePreviousSetupType=no
[Languages]
Name: "korean"; MessagesFile: "compiler:Languages\Korean.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"
[CustomMessages]
korean.UpdateTargetChanged=설치 대상이 업데이트 확인 이후 변경됐거나 확인한 설치본과 다릅니다. 대상 설치본에서 업데이트를 다시 확인해 주세요.
english.UpdateTargetChanged=The selected copy changed or differs from the copy checked for this update. Check for updates from the intended installation.
korean.ComponentsPrompt=이 PC의 그래픽 장치와 메모리에 맞춰 권장 항목을 선택했습니다. 설치할 항목을 바꿀 수 있습니다.
english.ComponentsPrompt=Setup selected components based on this PC's graphics and memory. You can change the selection before installing.
korean.CudaDownload=선택한 NVIDIA CUDA 라이브러리를 내려받는 중
english.CudaDownload=Downloading the selected NVIDIA CUDA libraries
korean.CudaDownloadFailed=CUDA 라이브러리를 내려받거나 검증하지 못했습니다. 인터넷 연결을 확인하거나 CUDA 선택을 해제한 후 진행하세요.
english.CudaDownloadFailed=Setup could not download or verify CUDA. Check your internet connection or clear the CUDA selection and continue.
[Types]
Name: "compact"; Description: "플레이어만 / Player only"
Name: "custom"; Description: "직접 선택 / Custom"; Flags: iscustom
[Components]
Name: "core"; Description: "플레이어 · CPU, Vulkan, Intel OpenVINO / Player"; Types: compact custom; Flags: fixed
Name: "cuda"; Description: "NVIDIA CUDA 12 라이브러리 다운로드 ({#CudaDownloadMB} MB) / CUDA runtime"; ExtraDiskSpaceRequired: {#CudaExpandedBytes}; Flags: disablenouninstallwarning
Name: "base"; Description: "Whisper Base + Intel OpenVINO 인코더 / Compact ASR"; ExtraDiskSpaceRequired: {#BaseExpandedBytes}; Flags: disablenouninstallwarning
Name: "turbo"; Description: "Whisper Turbo 인식 모델 / Fast ASR"; ExtraDiskSpaceRequired: {#TurboBytes}; Flags: disablenouninstallwarning
#ifdef IncludeFullModels
Name: "accurate"; Description: "Whisper full-v3 Q5 인식 모델 / High-accuracy ASR"; ExtraDiskSpaceRequired: {#AccurateBytes}; Flags: disablenouninstallwarning
Name: "translation"; Description: "Hy-MT2 한국어 번역 / Korean translation"; ExtraDiskSpaceRequired: {#TranslationBytes}; Flags: disablenouninstallwarning
#endif
[Files]
Source: "{#Payload}\setup\luma-setup-hardware.exe"; Flags: dontcopy noencryption
Source: "{#Payload}\*"; Excludes: "\setup\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs; Components: core
#include "models-standard.iss"
#ifdef IncludeFullModels
 #include "models-full.iss"
#endif
#include "downloads.iss"
[Icons]
Name: "{group}\LumaPlayer"; Filename: "{app}\LumaPlayer.exe"; WorkingDir: "{app}"
[Run]
Filename: "{app}\LumaPlayer.exe"; Description: "LumaPlayer 실행 / Launch LumaPlayer"; Flags: nowait postinstall skipifsilent unchecked
[Code]
var CudaProgress: TOutputMarqueeProgressWizardPage;

procedure InitializeWizard;
var Index, Profile: Integer;
    Argument, Selection, ProbePath: String;
begin
  if not WizardSilent then
    CudaProgress := CreateOutputMarqueeProgressPage(CustomMessage('CudaDownload'), '');
  if WizardSilent or (ExpandConstant('{param:LumaCurrentHash|}') <> '') then Exit;
  for Index := 1 to ParamCount do begin
    Argument := UpperCase(ParamStr(Index));
    if (Copy(Argument, 1, 6) = '/TYPE=') or (Copy(Argument, 1, 12) = '/COMPONENTS=') then Exit;
  end;
  try
    ExtractTemporaryFile('luma-setup-hardware.exe');
    ProbePath := ExpandConstant('{tmp}\luma-setup-hardware.exe');
    if not Exec(ProbePath, '', ExpandConstant('{tmp}'), SW_HIDE, ewWaitUntilTerminated, Profile) then Exit;
  except
    Exit;
  end;
  case Profile of
    $10: Selection := 'base';
    $11: Selection := 'base,cuda';
    $20: Selection := 'turbo';
    $21: Selection := 'turbo,cuda';
#ifdef IncludeFullModels
    $22: Selection := 'turbo,translation';
    $23: Selection := 'turbo,cuda,translation';
#else
    $22: Selection := 'turbo';
    $23: Selection := 'turbo,cuda';
#endif
  else
    Exit;
  end;
  WizardForm.TypesCombo.ItemIndex := WizardForm.TypesCombo.Items.Count - 1;
  WizardSelectComponents(Selection);
  WizardForm.SelectComponentsLabel.Caption := CustomMessage('ComponentsPrompt');
end;

function MatchesFile(Path, ExpectedBytes, ExpectedHash: String): Boolean;
var Bytes: Int64;
begin
  Result := False;
  if FileSize64(Path, Bytes) and (Bytes = StrToInt64Def(ExpectedBytes, -1)) then
    Result := CompareText(GetSHA256OfFile(Path), ExpectedHash) = 0;
end;

function NeedsInstallFile(RelativePath, ExpectedBytes, ExpectedHash: String): Boolean;
begin
  Result := not MatchesFile(ExpandConstant('{app}\') + RelativePath, ExpectedBytes, ExpectedHash);
end;

#include "download-checks.iss"

function PrepareCuda: String;
var ArchivePath, Curl, Args: String;
    ExitCode: Integer;
    Downloaded: Boolean;
begin
  Result := '';
  if not WizardIsComponentSelected('cuda') then Exit;
  if not NeedsCuda then Exit;
  ArchivePath := ExpandConstant('{tmp}\{#CudaArchiveName}');
  if MatchesFile(ArchivePath, '{#CudaDownloadBytes}', '{#CudaSha256}') then Exit;

  Result := CustomMessage('CudaDownloadFailed');
  Downloaded := False;
  DeleteFile(ArchivePath);
  if not WizardSilent then begin
    CudaProgress.SetText(CustomMessage('CudaDownload'), 'NVIDIA CUDA 12 ({#CudaDownloadMB} MB)');
    CudaProgress.Show;
    CudaProgress.Animate;
  end;
  try
    try
      Log('Downloading selected CUDA runtime from {#CudaUrl}');
      Curl := ExpandConstant('{sysnative}\curl.exe');
      if FileExists(Curl) then begin
        Args := '-q --fail --silent --show-error --location --max-redirs 8 --proto =https --proto-redir =https' +
          ' --connect-timeout 15 --speed-limit 1024 --speed-time 30 --max-time 1800' +
          ' --max-filesize {#CudaDownloadBytes} --output "' + ArchivePath + '" "{#CudaUrl}"';
        if Exec(Curl, Args, ExpandConstant('{tmp}'), SW_HIDE, ewWaitUntilTerminated, ExitCode) then begin
          Downloaded := ExitCode = 0;
          Log('Windows curl download exit code: ' + IntToStr(ExitCode));
        end else
          Log('Could not launch Windows curl: ' + SysErrorMessage(ExitCode));
      end else begin
        DownloadTemporaryFile('{#CudaUrl}', '{#CudaArchiveName}', '{#CudaSha256}', nil);
        Downloaded := True;
      end;
      if Downloaded and MatchesFile(ArchivePath, '{#CudaDownloadBytes}', '{#CudaSha256}') then begin
        Log('Verified optional CUDA archive SHA-256.');
        Result := '';
      end;
    except
      Log('Optional CUDA runtime download failed: ' + GetExceptionMessage);
    end;
  finally
    if not WizardSilent then CudaProgress.Hide;
    if Result <> '' then DeleteFile(ArchivePath);
  end;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var ExpectedHash, PlayerPath: String;
begin
  Result := '';
  ExpectedHash := ExpandConstant('{param:LumaCurrentHash|}');
  PlayerPath := ExpandConstant('{app}\LumaPlayer.exe');
  if ExpectedHash <> '' then
    if not FileExists(PlayerPath) then
      Result := ExpandConstant('{cm:UpdateTargetChanged}')
    else if CompareText(GetSHA256OfFile(PlayerPath), ExpectedHash) <> 0 then
      Result := ExpandConstant('{cm:UpdateTargetChanged}');
  if Result = '' then Result := PrepareCuda;
end;
