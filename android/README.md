# LumaPlayer Android

## 한국어

Android 9 이상에서 Windows LumaPlayer가 같은 네트워크에 공유한 파일과 Blu-ray 타이틀을 재생합니다. **PC 검색**을 눌러 연결을 켜 둔 PC를 찾거나 사설 IPv4 주소를 직접 입력하세요. 목록이 보이는 동안 검색을 반복하려면 **PC 자동 검색**을 켜세요. 주소에 포트가 없으면 `48762`를 사용합니다.

**0.2.3에서 0.2.5로 이동**하려면 새 APK를 설치하고 PC와 다시 페어링하세요. 이번 APK는 별도 앱 ID를 사용하므로 0.2.3과 함께 설치할 수 있습니다. 새 앱에서 연결을 확인한 뒤 이전 앱을 제거할 수 있습니다. [최신 Android APK](https://github.com/mk0000001/luma-player/releases/latest)

**매번 PC에서 승인** 또는 **이 PC 기억하기**를 선택하고 **승인 요청**을 누르세요. 휴대폰과 PC에 표시된 6자리 코드가 같으면 휴대폰에서 **코드 일치 확인**을 누르고 PC에서 승인하세요. 양쪽에서 기억하기를 선택하면 다음번에 목록의 기억한 PC를 선택해 다시 연결할 수 있습니다. 매번 승인하려면 **기억 지우기**를 누르세요.

승인된 연결은 HTTPS로 PC 인증서의 IP 이름과 공개 키 지문을 확인합니다. 기억한 연결의 자격 증명은 Android Keystore 키로 암호화하며, 현재 재생 토큰과 미디어는 앱 메모리에서 처리합니다. 재생 중 화면 캡처에는 보호된 영상 표면이 가려지고 컨트롤과 텍스트 자막은 UI에 표시됩니다. 재생 음성 캡처는 Android의 앱 정책으로 제한됩니다.

승인한 뒤 PC에서 **공유 시작**을 누르세요. Android에서 직접 시작하려면 PC에서 영상을 열고 **내부망 재생 → 연결된 기기에서 현재 미디어 재생 시작 허용**을 켠 다음, Android에서 **재생 시작**을 누르세요. PC에서 소스를 바꾸거나 공유를 중지하면 시작 권한을 다시 켜야 합니다. 재생 화면의 자막 아이콘으로 제공된 자막을 선택할 수 있습니다. 파일은 호스트와 컨테이너가 지원할 때 위치 이동이 가능하고, Blu-ray 타이틀은 실시간 스트림으로 재생합니다.

**재생 정보**에서 소스 형식, 초기화된 영상 디코더, AudioTrack 형식, 화면과 오디오 경로가 보고한 지원 형식을 확인할 수 있습니다. HDR10, HDR10+, Dolby Vision, Dolby Digital Plus JOC(Atmos), Dolby TrueHD 및 DTS의 사용 가능 여부는 실제 소스, 기기와 연결된 출력 장치에 따라 달라집니다.

Windows PowerShell에서 저장소 루트의 다음 명령으로 단위 테스트와 릴리스 Lint를 실행하고 로컬 베타 서명 APK를 만듭니다.

```powershell
.\android\build.ps1 -SignLocalBeta
```

기본 도구 경로는 `.deps/android-toolchain`입니다. 여유 공간이 있는 드라이브를 쓰려면 `-ToolchainRoot D:/LumaBuild/android-toolchain`처럼 경로를 전달하세요. 빌드 스크립트는 Temurin 17.0.21+9, Gradle 8.13, Android SDK 36 rev. 2, Build Tools 36.0.0, Platform Tools 37.0.3을 공식 배포 파일의 SHA-256으로 확인합니다. AGP 8.13.2와 앱 의존성은 Gradle에서 고정합니다. Noto Sans CJK KR 글꼴은 저장소의 `dependencies.lock.json`에 있는 SHA-256을 확인해 앱에 포함합니다. [AGP 호환성](https://developer.android.com/build/releases/agp-8-13-0-release-notes), [의존성 고지](THIRD_PARTY_NOTICES.md)

APK 경로는 도구 경로 아래 `build/app/outputs/apk/release/LumaPlayer-Android-<버전>.apk`입니다. 로컬 베타 서명 키는 현재 Windows 사용자 프로필의 `LumaPlayer/AndroidSigning` 폴더에 보관되며 암호는 Windows DPAPI로 보호됩니다.

## English

On Android 9 and later, the app plays media files and Blu-ray titles shared by LumaPlayer for Windows on the same network. Tap **PC 검색** to find PCs with connections enabled, or enter a private IPv4 address. Turn on **PC 자동 검색** to refresh while the list is open. The default port is `48762`.

**Moving from 0.2.3 to 0.2.5** requires installing the new APK and pairing with the PC again. This APK uses a separate application ID and can be installed alongside 0.2.3. You can remove the old app after confirming the connection in the new one. [Latest Android APK](https://github.com/mk0000001/luma-player/releases/latest)

Choose to ask every time or remember the PC, then request approval. If the six-digit codes match, confirm the match on the phone and approve on the PC. When both sides choose to remember, selecting the saved PC reconnects. Forget the PC on the phone to require approval again.

Approved HTTPS connections verify the PC certificate's IP name and public key fingerprint. An Android Keystore key encrypts remembered credentials. The current playback token and media stay in app memory. Screen capture obscures the secure video surface, while playback controls and text subtitle overlays remain in the UI. Android app policy restricts playback audio capture.

After approval, start sharing on the PC. To start from Android, open the media on the PC, enable **연결된 기기에서 현재 미디어 재생 시작 허용** under **내부망 재생**, then tap **Start playback** on Android. Enable the permission again after the PC changes the source or stops sharing. Choose an available subtitle track with the player's subtitle button. File seeking depends on the host and container. Blu-ray titles use a live stream.

**재생 정보** shows the source formats, initialized video decoder, AudioTrack format, and capabilities reported by the display and audio route. HDR10, HDR10+, Dolby Vision, Dolby Digital Plus JOC (Atmos), Dolby TrueHD, and DTS availability depends on the source, device, and connected outputs.

From the repository root in Windows PowerShell, run unit tests and release lint and produce a locally signed beta APK with this command.

```powershell
.\android\build.ps1 -SignLocalBeta
```

The default toolchain directory is `.deps/android-toolchain`. To use a drive with more space, pass a path such as `-ToolchainRoot D:/LumaBuild/android-toolchain`. The script verifies the official SHA-256-pinned archives for Temurin 17.0.21+9, Gradle 8.13, Android SDK 36 rev. 2, Build Tools 36.0.0, and Platform Tools 37.0.3. Gradle pins AGP 8.13.2 and the app dependencies. The app bundles the Noto Sans CJK KR font after checking its SHA-256 in the repository's `dependencies.lock.json`. [AGP compatibility](https://developer.android.com/build/releases/agp-8-13-0-release-notes), [dependency notices](THIRD_PARTY_NOTICES.md)

The APK is at `build/app/outputs/apk/release/LumaPlayer-Android-<version>.apk` under the toolchain directory. The local beta signing key is held in the current Windows user's `LumaPlayer/AndroidSigning` profile folder, with its password protected by Windows DPAPI.
