# 가사 시간·인식·번역 개선 계획 / Caption quality implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans for root-owned tasks. Independent native alignment and translation-service tasks use focused delegated workers with the same tests-first gates.

**목표 / Goal:** 원문 토큰 시간으로 가사를 표시하고 정확도 모델을 검증하며 최신 원문 문맥으로 한국어를 번역한다.

**구조 / Architecture:** Native Whisper가 SRT와 검증 가능한 token sidecar를 만든다. Qt가 이를 읽어 미디어 시각으로 바꾸고 토큰 경계에서 페이지를 나눈다. 번역 service는 payload fingerprint를 소유하고 UI는 미래 자막의 갱신 여부를 조회한다.

**기술 / Tech Stack:** C++20/Qt6, C++17/whisper.cpp b5454, CUDA/OpenVINO, PowerShell 5.1/7.

**Spec:** ../specs/2026-11-06-caption-quality-design.md

## 전체 제약 / Global constraints

- Python 런타임 없음, Windows 우선·Linux 빌드 유지.
- 기존 Kotoba와 유효한 NPU 검사 정책, 원문 텍스트 번역 경로를 보존.
- native timing schema와 Qt UTF-16 token 계약은 spec 그대로 사용.
- 공개 자료는 한국어 다음 영어. 보호 디스크 자료는 로컬에만 보관.
- alpha.6 불변; 결과를 새 버전으로 배포하고 실제 검증 범위만 기록.

## 검토 초점 / Review focus

- 느린 음절·긴 쉼에서 글자 수로 토큰 시각을 재분배하지 않는지.
- UTF-8 분할 token·결합 문자·같은 시점 token·잘못된 sidecar에서 손상/역행이 없는지.
- 취소·탐색·원문 변경 후 오래된 시각이나 번역이 표시되지 않는지.
- 미래 문맥 갱신이 현재 화면을 바꾸거나 재번역 루프를 만들지 않는지.
- 정확도 모델·DTW head·decoder 구조·GPU/CPU/NPU 선택이 실제 모델과 맞는지.

## 작업 1: 토큰 시각 소비와 페이지 경계 (root)

- [x] RED: 합성 비균일 token(1000, 4000, 4500ms)의 페이지 경계를 literal로 검증. 잘못된 UTF-8 범위·역행·중복·초과 시각 sidecar 거부와 SRT 호환 테스트.
- [x] `src/core/subtitle.h/.cpp`: `SubtitleTokenTiming`, `SubtitleCue::tokens` 추가. `captionPages`가 유효 token 시각을 사용하고 추정 경로는 token 없는 자막에 한정.
- [x] `src/subtitles/recognition_timing.h/.cpp`: bounded sidecar parser. `SubtitleJob`에서 요청 구간 offset을 정확히 한 번 적용하고 token까지 이동.
- [x] Qt 소비·페이징 회귀 GREEN, 전체 앱 검사.

## 작업 2: native 정렬과 정확도 모델 (native worker + root 설치 통합)

- [x] RED: standalone native timing/UTF-8·모델 구조/옵션 계약 tests.
- [x] `native/ai_worker`: full-v3 auto는 beam 5/DTW/flash attention off; Kotoba auto는 기존 fast. 정확한 구조를 확인한 full/turbo preset만 사용. `--quality auto|fast|accurate`를 지원하고 CPU 명시 경로도 native 결과를 만들 수 있게 한다.
- [x] schema대로 `.timings.json`을 쓰고 완료 표식 전에 두 출력 파일을 닫는다. `LUMA_ALIGNMENT`와 `LUMA_DECODING` 증거를 출력.
- [x] Root는 `dependencies.lock.json`, `scripts/install-asr-model.ps1`, fixture에 `accurate` 선택 프로필(MIT·고정 revision/SHA/크기)을 추가. 가중치는 core ZIP에 포함하지 않는다.
- [x] 소유 음성과 실물 디스크 로컬 비교, 실제 CUDA·메모리·지연·10/60초 관측. 품질이 개선된 관측과 미검증 부분을 구분.

## 작업 3: 최신 원문 문맥 번역 (service worker + root UI)

- [x] RED: 완료 결과 뒤 문맥 추가 시 `needsTranslation(cue)`가 true, 동일 문맥은 false. in-flight 문맥 변경 시 낡은 결과 signal 없음, 최신 결과 한 번.
- [x] `TranslationService::needsTranslation(const SubtitleCue&) const`를 추가하고 기존 payload fingerprint를 재사용. 모델/선호/취소는 기존 세션 불변식을 유지.
- [x] Root UI는 미래 완료 자막만 위 API로 재검사; 표시/지난 완료 자막은 보존. 실제 UI signal 회귀로 무한 재요청 방지.
- [x] 직접 작성한 일본어 부정·요청·존대·미완성 구절 A/B. 일반 지시를 실제 의미 개선 없이 채택하지 않는다.

## 작업 4: 통합 검증과 배포 (root)

- [x] 새 버전·한/영 문서, 전체 Qt/native/설치기 검사, 독립 코드 검토.
- [ ] 별도 portable 추출에서 실제 장치·자막 시간·문맥 번역 검증, 사용자 동의 상태 유지.
- [ ] 새 PR/Windows·Linux CI, 소스와 일치하는 태그·CUDA ZIP·체크섬·검증 보고서 공개. 이전 버전 보존.

## English

Execute the four tasks above through observed red/green tests: token-aware Qt timing, native alignment plus a pinned optional full model, bounded original-context refresh, then integration/device/package/release validation. Root owns Qt/UI/install/release work; delegated workers own only their named native or service files. No shared build directory or device benchmark runs concurrently. The user delegated routine implementation choices; this plan records them without introducing another approval gate.
