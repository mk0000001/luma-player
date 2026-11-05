# CUDA 패키지와 자막 품질 개선 구현 계획 / CUDA packaging and caption quality plan

> **For agentic workers:** Use superpowers:executing-plans with independent packaging and model investigations delegated under superpowers:dispatching-parallel-agents. Steps use checkboxes.

**목표 / Goal:** CUDA 음성인식·번역 런타임을 포함하는 새 Windows 배포본을 만들고, 비교 검증으로 자막 품질을 개선한다. / Publish a new Windows distribution containing CUDA ASR and translation runtimes, with measured caption quality improvements.

**설계 / Architecture:** 기존 CPU·Vulkan 경로와 CUDA 경로를 함께 제공한다. Whisper와 llama.cpp의 서로 다른 GGML DLL은 격리하고, 고정된 해시와 원본 라이선스를 검증한다. 모델 변경은 기존 개발·별도 검증 문장 및 직접 만든 음성으로 비교한 뒤 채택한다. / Keep CPU/Vulkan fallbacks alongside isolated Whisper and llama.cpp CUDA runtimes. Verify pinned hashes and original licenses. Adopt model changes after comparison on development and held-out text and owned audio.

**기술 / Tech Stack:** C++20, Qt 6.8.3, whisper.cpp b5454, llama.cpp b12125, PowerShell, CMake/CTest.

**Spec:** This document records the user's request for bundled CUDA and improved models; prior subtitle, licensing, versioning, and bilingual publication requirements continue to apply.

## Global Constraints

- 공개 설명은 한국어 다음 영어. / Public explanations use Korean first, then English.
- 기존 릴리스·태그·배포 파일을 덮어쓰지 않는다. / Preserve published releases, tags, and assets.
- 보호된 디스크 자료·키·인식 자막을 공개하지 않는다. / Never publish protected-disc material, keys, or recognized subtitles.
- 전역 CUDA 설치나 드라이버 변경 없이 앱 로컬 배포. / Use app-local CUDA without installing drivers or global CUDA.
- 모델 권리·하드웨어 실행·정확도는 실제 확인 범위만 기록. / Report only verified licensing, execution, and accuracy.

## Review Focus

- NVIDIA가 없는 PC: CUDA 로딩 실패가 CPU/Vulkan을 막지 않는다. / Missing NVIDIA hardware must not prevent CPU/Vulkan fallback.
- 잘못되거나 오래된 CUDA DLL: 패키징 단계에서 거절한다. / Reject corrupt or stale CUDA DLLs during packaging.
- GGML 버전 충돌: ASR와 번역 바이너리를 섞지 않는다. / Never mix ASR and translation GGML binaries.
- 자막 부정·의도·미완성 구절: 의미를 바꾸는 품질 퇴행을 거절한다. / Reject semantic regressions in negation, intent, and fragments.
- 동시 ASR·번역: 메모리 부족과 10초/60초 준비 규칙을 검증한다. / Check concurrent memory use and existing 10-second/60-second caption readiness.

## Tasks

- [x] **1. CUDA 배포 / CUDA distribution.** Added `-IncludeCuda`, verified isolated engines, and shared only three byte-identical NVIDIA DLLs after measuring the duplicated components above the asset limit. PowerShell 7/5.1 fixture suites each passed 28 cases; final archive verification remains Task 4.
- [x] **2. 번역 품질 / Translation quality.** Compared 196 requests across sampling, official Q6, and a general subtitle prompt. No clear semantic improvement justified changing the shipped Q4 model or settings. Recorded negative results and residual errors in `docs/translation-quality-validation.md`.
- [ ] **3. 음성인식 / Speech recognition.** Inspect decoding and candidate models against official sources. Compare quality/speed on owned speech, silence/noise, and actual disc privately. Adopt a bounded improvement without breaking timing, cancellation, or concurrent VRAM use. Add meaningful regression checks where behavior changes.
- [ ] **4. 통합 배포 / Integration and release.** Set `VERSION` to `0.1.0-alpha.4`; update bilingual docs/changelog. Run affected tests and full CTest, build a fresh CUDA package, verify extracted hashes/licenses and minimal-PATH ASR/translation execution, run native UI with the disc, obtain an independent review, publish source/PR/CI/tag/release with checksum and sanitized validation evidence.

## Execution Notes

- 기존 개발 폴더의 새 `codex/cuda-quality-package` 브랜치에서 진행한다. 대용량 로컬 SDK와 실행 중인 이전 배포본을 보존한다. / Work on the new feature branch in the existing development checkout, preserving local SDKs and the running previous distribution.
- 사용자에게서 반복 승인 없이 진행하라는 지시를 받았다. 일상적인 구현·버전 공개는 기존 승인 범위다. / The user delegated routine implementation and publication decisions; no repeated approval is needed.
- 추가 요청: 우클릭·가운데 클릭 후 영상 뒤에 가려진 재생바를 크기 변경 없이 복원한다. 네 가지 회귀 검사의 실패를 관측한 뒤 수정했으며, 실제 Windows의 파일 재생 화면에서 두 클릭과 전체화면 유휴 복원을 확인했다. 실물 디스크와 최종 포터블 검사는 Task 4에서 별도로 기록한다. / Additional request: restore transport stacking after right/middle clicks without resizing. Four regression rows were observed failing before the fix; native Windows file-playback checks confirmed both clicks and fullscreen idle restoration. Physical-disc and final-portable checks remain separately recorded in Task 4.
