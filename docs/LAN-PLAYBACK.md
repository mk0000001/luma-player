# 내부망에서 다른 기기로 재생 / LAN playback on Android and Windows

## 한국어

Windows PC가 Blu-ray를 읽고 같은 사설 네트워크의 Android나 다른 Windows PC에서 재생합니다. Android의 **자막** 버튼에서 PC의 실시간 AI 상태를 확인하고, 제어 권한이 있으면 켜거나 다시 시도할 수 있습니다. 준비한 자막과 불러온 자막도 받을 수 있고, Android 기기에서 SRT·VTT·ASS·SSA 파일을 직접 열 수 있습니다.

1. 송출 PC에서 **내부망 재생 → 기기 검색 켜기**를 누릅니다. 다음 실행부터 자동으로 검색되게 하려면 **시작 시 기기 검색 켜기**도 선택하세요. 재생할 기기를 같은 사설 네트워크에 연결하세요.
2. Android 9 이상에서 APK를 설치하고 **PC 검색**을 누릅니다. 앱의 PC 목록이 열려 있는 동안 주기적으로 찾으려면 **PC 자동 검색**을 켜세요. Windows에서 받으려면 **내부망 재생 → PC에서 받기**를 엽니다. 검색된 PC를 선택하거나 송출 PC의 **내부망 재생**에 표시된 사설 IP를 입력합니다. 기본 포트는 `48762`입니다.
3. 재생할 기기에서 다음 연결 방식을 선택합니다. 양쪽에 나온 6자리 코드가 같으면 받는 기기에서 코드 일치를 확인하고 송출 PC에서 승인하세요. 양쪽에서 기억을 선택하면 이후 그 기기에서 다시 연결할 수 있습니다.
4. 송출 PC에서 로컬 동영상이나 Blu-ray 본편을 엽니다. PC에서 **공유 시작**을 누르거나, **연결된 기기에서 현재 미디어 재생 시작 허용**을 켜고 Android에서 **재생 시작**을 누릅니다. PC에서 소스나 Blu-ray 타이틀을 바꾸거나 공유를 중지하면 Android 시작 권한을 다시 켜야 합니다.

송출 PC의 **내부망 재생**에서 연결 중인 기기와 기억한 기기를 확인할 수 있습니다. 수신 기기에서 PC를 조작하려면 연결 후 **연결된 기기에서 PC 플레이어 조작 허용**을 켜세요. 선택한 기기의 연결을 끊거나 기억을 삭제할 수 있습니다. 기억을 삭제한 기기가 다시 연결하려면 6자리 코드와 PC 승인이 필요합니다. **기기 검색 끄기**를 누르면 공유와 새 연결 수락을 함께 종료합니다.

같은 네트워크를 사용할 수 없으면 Windows에서 **공유기 없이 휴대폰 연결**을 누르고 Android 10 이상에서 **주변 PC 직접 연결**을 여세요. Bluetooth에서 찾은 Wi‑Fi 이름을 선택하거나 PC에 표시된 이름을 직접 입력한 뒤 임시 암호를 입력합니다. Android의 Wi‑Fi 연결을 승인한 뒤 양쪽의 6자리 코드를 확인합니다. Windows에서 **공유기 없는 연결 종료**를 누르면 임시 네트워크와 연결이 닫힙니다. 갤럭시 탭 `SM-X736N`에서 이름과 암호를 직접 입력해 HDR10+ 샘플을 재생했습니다. 이 테스트에서 Bluetooth 자동 검색은 확인되지 않았습니다.

WireGuard 등으로 연결한 경우, 송출 PC의 **내부망 재생**에서 VPN 쪽 사설 IPv4 주소를 선택하세요. 수신 기기가 다른 VPN 서브넷에 있으면 **VPN 연결 허용 범위**에 수신 기기의 사설 CIDR을 입력하고 **주소 적용 / 다시 시작**을 누릅니다. 수신 기기에서는 PC의 VPN 주소를 직접 입력할 수 있습니다. 목록으로 찾으려면 Android의 **VPN에서 PC 찾기** 또는 Windows 수신기의 **대역 검색**에 `10.8.0.0/24`처럼 사설 CIDR을 입력하세요. 검색은 `/24`부터 `/32`까지의 IPv4 대역에서 LumaPlayer에만 요청하며, 연결에는 기존 6자리 확인이 필요합니다.

Android에서 영상을 한 번 누르면 하단 탐색바와 재생 버튼이 나타납니다. 영상의 왼쪽이나 오른쪽을 두 번 탭하면 5초 뒤로 또는 앞으로 이동합니다. 같은 쪽을 더 누르면 한 번마다 5초를 더합니다. 세로에서는 영상 아래 챕터를 선택할 수 있습니다. 영상에서 위로 밀거나 전체화면 버튼을 누르면 가로 전체화면을 요청합니다. 전체화면에서 아래로 밀거나 종료 버튼을 누르면 이전 화면으로 돌아옵니다.

Android의 재생 설정에서 **자동**, **원본**, PC가 제공하는 **1080p·720p·480p** 중에서 선택할 수 있습니다. 자막 싱크, 건너뛰기 간격, 전체화면 비율, 재생 속도와 오디오 트랙도 조절할 수 있습니다. PC에서 **내부망 재생 → 선택 기기 재생 설정**을 열면 화질, HDR, 건너뛰기 간격, 화면 비율과 자막 싱크를 해당 연결에 적용할 수 있습니다.

**화질 프리셋**의 기본값은 **최대 화질**입니다. 원본으로 시작하며 원본에 있는 HDR도 전달합니다. **균형**은 최대 1080p SDR, **데이터 절약**은 최대 480p SDR로 변환합니다. 자동 화질은 원본 디코더를 사용할 수 없을 때 PC가 제공하는 호환 스트림을 시도합니다. AV1 원본 재생에는 수신기의 AV1 디코더가 필요합니다. [Android 지원 형식](https://developer.android.com/media/media3/exoplayer/supported-formats)

태블릿과 펼친 폴더블에서는 사용 가능한 창 폭에 맞춰 영상 옆에 챕터와 재생 정보를 배치합니다. 영상이 재생 중일 때 홈으로 나가면 PiP가 지원되는 기기에서 작은 화면을 엽니다. 화면을 끄거나 PiP를 사용할 수 없는 환경에서는 재생 알림으로 오디오를 조작할 수 있습니다. [Android PiP](https://developer.android.com/develop/ui/views/picture-in-picture)

로컬 MP4, M4V, MOV, MKV, WebM, TS, M2TS의 **원본** 모드는 파일을 그대로 보냅니다. HDR·Dolby Vision·공간 음향 재생은 원본과 수신기의 디코더 및 출력 장치에 따릅니다. 변환 화질은 H.264와 AAC 스테레오로 보내며 HDR 영상은 SDR로 변환합니다. Blu-ray는 PC에서 본편을 읽는 동안 PC 재생을 잠시 멈춥니다. Android는 본편 탐색과 챕터 이동을 지원합니다. 원본 영상에서 탐색하면 키프레임 기준으로 시작할 수 있습니다. 메뉴 조작은 PC에서 합니다. **원본 음성**은 호환되는 경로에서 Dolby Digital Plus Atmos 등 원본 형식을 전달합니다. [Android Media3 지원 형식](https://developer.android.com/media/media3/exoplayer/supported-formats)

Android의 **무선 화면 · Smart View**는 시스템 무선 화면 선택을 엽니다. Windows의 **Windows 무선 디스플레이**에서는 송출 또는 수신 설정을 열 수 있습니다. 보호 영상의 출력은 연결된 화면의 보안 출력 지원에 따릅니다. [Samsung Smart View](https://www.samsung.com/us/support/answer/ANS10001896/) · [Windows 무선 디스플레이](https://support.microsoft.com/en-us/windows/hardware/display-graphics/screen-mirroring-and-projecting-to-your-pc-or-wireless-display)

송출은 선택한 사설 IPv4 주소에서 수신하고, 해당 로컬 서브넷 또는 지정한 VPN 허용 범위의 수신기를 승인합니다. 확인 코드는 연결의 TLS 공개키와 두 기기가 생성한 난수에 묶입니다. 새 기기는 PC에서 승인한 뒤에만 재생 토큰을 받습니다. 기억한 기기 정보는 Windows DPAPI와 Android Keystore로 보호합니다. Android의 페어링 화면은 보안 창을 사용합니다. 재생 중에는 보안 영상 표면이 스크린샷·화면 녹화·비보안 전송에서 영상 부분을 가리고 조작 UI는 표시합니다. [Android 보안 창](https://developer.android.com/security/fraud-prevention/activities#flag_secure) · [보안 SurfaceView](https://developer.android.com/reference/android/view/SurfaceView#setSecure(boolean))

PC가 목록에 없으면 Wi‑Fi와 PC 주소를 확인하고 직접 입력으로 연결해 보세요. 게스트 네트워크의 기기 격리, Windows 방화벽, V3 같은 보안 프로그램에서 연결이 막힐 수 있습니다. 필요한 경우 설치된 `lumaplayer-mobile-host.exe`의 `48762` 포트를 해당 로컬 서브넷에 한해 허용하세요. 검색에는 UDP를, 페어링과 재생에는 TCP를 사용합니다.

## English

The Windows PC reads Blu-ray discs. Android and other Windows PCs on the same private network can receive playback. Android's caption button shows the host's live caption status. Devices with playback control can start or retry automatic captions. Android also receives prepared and imported PC subtitles and can open SRT, VTT, ASS, or SSA files stored on the device.

1. On the host PC, select **내부망 재생 → 기기 검색 켜기**. Check **시작 시 기기 검색 켜기** to start discovery on future launches. Connect the receiving device to the same private network.
2. Install the APK on Android 9 or later and tap **PC 검색**. Turn on **PC 자동 검색** to refresh the list while that screen is open. On Windows, open **내부망 재생 → PC에서 받기**. Select a discovered PC or enter the private IP shown under **내부망 재생** on the host. The default port is `48762`.
3. Choose how the next connection should work. Compare the six-digit code, confirm it on the receiving device, and approve on the host PC. If you opt to remember the device on both screens, select it to reconnect next time.
4. Open a local video or the main Blu-ray title on the host. Start sharing from the PC with **공유 시작**, or enable **연결된 기기에서 현재 미디어 재생 시작 허용** on the PC and tap **Start playback** on Android. After the PC changes the source or Blu-ray title, or stops sharing, enable the permission again to start from Android.

Use **내부망 재생** on the host to view connected and remembered devices. Turn on **연결된 기기에서 PC 플레이어 조작 허용** after connecting to control the host player from a receiver. Disconnect or forget a selected device. A forgotten device must confirm a new code and obtain PC approval on its next connection. Choose **기기 검색 끄기** to stop sharing and close the listener for new connections.

If a shared network is unavailable, choose **공유기 없이 휴대폰 연결** on Windows and open **주변 PC 직접 연결** on Android 10 or later. Select the Wi‑Fi name found over Bluetooth or enter the name displayed on the PC, then enter the temporary password. Approve the Android Wi‑Fi connection and compare the six-digit code. Use **공유기 없는 연결 종료** on Windows to close the temporary network and its connections. A Galaxy Tab `SM-X736N` played the HDR10+ fixture after manual Wi‑Fi entry. Bluetooth discovery was unverified on this setup.

On a WireGuard or similar VPN, select the VPN-side private IPv4 address under **내부망 재생** on the host. If the receiver is on a different VPN subnet, enter the receiver's private CIDR under **VPN 연결 허용 범위** and apply the address again. Enter the host's VPN address directly on the receiver, or discover it using **VPN에서 PC 찾기** on Android or **대역 검색** on Windows. Enter a private IPv4 CIDR such as `10.8.0.0/24`. Discovery sends LumaPlayer-specific requests within an explicitly entered `/24` through `/32`. The six-digit pairing check still applies.

On Android, tap the video to show playback controls and the bottom seek bar. Double-tap the left or right side to move back or forward five seconds. Each additional tap on the same side adds five seconds. In portrait, select a chapter below the video. Swipe up on the video or tap fullscreen to request landscape fullscreen. Swipe down or tap the exit button to return to the previous view.

Android playback settings offer Auto, Original, and the 1080p, 720p, or 480p limits the PC can provide. Adjust subtitle timing, skip intervals, fullscreen fit, playback speed, and audio tracks there. The host PC can apply quality, HDR, skip interval, fullscreen fit, and subtitle timing to an active receiver under **내부망 재생 → 선택 기기 재생 설정**.

The default quality preset is **Maximum**. It starts with the original stream and preserves source HDR. **Balanced** converts to up to 1080p SDR and **Data Saver** converts to up to 480p SDR. Auto quality tries a compatible stream when the receiver cannot use its decoder for the original. Original AV1 playback requires an AV1 decoder on the receiver. [Android supported formats](https://developer.android.com/media/media3/exoplayer/supported-formats)

On tablets and unfolded devices, the app uses the available window width to decide when to place chapters and playback details beside the video. Leaving for Home during playback opens PiP on devices that support it. The playback notification controls audio while the screen is off or PiP is unavailable. [Android PiP](https://developer.android.com/develop/ui/views/picture-in-picture)

Original mode sends local MP4, M4V, MOV, MKV, WebM, TS, and M2TS files unchanged. HDR, Dolby Vision, and spatial audio depend on the source, receiver decoder, and output device. Converted qualities use H.264 with stereo AAC and tone-map HDR to SDR. The PC pauses its own playback while reading the main Blu-ray title. Android supports title seeking and chapter navigation. Original video may seek to a keyframe. Use the desktop player for disc menus. **원본 음성** preserves formats such as Dolby Digital Plus Atmos on compatible paths. [Android Media3 supported formats](https://developer.android.com/media/media3/exoplayer/supported-formats)

**무선 화면 · Smart View** opens Android's wireless display picker. **Windows 무선 디스플레이** opens sender or receiver settings on Windows. Protected video requires secure output on the connected display. [Samsung Smart View](https://www.samsung.com/us/support/answer/ANS10001896/) · [Windows wireless display](https://support.microsoft.com/en-us/windows/hardware/display-graphics/screen-mirroring-and-projecting-to-your-pc-or-wireless-display)

The stream listens on the selected private IPv4 address and accepts receivers from its local subnet or the configured VPN range. The confirmation code is bound to the TLS public key and randomness from both devices. A new device receives a playback token after PC approval. Windows DPAPI and Android Keystore protect remembered device information. Android secures the pairing window. During playback, the secure surface hides the video in screenshots, recordings, and non-secure presentation while leaving the playback controls visible. [Android secure windows](https://developer.android.com/security/fraud-prevention/activities#flag_secure) · [Secure SurfaceView](https://developer.android.com/reference/android/view/SurfaceView#setSecure(boolean))

If a PC is missing from the list, check Wi-Fi and the PC address and try entering it directly. Guest-network isolation, Windows Firewall, and security software such as V3 can prevent access. If a rule is needed, scope port `48762` for the installed `lumaplayer-mobile-host.exe` to that local subnet. Discovery uses UDP. Pairing and playback use TCP.
