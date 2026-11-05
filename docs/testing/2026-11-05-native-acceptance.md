# Windows 네이티브 검증 기록 — 2026-11-05 / Windows native acceptance

## 한국어

이 문서는 `0.1.0-alpha.2` 소스 빌드에서 실제 실행한 검사와 관측 범위를 기록합니다. 소유·생성 fixture와 현재 TUKI 실물 디스크를 사용했습니다. 디스크 영상·음성·메뉴 이미지·인식 자막·키·인증서·사용자 경로는 공개하지 않으며 아래에는 검사 식별자와 수치만 남깁니다.

이 기록과 배포 ZIP의 합격은 구분합니다. 공개된 커밋·태그·CI·ZIP·SHA-256·새로 푼 패키지의 검사 결과는 [해당 GitHub Release](https://github.com/mk0000001/luma-player/releases)의 본문과 자산 manifest에서 확인합니다. 이 문서가 아직 만들지 않은 배포물이나 CI의 완료를 뜻하지는 않습니다.

### 자동 검사와 설치기

| 검사 | 관측한 결과 | 범위 |
| --- | --- | --- |
| 최종 소스 빌드 CTest | **13/13 통과, 43.01초** | `release-source-build-test.txt`. 기본 opt-in 네이티브 슬롯은 건너뛰므로 아래 실물 검사와 구분 |
| 자막 워커 QtTest | **15/15 통과** | 모델 준비·상주·최신 요청·취소 등 워커 회귀 |
| 패키징 검사 | **16/16 통과** | `package-final.txt`. 스크립트/패키지 구조 검사이며 최종 배포 ZIP 재실행과는 별개 |
| 번역 모델 설치기 | **8/8 통과** | `installer-final.txt`. 작은 HTTP 검증 파일의 다운로드·해시 흐름, 실제 약 2.50 GB 모델 재사용, Windows PowerShell 5.1 실행 확인 |

설치기 확인은 빈 환경에서 2.50 GB 모델 전체를 새로 내려받은 검증으로 확대하지 않습니다. 이미 유효한 모델이 있으면 크기·SHA-256을 확인해 재사용하는 경로를 실제 모델로 확인했습니다. [사용자 설치 안내](../INSTALL-TRANSLATION.md)를 따르며 설치 후 번역은 로컬에서 실행합니다.

### 실제 NPU 자막 준비와 재생 의도

`release-ui.txt`는 **6/6 통과**했습니다. 실제 NPU 인코더/OpenVINO와 CPU 디코더를 사용했고, 당시 OpenVINO 컴파일 캐시는 이미 준비돼 있었습니다.

| 동작 | 관측 시간 |
| --- | --- |
| 이 실행의 최초 자막 준비 | **2,127ms** |
| 탐색 후 자막 준비 | **1,780ms** |
| 즉시 수동 일시정지 검사 | **1,125ms**, 준비가 끝나도 사용자의 일시정지 유지 |

**2,127ms를 콜드 시작 시간이라고 부르지 않습니다.** 이전 `actual-ui-npu.txt` 실행에서는 최초 41.819초, 재사용 1.787초를 관측했고 그 실행 전체는 4통과/1실패였습니다. 위 최신 실행은 수정 후 6통과/0실패입니다. 서로 다른 캐시 상태·검사 실행의 수치로 보며 첫 설치·드라이버·모델·동시 부하가 같은 결과를 낸다고 보장하지 않습니다.

15초 인식 구간과 재생 전·탐색 후 최소 10초 준비를 사용합니다. 선행 분량이 30초 미만이면 다음 구간을 준비하므로 목표는 **30초 이상, 실제 약 30~45초**이며 수동 일시정지 중 42.6초도 관측했습니다. 30초가 상한은 아닙니다. 상주 모델·최신 요청 우선과 수동 일시정지를 보존합니다. 실패 재시도는 2/4/8초로 제한하며 이후 자막 없이 계속할 수 있고 AI 설정은 유지합니다. ASR의 NPU 성공은 별도 번역 모델의 NPU 성공을 뜻하지 않습니다.

### 실물 디스크의 AI 선행 활성화 회귀

실제 GUI에서 AI를 먼저 켜면 First Play 끝에서 자막 버퍼가 재생을 멈춰 메뉴에 들어가지 못하는 오류를 재현했습니다. 수정 전 보고서는 `firstPlayEverHeld=true`, `menuReady=false`였습니다. 메뉴/First Play를 본편 자막 타임라인과 구분하고 EOF의 불필요한 대기를 해제한 뒤 같은 실물 경로를 다시 검사했습니다.

최종 `release-ui.txt`의 실물 전환 검사와 `release-disc-navigation.json`에서 다음을 확인했습니다.

- **22.594초**에 First Play에서 메뉴로 자연스럽게 진행.
- `firstPlayEverHeld=false`, `menuReady=true`, `aiStayedEnabled=true`.
- First Play를 건너뛰어 메뉴에 도달한 결과가 아니라 인트로 시간 진행을 확인한 결과.

본편의 안정 기본값은 소프트웨어 디코딩과 10초 디스크 캐시입니다. 메뉴 분류는 타이틀 플래그와 First Play 매핑을 사용하며 길이가 0인 유효한 인터랙티브 메뉴도 인정합니다. 특정 메뉴 길이를 합격 조건으로 하드코딩하지 않습니다. 하드웨어 경로에서는 표면 할당 오류 8회를 재현했으므로 이를 안정 기본값으로 표시하지 않습니다.

### AI를 켠 본편·탐색·일시정지 결합 검사

실제 메뉴의 **ALL PLAY를 마우스로 눌러** 타이틀 1 본편에 들어간 뒤 수동으로 일시정지할 때까지 **4분 17초** 자연 진행했습니다. 이전 약 20초 정지는 이 실행에서 재현되지 않았습니다.

`disc-continuous-final.json`의 8개 표본은 본편 50.454→85.485초 구간을 약 5초마다 관측했습니다. 모든 표본에서 재생·AI가 켜져 있었고 caption hold는 false, AI 재시도는 0이었습니다. 이 구간의 디코드 영상 카운터는 **4,648→6,322**, 재생 오디오 버퍼는 **19,582→26,444**, 자막 선행은 **19.5~29.6초**였습니다. 본편 125.447초에는 NPU 백엔드·24.553초 선행·원문 cue 12개·번역 5개를 관측했습니다. 이는 번역 정확도나 모든 cue의 완료를 뜻하지 않습니다.

수동 pause 후 슬라이더로 **660.037초**로 이동했을 때는 hold=true·선행0·재생 의도=false였습니다. 준비 후 **15.863초 이상 분량**을 확보해 hold가 풀려도 pause는 유지됐고 AI도 켜져 있었습니다. 이후 수동 Play로 11분 이후의 영상 진행을 다시 확인했습니다. 전체화면 캡처 정책 재적용과 로컬 캡처 제외를 확인한 뒤 Alt+F4로 정상 종료했습니다.

**완전한 무지터로 표현하지 않습니다.** 같은 실행의 VLC 로그에는 **49~216ms 지연 프레임 5개**가 기록됐습니다. 약 20초에서 멈추는 버퍼 교착 수정과 간헐적인 프레임 지연은 별개입니다. 별도 소스 검사에서 확인한 네 번 탐색·챕터·메뉴 결과도 이 결합 실행의 모든 조건을 대신하지 않습니다.

후반에는 긴 문장의 번역 응답 `finish_reason!=stop` 이후 번역 9개 상태에서 후속 UI 번역 큐가 멈추는 문제를 관측했습니다. 수정 후 잘린 생성·잘못된 응답·HTTP 중단·시간/크기 초과는 해당 cue만 제외하고 같은 모델 프로세스로 다음 cue를 처리합니다. 합성 HTTP 서비스 검사 **15/15**와 UI의 설정·세대·제외 목록 회귀가 통과했습니다. 실제 콘서트 번역의 품질·지연과 배포 ZIP의 재검사는 별도입니다. 끝 위치 100% 탐색도 소유 H.264/AV1 파일에서 각각 **3/3** 통과했으며 자막 대기·재시도 없이 수동 pause와 AI 설정을 유지했습니다.

### 소유 영상·음성 fixture와 요청 GPU 설정

| 검사 | 결과와 해석 |
| --- | --- |
| `owned-media-arc.txt` | **10/10 통과**. AV1, HDR 소스 감지·SDR 출력·광학 흐름 차단, FLAC/ALAC, 요청 Arc 설정의 네 가지 재열기 복원 조합 |
| `owned-media-nvidia.txt` | **6/6 통과**. 요청 NVIDIA 설정의 AV1/두 오디오 파일 × 일시정지/재생 네 가지 재열기 복원 조합 |
| HDR fixture | 실제 디코드에서 **PQ/BT.2020**을 감지하고 현재 SDR 디스플레이의 출력 transfer를 **gamma2.2**로 관측. 광학 흐름 요청은 HDR 사유로 차단되고 시계 진행 유지 |
| FLAC / ALAC | **96 kHz·모노**, lossless 메타데이터와 **WASAPI** 출력·재생 시계·일시정지/재개 확인 |
| GPU 재열기 | 새 엔진을 열 때 로드 완료 전에 요청한 seek가 복원되고, 일시정지 의도와 재생 재개가 유지됨 |

GPU 보고서의 어댑터는 **요청 이름**입니다. 현재 API로 실제 출력 어댑터의 식별자를 조회하지 않았으므로 `adapterIdentityVerified=false`를 유지합니다. 목록에 이름이 있거나 재생에 성공했다는 이유만으로 실제 장치 선택까지 검증했다고 선언하지 않습니다. 파일 영상 설정의 결과를 ASR/번역 GPU 실행 결과와도 혼동하지 않습니다.

현재 디스플레이는 SDR입니다. 이 검사는 HDR 소스 메타데이터·SDR 출력 상태·필터 차단을 확인한 것으로, HDR 디스플레이 패스스루·색 정확도·Dolby Vision 실물 파일·인증 Dolby Vision 출력을 검증한 것은 아닙니다. 무손실 디코드와 WASAPI 시계 진행도 비트 퍼펙트·독점 출력·HD 오디오 비트스트림 인증과 다릅니다.

파일용 광학 흐름은 [별도 합성 검사](2026-11-05-motion-interpolation.md)에서 360 fps 타임스탬프를 확인했지만1080p 예시는 약 1.4 출력 fps였습니다. 실제 360 Hz 표시 또는 실시간 360 fps 보장은 없습니다. 디스플레이 혼합은 실제 모니터 주사율을 사용하며 libVLC 메뉴에는 프레임 보간을 적용하지 않습니다.

### 캡처 제외와 수동 UI 확인

`platform-green.txt`는 **4/4 통과**했습니다. 본창과 전체화면 자막 오버레이의 Windows 캡처 제외 적용 상태를 네이티브 API로 확인했습니다. 실제 보호 디스크 본창이 로컬 캡처에서 검정 영역으로 나타나는 것도 관측했습니다. 상용 화면의 캡처 결과는 공개하지 않습니다. 오버레이의 API 상태 확인과 본창의 실제 캡처 관측을 같은 종류의 증거로 확대하지 않습니다.

실제 UI에서는 다크/라이트 전환, 원문·독음·번역 세 줄 표시, 약 74px 전체화면 바, 5초 유휴 숨김과 움직임 시 표시를 확인했습니다. Windows 캡처 제외는 지원되는 OS/API 동작이며 DRM/HDCP 인증이나 모든 캡처 방법의 완전 차단이 아닙니다.

### 해석의 한계

- 현재 장치·설정·합성 fixture와 한 장의 디스크에 대한 결과입니다. 모든 디스크·GPU·모델·드라이버의 호환성 또는 장시간 무중단을 보장하지 않습니다.
- 번역의 확인된 가속은 Arc Vulkan이며 현재 번역 NPU 드라이버 오류와 NVIDIA OpenVINO 충돌은 CPU 대체로 기록합니다. 별도 [번역 검증](../local-translation.md)을 참고하세요.
- Linux CI 컴파일과 Windows 소스 빌드 검사는 Linux 실물 재생·Wayland·HDR 검증이 아닙니다. macOS는 현재 대상이 아닙니다.
- 배포본은 해당 릴리스에 적힌 소스 커밋·체크섬과 새로 푼 패키지 검사를 기준으로 판단합니다. 공개 태그와 바이너리를 덮어쓰지 않습니다.

## English

This record describes actual `0.1.0-alpha.2` **source-build** tests on Windows, using owned/generated fixtures and the current physical TUKI disc. Only metrics and test identifiers are published; commercial media/menu captures/transcripts, keys, certificates and user paths are excluded.

A source-build pass is distinct from a distributed ZIP pass. The relevant [GitHub Release](https://github.com/mk0000001/luma-player/releases) body and asset manifest own the exact commit/tag, CI links, archive/hash and independently extracted-package result. This document does not declare unfinished artifacts or CI complete.

### Automated and installer checks

- Final source CTest: **13/13 passed in 43.01 seconds**. Native opt-in slots skip by default; actual hardware runs below are separate evidence.
- Subtitle worker: **15/15 passed** for resident/warmup/request/cancellation regressions.
- Packaging checks: **16/16 passed**; script/structure checks do not establish execution of the final release ZIP.
- Installer: **8/8 passed**, including a small HTTP fixture's download/hash path, reuse of the actual approximately 2.50 GB model, and Windows PowerShell 5.1. This is not a fresh full-model download test. A valid existing model was size/hash verified and reused.

### NPU caption timing

The latest real-NPU buffer/UI run passed **6/6**. Its OpenVINO compiled cache was already warm: initial preparation **2,127ms**, seek preparation **1,780ms**, immediate-manual-pause check **1,125ms**, with pause preserved after preparation.

**The 2.372-second result is not cold start.** The earlier run measured 41.819 seconds initially and 1.787 seconds on reuse, and its overall result was 4 passed/1 failed. The latest corrected run was 5 passed/0 failed. Cache state, driver/model and concurrent load differ; these are observations, not universal latency promises.

The pipeline uses 15-second chunks and a ten-second minimum buffer. Another chunk is prepared when coverage is below 30 seconds, targeting **at least 30 seconds, typically about 30–45 seconds**; 42.6 seconds was observed while manually paused. Thirty seconds is not a cap. Resident/newest-request scheduling and bounded 2/4/8-second recovery preserve manual pause and leave AI enabled. Confirmed ASR NPU operation means NPU encoder plus CPU decoder, not full-model NPU execution or translation-NPU success.

### Physical navigation regression

AI enabled before opening the disc previously held First Play at its end and blocked the menu. The RED record had `firstPlayEverHeld=true` and `menuReady=false`. After separating menu/First Play from caption timelines and removing EOF buffering, the final native run passed **3/3**, naturally reaching the menu in **22.594 seconds** with `firstPlayEverHeld=false`, `menuReady=true`, `aiStayedEnabled=true`.

Software decoding and a ten-second cache are the disc default. Navigation classification uses title flags/First Play mapping and accepts valid zero-length interactive menus, without a hardcoded menu duration. Eight hardware surface errors were reproduced, so hardware decoding is not the stable default.

### Combined AI-enabled main-title test

Clicking **ALL PLAY with the mouse** entered main title 1, which advanced naturally for **4 minutes 17 seconds** before manual pause; the prior roughly 20-second freeze did not recur. Eight samples at approximately five-second intervals covered main-title 50.454→85.485 seconds: all were playing/AI enabled, hold=false and retry=0. Decoded video increased **4,648→6,322**, played audio buffers **19,582→26,444**, with **19.5–29.6 seconds** of captions ahead. At 125.447 seconds the run reported NPU, 24.553 seconds ahead, 12 source cues and 5 translations; these counts do not establish translation accuracy/completeness.

After a manual pause and slider seek to **660.037 seconds**, the state was hold=true, zero coverage and requested-playing=false. Preparation obtained **more than 15.863 seconds of coverage**, released the hold and preserved pause/AI. Manual Play then resumed video beyond minute 11. Fullscreen capture policy was reapplied/observed locally, followed by clean Alt+F4 shutdown.

This was **not zero jitter**: VLC logged **five late pictures, 49–216ms**. Fixing the approximately 20-second buffer deadlock is distinct from intermittent frame delay. Separately recorded four-seek/chapter/menu tests do not substitute for every condition of this combined run.

Later, a long translation response with `finish_reason!=stop` stopped the subsequent UI queue after nine translations. The fix skips individual invalid/truncated/timed-out/oversized cues and continues in the same model process. Synthetic HTTP service checks passed **15/15**, including subsequent valid cues, with UI generation/settings/skip-list regressions. Real concert translation quality/latency and extracted-package acceptance remain separate. Seeking to 100% in owned H.264 and AV1 fixtures also passed **3/3 each**, preserving manual pause and AI without caption hold/retries.

### Owned media and requested adapters

The requested-Arc run passed **10/10**, covering AV1, PQ/BT.2020 source detection, **gamma2.2 SDR output**, HDR optical-flow rejection, FLAC/ALAC and four reopen/seek/pause combinations. The requested-NVIDIA reopen run passed **6/6** across the four AV1/two-audio × paused/playing combinations.

FLAC and ALAC reported 96 kHz mono lossless metadata, WASAPI output and advancing/pause/resume clocks. Reopen tests issued seek before load completion, preventing normal playback from disguising a dropped restore request.

Adapter names are **requested settings**. Actual output-adapter identity was not queried; reports retain `adapterIdentityVerified=false`. Inventory/reopen success does not certify the selected physical adapter, nor does it establish an ASR/translation GPU backend.

The display was SDR. Metadata/SDR-state/filter checks do not certify HDR passthrough, color accuracy, a real Dolby Vision fixture or certified Dolby Vision output. Lossless decoding/WASAPI clock advancement is distinct from bit-perfect/exclusive/HD-audio-bitstream validation. Synthetic 360 fps timestamps passed separately; a 1080p optical-flow example produced about 1.4 output fps, not real-time 360 fps or physical 360 Hz presentation. No libVLC menu interpolation is implemented.

### Capture and UI

Native platform checks passed **4/4**, including main-window/fullscreen-caption capture-affinity state. An actual local capture of the protected playback main window showed a black area; commercial capture imagery is not published. Overlay API state and observed main-window capture behavior are distinct evidence. This is neither DRM/HDCP certification nor absolute prevention of every capture method.

Manual UI checks observed dark/light themes, three caption lines, an approximately 74 px fullscreen bar, five-second idle hide and movement reveal. Results remain bounded to the tested device/settings. Translation acceleration/fallback details are in the [separate report](../local-translation.md); Linux hardware playback/Wayland/HDR and macOS remain outside this Windows acceptance. Judge released binaries by their release-specific manifest and extracted-package result.
