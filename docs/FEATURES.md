# 기능 대응표 / Capability matrix

## 한국어

대상은 `0.1.0-alpha.2`입니다. **구현**, **실제로 확인한 경로**, **남은 검증**을 구분합니다. 엔진이 지원하는 모든 기능이 LumaPlayer UI에 구현된 것은 아니며, 팟플레이어 전체 기능 대응은 진행 중입니다. 배포본의 최종 상태는 [릴리스 기록](releases/0.1.0-alpha.2.md)을 따릅니다.

| 영역 | 구현·관측 | 남은 범위·제한 |
| --- | --- | --- |
| Blu-ray | libVLC HDMV 메뉴·마우스·리모컨·타이틀·챕터. 현재 TUKI 보호 디스크의 메뉴·영상·LPCM, 새 25초 연속 진행·네 번 탐색·챕터·메뉴 확인 | 기본 소프트웨어 디코딩/10초 디스크 캐시. 하드웨어 경로에서 표면 할당 오류 8회 재현. 다른 디스크, BD-J·BD-Live·PiP·멀티앵글은 미검증 |
| 보호 디스크 읽기 | 로컬 libbluray/libaacs/libbdplus 연결 | 모든 디스크 접근·DRM 인증 보장 없음. 키·인증서 미포함 |
| 파일 영상 | libmpv GPU Next, AV1/H.264/HEVC 등 엔진 경로, 비율 유지·파일 드롭 | 코덱·컨테이너·GPU별 새 포터블 QA 확대. 엔진 지원 목록 전체를 합격으로 표시하지 않음 |
| 재생 제어 | 일시정지, 탐색, 챕터·타이틀, 속도·음량, 키보드·마우스 | 프레임 이동, A–B 반복, 북마크·이어보기·재생목록 등 [추적 이슈](https://github.com/mk0000001/luma-player/issues/2) |
| UI | 기본 다크/선택 라이트, 자체 SVG 로고·버튼·선택기, Noto Sans, 약 74px 전체화면 바·5초 유휴 숨김·조작 중 유지, 개발자 전용 진단 | 최종 추출 패키지의 배율·다중 모니터·접근성 QA 대기 |
| 원문 자막 | 내부/외부 자막, 로컬 인식, 원문 보존·SRT 저장, 세대 취소·정렬·재시도 | 노래·반주·관객 소리의 인식 정확도는 보장하지 않음. OCR·편집·검색 등은 후속 |
| 한글 독음 | 네이티브 MeCab/IPADIC의 일본어 읽기를 한글 표기로 변환. 원문·번역과 별도 토글 | 고유명사·문맥별 한자·가사 읽기 오류 가능. 뜻을 번역하는 기능과 다름 |
| ASR 장치 | whisper.cpp 네이티브: OpenVINO 2024.1 NPU **인코더+CPU 디코더**, 선택 Vulkan GPU(Intel Arc 실행 확인), CPU 대체 | NPU 전체 추론 또는 모든 GPU 지원을 의미하지 않음. 선택/실행/대체 이유를 구분해 표시 |
| ASR 지연·버퍼 | 모델 준비·상주·최신 요청 우선. 15초 구간, 최소 10초 준비, 약 30~45초 선행. 디스크의 미확보 위치는 실제 음성을 2초 앞에서 읽어 시계 보정을 흡수. 수동 pause·2/4/8초 재시도·AI 설정 유지 | 준비된 NPU 캐시에서 소유 파일 최초 2,127ms/탐색 1,780ms 관측. 콜드 시작·실물 디스크 탐색 지연과 구분하며 기기별 속도는 보장하지 않음 |
| 한국어 번역 | 선택 Qwen3-4B Q4_K_M/llama.cpp, 로컬 원문→한국어. Arc Vulkan 실제 실행·준비 후약 1.75초 예시 확인, CPU 대체 | 약 2.50 GB 모델 별도 설치. 번역 NPU는 현재 드라이버에서 실패, NVIDIA OpenVINO 실행기는 충돌. 기본 배포는 번역 OpenVINO를 제외. 번역 품질은 전문 자막 수준 보장 없음 |
| GPU 설정 | DXGI 파일 출력 어댑터와 ASR/번역 장치 선택을 분리. 실제 AI 백엔드 표시. Arc/NVIDIA 요청 뒤 탐색·pause/play 복원 확인 | 영상 출력 GPU 선택은 AI 선택과 별개. 실제 출력 어댑터 ID는 미조회이며 요청 이름을 실제 GPU 증명으로 표시하지 않음 |
| HDR / Dolby Vision | 디스플레이 상태와 PQ/BT.2020 소스 감지, gamma2.2 SDR 출력·HDR 광학 흐름 차단 확인 | SDR 디스플레이 검사. 실제 HDR 패스스루·색 정확도·Dolby Vision 미검증, 인증 Dolby Vision 미구현. [추적](https://github.com/mk0000001/luma-player/issues/5) |
| 무손실 음성 | TUKI LPCM과 소유 FLAC/ALAC 96 kHz mono의 무손실 메타데이터·WASAPI·시계·pause/play 확인 | 비트 퍼펙트·독점·TrueHD/DTS-HD 비트스트림 보장은 별도 |
| 프레임 보간 | 파일 전용·기본 꺼짐. GPU 디스플레이 혼합, 실험적 CPU 광학 흐름 24~360 fps 목표. 실제 합성 360 fps 타임스탬프 확인 | 1080p 예시는 약 1.4 출력 fps. 실시간360 fps·RIFE·DLSS 미구현. 광학 흐름은 8비트 처리로 HDR/미확인 메타데이터 차단. libVLC 메뉴 미지원 |
| 캡처 제외 | Windows `WDA_EXCLUDEFROMCAPTURE`, 본창·자막 창 적용 상태 네이티브 4/4 및 실제 본창 검정 캡처 관측 | DRM/HDCP 인증이나 모든 캡처 방법 차단 아님. 최종 배포 자산의 재검사는 릴리스 기록 기준 |
| 라이브러리·네트워크·확장 | 로컬 파일·디스크와 엔진의 직접 URL 경로 | 라이브러리 DB·재생목록·네트워크 UI·DSP/셰이더·키 매핑·스크린리더 실측 등 [이슈#2](https://github.com/mk0000001/luma-player/issues/2) |
| 플랫폼 | Windows x64 개발/포터블, Linux CI 컴파일·자동 검사 | Linux 광학 드라이브·표시·오디오·Wayland·HDR·패키징 미검증. macOS는 대상 밖. [추적](https://github.com/mk0000001/luma-player/issues/4) |
| 배포 | 고정 의존성·라이선스, 모델 분리 코어 ZIP, SHA-256 설치기, 배포별 불변 태그 정책 | 새 포터블 추출·CI·공개 릴리스 최종 확인 대기. [배포 추적](https://github.com/mk0000001/luma-player/issues/5) |

ASR NPU 성공과 번역 NPU 실패는 서로 다른 모델·실행기 결과입니다. [번역 검증](local-translation.md), [프레임 보간 검증](testing/2026-11-05-motion-interpolation.md), [초기 Windows 기록](testing/2026-11-05-windows.md)을 구분해 읽으세요. 오래된 실행 결과는 최신 배포본의 합격 증거를 대신하지 않습니다.

libVLC의 외부 자막 트랙이 긴 세션에서 누적될 수 있는 제한은 [이슈#6](https://github.com/mk0000001/luma-player/issues/6)으로 남깁니다. 관리 트랙 해제·원문/독음/번역의 별도 오버레이가 이 이력을 자동으로 해결했다는 뜻은 아닙니다.

## English

This matrix targets `0.1.0-alpha.2`. Implemented controls, observed execution and unverified capabilities are separate. Backend potential does not imply a completed UI or full PotPlayer parity; the [release record](releases/0.1.0-alpha.2.md) owns final package acceptance.

| Area | Implemented/observed | Remaining limits |
| --- | --- | --- |
| Blu-ray | libVLC HDMV menus, mouse/remote, titles/chapters. Current protected TUKI disc passed a fresh 25-second main-title run, four seeks, chapter/menu transitions, picture and LPCM | Software decoder/ten-second cache default. Eight hardware surface errors reproduced. Other discs and BD-J/BD-Live/PiP/multi-angle unverified |
| Protected reading | Local libbluray/libaacs/libbdplus integration | No universal access or certified DRM claim; no keys/certificates included |
| File video/controls | libmpv GPU Next; AV1/H.264/HEVC engine paths; aspect, drop, pause/seek/chapter/title/rate/volume | New portable codec/GPU matrix pending. Frame stepping, A–B repeat, bookmarks/resume/playlists and other parity work remain |
| UI | Dark/light custom SVG controls and branding, Noto fonts, approximately 74 px fullscreen bar, five-second idle hide, interaction lock, developer-only diagnostics | Final extracted-package, scaling, multi-monitor and accessibility QA pending |
| Source/readings | Internal/external captions, local ASR, source-preserving SRT, generation cancellation; native MeCab/IPADIC Hangul readings kept separate | Songs, names and context-sensitive readings can be wrong; OCR/edit/search remain future work |
| ASR devices | Native whisper.cpp NPU encoder/OpenVINO 2024.1 with CPU decoder; selected Intel Arc Vulkan execution and CPU fallback observed | Not full-NPU inference or universal GPU support; actual backend/fallback is reported |
| ASR latency/buffering | Resident/warmed model, newest request first, 15-second chunks, ten-second minimum and about 30–45 seconds ahead. Uncovered disc positions read two seconds of real preroll to cover clock correction. Manual pause and bounded 2/4/8-second retries preserve AI preferences | Owned-file preparation measured 2,127ms initially/1,780ms after seek with an already warm NPU cache. Cold startup and physical optical seeking are separate; no universal latency promise |
| Korean translation | Optional native Qwen3-4B/llama.cpp; Arc Vulkan verified at about 1.75 seconds for a warm example, CPU fallback | Separate 2.50 GB model. Translation NPU failed with the current driver; NVIDIA OpenVINO crashed. Default package omits translation OpenVINO. Quality remains imperfect |
| GPU settings | Separate DXGI file output and AI device preferences; seek and pause/play restoration verified after Arc/NVIDIA requests | Actual output-adapter ID was not queried; a requested name does not establish physical GPU identity |
| HDR/Dolby Vision | PQ/BT.2020 metadata detection, gamma2.2 SDR output and HDR optical-flow rejection observed | SDR display only. HDR passthrough, color accuracy and Dolby Vision remain unverified; no certified Dolby Vision output |
| Lossless audio | Physical LPCM plus owned 96 kHz mono FLAC/ALAC metadata, WASAPI clocks and pause/play verified | Bit-perfect, exclusive mode and TrueHD/DTS-HD bitstream output remain separate |
| Motion | File-only, off by default; display blending and experimental 24–360 fps CPU optical target; synthetic 360 fps timestamps verified | 1080p example achieved about 1.4 output fps. No real-time 360/RIFE/DLSS. Eight-bit optical path rejects HDR/unknown metadata; no libVLC menu interpolation |
| Capture exclusion | Both windows' native affinity checks passed 4/4; an actual protected main-window capture appeared black | Not certified DRM/HDCP or absolute capture prevention; release-specific package acceptance remains separate |
| Platform/distribution | Windows x64, Linux compile/automated CI, pinned licenses/dependencies, model-separated core and immutable-release policy | Final package/CI/publication pending; Linux hardware playback/Wayland/HDR/packaging unverified; macOS out of scope |

Track the remaining scope through [#2](https://github.com/mk0000001/luma-player/issues/2), [#3](https://github.com/mk0000001/luma-player/issues/3), [#4](https://github.com/mk0000001/luma-player/issues/4), [#5](https://github.com/mk0000001/luma-player/issues/5) and the long-session libVLC subtitle-track issue [#6](https://github.com/mk0000001/luma-player/issues/6). See the dedicated translation/motion records linked above. Successful ASR NPU execution is distinct from failed translation NPU execution, and historical results do not certify a new package.
