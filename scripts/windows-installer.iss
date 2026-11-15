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
Compression=lzma2/fast
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
korean.ComponentsPrompt=이 PC의 그래픽 장치와 메모리에 맞춰 권장 항목을 선택했습니다. 추가 다운로드를 선택하거나 해제할 수 있습니다.
english.ComponentsPrompt=Setup selected components based on this PC's graphics and memory. You can change the selection before downloading.
[Types]
Name: "compact"; Description: "플레이어만 · 오프라인 / Player only (offline)"
Name: "custom"; Description: "직접 선택 / Custom"; Flags: iscustom
[Components]
Name: "core"; Description: "플레이어 · CPU, Vulkan, Intel OpenVINO / Player"; Types: compact custom; Flags: fixed
Name: "cuda"; Description: "NVIDIA CUDA 12 라이브러리 다운로드 ({#CudaDownloadMB} MB) / CUDA runtime"; Flags: disablenouninstallwarning
Name: "base"; Description: "Whisper Base + Intel OpenVINO 인코더 다운로드 (172 MB) / Compact ASR"; Flags: disablenouninstallwarning
Name: "turbo"; Description: "Whisper Turbo 인식 모델 다운로드 (575 MB) / Fast ASR"; Flags: disablenouninstallwarning
Name: "accurate"; Description: "Whisper full-v3 Q5 인식 모델 다운로드 (1.08 GB) / High-accuracy ASR"; Flags: disablenouninstallwarning
Name: "translation"; Description: "Hy-MT2 한국어 번역 다운로드 (4.62 GB) / Korean translation"; Flags: disablenouninstallwarning
[Files]
Source: "{#Payload}\*"; Excludes: "\setup\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs; Components: core
Source: "{#Payload}\setup\luma-setup-hardware.exe"; Flags: dontcopy noencryption
#include "downloads.iss"
[Icons]
Name: "{group}\LumaPlayer"; Filename: "{app}\LumaPlayer.exe"; WorkingDir: "{app}"
[Run]
Filename: "{app}\LumaPlayer.exe"; Description: "LumaPlayer 실행 / Launch LumaPlayer"; Flags: nowait postinstall skipifsilent unchecked
[Code]
procedure InitializeWizard;
var Index, Profile: Integer;
    Argument, Selection, ProbePath: String;
begin
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
    $22: Selection := 'turbo,translation';
    $23: Selection := 'turbo,cuda,translation';
  else
    Exit;
  end;
  WizardSelectComponents(Selection);
  WizardForm.SelectComponentsLabel.Caption := CustomMessage('ComponentsPrompt');
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
end;

function NeedsDownloadFile(RelativePath, ExpectedBytes, ExpectedHash: String): Boolean;
var Path: String;
    Bytes: Int64;
begin
  Path := ExpandConstant('{app}\') + RelativePath;
  Result := True;
  if FileSize64(Path, Bytes) and (Bytes = StrToInt64Def(ExpectedBytes, -1)) then
    Result := CompareText(GetSHA256OfFile(Path), ExpectedHash) <> 0;
end;

#include "download-checks.iss"
