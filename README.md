# LumaPlayer

## 한국어

Blu-ray 메뉴를 열고, 보고 싶은 장면으로 이동하고, 외국어 대사를 내 언어로 읽을 수 있는 오픈소스 플레이어를 찾기 어려웠습니다. LumaPlayer는 그 필요에서 시작했습니다. 디스크를 넣고 감상하는 흐름 안에서 영상, 자막, 번역을 함께 다룰 수 있게 만들고 있습니다.

### LumaPlayer로 할 수 있는 일

- **디스크를 감상하세요.** Blu-ray 메뉴에서 본편과 챕터를 고르고, 마지막으로 본 위치에서 이어볼 수 있습니다. 일반 동영상 파일도 열 수 있습니다.
- **듣고 읽고 이해하세요.** 일본어 원문 아래에 한글 독음과 한국어 번역을 함께 띄울 수 있습니다. 가지고 있는 자막 파일도 열 수 있고, 글꼴과 크기, 글자·배경 투명도를 조절할 수 있습니다.
- **감상 전에 자막을 준비하세요.** 전체 영상의 자막과 번역을 저장해 두면, 다시 열거나 다른 장면으로 이동할 때 준비된 자막을 바로 쓸 수 있습니다.
- **다른 기기에서 보세요.** Android 또는 Windows에서 같은 내부망의 LumaPlayer PC를 찾아 연결하세요. 첫 연결은 양쪽의 코드를 확인해 송출 PC에서 승인합니다. 연결된 기기는 PC에서 확인하고 해제할 수 있습니다. [내부망 재생 안내](docs/LAN-PLAYBACK.md)
- **새 버전을 확인하세요.** Windows 앱은 실행할 때 GitHub에서 업데이트를 확인합니다. 설정에서 수동으로 확인하거나 자동 알림을 끌 수 있습니다. [업데이트 안내](docs/UPDATES.md)
- **내 화면에 맞춰 감상하세요.** 전체화면이나 항상 위에 뜨는 작은 화면(PiP)으로 볼 수 있습니다. 탐색 바를 클릭하거나 방향키로 장면을 이동하고, 다크·라이트 모드를 고를 수 있습니다.

### Windows 설치

릴리스에서 Windows x64 기본 설치 파일 또는 NVIDIA CUDA 포함 설치 파일을 받아 실행하세요. 기본 Turbo 음성 인식 모델이 포함되며, 첫 실행에서 GPU·NPU에 맞는 선택 가속 패키지를 확인할 수 있습니다. 권장 설치는 full-v3 Q5와 Hy-MT2 한국어 번역 모델을 자동으로 내려받습니다. 추가 다운로드는 약 5.71 GB입니다. [릴리스 다운로드](https://github.com/mk0000001/luma-player/releases) · [설치·모델 선택 안내](docs/WINDOWS-INSTALLER.md)

시작 메뉴에서 **LumaPlayer**를 열고 **Blu-ray 재생**을 누르거나 영상 파일을 여세요. **AI 자막**에서 음성 언어와 표시할 줄을 고릅니다. 처음 설치한 경우 기본 인식 모델은 Turbo입니다. 설치한 full-v3 Q5는 **음성 인식 모델 선택**에서 고를 수 있습니다.

### 최소·권장 사양과 실제 테스트

Windows 11 x64에서 1080p H.264 재생과 full-v3 Q5 음성 인식 + Hy-MT2-7B 한국어 번역의 부하를 따로 쟀습니다. 아래 최소 사양은 한 대의 실측을 바탕으로 한 추정치입니다. [측정값과 계산 근거](docs/testing/2026-11-12-beta5-performance-security.md)

| 사용 방식 | 추정 최소 | 권장 |
| --- | --- | --- |
| 1080p 재생·준비된 자막 | 4코어 CPU, RAM 8 GB, H.264 하드웨어 디코딩 GPU, SSD 여유 8 GB | 4코어 이상, RAM 16 GB, 하드웨어 디코딩 GPU, SSD 여유 8 GB |
| 실시간 원문 인식 + 한국어 번역 | 6코어 CPU, RAM 16 GB, NVIDIA CUDA GPU VRAM 10 GB, SSD 여유 16 GB | Core Ultra 9 185H급, RAM 32 GB, RTX 4080 Laptop급 이상과 VRAM 12 GB 이상, SSD 여유 16 GB |

테스트 PC는 Windows 11 Home x64 빌드 26300, Intel Core Ultra 9 185H(14코어·20스레드), RAM 32 GB, RTX 4080 Laptop GPU 12 GB(드라이버 617.42), SOLIDIGM NVMe SSD, PIONEER BD-ROM BDCTD03RS USB 드라이브입니다. Blu-ray에는 읽을 수 있는 광학 드라이브가 필요합니다.

**츠키 무도관 Blu-ray (TUKI NIPPON BUDOKAN BD)**로 메뉴, ALL PLAY 본편 재생, 탐색과 자막 표시를 테스트했습니다. [버전별 검증 기록](https://github.com/mk0000001/luma-player/releases)

**권장 사양에 못 미치면 재생 전에 자막을 먼저 생성하세요.** **AI 자막 → 재생 전 전체 자막 만들기**에서 파일이나 Blu-ray 타이틀을 선택합니다. 생성이 끝나면 **미리 만든 자막 프로젝트 열기**로 열어 감상하세요. [미리 생성·이어하기 안내](docs/PREPARE-CAPTIONS.md)

### 문제가 생기면

| 증상 | 확인할 곳 |
| --- | --- |
| 재생 시작이나 탐색 뒤 잠시 멈춤 | 실시간 자막은 먼저 10초 분량을 준비하고 이후 60초 앞까지 채웁니다. AI 자막의 준비 상태를 확인하세요. 반복해서 기다리게 되면 전체 자막을 미리 만들어 사용하세요. |
| 하단에 CPU 표시가 뜸 | 하단은 영상 디코딩 상태를 표시합니다. 음성 인식·번역에 실제 사용한 장치는 AI 자막 탭에서 확인할 수 있습니다. |
| 한국어 번역이 안 나옴 | **한국어 번역**을 켜고 **번역 모델 선택**에서 설치된 Hy-MT2 파일을 고르세요. |
| Blu-ray 메뉴나 재생이 열리지 않음 | **새로 확인**으로 디스크를 감지한 뒤 **Blu-ray 재생**을 눌러 보세요. 계속 실패하면 개발자 모드의 진단에서 첫 오류를 확인하세요. |

[재생·자막·단축키 안내](docs/CAPTIONS-AND-PLAYBACK.md) · [번역 모델 안내](docs/INSTALL-TRANSLATION.md)

### 기술과 개발

LumaPlayer는 C++20과 Qt 6으로 만든 네이티브 앱입니다. Blu-ray 메뉴와 재생에는 libVLC, 일반 미디어에는 libmpv를 사용합니다. Whisper 계열 모델이 음성을 인식하고, 로컬 번역 모델이 인식된 텍스트와 문맥을 읽어 번역합니다. Windows를 먼저 개발하며 Linux 개발과 검증은 저장소에서 이어갑니다.

[구현 결정](docs/DECISIONS.md) · [선택 NPU 설정](docs/INSTALL-KOTOBA-NPU.md) · [라이선스](LICENSE) · [포함 라이브러리와 고지](THIRD_PARTY_NOTICES.md)

## English

We wanted an open-source Blu-ray player that could open disc menus, jump to a favorite scene, and show dialogue in a language we could read. That need started LumaPlayer. It brings video, captions, and translation into the same viewing experience.

### What you can do with LumaPlayer

- **Watch your discs.** Choose the main feature or a chapter from the Blu-ray menu, pick up where you left off, and open regular video files too.
- **Listen, read, and understand.** Show the Japanese original, its Hangul pronunciation, and a Korean translation together. Load your own subtitles and adjust the font, size, or text and background opacity.
- **Prepare captions before watching.** Save captions and translation for a whole title, then reuse the completed project when you reopen it or seek to another scene.
- **Watch on another device.** Find a LumaPlayer PC on the same network from Android or Windows. Compare the code on both screens and approve the first connection on the host PC. View and disconnect paired devices on the PC. [LAN playback guide](docs/LAN-PLAYBACK.md)
- **Check for a new version.** The Windows app checks GitHub at launch. Check manually or turn off automatic notifications in settings. [Update guide](docs/UPDATES.md)
- **Make it fit your screen.** Switch to fullscreen or always-on-top picture-in-picture. Click the seek track or use the arrow keys to jump, and choose dark or light mode.

### Install on Windows

Run the standard or NVIDIA CUDA-bundled Windows x64 installer from the release. Both include the default Turbo model and offer hardware-matched acceleration packages at first launch. The recommended installation downloads full-v3 Q5 and the Hy-MT2 Korean translation model, totaling about 5.71 GB. [Release downloads](https://github.com/mk0000001/luma-player/releases) · [Installation and model selection](docs/WINDOWS-INSTALLER.md)

Open **LumaPlayer** from the Start Menu and select **Blu-ray 재생** or open a video file. Choose the spoken language and caption lines in **AI 자막**. Fresh installations use Turbo by default. To use full-v3 Q5, select it in **음성 인식 모델 선택**.

### Estimated minimum, recommended, and tested hardware

We measured 1080p H.264 playback and live full-v3 Q5 recognition plus Hy-MT2-7B Korean translation separately on Windows 11 x64. The minimums below are estimates based on one measured machine. [Measurements and derivation](docs/testing/2026-11-12-beta5-performance-security.md)

| Workload | Estimated minimum | Recommended |
| --- | --- | --- |
| 1080p playback with prepared captions | 4-core CPU, 8 GB RAM, hardware H.264 decode, 8 GB free SSD space | 4+ cores, 16 GB RAM, hardware video decode, 8 GB free SSD space |
| Live recognition and Korean translation | 6-core CPU, 16 GB RAM, NVIDIA CUDA GPU with 10 GB VRAM, 16 GB free SSD space | Core Ultra 9 185H class, 32 GB RAM, RTX 4080 Laptop class or faster with 12 GB+ VRAM, 16 GB free SSD space |

The test PC runs Windows 11 Home x64 build 26300 with a Core Ultra 9 185H (14 cores and 20 threads), 32 GB RAM, RTX 4080 Laptop GPU with 12 GB VRAM (driver 617.42), a SOLIDIGM NVMe SSD, and a PIONEER BD-ROM BDCTD03RS USB drive. Blu-ray playback requires a compatible optical drive.

Menu navigation, ALL PLAY, seeking, and captions were tested using **TUKI NIPPON BUDOKAN BD**. [Version-specific verification records](https://github.com/mk0000001/luma-player/releases)

**If your system falls below the recommendation, prepare captions before playback.** Choose a file or Blu-ray title in **AI 자막 → 재생 전 전체 자막 만들기**, then open the completed project with **미리 만든 자막 프로젝트 열기**. [Preparation and resume guide](docs/PREPARE-CAPTIONS.md)

### Troubleshooting

| Symptom | What to check |
| --- | --- |
| Playback waits at startup or after a seek | Live captions first prepare 10 seconds of media, then work toward 60 seconds ahead. Check the AI caption tab. Use a completed project if waits keep recurring. |
| CPU appears in the playback bar | The bar shows video decoding. The AI caption tab displays the devices used for recognition and translation. |
| Korean translation is missing | Enable **한국어 번역** and select the installed Hy-MT2 file with **번역 모델 선택**. |
| Disc menu or playback fails to open | Refresh disc detection with **새로 확인**, then retry **Blu-ray 재생**. If it still fails, check the first error in developer diagnostics. |

[Playback, captions, and shortcuts](docs/CAPTIONS-AND-PLAYBACK.md) · [Translation models](docs/INSTALL-TRANSLATION.md)

### Technology and development

LumaPlayer is a native C++20 and Qt 6 application. It uses libVLC for Blu-ray menus and playback and libmpv for media files. Local Whisper-family models recognize speech, and local text models translate the recognized text with context. Windows development comes first, with Linux development and verification tracked in the repository.

[Engineering decisions](docs/DECISIONS.md) · [Optional NPU setup](docs/INSTALL-KOTOBA-NPU.md) · [License](LICENSE) · [Bundled libraries and notices](THIRD_PARTY_NOTICES.md)
