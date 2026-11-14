#ifndef Payload
 #error Payload must be verified by build-installer.ps1
#endif
#ifndef PayloadFlavor
 #define PayloadFlavor "windows-x64-cuda"
#endif
[Setup]
AppId={{C6A16658-7E17-4F16-B49F-AC61297DD921}
AppName=LumaPlayer
AppVersion={#InstallerVersion}
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
OutputBaseFilename=LumaPlayer-{#PlayerVersion}-setup.1-{#PayloadFlavor}
Compression=lzma2/fast
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\LumaPlayer.exe
CloseApplications=no
RestartApplications=no
SetupMutex=LumaPlayer.Setup.C6A16658
DisableProgramGroupPage=yes
SetupLogging=no
[Languages]
Name: "korean"; MessagesFile: "compiler:Languages\Korean.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"
[CustomMessages]
korean.UpdateTargetChanged=설치 대상이 업데이트 확인 이후 변경됐거나 확인한 설치본과 다릅니다. 대상 설치본에서 업데이트를 다시 확인해 주세요.
english.UpdateTargetChanged=The selected copy changed or differs from the copy checked for this update. Check for updates from the intended installation.
[Types]
Name: "full"; Description: "권장: 고정밀 인식 + 한국어 번역 / Recommended: accurate ASR + Korean translation"
Name: "compact"; Description: "기본 Turbo만 (오프라인) / Bundled Turbo only (offline)"
Name: "custom"; Description: "직접 선택 / Custom"; Flags: iscustom
[Components]
#if PayloadFlavor == "windows-x64-cuda"
Name: "core"; Description: "플레이어 + CUDA + Turbo / Player + CUDA + Turbo"; Types: full compact custom; Flags: fixed
#else
Name: "core"; Description: "플레이어 + Turbo / Player + Turbo"; Types: full compact custom; Flags: fixed
#endif
Name: "accurate"; Description: "full-v3 Q5 인식 모델 다운로드 (1.08 GB, MIT) / Download accurate ASR"; Types: full
Name: "translation"; Description: "Hy-MT2 한국어 번역 다운로드 (4.62 GB, Apache-2.0) / Download translation"; Types: full
[Files]
Source: "{#Payload}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs; Components: core
#include "models.iss"
[Icons]
Name: "{group}\LumaPlayer"; Filename: "{app}\LumaPlayer.exe"; WorkingDir: "{app}"
[Run]
Filename: "{app}\LumaPlayer.exe"; Description: "LumaPlayer 실행 / Launch LumaPlayer"; Flags: nowait postinstall skipifsilent unchecked
[Code]
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

function NeedsModel(RelativePath, ExpectedHash: String): Boolean;
var Path: String;
begin
  Path := ExpandConstant('{app}\') + RelativePath;
  Result := True;
  if FileExists(Path) then
    Result := CompareText(GetSHA256OfFile(Path), ExpectedHash) <> 0;
end;
