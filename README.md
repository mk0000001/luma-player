# LumaPlayer

## 한국어

C++20/Qt 6으로 만드는 오픈소스 네이티브 Blu-ray·미디어 플레이어입니다. **Windows x64 먼저, Linux 다음** 순서로 개발합니다. 현재 대상은 `0.1.0-alpha.2` 프리뷰이며 [릴리스 검증 상태](docs/releases/0.1.0-alpha.2.md)를 공개합니다. 팟플레이어 전체 기능, 모든 보호 디스크, 인증 Dolby Vision 출력을 완료한 제품은 아닙니다.

- **Blu-ray:** libVLC 메뉴·리모컨·타이틀·챕터. 현재 TUKI 보호 HDMV 디스크에서 메뉴·본편 영상·LPCM, 25초 연속 진행과 네 번의 탐색·챕터·메뉴 전환을 확인했습니다. 안정화 기본값은 소프트웨어 디코딩과 10초 디스크 캐시입니다.
- **파일:** libmpv GPU Next, AV1 등 엔진 지원 코덱, 파일 출력 GPU 선택. HDR/Dolby Vision 처리 경로와 실제 출력 검증은 구분합니다.
- **로컬 자막:** 원문 인식, 일본어의 한글 독음, 선택 한국어 번역을 별도 데이터로 표시합니다. 실행 중 외부 API·계정·Python이 필요하지 않습니다.
- **화면:** 기본 다크/선택 라이트, 자체 SVG 로고·컨트롤, Noto Sans, 약 74px 전체화면 바와 5초 유휴 숨김. 진단은 개발자 모드에서 표시합니다.
- **선택 프레임 보간:** 파일용 디스플레이 동기 혼합과 실험적 CPU 광학 흐름(24~360 fps **목표**). 360 fps 실시간 표시를 보장하지 않습니다.

### 실행과 선택 번역 모델

[GitHub Releases](https://github.com/mk0000001/luma-player/releases)에서 공개된 Windows x64 포터블 ZIP을 확인하세요. 전체 폴더를 풀고 `LumaPlayer.exe`를 실행합니다. 실행 파일과 `runtime` 폴더를 함께 유지해야 합니다.

포터블 **코어**에는 원문 음성인식 모델과 번역 실행기가 들어가며, 약 **2.50 GB(2.33 GiB)**인 Qwen3-4B 번역 모델은 별도 설치합니다. 모델이 없어도 재생·원문·한글 독음을 사용할 수 있습니다. 압축을 푼 플레이어 폴더에서 다음 명령을 실행합니다.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\install-translation.ps1
```

설치 시에만 인터넷을 사용하고 파일 크기·SHA-256을 확인합니다. [설치 안내](docs/INSTALL-TRANSLATION.md)와 [번역 장치·성능·라이선스](docs/local-translation.md)를 참고하세요. 전체 모델을 이미 넣은 로컬 포터블 폴더에서는 다시 받을 필요가 없습니다.

파일을 열거나 **Blu-ray 재생**을 누른 뒤 **AI 자막** 탭에서 자동자막과 원문·독음·번역 표시를 선택합니다. **표시 자막 저장**으로 현재 선택한 내용을 SRT로 내보냅니다.

AI는 15초 구간으로 인식하며 재생 전·탐색 후 최소 10초 분량을 준비하고 약 30~45초 앞까지 채웁니다. 시작 안내·디스크 메뉴는 자막을 기다리지 않습니다. 수동 일시정지를 유지하며, 실패 시 제한된 재시도 후 재생을 계속할 수 있습니다. 설정의 영상 출력 GPU와 AI 장치 선택은 별개이고 실제 사용 백엔드를 표시합니다. 노래·반주·관객 소리, 한자 읽기와 기계 번역에는 오류가 있을 수 있습니다.

Space: 재생/일시정지 · Ctrl+O: 파일 · Ctrl+D: 디스크 · F11: 전체화면 · Esc: 전체화면 해제 · 영상에 포커스가 있을 때 Shift+←/→: 10초 탐색, 방향키/Enter: 디스크 메뉴.

### 보호 콘텐츠와 출력 상태

AACS/BD+ 라이브러리 연결, Windows 캡처 제외, 인증 DRM/HDCP 출력은 서로 다른 기능입니다. 키·인증서·상용 디스크 영상·음성·메뉴 이미지·생성 자막은 배포하지 않습니다. TUKI 한 장의 성공이 모든 디스크의 호환성을 뜻하지 않습니다.

보호 디스크에서는 지원되는 Windows의 캡처 제외를 본창과 자막 창에 요청하고 적용 상태를 표시합니다. 모든 캡처 경로의 차단이나 DRM 인증을 뜻하지 않으며 최종 네이티브 QA는 [릴리스 기록](docs/releases/0.1.0-alpha.2.md)에서 확인합니다. HDR/Dolby Vision 실물 출력·무손실 파일의 최종 검증도 같은 기록에서 통과 여부를 구분합니다.

### 소스에서 빌드

Windows: Visual Studio 2022 C++ Build Tools, Windows SDK, CMake 3.25+, Ninja, Git, PowerShell 7이 필요합니다. 의존성 다운로드에는 인터넷과 충분한 디스크 공간이 필요합니다.

```powershell
git clone https://github.com/mk0000001/luma-player.git
cd luma-player
pwsh -File scripts/bootstrap-windows.ps1
pwsh -File scripts/build-local-ai.ps1
pwsh -File scripts/bootstrap-translation.ps1 -SkipModel -SkipOpenVino
pwsh -File scripts/build-windows.ps1 -BuildDirectory build-next
pwsh -File scripts/stage-runtime.ps1 -Destination build-next -SkipTranslationModel
.\.deps\qt\bin\windeployqt.exe --release --no-translations build-next\LumaPlayer.exe
.\build-next\LumaPlayer.exe
```

위 구성은 확인된 Vulkan/CPU 번역 실행기와 별도의 ASR NPU 실행기를 준비합니다. 번역용 OpenVINO를 생략해도 ASR NPU는 제거되지 않습니다. 선택 번역 모델까지 개발 폴더에 받으려면 `bootstrap-translation.ps1 -SkipOpenVino`를 사용합니다. URL·체크섬은 [공통 잠금](dependencies.lock.json)과 [번역 잠금](translation-dependencies.lock.json)에 고정합니다.

Linux 개발 빌드:

```sh
sudo apt install cmake ninja-build g++ qt6-base-dev libvlc-dev libmpv-dev ffmpeg
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
./build/LumaPlayer
```

Linux CI는 컴파일·자동 검사 범위입니다. 광학 드라이브·오디오·X11/XWayland·네이티브 Wayland/HDR 실측과 패키징은 후속 작업이며 macOS는 현재 대상이 아닙니다.

### 기록과 라이선스

[기능 대응표](docs/FEATURES.md) · [변경 기록](CHANGELOG.md) · [구현 결정](docs/DECISIONS.md) · [기술 근거](docs/research/engine-decisions.md) · [버전 정책](docs/VERSIONING.md) · [제3자 고지](THIRD_PARTY_NOTICES.md)

애플리케이션은 **GPL-3.0-or-later**입니다. 의존성은 각 라이선스를 따릅니다. 수정하지 않은 Noto 글꼴은 SIL OFL 1.1과 저작권 고지를 함께 보존해 앱에 포함합니다. 선택 Qwen3-4B 모델은 Apache 2.0입니다. 상용 플레이어의 비공개 코드·DLL·디자인 자산을 포함하지 않습니다.

## English

LumaPlayer is an open-source native C++20/Qt 6 Blu-ray/media player, **Windows x64 first, Linux next**. The current target is `0.1.0-alpha.2`; see its [validation status](docs/releases/0.1.0-alpha.2.md). Full PotPlayer parity, universal protected-disc compatibility and certified Dolby Vision output are unfinished.

Current work includes libVLC disc menus/title/chapter controls, libmpv GPU Next file playback and output-adapter selection, local original-language captions/Japanese Hangul readings/optional Korean translation, dark/light custom controls, Noto fonts, and a compact fullscreen bar that hides after five idle seconds. File-only motion options are display-synchronized temporal blending and experimental CPU optical flow with a 24–360 fps processing target.

The current protected TUKI HDMV disc passed a fresh 25-second main-title run, four seeks, chapter/menu transitions, picture and LPCM audio with the software decoder and a ten-second disc cache. This is a bounded observation on one disc. Earlier local builds exhibited a freeze around 20 seconds; hardware-decoder surface failures remain documented.

### Run and install optional translation

Check [GitHub Releases](https://github.com/mk0000001/luma-player/releases) for published Windows x64 archives. Extract the complete portable folder and launch `LumaPlayer.exe`, keeping its runtime beside it. The portable **core** includes ASR and translation runtimes but omits the approximately **2.50 GB/2.33 GiB** translation model. Playback, source captions and Hangul readings work without that optional model.

From the extracted folder:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\install-translation.ps1
```

Installation downloads and verifies the pinned model; subsequent inference is local and needs no external API account/key or Python. See the [installer guide](docs/INSTALL-TRANSLATION.md) and [translation results](docs/local-translation.md). An existing valid full-model installation is reused.

Open a file or disc, then enable automatic captions and choose source/reading/translation in the **AI 자막** tab. **표시 자막 저장** exports the selected display text to SRT.

ASR uses 15-second chunks, prepares at least ten seconds before playback/after seeking, and works around 30–45 seconds ahead. Disc First Play and menus continue without a caption hold. Manual pause is respected; retries are bounded. Video output GPU and AI device settings are separate. Reported backends distinguish NPU encoder/CPU decoder, selected GPU, and CPU fallback. Songs, pronunciation and machine translation can be inaccurate.

Space toggles playback; Ctrl+O opens files; Ctrl+D opens a disc; F11 enters fullscreen and Esc exits. With video focus, Shift+Left/Right seeks ten seconds and arrows/Enter navigate disc menus.

### Build and limits

Windows requires Visual Studio 2022 C++ Build Tools, Windows SDK, CMake 3.25+, Ninja, Git and PowerShell 7. Use the complete Windows commands in the Korean section: bootstrap dependencies, build native AI, bootstrap translation with `-SkipModel -SkipOpenVino`, build/stage `build-next`, deploy Qt, and run. Skipping translation OpenVINO does not remove the separate ASR NPU runtime. Source URLs/hashes are pinned in the two dependency locks. The Linux commands above exercise the development build; native optical-drive/display testing and packaging remain pending. macOS is outside the current target.

AACS/BD+ reading, Windows capture exclusion and certified DRM/HDCP output are separate. No private keys, certificates, commercial disc media, menu images or generated commercial transcripts are shipped. Capture policy covers the application and caption windows but is not an absolute capture guarantee. Final capture, HDR/lossless, package and hardware validation status belongs in the [release record](docs/releases/0.1.0-alpha.2.md).

See the [capability matrix](docs/FEATURES.md), [changelog](CHANGELOG.md), [decisions](docs/DECISIONS.md), [research](docs/research/engine-decisions.md), [version policy](docs/VERSIONING.md) and [third-party notices](THIRD_PARTY_NOTICES.md). The application is GPL-3.0-or-later; unchanged Noto fonts retain SIL OFL 1.1 notices and the optional Qwen3-4B model is Apache 2.0. Other dependencies keep their own licenses.
