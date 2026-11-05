# 엔진 결정과 근거 / Engine decisions and primary references

## 한국어

이 문서는 기술 선택의 근거를 설명합니다. 상류 프로젝트의 기능 설명은 LumaPlayer의 실물 합격 증거가 아닙니다. 현재 구현 범위는 [기능표](../FEATURES.md), 이번 빌드의 검증 상태는 [alpha.2 기록](../releases/0.1.0-alpha.2.md)을 따릅니다.

### 네이티브 구조와 두 미디어 엔진

C++20/Qt 6은 Windows 창과 libVLC/libmpv의 C 인터페이스를 직접 연결하고 Linux 컴파일 경로를 유지하기 위해 선택했습니다. 애플리케이션과 배포 실행 경로에는 Python을 요구하지 않습니다. [Qt 플랫폼 문서](https://doc.qt.io/qt-6/supported-platforms.html), [libVLC API](https://videolan.videolan.me/vlc-3.0/group__libvlc__media__player.html), [고정 mpv 클라이언트 API](https://github.com/mpv-player/mpv/blob/3138fc8eea/include/mpv/client.h)가 각 연결의 기준입니다.

디스크 메뉴·오버레이·내비게이션은 libVLC/libbluray, 파일 영상·AV1·GPU Next 렌더링은 libmpv를 사용합니다. 현재 TUKI는 HDMV 경로이며 BD-J 디스크에는 별도의 Java 런타임·실물 검증이 필요합니다. [libbluray 범위](https://www.videolan.org/developers/libbluray.html), [VLC3.0.24 Blu-ray 구현](https://github.com/videolan/vlc/blob/3.0.24/modules/access/bluray.c)을 참고합니다.

현재 TUKI 본편 전환의 하드웨어 표면 오류를 재현해 디스크에는 소프트웨어 디코더와 10초 캐시를 기본 적용했습니다. 새 25초 연속 실행·네 번 탐색·챕터·메뉴 진행 결과는 이 선택의 관측 근거입니다. 더 긴 안정성, 다른 디스크, 다른 GPU는 별도 항목입니다.

### AACS 라이브러리 발견과 자막 음성 정렬

Windows libbluray의 로더는 호스트 EXE와 System32를 명시해 검색합니다. `AddDllDirectory`로 libVLC를 불러와도 이 별도 검색 정책은 바뀌지 않습니다. AACS/BD+ 의존성 네 DLL을 앱 EXE와 음성 추출용 FFmpeg EXE 옆에 각각 둡니다. [VideoLAN 계열 로더 소스](https://github.com/ShiftMediaProject/libbluray/blob/master/src/file/dl_win32.c), [AACS 어댑터](https://github.com/ShiftMediaProject/libbluray/blob/master/src/libbluray/disc/aacs.c)가 원인을 설명합니다. 이는 로더 수정이며 디스크 접근 정보·인증을 제공한다는 의미는 아닙니다.

메뉴를 추출 프로세스에서 다시 실행하면 첫 재생 명령 제한과 다른 오디오 선택을 만날 수 있습니다. FFmpeg full의 libbluray 프로토콜로 현재 MPLS와 오디오 PID를 지정해 추출하고, 길이 대조로 플레이리스트가 유일하게 정해지지 않으면 거절합니다. TUKI에서는 가장 긴 목록이 본편보다 긴 코멘터리일 수 있어 최장 목록 추정은 사용하지 않습니다. [FFmpeg Blu-ray 프로토콜](https://ffmpeg.org/ffmpeg-protocols.html#bluray), [full 빌드 라이브러리](https://www.gyan.dev/ffmpeg/builds/)를 근거로 합니다.

### 원문·독음·번역과 장치

ASR은 [whisper.cpp b5454](https://github.com/ggml-org/whisper.cpp/tree/b5454) 네이티브 경로를 사용합니다. 이 앱의 확인된 NPU 구성은 OpenVINO 2024.1 인코더와 CPU 디코더의 조합이며, 선택 Intel Arc Vulkan과 CPU 경로도 구분합니다. ‘NPU 선택’과 전체 추론의 NPU 실행은 같은 뜻이 아닙니다. 모델 백그라운드 준비·상주·최신 요청 우선은 구현됐지만 새 구조의 실제 지연은 아직 검증 중입니다.

일본어 읽기는 [MeCab/IPADIC](https://taku910.github.io/mecab/)에서 얻어 한글로 표기합니다. 한국어 뜻 번역은 별도의 [Qwen3-4B 공식 GGUF](https://huggingface.co/Qwen/Qwen3-4B-GGUF/tree/bc640142c66e1fdd12af0bd68f40445458f3869b)와 [고정 llama.cpp](https://github.com/ggml-org/llama.cpp/tree/7d516e25ead4653169236b3f90d13f7bd8629724)를 사용합니다. 원문·읽기·번역은 별도 데이터로 보존합니다. Whisper의 영어 번역 옵션을 한국어 번역으로 표시하지 않습니다.

현재 번역의 확인된 가속은 Intel Arc Vulkan입니다. NPU는 현재 드라이버 옵션 오류, NVIDIA OpenVINO 경로는 프로세스 충돌을 관측해 CPU 대체를 기록했습니다. 기본 배포는 번역 OpenVINO를 생략하며 별도 ASR NPU는 유지합니다. 정확한 장치·응답·지연은 [로컬 번역 기록](../local-translation.md)에 있습니다. 상류 [OpenVINO 백엔드 문서](https://github.com/ggml-org/llama.cpp/blob/7d516e25ead4653169236b3f90d13f7bd8629724/docs/backend/OPENVINO.md)의 표만으로 현재 드라이버 성공을 선언하지 않습니다.

### HDR·음성·프레임 보간

mpv의 HDR 메타데이터 처리·SDR 톤매핑과 인증 Dolby Vision 출력은 구분합니다. DXGI 디스플레이 상태와 소스 메타데이터를 보고하고 실제 파일·디스플레이 검사는 별도로 기록합니다. PCM/LPCM·FLAC·ALAC를 디코딩하는 것만으로 비트 퍼펙트·WASAPI 독점·HD 오디오 비트스트림이 검증되지는 않습니다. [고정 mpv 옵션 문서](https://github.com/mpv-player/mpv/blob/3138fc8eea/DOCS/man/options.rst)와 [mpv 매뉴얼](https://mpv.io/manual/master/)이 엔진 설정의 근거입니다.

디스플레이 혼합은 `interpolation=yes`와 `video-sync=display-resample`로 실제 주사율에 맞춥니다. `display-fps-override`로360 Hz를 가장하지 않습니다. 광학 흐름은 FFmpeg `minterpolate`의 CPU `mci` 경로이며24~360 fps는 목표입니다. 8비트 형식만 처리하므로 HDR·미확인 소스에는 적용하지 않습니다. 합성 360 fps 타임스탬프와 1080p 약 1.4 출력 fps 관측은 [전용 보고서](../testing/2026-11-05-motion-interpolation.md)에 있습니다. [고정 mpv lavfi](https://github.com/mpv-player/mpv/blob/3138fc8eea/filters/f_lavfi.c), [FFmpeg 필터](https://ffmpeg.org/ffmpeg-filters.html#minterpolate)를 사용하며 RIFE·DLSS 또는 libVLC 메뉴 보간을 구현한 것은 아닙니다.

### Windows 캡처 제외와 라이선스

본창과 별도 자막 창에 `WDA_EXCLUDEFROMCAPTURE`를 요청하고 실제 affinity를 읽습니다. Windows10 2004부터의 제외 API이며 지원 OS·DWM·호출 결과에 의존합니다. [Microsoft 문서](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowdisplayaffinity)는 이를 DRM이나 모든 방법의 캡처 방지로 보장하지 않습니다. 이번 패키지의 네이티브 적용·캡처 QA는 릴리스 기록에서 별도로 판정합니다.

수정하지 않은 Noto 글꼴은 SIL OFL 1.1에 따라 저작권·라이선스를 보존해 소프트웨어에 포함할 수 있습니다. [OFL 공식 FAQ](https://openfontlicense.org/ofl-faq/)와 `LICENSES/NotoSans-OFL.txt`, `LICENSES/NotoSansCJK-OFL.txt`를 따릅니다. 앱의 GPL과 글꼴 라이선스를 혼동하지 않습니다. 선택 Qwen 모델의 [고정 Apache 2.0 라이선스](https://huggingface.co/Qwen/Qwen3-4B-GGUF/blob/bc640142c66e1fdd12af0bd68f40445458f3869b/LICENSE), llama.cpp·OpenMP 등 원문은 별도 보존합니다.

번역 모델은 2,497,280,256바이트(약 2.50 GB/2.33 GiB)입니다. [GitHub의 개별 릴리스 자산 제한](https://docs.github.com/en/repositories/releasing-projects-on-github/about-releases)은 2 GiB 미만이므로 코어 ZIP에서 모델을 분리하고 고정 크기·SHA-256 설치기를 제공합니다. 설치 이후 인식·번역은 로컬에서 실행하며 공개 자료에는 상용 디스크 내용·자막·키·인증서를 포함하지 않습니다.

## English

Upstream capability is not LumaPlayer hardware acceptance. See the [matrix](../FEATURES.md) for scope and [alpha.2](../releases/0.1.0-alpha.2.md) for build-specific validation. References are linked in the Korean section.

### Engines and disc behavior

Native C++20/Qt 6 directly connects Windows windows and C media APIs while retaining Linux compilation. No Python runtime is required. libVLC/libbluray owns disc menus/navigation; libmpv owns file decoding and GPU Next rendering. Current TUKI uses HDMV; BD-J needs its own Java/runtime and physical testing.

Reproduced hardware surface-allocation failures motivated software disc decoding and a ten-second cache. A fresh 25-second main-title run/four seeks/chapter/menu transitions support that bounded choice, not universal or long-duration compatibility.

libbluray's explicit Windows host-directory lookup requires AACS/BD+ dependencies beside the player and FFmpeg acquisition EXE. Correct library discovery does not provide disc access credentials. Acquisition uses the active MPLS/audio identity, rejects ambiguous playlist matching, and does not infer the main title from the longest playlist.

### Local caption pipeline

Native whisper.cpp provides source ASR. Confirmed NPU operation means OpenVINO 2024.1 encoder plus CPU decoder; selected Arc Vulkan and CPU remain separate paths. Background warmup/model residency/newest-request scheduling are implemented, but the new end-to-end latency still needs measurement.

MeCab/IPADIC supplies Japanese readings; Hangul transliteration is distinct from meaning translation. Optional Qwen3-4B/llama.cpp supplies Korean translation while preserving source/timing. Translation's observed Arc Vulkan success is distinct from the current NPU driver error and NVIDIA OpenVINO crash. Default translation packaging omits OpenVINO; the separate ASR NPU runtime remains. Backend documentation does not prove driver success.

### Rendering, capture and distribution

Separate HDR metadata/SDR tone mapping from certified Dolby Vision output. Separate lossless decoding from bit-perfect/exclusive/bitstream output. Display blending uses real refresh-rate synchronization; experimental CPU minterpolate targets 24–360 fps and rejects HDR/unknown metadata because its supported formats are eight-bit. Synthetic 360 fps timestamps passed; the 1080p example achieved only about 1.4 output fps. No RIFE/DLSS or libVLC menu interpolation is claimed.

Windows capture exclusion is requested/read back for both application and caption windows. Microsoft's API depends on supported OS/DWM behavior and is not DRM certification or an absolute prevention guarantee. Final native QA remains a release gate.

Unchanged Noto fonts retain their SIL OFL 1.1/copyright notices; optional Qwen3-4B retains Apache 2.0, alongside all runtime notices. The 2,497,280,256-byte model exceeds GitHub's under 2 GiB per-asset limit, so distribute a model-separated core plus a pinned size/SHA-256 installer. Runtime inference stays local; commercial media/transcripts, keys and certificates are excluded from public artifacts.
