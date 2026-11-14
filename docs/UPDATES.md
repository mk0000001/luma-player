# Windows 업데이트 / Windows updates

## 한국어

Windows LumaPlayer의 설정에서 **업데이트 확인**을 누르면 공식 GitHub 공개 릴리스를 조회합니다. 실행할 때마다 확인하려면 **시작 시 업데이트 확인**을 켜세요. 새 버전이 있으면 업데이트 창에서 설치 파일을 받을 위치를 선택할 수 있습니다. [공식 릴리스](https://github.com/mk0000001/luma-player/releases)

다운로드 위치로 여유 공간이 있는 로컬 드라이브를 선택하세요. 앱은 Windows 사용자와 시스템에만 접근 권한이 있는 폴더에 설치 파일을 저장하고 서명, 파일 크기, SHA-256을 확인합니다. **설치 시작**을 누르면 확인한 설치 프로그램을 열고 현재 플레이어를 종료합니다. 이미 설치된 버전도 비교하고, 선택한 설치본이 확인 이후 바뀌면 업데이트 설치가 중단됩니다. GitHub에 직접 접속할 수 없는 네트워크에서는 공식 릴리스에서 설치 파일을 받을 수 있습니다.

배포할 때 검증을 마친 Windows x64 설치 파일과 함께 서명 자료를 첨부합니다. 현재 Windows 사용자에 연결된 서명 키는 배포 머신에서 관리합니다. 설치본을 만들 때 `build-installer.ps1`에 `-SignUpdateManifest`를 전달하거나 기존 설치본을 다음 명령으로 서명하세요.

```powershell
./scripts/sign-update-manifest.ps1 -Version <버전> -InstallerPath <설치 파일> -OutputDirectory <릴리스 폴더>
```

같은 GitHub 릴리스에 `luma-update.json`, `luma-update.sig`, 서명한 정보에 해당하는 설치 파일과 체크섬을 첨부합니다.

## English

Select **업데이트 확인** in LumaPlayer's Windows settings to check the official GitHub releases. Turn on **시작 시 업데이트 확인** to check at launch. When a new version is available, use the update prompt to choose where to download the installer. [Official releases](https://github.com/mk0000001/luma-player/releases)

Choose a local drive with enough free space for the download. The app stages the installer in a directory accessible to the Windows user and system, then verifies the signature, file size, and SHA-256. Select **설치 시작** to open the verified installer and close the player. The updater also checks the installed version and stops if the selected installation changes after verification. On networks without direct access to GitHub, use the official release downloads.

For distribution, sign metadata for the verified Windows x64 installer on the release machine. The signing key is scoped to the current Windows user. Pass `-SignUpdateManifest` to `build-installer.ps1` or sign an existing installer with this command.

```powershell
./scripts/sign-update-manifest.ps1 -Version <version> -InstallerPath <installer> -OutputDirectory <release folder>
```

Attach `luma-update.json`, `luma-update.sig`, the corresponding installer, and its checksum to the same GitHub release.
