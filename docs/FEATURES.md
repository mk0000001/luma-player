# 기능 대응표 / Capability matrix

## 한국어

대상은 개발 중인 `0.1.0-alpha.6`입니다. **구현**, **실제로 확인한 경로**, **남은 검증**을 구분하며 과거 실물 관측을 새 패키지의 합격으로 옮기지 않습니다. 엔진이 지원하는 모든 기능이 LumaPlayer UI에 구현된 것은 아니며, 팟플레이어 전체 기능 대응은 진행 중입니다. 최종 배포 상태는 [릴리스 기록](releases/0.1.0-alpha.6.md)을 따릅니다.

| 영역 | 구현·관측 | 남은 범위·제한 |
| --- | --- | --- |
| Blu-ray | libVLC HDMV 메뉴·마우스·리모컨·타이틀·챕터. 이전 실물 시험에서 보호 디스크의 메뉴·영상·LPCM, 25초 연속 진행·네 번 탐색·챕터·메뉴 확인 | 기본 소프트웨어 디코딩/10초 디스크 캐시. 과거 하드웨어 표면 오류와 최근 DXGI 출력 장치 손실 관측. 새 빌드의 복잡한 메뉴·장시간 재생, 다른 디스크·BD-J·BD-Live·PiP·멀티앵글 미검증 |
| 보호 디스크 읽기 | 로컬 libbluray/libaacs/libbdplus 연결 | 모든 디스크 접근·DRM 인증 보장 없음. 키·인증서 미포함 |
| 파일 영상 | libmpv GPU Next, AV1/H.264/HEVC 등 엔진 경로, 비율 유지·파일 드롭 | 코덱·컨테이너·GPU별 새 포터블 QA 확대. 엔진 지원 목록 전체를 합격으로 표시하지 않음 |
| 영상 출력 복구 | 실제 `SwapChain Present failed / DEVICE_REMOVED` 관측. 세션당 한 번 감지·출력 재생성. 여섯 소유 파일 조건과 실물 디스크 메뉴 동작 뒤 위치·일시정지·가져온 자막 보존 회귀 통과 | 반복 손실·오래된 오류의 새 소스 재열기 차단. 장시간·다양한 복잡한 메뉴 검증 대기 |
| 재생 제어 | 일시정지, 탐색, 챕터·타이틀, 속도·음량, 키보드·마우스 | 프레임 이동, A–B 반복, 북마크·이어보기·재생목록 등 [추적 이슈](https://github.com/mk0000001/luma-player/issues/2) |
| UI | 기본 다크/선택 라이트, 자체 SVG 로고·버튼·선택기, Noto Sans, 약 74px 전체화면 바·5초 유휴 숨김·조작 중 유지, 개발자 전용 진단 | 최종 추출 패키지의 배율·다중 모니터·접근성 QA 대기 |
| 원문 자막 | 내부/외부 자막, 로컬 인식, 원문 보존·SRT 저장, 세대 취소·정렬·재시도 | 노래·반주·관객 소리의 인식 정확도는 보장하지 않음. OCR·편집·검색 등은 후속 |
| 한글 독음 | 네이티브 MeCab/IPADIC의 일본어 읽기를 한글 표기로 변환. 원문·번역과 별도 토글 | 고유명사·문맥별 한자·가사 읽기 오류 가능. 뜻을 번역하는 기능과 다름 |
| ASR 장치 | Whisper b5454, OpenVINO **2026.4.1 NPU 인코더+CPU 디코더**, CPU/Vulkan 및 선택 CUDA 경로. 드라이버 5540의 Kotoba 공개 16초 음성 네 요청 모두 실제 NPU·원시 호출 1회 확인 | 자동 Kotoba NPU는 원본·IR·worker·runtime·드라이버에 맞는 합성 실측 calibration 필요. 기존 base와 명시적 NPU 선택 유지. 최신 40+ INT8 TOPS 제품 전체의 실물 통과를 뜻하지 않음 |
| ASR 지연·버퍼 | 모델 준비·상주·최신 요청 우선. 15초 구간, 최소 10초 준비·60초 선행 목표. 디스크의 미확보 위치는 실제 음성을 2초 앞에서 읽어 시계 보정을 흡수. 수동 pause·2/4/8초 재시도·AI 설정 유지 | 공개 NPU 음성 후속 추론 2,480~2,554ms, 정상 빌드 합성 후속 2,053~2,061ms 모두 실제 호출 확인. 콜드 준비 약 299,730ms. 적합성은 합성 16초 4회 중 후속 3회 모두 추론·벽시계 ≤3초. 최종 포터블 profile·실제 GUI 10/60초 버퍼 대기 |
| 한국어 번역 | 선택 Qwen3-4B·Hy-MT2-7B Q4_K_M/llama.cpp, 로컬 원문→한국어. 최신 자막 버퍼의 대상 전후 최대 60초 문맥으로 대상 cue만 번역 | Qwen 약 2.50GB/Hy-MT2 약 4.62GB 별도 설치. Hy-MT2 JSON 되풀이를 수정하고 실제 CUDA 소유 무문맥·문맥 입력 검사 통과. 모든 가사 품질 보장 아님. 과거 번역 NPU/NVIDIA OpenVINO 실패와 ASR NPU 성공을 구분 |
| GPU 설정 | DXGI 파일 출력 어댑터와 ASR/번역 장치 선택을 분리. 실제 AI 백엔드 표시. Arc/NVIDIA 요청 뒤 탐색·pause/play 복원 확인 | 영상 출력 GPU 선택은 AI 선택과 별개. 실제 출력 어댑터 ID는 미조회이며 요청 이름을 실제 GPU 증명으로 표시하지 않음 |
| HDR / Dolby Vision | 디스플레이 상태와 PQ/BT.2020 소스 감지, gamma2.2 SDR 출력·HDR 광학 흐름 차단 확인 | SDR 디스플레이 검사. 실제 HDR 패스스루·색 정확도·Dolby Vision 미검증, 인증 Dolby Vision 미구현. [추적](https://github.com/mk0000001/luma-player/issues/5) |
| 무손실 음성 | TUKI LPCM과 소유 FLAC/ALAC 96 kHz mono의 무손실 메타데이터·WASAPI·시계·pause/play 확인 | 비트 퍼펙트·독점·TrueHD/DTS-HD 비트스트림 보장은 별도 |
| 프레임 보간 | 파일 전용·기본 꺼짐. GPU 디스플레이 혼합, 실험적 CPU 광학 흐름 24~360 fps 목표. 실제 합성 360 fps 타임스탬프 확인 | 1080p 예시는 약 1.4 출력 fps. 실시간360 fps·RIFE·DLSS 미구현. 광학 흐름은 8비트 처리로 HDR/미확인 메타데이터 차단. libVLC 메뉴 미지원 |
| 캡처 제외 | Windows `WDA_EXCLUDEFROMCAPTURE`, 이전 본창·자막 창 적용 상태 4/4 및 실제 본창 검정 캡처 관측. 기본 캡처 방지 켬, 개발자 모드에서만 해제 제공 | 개발자 활성화·캡처 방지 해제의 별도 확인, 동의 대기 중 차단·이전 설정 우회 방지 회귀 통과. GPLv3·적용 법률 범위의 안내이며 포괄 면책 아님. 최종 추출본 대기. DRM/HDCP 인증·모든 캡처 차단 보장 아님 |
| 라이브러리·네트워크·확장 | 로컬 파일·디스크와 엔진의 직접 URL 경로 | 라이브러리 DB·재생목록·네트워크 UI·DSP/셰이더·키 매핑·스크린리더 실측 등 [이슈#2](https://github.com/mk0000001/luma-player/issues/2) |
| 플랫폼 | Windows x64 개발/포터블, Linux CI 컴파일·자동 검사 | Linux 광학 드라이브·표시·오디오·Wayland·HDR·패키징 미검증. macOS는 대상 밖. [추적](https://github.com/mk0000001/luma-player/issues/4) |
| 배포 | 고정 의존성·라이선스, 모델 분리 코어 ZIP, SHA-256 설치기, 배포별 불변 태그 정책 | 새 포터블 추출·CI·공개 릴리스 최종 확인 대기. [배포 추적](https://github.com/mk0000001/luma-player/issues/5) |

ASR NPU 표본의 네 번 반복 성공과 과거 번역 NPU 실패는 서로 다른 모델·실행기 결과입니다. 표본의 CUDA 인식문과 같은 결과였지만 정답 대비 WER나 모든 가사 품질 통과는 아닙니다. [Kotoba 성능](PERFORMANCE-KOTOBA.md), [번역 검증](local-translation.md), [프레임 보간 검증](testing/2026-11-05-motion-interpolation.md), [초기 Windows 기록](testing/2026-11-05-windows.md)을 구분해 읽으세요. 오래된 실행 결과는 최신 배포본의 합격 증거를 대신하지 않습니다.

libVLC의 외부 자막 트랙이 긴 세션에서 누적될 수 있는 제한은 [이슈#6](https://github.com/mk0000001/luma-player/issues/6)으로 남깁니다. 관리 트랙 해제·원문/독음/번역의 별도 오버레이가 이 이력을 자동으로 해결했다는 뜻은 아닙니다.

## English

This matrix targets `0.1.0-alpha.6` in development. Implemented controls, observations and unverified capabilities remain separate; historical hardware results do not certify a new package. Backend potential does not imply a completed UI or full PotPlayer parity; the [release record](releases/0.1.0-alpha.6.md) owns final package acceptance.

| Area | Implemented/observed | Remaining limits |
| --- | --- | --- |
| Blu-ray | libVLC HDMV menus, mouse/remote, titles/chapters. An earlier protected-disc test passed a 25-second main-title run, four seeks, chapter/menu transitions, picture and LPCM | Software decoder/ten-second cache default. Historical hardware surface errors and recent DXGI output-device loss observed. New-build complex-menu/long playback, other discs and BD-J/BD-Live/PiP/multi-angle unverified |
| Protected reading | Local libbluray/libaacs/libbdplus integration | No universal access or certified DRM claim; no keys/certificates included |
| File video/controls | libmpv GPU Next; AV1/H.264/HEVC engine paths; aspect, drop, pause/seek/chapter/title/rate/volume | New portable codec/GPU matrix pending. Frame stepping, A–B repeat, bookmarks/resume/playlists and other parity work remain |
| Video-output recovery | Actual `SwapChain Present failed / DEVICE_REMOVED` observed. Once-per-session bounded output recreation; six owned-file cases and physical-disc menu recovery preserving position/pause/imported captions passed | Repeated loss stops recovery; stale errors cannot reopen a newer source. Long playback/diverse complex-menu checks pending |
| UI | Dark/light custom SVG controls and branding, Noto fonts, approximately 74 px fullscreen bar, five-second idle hide, interaction lock, developer-only diagnostics | Final extracted-package, scaling, multi-monitor and accessibility QA pending |
| Source/readings | Internal/external captions, local ASR, source-preserving SRT, generation cancellation; native MeCab/IPADIC Hangul readings kept separate | Songs, names and context-sensitive readings can be wrong; OCR/edit/search remain future work |
| ASR devices | Whisper b5454, **OpenVINO 2026.4.1 NPU encoder plus CPU decoder**, CPU/Vulkan and optional CUDA. Four actual NPU requests for public 16-second Kotoba speech on driver 5540, one raw call each | Automatic Kotoba NPU needs synthetic calibration matching source/IR/worker/runtime/driver. Legacy base/manual NPU remain. No blanket hardware pass for recent 40+ INT8 TOPS devices |
| ASR latency/buffering | Resident/warmed model, newest request first, 15-second chunks, ten-second minimum/60-second lookahead target. Two-second disc preroll, manual pause and bounded 2/4/8-second retries preserve AI preferences | Actual public-speech NPU subsequent inference 2,480–2,554ms; normal synthetic 2,053–2,061ms. Cold readiness about 299,730ms. Qualification requires four synthetic 16-second requests, all three subsequent inference/wall times ≤3s. Final portable-profile/GUI 10–60-second coverage pending |
| Korean translation | Optional native Qwen3-4B/Hy-MT2-7B Q4_K_M/llama.cpp. Up to ±60 seconds from the latest source buffer, translating only the target cue | Separate Qwen about 2.50GB/Hy-MT2 about 4.62GB models. Hy-MT2 JSON echo corrected; actual CUDA owned raw/context tests passed, not general lyrics quality. Historical translation NPU/NVIDIA OpenVINO failures remain distinct from ASR NPU success |
| GPU settings | Separate DXGI file output and AI device preferences; seek and pause/play restoration verified after Arc/NVIDIA requests | Actual output-adapter ID was not queried; a requested name does not establish physical GPU identity |
| HDR/Dolby Vision | PQ/BT.2020 metadata detection, gamma2.2 SDR output and HDR optical-flow rejection observed | SDR display only. HDR passthrough, color accuracy and Dolby Vision remain unverified; no certified Dolby Vision output |
| Lossless audio | Physical LPCM plus owned 96 kHz mono FLAC/ALAC metadata, WASAPI clocks and pause/play verified | Bit-perfect, exclusive mode and TrueHD/DTS-HD bitstream output remain separate |
| Motion | File-only, off by default; display blending and experimental 24–360 fps CPU optical target; synthetic 360 fps timestamps verified | 1080p example achieved about 1.4 output fps. No real-time 360/RIFE/DLSS. Eight-bit optical path rejects HDR/unknown metadata; no libVLC menu interpolation |
| Capture exclusion | Historical two-window affinity checks passed 4/4 and protected main-window capture appeared black. Protection defaults on; disabling is available only within developer mode | Separate developer/capture acknowledgement, blocking during consent and preventing legacy bypass passed regressions. GPLv3/applicable-law notice, not blanket immunity. Final extraction pending; no certified DRM/HDCP or absolute capture prevention |
| Platform/distribution | Windows x64, Linux compile/automated CI, pinned licenses/dependencies, model-separated core and immutable-release policy | Final package/CI/publication pending; Linux hardware playback/Wayland/HDR/packaging unverified; macOS out of scope |

Track the remaining scope through [#2](https://github.com/mk0000001/luma-player/issues/2), [#3](https://github.com/mk0000001/luma-player/issues/3), [#4](https://github.com/mk0000001/luma-player/issues/4), [#5](https://github.com/mk0000001/luma-player/issues/5) and the long-session libVLC subtitle-track issue [#6](https://github.com/mk0000001/luma-player/issues/6). See [Kotoba performance](PERFORMANCE-KOTOBA.md) and the translation/motion records linked above. Four repeated ASR NPU requests and CUDA text agreement are sample-specific, not ground-truth WER/general lyrics quality or translation NPU success. Historical results do not certify a new package.
