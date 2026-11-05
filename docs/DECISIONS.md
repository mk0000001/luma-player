# 구현 결정과 비용 / Implementation decisions and tradeoffs

## 한국어

기술 선택과 실행은 위임됐으며 다음은 구현자가 정한 결정입니다. 전체 요구사항 완료 여부는 [기능표](FEATURES.md), 배포 판정은 [alpha.2 기록](releases/0.1.0-alpha.2.md)에서 확인합니다.

| 결정 | 선택한 이유 | 비용·제한 |
| --- | --- | --- |
| C++20/Qt 6 네이티브 앱 | Windows 창·C 미디어 API와 직접 연결하고 Linux 빌드를 유지 | 네이티브 수명·스레드·종료 재진입을 명시적으로 관리 |
| 메뉴 libVLC / 파일 libmpv | 디스크 메뉴·오버레이·내비게이션과 GPU Next 파일 렌더링을 각각 사용 | 상태·입력·의존성 두 경로의 검증이 필요. 메뉴 기능과 파일 처리 기능을 섞어 표시하지 않음 |
| 디스크 소프트웨어 기본 | 현 TUKI 본편 전환·탐색에서 하드웨어 디코더 표면 오류를 재현하고 새 소프트웨어 실행의 진행을 확인 | CPU 사용 증가와 10초 캐시. 한 장의 짧은 검증을 모든 디스크의 장시간 보장으로 확대하지 않음 |
| 현재 MPLS·오디오 정렬 | FFmpeg full/libbluray로 실제 본편 플레이리스트와 선택 오디오를 추출 | 길이만으로 유일하게 식별되지 않는 타이틀은 거절. 가장 긴 플레이리스트를 임의로 사용하지 않음 |
| ASR 상주·버퍼 | 모델을 백그라운드에서 준비하고 탐색 간 재사용,15초 단위와 최소 10초 준비/30초 선행으로 끊김 완화 | 첫 준비·메모리·발열 비용. 새 구조의 실제 지연 개선은 아직 측정 중. 취소 후 이전 결과 적용 금지 |
| 실패 후 제한 재시도 | 2/4/8초 후 재시도하고 계속 실패하면 재생을 계속할 수 있게 함 | AI를 몰래 끄지 않음. 수동 일시정지 의도와 ‘자막 없이 계속’을 보존 |
| 원문·독음·번역 분리 | MeCab/IPADIC 한글 독음과 Qwen3-4B 번역을 원문 위에 선택 표시 | 읽기·번역은 근사 결과. 토글·내보내기가 원문·시간을 바꾸지 않도록 별도 데이터 유지 |
| 모델별 실제 장치 보고 | ASR NPU 인코더/CPU 디코더와 번역 Arc Vulkan/CPU를 구분 | 번역 NPU 드라이버 오류·NVIDIA OpenVINO 충돌을 성공으로 표시하지 않음. 번역 OpenVINO는 기본 배포에서 제외하되 ASR NPU는 유지 |
| 선택 번역 모델 분리 | 약 2.50 GB 모델을 코어 ZIP에서 제외해 개별 GitHub 자산의 2 GiB 미만 제한 아래 배포 | 설치 시 별도 다운로드·공간 필요. 고정 URL·크기·SHA-256 확인, 모델 설치 후 오프라인 사용 |
| 자체 UI·Noto | 검정/흰색·파란 강조의 독립 디자인, 자체 SVG 로고·컨트롤과 수정하지 않은 OFL 글꼴 | 다른 서비스의 코드·자산을 복사하지 않음. 글꼴 저작권·OFL 원문과 제3자 고지 보존 |
| Windows 캡처 제외 | 보호 디스크 본창과 별도 자막 창에 동일한 `WDA_EXCLUDEFROMCAPTURE` 정책 요청 | OS 공개 캡처 API의 제외 기능. 실제 적용·최종 QA를 확인하며 DRM/HDCP 인증이나 완전한 캡처 방지로 부르지 않음 |
| HDR·광학 흐름 분리 | 파일 GPU Next의 HDR/SDR 처리와 8비트 CPU minterpolate를 구분 | HDR/미확인 메타데이터에서는 광학 흐름 차단. 인증 Dolby Vision·실물 HDR 정확도는 별도 검증 |
| 배포별 불변 버전 | 실제 배포마다 태그·Release·체크섬·검증 기록을 보존 | 모든 로컬 시험 컴파일에 태그를 만들지 않음. 공개 바이너리 수정은 새 버전으로 처리 |

네이티브 stop이 GUI 창 메시지를 기다릴 수 있어 libVLC 종료는 창 이벤트와 상태 재진입을 함께 관리합니다. DLL 검색 경로도 엔진별로 다릅니다. libbluray가 호스트 EXE 폴더를 직접 검색하므로 AACS/BD+ 의존성은 앱과 추출 실행기 옆에 각각 배치합니다. 이는 라이브러리 발견을 고치는 조치이며 디스크별 접근 권한을 제공하는 조치가 아닙니다.

팟플레이어 대응·HDR·Linux·배포 후속 범위는 [#2](https://github.com/mk0000001/luma-player/issues/2)·[#3](https://github.com/mk0000001/luma-player/issues/3)·[#4](https://github.com/mk0000001/luma-player/issues/4)·[#5](https://github.com/mk0000001/luma-player/issues/5), libVLC 장시간 자막 트랙 제한은 [#6](https://github.com/mk0000001/luma-player/issues/6)에서 추적합니다.

## English

Implementation choices were delegated. The [capability matrix](FEATURES.md) owns functional scope and the [alpha.2 record](releases/0.1.0-alpha.2.md) owns release acceptance.

- **Native C++20/Qt 6:** direct Windows-window/media-API integration with a Linux build path; explicit lifetime/thread/shutdown handling remains necessary.
- **Two engines:** libVLC handles menus/navigation; libmpv handles GPU Next file rendering. Their capabilities and tests stay separate.
- **Software disc default:** observed TUKI hardware surface failures justify software decoding and a ten-second cache for the tested path. CPU cost and short-test limits remain visible.
- **Aligned audio acquisition:** FFmpeg full/libbluray uses the active MPLS/audio identity; ambiguous playlist matches fail rather than selecting the longest playlist.
- **Resident ASR and buffering:** background warmup, model reuse across seeks, 15-second chunks, ten-second minimum buffer and thirty-second lookahead trade memory/startup cost for fewer interruptions. New end-to-end latency is still pending.
- **Bounded recovery:** retry after 2/4/8 seconds, then allow playback to continue without silently disabling AI; respect manual pause and explicit buffer bypass.
- **Separate caption data:** original, MeCab/IPADIC Hangul reading and Qwen3-4B Korean translation retain separate values and shared timing. Readings/translations are approximate.
- **Per-model acceleration:** ASR uses an NPU encoder/CPU decoder or selected Vulkan GPU/CPU. Translation's verified Arc Vulkan/CPU paths remain distinct from failed NPU/NVIDIA OpenVINO attempts. Omit translation OpenVINO by default while retaining ASR NPU.
- **Optional model distribution:** omit the approximately 2.50 GB translation model from the core ZIP to respect the per-asset 2 GiB GitHub limit; install with pinned URL/size/SHA-256 before offline use.
- **Independent UI and fonts:** custom SVG controls/branding and unchanged Noto fonts, preserving OFL/copyright notices; no third-party private design assets.
- **Capture policy:** request the same Windows exclusion for playback and caption windows, verify/report application status, and keep this distinct from DRM/HDCP certification and absolute capture prevention.
- **Honest image processing:** keep HDR/SDR rendering separate from eight-bit optical interpolation. Reject optical flow for HDR/unknown metadata; certified Dolby Vision and actual HDR fidelity remain separate gates.
- **Immutable distribution:** version released builds with tags/releases/checksums, preserve previous assets, and keep scratch compilations as development history.

libVLC shutdown must accommodate native window messages without uncontrolled reentry. libbluray's explicit host-directory DLL lookup also requires AACS/BD+ dependencies beside the player and acquisition executable. Correct discovery does not supply disc access credentials. Remaining scope is tracked by the issue links above.
