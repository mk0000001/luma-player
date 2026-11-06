# Kotoba NPU Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development or superpowers:executing-plans to implement task-by-task. Steps use checkbox syntax for tracking.

**Goal:** Kotoba 인코더를 실제 NPU에서 검증하고 NPU 우선 실행·성능 근거와 Blu-ray 오류 복구를 제공합니다.

**Architecture:** 기존 Whisper 로더를 사용하는 C++ 인코더 변환기와 명시 장치 ASR 실행기를 사용합니다. OpenVINO IR은 원래 Q5 모델에서 유도하고 사용자별 컴파일 캐시는 배포하지 않습니다.

**Tech Stack:** C++17/20, Qt6, pinned Whisper b5454, OpenVINO2024.1, PowerShell.

**Spec:** `docs/superpowers/specs/2026-11-06-kotoba-npu-design.md`

## Global Constraints

- 고정 Kotoba SHA-256 `4a3b92192b5d3578ff854a5876213e2e27af0c2d357492c2d14271e82c303658`, 537819875 bytes.
- NPU 성공은 명시 NPU에서 실제 추론한 경우에만 기록. 조용한 CPU 대체를 합격으로 세지 않음.
- 최소 10초 준비와 60초 선행·기존 수동 일시정지 유지.
- Python 실행 의존성 없음. 드라이버 변경은 사용자가 별도로 승인·완료한 공식 NPU 업데이트만 기록하며, 전원·보안 설정은 변경하지 않음.
- 공개 자료에 보호 디스크 키·인증서·영상·음성·메뉴·인식 자막 없음. 한국어 다음 영어.

## Review Focus

- 인코더가 다른 모델이거나 손상된 IR이면 사용을 거부하고 가용 대체 경로를 유지.
- NPU 컴파일 지연·취소·탐색에서 이전 작업이 현재 자막을 덮어쓰지 않음.
- OpenVINO 인코더 추론 실패 뒤 성공 마커/잘못된 자막을 내보내지 않음.
- Q5와 FP16·flash attention 차이를 내보내기 오류와 혼동하지 않음.
- AACS 파일 존재만으로 파싱·디스크 읽기 성공을 단정하지 않음.

### Task 1: Native encoder conversion and correctness

Files: `native/ai_worker/backend_info.inc`, `native/ai_worker/CMakeLists.txt`, new `native/ai_worker/encoder_*`, dedicated native tests.

Interfaces: read encoder tensors through a bounded native accessor; export static OpenVINO XML/BIN; opt-in native reference comparison.

- [ ] Add small deterministic graph tests for shape, normalization, nonzero attention/residuals and invalid tensors; observe RED.
- [ ] Implement tensor access/export and graph math, observe GREEN on OpenVINO CPU; no NPU claim from these tests.
- [ ] Compare a real Kotoba encoder and ASR result with explicit native baselines; record precision/attention settings.
- [ ] Propagate an actual OpenVINO inference failure; prove incompatible input cannot emit accelerated success.

### Task 2: Timing and device evidence

Files: `native/ai_worker/main.cpp`, ignored benchmark orchestration, public performance report.

Interfaces: per-request total/mel/encode/decode timing and encoder-call counts; explicit backend/device markers.

- [ ] Add a meaningful worker timing contract check, observe missing markers before implementation.
- [ ] Emit per-request timing deltas and encoder invocation counts; preserve the existing resident protocol.
- [ ] Measure cold/first/warm runs of public or owned audio on CUDA and NPU. Record unavailable device properties rather than treating zero GOPS as measured throughput.
- [ ] Calculate operation counts with precision/1MAC=2FLOPs assumptions. Report real-time, initial-buffer and same-latency budgets separately.

### Task 3: NPU-first installation and application behavior

Files: `scripts/install-asr-model.ps1`, staging/packaging safeguards, `src/subtitles/ai_backend.*`, subtitle-worker readiness tests as needed.

- [ ] Reproduce absent/mismatched/corrupt encoder and fallback ordering before changing selection.
- [ ] Add verified optional native encoder generation/installation with atomic XML/BIN publication and preserved old valid files.
- [ ] Run explicit NPU inference and measure cold/warm readiness. Keep failed/too-slow paths visible and preserve usable GPU/CPU fallbacks.
- [ ] Verify 10/60-second coverage with the physical disc and current settings.

### Task 4: AACS failure diagnosis and recovery

Files: `src/media/playback.*`, relevant native/media tests and playback guide as evidence requires.

- [ ] Identify the current error and reproduce without exposing key contents; separate transient/past/current logs.
- [ ] Write the regression for the verified cause or misleading error classification and observe RED.
- [ ] Implement the smallest supported local/configuration or error-reporting correction and observe GREEN.
- [ ] Re-open the connected disc and verify menu/main playback when access is available.

### Task 5: Version, review and release

- [ ] Run relevant native tests and full CTest, inspect each result, and obtain a fresh whole-branch review.
- [ ] Record numerical/NPU/TFLOPS/AACS limits in Korean-first/English-second docs.
- [ ] Build a new immutable version, independently extract/verify it, run Windows/Linux CI, then publish source/ZIP/checksum/evidence on GitHub. Never overwrite alpha.5.

## English

Execute the five tasks above: native encoder export/correctness; timing and precision-aware compute evidence; verified installation with fallback; evidence-led AACS recovery; full validation, independent review and a new immutable release. Model identity, genuine NPU execution, strict caption coverage, no Python runtime and private-disc exclusion remain binding constraints. Record only the official NPU driver update separately authorized and completed by the user; do not change power/security settings. Continue with the [caption/context/calibration plan](2026-11-06-caption-context-and-install-calibration.md).
