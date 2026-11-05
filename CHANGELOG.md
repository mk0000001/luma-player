# 변경 기록 / Changelog

## 한국어

공개 배포에는 불변 태그·GitHub Release·체크섬을 남깁니다. 로컬 시험 빌드와 실제 배포를 구분하며, 아래 프리뷰의 최종 검증은 각 릴리스 기록을 따릅니다.

### 0.1.0-alpha.3 — 2026-11-06 프리뷰 작업

- 긴 원문을 시간 안에서 짧게 나누고 원문·독음·번역을 같은 페이지로 표시. 실제 글자 배치로 공백 없는 문자열도 줄바꿈하며 결합 문자·이모지를 보존.
- 재생·탐색 후 최소 10초 준비와 60초 선행 작업을 분리. 실패로 최소 버퍼를 자동 우회하지 않고 수동 일시정지를 유지하며 재시도/자막 없이 계속을 제공.
- 엄격한 UTF-8/UTF-16 SRT와 로컬 FFmpeg의 ASS/SSA/VTT/SMI, CP949/Shift-JIS 추정 변환. 같은 이름 검색·드롭·실패 시 현재 언어 AI 전환, 파일/디스크 본편별 로컬 이어보기 추가.
- 영상과 재생바를 독립 창으로 겹쳐 숨김 때 영상 배치를 유지. 74 px 전체화면 바는 정지 포인터가 바 위에 있어도 5초 후 숨고 메뉴·끌기·키 조작은 유지. Windows 캡처 제외는 영상·자막만 적용하며 본창·메뉴·재생바는 보임.
- F/Enter/F11 전체화면, Esc 해제, 좌우 10초, 위아래 음량 5%, M 음소거. 디스크 메뉴는 Ctrl+방향키/Ctrl+Enter; 입력 위젯의 키 유지.
- 코어에 MIT Whisper large-v3-turbo Q5 모델 추가. CUDA 음성 인식·번역은 원문 NVIDIA CUDA 12.4 EULA를 보존하는 선택 수동 설치로 제공하며 드라이버를 변경하지 않음.
- 선택 번역 프로필: fast Qwen3-4B 약 2.50 GB / quality Hy-MT2-7B 약 4.624 GB(Apache-2.0). 설치한 quality는 명시한 모델 설정이 없을 때만 시작 기본값. 한 cue의 잘린/무효 번역은 건너뛰고 후속 번역 계속.
- 소유 파일의 실제 키·자막 가져오기·이어서보기 검사와 개발 PC CUDA 실행 확인은 최종 CI·포터블 검증과 구분. [사용 안내](docs/CAPTIONS-AND-PLAYBACK.md)와 [alpha.3 기록](docs/releases/0.1.0-alpha.3.md)에 확인 범위를 기재하며 배포 완료를 미리 주장하지 않음.

### 0.1.0-alpha.2 — Windows 프리뷰

- TUKI 메뉴→본편 안정화: 소프트웨어 디코딩과 10초 디스크 캐시를 기본으로 선택. 새 연속 재생·네 번 탐색·챕터·메뉴 검사를 통과했으며 하드웨어 디코더의 표면 할당 오류는 재현·기록.
- 원문·한글 독음·한국어 번역을 분리해 표시. 네이티브 MeCab/IPADIC과 선택 Qwen3-4B 번역 추가. 포터블 코어에서는 약 2.50 GB 모델을 분리 설치.
- ASR NPU 인코더/OpenVINO 2024.1, CPU 디코더, 선택 Vulkan GPU와 CPU 대체. 번역은 확인된 Arc Vulkan/CPU 경로를 기본 배포하고 실패한 번역 NPU/OpenVINO GPU를 성공으로 표시하지 않음.
- AI 모델 백그라운드 준비, 탐색 간 모델 상주, 최신 요청 우선. 15초 구간·최소 10초 준비·약 30~45초 선행, 수동 일시정지 유지와 2/4/8초 재시도. 디스크는 실제 음성 2초 선행 읽기로 탐색 뒤 시계 보정에 대응. 준비된 NPU 캐시에서 소유 파일 2,127ms/탐색 1,780ms 관측이며 콜드 시작·광학 탐색 시간과 구분.
- 기본 다크/선택 라이트, 자체 SVG 로고·컨트롤, Noto 글꼴, 약 74px 전체화면 바와 5초 유휴 숨김·상호작용 유지. 진단을 개발자 모드로 이동.
- 파일 출력 GPU 선택과 별도 AI 장치 선택, Windows 본창·자막 창의 캡처 제외 상태 표시, HDR/SDR 렌더링 진단.
- 파일 전용 프레임 보간: 디스플레이 혼합과 실험적 CPU 광학 흐름 24~360 fps 목표. 합성 360 fps 타임스탬프 확인; 1080p 실시간 성능은 충족하지 못함. HDR·미확인 메타데이터에서는 광학 흐름을 차단.
- 캡처·HDR/무손실·신규 ASR 지연·최종 포터블·CI 완료 여부는 [alpha.2 릴리스 기록](docs/releases/0.1.0-alpha.2.md)에 별도로 기록.

### 0.1.0-alpha.1 — 2026-11-05 로컬 개발 스냅샷

- C++20/Qt 6 Windows 기반, libVLC Blu-ray 메뉴·리모컨, libmpv 파일 재생, 로컬 자막 작업과 초기 배포 스크립트.
- 공개된 불변 릴리스로 취급하지 않는 초기 로컬 빌드. 일부 실행에서 메뉴→본편 전환 후 약20초 정지를 관측했으므로 초기 누적 재생 수치만으로 장기 안정성을 판단하지 않음.
- 당시 기록은 이력이며 alpha.2의 새 빌드·패키지 검증을 대신하지 않음.

## English

Published distributions receive immutable tags, GitHub Releases and checksums. Local test builds are distinct from distributed versions; each release record owns its final validation status.

### 0.1.0-alpha.3 — preview work, 2026-11-06

- Split long source cues into timed pages and align source/readings/translations. Measured text layout wraps unbroken strings while preserving combining marks and emoji.
- Separate the strict ten-second playback/seek minimum from continued 60-second lookahead. Preserve manual pause; failure does not automatically bypass readiness. Provide retry and explicit continue-without-captions actions.
- Add strict UTF-8/UTF-16 SRT, local FFmpeg ASS/SSA/VTT/SMI import and heuristic CP949/Shift-JIS conversion. Add same-stem discovery, dropping, current-language AI fallback and local per-file/disc-title resume.
- Float video/transport without reflow. The 74 px fullscreen bar hides after five idle seconds even with the pointer over it, while menus/dragging/key activity remain usable. Exclude only video/captions from Windows capture; main UI, menus and transport stay visible.
- Add F/Enter/F11 fullscreen, Esc exit, ten-second arrows, 5% volume arrows and M mute. Use Ctrl+arrows/Ctrl+Enter for disc menus while preserving input-control keys.
- Include the MIT Whisper large-v3-turbo Q5 model in core. Offer manual optional ASR/translation CUDA installation, retaining original NVIDIA CUDA 12.4 EULA and leaving drivers unchanged.
- Offer optional fast Qwen3-4B (~2.50 GB) and quality Hy-MT2-7B (~4.624 GB, Apache-2.0) translation. Installed quality becomes the startup default only without an explicit saved model. Skip an invalid/truncated cue translation and continue later cues.
- Owned-media native keys/import/resume checks and development-PC CUDA execution are distinct from final CI/portable validation. The [guide](docs/CAPTIONS-AND-PLAYBACK.md) and [alpha.3 record](docs/releases/0.1.0-alpha.3.md) state scope without claiming publication is complete.

### 0.1.0-alpha.2 — Windows preview

- Stabilize the tested TUKI menu/main-title path with software decoding and a ten-second disc cache. A fresh playback/four-seek/chapter/menu sequence passed; hardware surface-allocation failures were reproduced and recorded.
- Separate original captions, Japanese Hangul readings and Korean translation. Add native MeCab/IPADIC and optional Qwen3-4B translation; install its approximately 2.50 GB model separately from the portable core.
- Use native NPU encoder/OpenVINO 2024.1 with CPU decoding, selected Vulkan GPU and CPU fallback for ASR. Default translation packaging uses verified Arc Vulkan/CPU; failed translation NPU/OpenVINO GPU runs are not reported as acceleration success.
- Add background ASR warmup, resident models and newest-request priority. Use 15-second chunks, a ten-second minimum, about 30–45 seconds ahead, preserved manual pause and bounded 2/4/8-second retries. Read two seconds of real disc preroll to cover post-seek clock correction. Owned-file NPU preparation measured 2,127ms/1,780ms after seek with a warm compiled cache, distinct from cold startup and optical seeking.
- Add dark/light custom controls, SVG branding, Noto fonts, an approximately 74 px fullscreen bar with five-second inactivity behavior, and opt-in developer diagnostics.
- Add file output-adapter and separate AI device choices, capture-affinity status for both windows, and HDR/SDR diagnostics.
- Add file-only display blending and experimental 24–360 fps CPU optical-flow targets. Synthetic 360 fps timestamps passed; 1080p real-time throughput did not. Optical flow rejects HDR/unknown metadata.
- Keep final capture, HDR/lossless, ASR timing, portable package and CI results explicit in the [alpha.2 record](docs/releases/0.1.0-alpha.2.md).

### 0.1.0-alpha.1 — local development snapshot, 2026-11-05

- Establish native C++20/Qt 6, libVLC disc menus, libmpv files, local subtitle jobs and initial packaging scripts.
- This early local snapshot is not a published immutable release. A main-title freeze around 20 seconds was observed in some runs; cumulative intro counters did not establish sustained main-title stability.
- Historical records do not substitute for alpha.2 build/package validation.
