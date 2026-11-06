# LumaPlayer

## 한국어

C++20/Qt 6으로 만드는 오픈소스 네이티브 Blu-ray·미디어 플레이어입니다. **Windows x64 먼저, Linux 다음** 순서로 개발합니다. 현재 대상은 `0.1.0-alpha.6` 프리뷰입니다. 구현과 실제 확인 범위는 [자막·재생 안내](docs/CAPTIONS-AND-PLAYBACK.md), 새 인코더·오류 처리 검증은 [릴리스 기록](docs/releases/0.1.0-alpha.6.md)에서 구분합니다. 모든 보호 디스크의 호환성이나 인증 Dolby Vision 출력을 보장하지 않습니다.

선택 Kotoba 모델용 C++ NPU 인코더 변환·설치·검증 도구를 제공합니다. [NPU 준비 안내](docs/INSTALL-KOTOBA-NPU.md)와 [성능·제한 기록](docs/PERFORMANCE-KOTOBA.md)을 확인하세요. 현재 개발 PC의 구형 NPU 드라이버에서는 전체 인코더 컴파일이 30분 안에 끝나지 않아 NPU 실제 추론은 미통과이며, CUDA 경로를 유지합니다.

- **Blu-ray:** libVLC 메뉴·리모컨·타이틀·챕터. 기본값은 소프트웨어 디코딩과 10초 디스크 캐시입니다. 한 디스크의 관찰을 전체 호환성으로 확대하지 않습니다.
- **파일:** libmpv GPU Next, AV1 등 엔진 지원 코덱, 파일 출력 GPU 선택. HDR/Dolby Vision 처리 경로와 실제 출력 검증은 구분합니다.
- **로컬 자막:** 원문·일본어 한글 독음·한국어 번역을 분리하고, 긴 자막을 짧은 시간 페이지와 화면 폭에 맞게 표시합니다. 최소 10초 준비 후에도 60초 앞까지 계속 인식합니다. 실행 중 외부 API·계정·Python이 필요하지 않습니다.
- **외부 자막·이어보기:** SRT/ASS/SSA/VTT/SMI, 같은 이름 자막 자동 검색, 불러오기 실패 시 현재 언어의 AI 전환. 파일별·디스크 본편 타이틀별 위치를 로컬에 저장합니다.
- **화면:** 기본 다크/선택 라이트, 자체 컨트롤과 Noto Sans. 영상 위의 74 px 전체화면 바는 영상 크기를 바꾸지 않고 5초 비활동 시 커서와 함께 숨습니다. alpha.4에는 이미 포커스된 VLC 영상에서 정지 포인터의 가운데 클릭이 재생바를 복원하지 못하는 [알려진 문제](docs/releases/0.1.0-alpha.4.md)가 남았습니다. alpha.5에서 창/전체화면 우클릭·가운데 클릭 복원과 유휴 숨김을 실물 디스크로 확인했습니다. 진단은 개발자 모드에서 표시합니다.
- **선택 프레임 보간:** 파일용 디스플레이 동기 혼합과 실험적 CPU 광학 흐름(24~360 fps **목표**). 360 fps 실시간 표시를 보장하지 않습니다.

### 실행과 선택 번역 모델

[GitHub Releases](https://github.com/mk0000001/luma-player/releases)에서 공개된 Windows x64 포터블 ZIP을 확인하세요. 전체 폴더를 풀고 `LumaPlayer.exe`를 실행합니다. 실행 파일과 `runtime` 폴더를 함께 유지해야 합니다.

두 포터블 구성 모두 Whisper large-v3-turbo Q5 원문 음성인식 모델과 번역 실행기를 포함합니다. `windows-x64` 코어는 CUDA를 생략하고, `windows-x64-cuda`는 **음성 인식·번역 CUDA 12.4 실행기를 함께 포함**합니다. CUDA 포함판도 시스템 드라이버를 바꾸지 않으며 CPU/Vulkan 경로를 유지합니다. 번역 가중치는 두 구성 모두 선택 설치이고, 모델 없이도 재생·원문·한글 독음을 사용할 수 있습니다. 압축을 푼 플레이어 폴더에서 실행하세요.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\install-translation.ps1
```

기본 `fast`는 Qwen3-4B Q4_K_M **약 2.50 GB**입니다. 선택 `quality`는 Hy-MT2-7B Q4_K_M **약 4.624 GB**입니다.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\install-translation.ps1 -Profile quality
```

설치 시에만 인터넷을 사용하고 크기·SHA-256을 확인하며, 정상 기존 파일은 재사용합니다. 설치한 quality 모델은 별도 모델 선택을 저장하지 않은 경우 재시작 후 기본으로 선택됩니다. 이미 선택한 모델이 있다면 **번역 모델 선택**에서 파일을 직접 고르세요. CUDA를 생략한 코어에 실행기를 추가하는 방법은 [설치 안내](docs/INSTALL-TRANSLATION.md)의 `install-translation-cuda.ps1`·`install-asr-cuda.ps1`을 참고하세요. 번역 Q6·디코딩·프롬프트 비교에서는 확실한 의미 개선이 없어 기존 Q4 프로필을 유지했습니다. [비교 기록](docs/translation-quality-validation.md)에 잔여 오류를 남깁니다.

일본어 음성에는 선택 **Kotoba-Whisper v2.0 Q5** 모델(약 538 MB, Apache-2.0)을 추가로 설치할 수 있습니다. 아래 명령 후 **음성 인식 모델 선택**에서 `runtime/models/ggml-kotoba-whisper-v2.0-q5_0.bin`을 고릅니다. 다국어 기본 Turbo는 유지합니다. [모델 비교 기록](docs/asr-model-validation.md)에 실제 실행과 정확도 검증의 범위를 구분합니다.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\install-asr-model.ps1 -Profile japanese
```

파일을 열거나 **Blu-ray 재생**을 누른 뒤 **AI 자막** 탭에서 자동자막과 원문·독음·번역 표시를 선택합니다. **표시 자막 저장**으로 현재 선택한 내용을 SRT로 내보냅니다.

AI는 재생 전·탐색 후 최소 10초 분량(끝에서는 남은 분량)을 준비하고, 준비 후에도 60초 앞까지 작업을 계속합니다. 처리 단위로 목표를 조금 넘을 수 있습니다. 시작 안내·디스크 메뉴는 기다리지 않습니다. 수동 일시정지를 유지하고, 실패로 최소 버퍼를 자동 우회하지 않습니다. **자막 다시 시도** 또는 **자막 없이 계속**을 선택할 수 있습니다. 영상 출력·음성 인식·번역 장치는 각각 선택하며 실제 사용 백엔드를 표시합니다. 모델마다 지원 장치가 다르고 인식·독음·번역에는 오류가 있을 수 있습니다.

재생 화면에서 Space: 재생/일시정지 · F/Enter/F11: 전체화면 · Esc: 해제 · ←/→: 10초 탐색 · ↑/↓: 음량 5% · M: 음소거. 디스크 메뉴는 Ctrl+방향키/Ctrl+Enter 또는 마우스·화면 리모컨으로 조작합니다. 입력 위젯의 기본 키는 유지합니다. [전체 사용 안내](docs/CAPTIONS-AND-PLAYBACK.md)를 참고하세요.

### 보호 콘텐츠와 출력 상태

AACS/BD+ 라이브러리 연결, Windows 캡처 제외, 인증 DRM/HDCP 출력은 서로 다른 기능입니다. 키·인증서·상용 디스크 영상·음성·메뉴 이미지·생성 자막은 배포하지 않습니다. TUKI 한 장의 성공이 모든 디스크의 호환성을 뜻하지 않습니다.

보호 디스크에서는 지원되는 Windows 캡처 제외를 **영상과 자막 창에만** 요청합니다. 본창·설정·메뉴·재생바는 캡처할 수 있게 유지하며 적용 상태를 표시합니다. 이전 alpha.3의 실제 관찰에서 검은 영상과 보이는 본창·재생바를 확인했습니다. DRM/HDCP 인증이나 모든 캡처 경로 차단과는 별개입니다. HDR/Dolby Vision·무손실 출력, 새 CI·패키지의 최종 통과 여부는 [alpha.5 기록](docs/releases/0.1.0-alpha.5.md)에 따릅니다.

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

위 구성은 Vulkan/CPU 번역 실행기와 별도의 ASR 실행기를 준비합니다. 번역용 OpenVINO를 생략해도 ASR NPU 런타임은 제거되지 않습니다. 번역 모델까지 받으려면 `bootstrap-translation.ps1 -SkipOpenVino`에 `-ModelProfile fast` 또는 `quality`를 선택하세요. URL·체크섬은 [공통 잠금](dependencies.lock.json)과 [번역 잠금](translation-dependencies.lock.json)에 고정합니다.

소스 빌드 후 CUDA 포함 ZIP을 만들려면 다음을 실행합니다. 고정 배포물과 라이선스를 확인해 앱 폴더에 준비하며 시스템 드라이버는 설치하지 않습니다.

```powershell
pwsh -File scripts/install-asr-cuda.ps1 -Development
pwsh -File scripts/install-translation-cuda.ps1 -Development
pwsh -File scripts/package-windows.ps1 -BuildDirectory build-next -IncludeCuda
```

출력 이름은 `LumaPlayer-0.1.0-alpha.6-windows-x64-cuda.zip`입니다. `-IncludeCuda`를 생략하면 기존 `windows-x64` 코어를 만듭니다. 릴리스 워크플로의 수동 입력 `include_cuda` 기본값은 `true`이며, 최종 ZIP 크기·해시와 실행 검증은 릴리스 기록에서 확인합니다.

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

애플리케이션은 **GPL-3.0-or-later**입니다. Whisper/Turbo는 MIT, 선택 Qwen3-4B·Hy-MT2 모델은 Apache 2.0이며, 선택 CUDA는 별도 NVIDIA 원문 조건을 따릅니다. Noto는 수정 없이 SIL OFL 1.1·저작권 고지와 함께 포함합니다. 상용 플레이어의 비공개 코드·DLL·디자인 자산을 포함하지 않습니다.

## English

LumaPlayer is an open-source native C++20/Qt 6 Blu-ray/media player, **Windows x64 first, Linux next**. The current target is `0.1.0-alpha.6`. The [caption/playback guide](docs/CAPTIONS-AND-PLAYBACK.md) distinguishes implementation from observed behavior; the [release record](docs/releases/0.1.0-alpha.6.md) tracks the new encoder and error handling. Universal protected-disc compatibility and certified Dolby Vision output are unfinished.

Native C++ conversion, installation and verification tools are available for the optional Kotoba NPU encoder. See [setup](docs/INSTALL-KOTOBA-NPU.md) and [performance limits](docs/PERFORMANCE-KOTOBA.md). On the development PC's old NPU driver, full encoder compilation did not finish within 30 minutes, so genuine NPU inference remains unverified and CUDA remains available.

Features include libVLC disc menus/remote/title/chapter controls; libmpv GPU Next file playback; separate source captions, Japanese Hangul readings and Korean translation; timed long-caption pages and wrapping; SRT/ASS/SSA/VTT/SMI import with AI fallback; per-file/disc-title resume; and dark/light Noto-based controls. The floating 74 px fullscreen bar and cursor hide after five inactive seconds without moving the video, including when the pointer rests over the bar. File-only motion options target 24–360 fps processing, without promising realtime 360 fps display.

Alpha.4 retains a [known issue](docs/releases/0.1.0-alpha.4.md): a stationary middle click on already-focused VLC video after idle can fail to restore the bar. Alpha.5 verified right/middle-click restoration in windowed/fullscreen modes and idle hiding with the physical disc.

Disc defaults are software decoding and a ten-second cache. Results on one protected disc do not establish universal compatibility. Native owned-media checks, protected-disc observations and remaining limits are recorded separately from final CI/package validation.

### Run and install optional translation

Check [GitHub Releases](https://github.com/mk0000001/luma-player/releases) for published Windows x64 archives. Extract the complete folder and launch `LumaPlayer.exe`, keeping its runtime beside it. Both flavors include Whisper large-v3-turbo Q5 and the translation runtime. The `windows-x64` core omits CUDA; `windows-x64-cuda` bundles **both ASR and translation CUDA 12.4 runtimes**, retaining CPU/Vulkan alternatives and leaving system drivers unchanged. Translation weights remain optional in both; playback, source captions and Hangul readings work without them.

From the extracted folder:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\install-translation.ps1
```

The default `fast` profile downloads Qwen3-4B Q4_K_M, about **2.50 GB**. Add `-Profile quality` for Hy-MT2-7B Q4_K_M, about **4.624 GB**. Size/SHA-256 verification and valid-file reuse are enforced. An installed quality model becomes the startup default only without a saved explicit model selection; otherwise choose its file through **번역 모델 선택**. Restart after installing while the app was open. The [installer guide](docs/INSTALL-TRANSLATION.md) covers adding CUDA to core. Subsequent inference is local, without an external API account/key or Python. Q6, decoding and prompt comparisons did not establish a clear semantic improvement, so the Q4 profiles remain; see the [comparison record](docs/translation-quality-validation.md).

For Japanese speech, optionally run `tools/install-asr-model.ps1 -Profile japanese` to install **Kotoba-Whisper v2.0 Q5** (about 538 MB, Apache-2.0). Select `runtime/models/ggml-kotoba-whisper-v2.0-q5_0.bin` through the ASR model picker. Multilingual Turbo remains the default. The [ASR evidence](docs/asr-model-validation.md) distinguishes native execution from accuracy validation.

Open a file or disc, then enable automatic captions and choose source/reading/translation in the **AI 자막** tab. **표시 자막 저장** exports the selected display text to SRT.

ASR requires ten seconds before playback/after seeking, shortened near EOF, then keeps working toward 60 seconds ahead. Chunk boundaries may exceed the target slightly. Disc navigation does not wait for main-title captions. Manual pause is preserved, and failure does not silently bypass minimum readiness: use retry or **자막 없이 계속**. Video, ASR and translation devices are separate and report actual backends; support varies by model. Recognition, readings and translation can be inaccurate.

With playback focus: Space plays/pauses; F/Enter/F11 toggles fullscreen; Esc exits; Left/Right seeks ten seconds; Up/Down changes volume by 5%; M mutes/restores volume. Ctrl+arrows/Ctrl+Enter navigate disc menus, alongside mouse/onscreen controls. Editable/control widgets retain normal keys. See the [user guide](docs/CAPTIONS-AND-PLAYBACK.md).

### Build and limits

Windows requires Visual Studio 2022 C++ Build Tools, Windows SDK, CMake 3.25+, Ninja, Git and PowerShell 7. Use the complete Windows commands in the Korean section: bootstrap dependencies, build native AI, bootstrap translation with `-SkipModel -SkipOpenVino`, build/stage `build-next`, deploy Qt, and run. Skipping translation OpenVINO does not remove the separate ASR NPU runtime. Source URLs/hashes are pinned in the two dependency locks. The Linux commands above exercise the development build; native optical-drive/display testing and packaging remain pending. macOS is outside the current target.

For the CUDA ZIP, run both `install-asr-cuda.ps1 -Development` and `install-translation-cuda.ps1 -Development`, then `package-windows.ps1 -BuildDirectory build-next -IncludeCuda`, as shown above. Omitting `-IncludeCuda` preserves the core flavor. The release workflow's manual `include_cuda` input defaults to `true`; final archive size, hashes and native results remain in the release record.

AACS/BD+ reading, Windows capture exclusion and certified DRM/HDCP output are separate. No protected keys/certificates or commercial media/menu/transcript assets are shipped. Capture exclusion targets **video and captions only**, leaving the main UI, settings, menus and transport capturable. Prior alpha.3 observations showed black video with visible UI/transport. This is not absolute capture prevention. Final HDR/lossless, CI and package status belongs in the [alpha.5 record](docs/releases/0.1.0-alpha.5.md).

See the [changelog](CHANGELOG.md), [version policy](docs/VERSIONING.md) and [third-party notices](THIRD_PARTY_NOTICES.md). The application is GPL-3.0-or-later, Whisper/Turbo is MIT, optional Qwen3-4B/Hy-MT2 weights are Apache 2.0, and optional CUDA keeps its separate NVIDIA terms. Unmodified Noto fonts retain SIL OFL 1.1 notices.
