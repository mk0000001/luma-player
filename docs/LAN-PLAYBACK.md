# 내부망에서 다른 기기로 재생 / LAN playback on Android and Windows

## 한국어

Windows PC가 Blu-ray를 읽고 같은 사설 네트워크의 Android나 다른 Windows PC에서 재생합니다. PC에서 이미 준비했거나 불러온 자막은 현재 표시 중인 원문·독음·번역 줄을 기준으로 함께 보낼 수 있습니다.

1. 송출 PC에서 **LumaPlayer 0.2**를 실행합니다. 재생할 기기를 같은 사설 네트워크에 연결하세요.
2. Android 9 이상에서 APK를 설치하고 앱을 엽니다. Windows에서 받으려면 **내부망 재생 → PC에서 받기**를 엽니다. 검색된 PC를 선택하거나 송출 PC의 **내부망 재생**에 표시된 사설 IP를 입력합니다. 기본 포트는 `48762`입니다.
3. 재생할 기기에서 다음 연결 방식을 선택합니다. 양쪽에 나온 6자리 코드가 같으면 받는 기기에서 코드 일치를 확인하고 송출 PC에서 승인하세요. 양쪽에서 기억을 선택하면 이후 그 기기에서 다시 연결할 수 있습니다.
4. 송출 PC에 열린 로컬 동영상이나 Blu-ray 본편을 재생합니다. 먼저 연결했다면 미디어를 연 뒤 송출 PC에서 **공유 시작**을 누릅니다. **공유 중지**로 재생을 종료합니다.

송출 PC의 **내부망 재생**에서 연결 중인 기기와 기억한 기기를 확인할 수 있습니다. 선택한 기기의 연결을 끊거나 기억을 삭제할 수 있습니다. 기억을 삭제한 기기가 다시 연결하려면 6자리 코드와 PC 승인이 필요합니다. **공유 중지**를 누른 뒤 이 플레이어에서 다시 송출하려면 **공유 시작**을 누르세요.

로컬 MP4, M4V, MOV, MKV, WebM, TS, M2TS는 원본 바이트를 보냅니다. 파일 탐색이 가능하며 원본의 HDR·Dolby Vision·공간 음향 데이터가 유지됩니다. 재생 가능한 컨테이너와 프로필은 수신 기기의 디코더와 출력 장치에 따릅니다. Blu-ray는 본편을 처음부터 보내고 PC 재생을 잠시 멈춥니다. 영상은 재인코딩 없이 전달하며, 호환 음성은 AAC 스테레오를 사용합니다. **원본 음성**을 선택하면 호환 출력 경로에서 Dolby Digital Plus Atmos 등 원본 음성 형식을 사용할 수 있습니다. Blu-ray 메뉴 조작과 탐색은 PC 플레이어에서 사용합니다. [Android Media3 지원 형식](https://developer.android.com/media/media3/exoplayer/supported-formats)

송출은 선택한 사설 IPv4 주소와 해당 서브넷에서 동작합니다. 확인 코드는 연결의 TLS 공개키와 두 기기가 생성한 난수에 묶입니다. 새 기기는 PC에서 승인한 뒤에만 재생 토큰을 받습니다. 기억한 기기 정보는 Windows DPAPI와 Android Keystore로 보호합니다. Android의 페어링 화면은 보안 창을 사용합니다. 재생 중에는 보안 영상 표면이 스크린샷·화면 녹화·비보안 전송에서 영상 부분을 가리고 조작 UI는 표시합니다. [Android 보안 창](https://developer.android.com/security/fraud-prevention/activities#flag_secure) · [보안 SurfaceView](https://developer.android.com/reference/android/view/SurfaceView#setSecure(boolean))

PC가 목록에 없으면 Wi‑Fi와 PC 주소를 확인하고 직접 입력으로 연결해 보세요. 게스트 네트워크의 기기 격리, Windows 방화벽, V3 같은 보안 프로그램에서 연결이 막힐 수 있습니다. 필요한 경우 설치된 `lumaplayer-mobile-host.exe`의 `48762` 포트를 해당 로컬 서브넷에 한해 허용하세요. 검색에는 UDP를, 페어링과 재생에는 TCP를 사용합니다.

## English

The Windows PC reads Blu-ray discs. Android and other Windows PCs on the same private network can receive playback. Captions already prepared or opened on the PC can accompany playback, using the original, reading, and translation lines selected in the player.

1. Open LumaPlayer 0.2 on the host PC. Connect the receiving device to the same private network.
2. Install and open the APK on Android 9 or later, or open **내부망 재생 → PC에서 받기** on Windows. Select a discovered PC or enter the private IP shown under **내부망 재생** on the host. The default port is `48762`.
3. Choose how the next connection should work. Compare the six-digit code, confirm it on the receiving device, and approve on the host PC. If you opt to remember the device on both screens, select it to reconnect next time.
4. Play the local video or main Blu-ray title open on the host. If you connected before opening media, open a title and choose **공유 시작** on the host. Select **공유 중지** to end playback.

Use **내부망 재생** on the host to view connected and remembered devices. Disconnect or forget a selected device. A forgotten device must confirm a new code and obtain PC approval on its next connection. After choosing **공유 중지**, select **공유 시작** to resume sharing from that player.

Local MP4, M4V, MOV, MKV, WebM, TS, and M2TS files are sent unchanged. File seeking and the original HDR, Dolby Vision, and spatial-audio data are retained. Container and profile support depends on the receiver's decoder and output device. Blu-ray shares a selected main title from the beginning and pauses PC playback. The video is remuxed without re-encoding. Compatible audio uses stereo AAC. Choose **원본 음성** to use formats such as Dolby Digital Plus Atmos on a compatible output path. Use the desktop player for disc menu interaction and seeking. [Android Media3 supported formats](https://developer.android.com/media/media3/exoplayer/supported-formats)

The stream is served only on the selected private IPv4 address and subnet. The confirmation code is bound to the TLS public key and randomness from both devices. A new device receives a playback token after PC approval. Windows DPAPI and Android Keystore protect remembered device information. Android secures the pairing window. During playback, the secure surface hides the video in screenshots, recordings, and non-secure presentation while leaving the playback controls visible. [Android secure windows](https://developer.android.com/security/fraud-prevention/activities#flag_secure) · [Secure SurfaceView](https://developer.android.com/reference/android/view/SurfaceView#setSecure(boolean))

If a PC is missing from the list, check Wi-Fi and the PC address and try entering it directly. Guest-network isolation, Windows Firewall, and security software such as V3 can prevent access. If a rule is needed, scope port `48762` for the installed `lumaplayer-mobile-host.exe` to that local subnet. Discovery uses UDP. Pairing and playback use TCP.
