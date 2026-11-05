# 변경 기록 / Changelog

## 한국어

공개 배포에는 불변 태그·GitHub Release·체크섬을 남깁니다. 로컬 시험 빌드와 실제 배포를 구분하며, 아래 프리뷰의 최종 검증은 각 릴리스 기록을 따릅니다.

### 0.1.0-alpha.2 — 검증 중

- TUKI 메뉴→본편 안정화: 소프트웨어 디코딩과 10초 디스크 캐시를 기본으로 선택. 새 연속 재생·네 번 탐색·챕터·메뉴 검사를 통과했으며 하드웨어 디코더의 표면 할당 오류는 재현·기록.
- 원문·한글 독음·한국어 번역을 분리해 표시. 네이티브 MeCab/IPADIC과 선택 Qwen3-4B 번역 추가. 포터블 코어에서는 약 2.50 GB 모델을 분리 설치.
- ASR NPU 인코더/OpenVINO 2024.1, CPU 디코더, 선택 Vulkan GPU와 CPU 대체. 번역은 확인된 Arc Vulkan/CPU 경로를 기본 배포하고 실패한 번역 NPU/OpenVINO GPU를 성공으로 표시하지 않음.
- AI 모델 백그라운드 준비, 탐색 간 모델 상주, 최신 요청 우선. 15초 구간·최소 10초 준비·30초 선행, 수동 일시정지 유지, 2/4/8초 재시도 후 재생 계속과 AI 설정 유지. **새 준비 방식의 실제 지연 개선은 검증 중.**
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

### 0.1.0-alpha.2 — validation in progress

- Stabilize the tested TUKI menu/main-title path with software decoding and a ten-second disc cache. A fresh playback/four-seek/chapter/menu sequence passed; hardware surface-allocation failures were reproduced and recorded.
- Separate original captions, Japanese Hangul readings and Korean translation. Add native MeCab/IPADIC and optional Qwen3-4B translation; install its approximately 2.50 GB model separately from the portable core.
- Use native NPU encoder/OpenVINO 2024.1 with CPU decoding, selected Vulkan GPU and CPU fallback for ASR. Default translation packaging uses verified Arc Vulkan/CPU; failed translation NPU/OpenVINO GPU runs are not reported as acceleration success.
- Add background ASR warmup, resident models across seeking and newest-request priority. Use 15-second chunks, a ten-second playback buffer, thirty-second lookahead, manual-pause preservation and bounded 2/4/8-second retries while leaving AI enabled. **End-to-end latency with the new warmup remains pending.**
- Add dark/light custom controls, SVG branding, Noto fonts, an approximately 74 px fullscreen bar with five-second inactivity behavior, and opt-in developer diagnostics.
- Add file output-adapter and separate AI device choices, capture-affinity status for both windows, and HDR/SDR diagnostics.
- Add file-only display blending and experimental 24–360 fps CPU optical-flow targets. Synthetic 360 fps timestamps passed; 1080p real-time throughput did not. Optical flow rejects HDR/unknown metadata.
- Keep final capture, HDR/lossless, ASR timing, portable package and CI results explicit in the [alpha.2 record](docs/releases/0.1.0-alpha.2.md).

### 0.1.0-alpha.1 — local development snapshot, 2026-11-05

- Establish native C++20/Qt 6, libVLC disc menus, libmpv files, local subtitle jobs and initial packaging scripts.
- This early local snapshot is not a published immutable release. A main-title freeze around 20 seconds was observed in some runs; cumulative intro counters did not establish sustained main-title stability.
- Historical records do not substitute for alpha.2 build/package validation.
