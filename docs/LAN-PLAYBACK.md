# 휴대폰에서 내부망 재생 / LAN playback on Android

## 한국어

Windows PC가 Blu-ray를 읽고 Android 앱이 같은 사설 네트워크에서 영상과 음성을 재생합니다. PC에서 이미 준비했거나 불러온 자막은 현재 표시 중인 원문·독음·번역 줄을 기준으로 함께 보낼 수 있습니다.

1. Windows에서 **LumaPlayer 0.2**를 실행합니다. 휴대폰은 PC와 같은 사설 네트워크에 연결하세요.
2. Android 9 이상에서 LumaPlayer APK를 설치하고 앱을 엽니다. 검색된 PC를 선택하거나 Windows의 **휴대폰에서 보기**에 표시된 사설 IP를 직접 입력합니다. 기본 포트는 `48762`입니다.
3. Android에서 다음 연결 방식을 선택합니다. 양쪽에 나온 6자리 코드가 같으면 휴대폰에서 **코드 일치 확인**을 누르고 PC에서 승인하세요. 두 화면에서 기기를 기억하도록 선택하면 이후 그 기기에서 **연결**을 눌러 재생할 수 있습니다.
4. PC에 열린 로컬 동영상 또는 Blu-ray 본편을 휴대폰에서 재생합니다. 미디어를 열기 전 연결했다면 PC에서 파일이나 본편을 연 뒤 **휴대폰에서 보기 → 공유 시작**을 누릅니다. **중지**를 누르면 현재 재생을 종료합니다. 기억한 기기는 PC에서 지울 수 있습니다.

로컬 MP4, M4V, MOV, MKV, WebM, TS, M2TS는 원본 바이트를 보냅니다. 파일 탐색이 가능하며 원본의 HDR·Dolby Vision·공간 음향 데이터가 유지됩니다. 재생 가능한 컨테이너와 프로필은 Android 디코더와 출력 기기에 따릅니다. Blu-ray는 본편을 처음부터 보내고 PC 재생을 잠시 멈춥니다. 영상은 재인코딩 없이 전달하며, 호환 음성은 AAC 스테레오를 사용합니다. **원본 음성**을 선택하면 호환 Android 출력 경로에서 Dolby Digital Plus Atmos 등 원본 음성 형식을 사용할 수 있습니다. Blu-ray 메뉴 조작과 탐색은 PC 플레이어에서 사용합니다. [Android Media3 지원 형식](https://developer.android.com/media/media3/exoplayer/supported-formats)

송출은 선택한 사설 IPv4 주소와 해당 서브넷에서 동작합니다. 확인 코드는 연결의 TLS 공개키와 두 기기가 생성한 난수에 묶입니다. 새 기기는 PC에서 승인한 뒤에만 재생 토큰을 받습니다. 기억한 기기 정보는 Windows DPAPI와 Android Keystore로 보호합니다. Android의 페어링 화면은 보안 창을 사용합니다. 재생 중에는 보안 영상 표면이 스크린샷·화면 녹화·비보안 전송에서 영상 부분을 가리고 조작 UI는 표시합니다. [Android 보안 창](https://developer.android.com/security/fraud-prevention/activities#flag_secure) · [보안 SurfaceView](https://developer.android.com/reference/android/view/SurfaceView#setSecure(boolean))

PC가 목록에 없으면 Wi‑Fi와 PC 주소를 확인하고 직접 입력으로 연결해 보세요. 게스트 네트워크의 기기 격리, Windows 방화벽, V3 같은 보안 프로그램에서 연결이 막힐 수 있습니다. 필요한 경우 설치된 `lumaplayer-mobile-host.exe`의 `48762` 포트를 해당 로컬 서브넷에 한해 허용하세요. 검색에는 UDP를, 페어링과 재생에는 TCP를 사용합니다.

## English

The Windows PC reads Blu-ray discs, and the Android app plays the video and audio on the same private network. Captions already prepared or opened on the PC can accompany playback, using the original, reading, and translation lines selected in the player.

1. Open LumaPlayer 0.2 on Windows. Connect the phone to the same private network as the PC.
2. Install the LumaPlayer APK on Android 9 or later and open it. Select a discovered PC or enter the private IP shown under **휴대폰에서 보기** on Windows. The default port is `48762`.
3. Choose how the next connection should work on Android. Compare the six-digit code on both screens, confirm it on Android, and approve on the PC. If you opt to remember the device on both screens, tap **연결** on Android next time.
4. Play the local video or main Blu-ray title open on the PC. If you connected before opening media, open a title on Windows and choose **휴대폰에서 보기 → 공유 시작**. Select **중지** to end playback. You can clear remembered devices on the PC.

Local MP4, M4V, MOV, MKV, WebM, TS, and M2TS files are sent unchanged. File seeking and the original HDR, Dolby Vision, and spatial-audio data are retained. Container and profile support depends on the Android decoder and output device. Blu-ray shares a selected main title from the beginning and pauses PC playback. The video is remuxed without re-encoding. Compatible audio uses stereo AAC. Choose **원본 음성** to use formats such as Dolby Digital Plus Atmos on a compatible Android output path. Use the desktop player for disc menu interaction and seeking. [Android Media3 supported formats](https://developer.android.com/media/media3/exoplayer/supported-formats)

The stream is served only on the selected private IPv4 address and subnet. The confirmation code is bound to the TLS public key and randomness from both devices. A new device receives a playback token after PC approval. Windows DPAPI and Android Keystore protect remembered device information. Android secures the pairing window. During playback, the secure surface hides the video in screenshots, recordings, and non-secure presentation while leaving the playback controls visible. [Android secure windows](https://developer.android.com/security/fraud-prevention/activities#flag_secure) · [Secure SurfaceView](https://developer.android.com/reference/android/view/SurfaceView#setSecure(boolean))

If a PC is missing from the list, check Wi-Fi and the PC address and try entering it directly. Guest-network isolation, Windows Firewall, and security software such as V3 can prevent access. If a rule is needed, scope port `48762` for the installed `lumaplayer-mobile-host.exe` to that local subnet. Discovery uses UDP. Pairing and playback use TCP.
