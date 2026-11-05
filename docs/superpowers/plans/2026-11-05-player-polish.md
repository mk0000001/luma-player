# 플레이어 안정화·개편 계획 / Player reliability and refinement plan

> **구현 담당:** 아래 독립 영역은 superpowers:dispatching-parallel-agents로 나누고 통합 후 전체 변경을 검토합니다. 작업별 실패 재현→수정 후 통과 근거를 보존합니다.
> **For agentic workers:** Use superpowers:dispatching-parallel-agents for the independent ownership domains below, then integrate and obtain a whole-change review. Preserve task-level RED→GREEN evidence.

**Goal:** 츠키 실물 재생을 안정화하고 어두운 기본 UI·5초 자동 숨김·번역·빠른 자막·GPU 선택·캡처 차단을 제공한다. / Stabilize physical TUKI playback and deliver the requested UI, captions, device selection and capture behavior.

**Architecture:** Keep the C++20/Qt6/libVLC/libmpv app. Native asynchronous workers provide local ASR and translation. Window capture policy and device/display inspection are isolated platform services.

**Spec:** `docs/superpowers/specs/2026-11-05-player-polish-design.md`

## Global constraints / 공통 제약

- Windows first; keep Linux compiling.
- Korean first, English second in public descriptions; upstream licenses unchanged.
- No Python/cloud requirement, keys, certificates or commercial disc content in Git/release artifacts.
- NPU/GPU selection means actual confirmed execution. Capture exclusion is not DRM certification.
- Do not edit another worker's owned files; primary owns CMake and common lock.

## Review focus / 검토 항목

- Menu/title/seek transitions must preserve advancing frames, audio and time after intro counters already exist.
- A stale AI or translation result after seeking/source/model changes must not reappear.
- Fullscreen inactivity must not hide open controls, interrupt input, or lose the ability to reveal the bar.
- Protected video and the separate caption window must share capture policy; API failure must be visible.
- Device names, HDR metadata and model licenses must support the exact capabilities claimed in the portable package.

### Task 1: 재생 정지·플랫폼 / Playback and platform (primary)

- [ ] Strengthen physical regression checks and reproduce graphics errors before changing decoder defaults.
- [ ] Verify stable menu→title, repeated seeking/chapter/menu and clean shutdown; keep software fallback bounded and explicit.
- [ ] Add platform GPU/display/capture APIs and verify Windows capture affinity and lossless/HDR fixtures.

### Task 2: 화면·상호작용 / UI and interaction (UI worker)

- [ ] Implement dark default/light toggle, custom controls/icons/logo and compact fullscreen bar.
- [ ] Test five-second inactivity, reveal, interaction lock and caption layout; integrate root-provided device/capture APIs and translation hooks.
- [ ] Provide self-owned preview content for visual QA; never publish commercial menu/video/subtitle captures.

### Task 3: 로컬 번역 / Local translation (translation worker)

- [ ] Select and pin a commercially redistributable native model/runtime using primary sources.
- [ ] Implement asynchronous generation-aware translation, caching and bounded worker lifecycle; test failures and cancellation.
- [ ] Run actual Japanese→Korean inference and record backend and latency; hand over integration and staging instructions.

### Task 4: 빠른 음성 인식·기기 선택 / Faster ASR and device selection (primary)

- [ ] Test explicit NPU/GPU/CPU mode and GPU identifiers, including integrated GPUs.
- [ ] Reduce extraction/chunk latency and reuse persistent model state where useful; compare actual disc timings.
- [ ] Retest native NPU and selected GPU inference with honest status.

### Task 5: 통합·버전 / Integration and versions (primary)

- [ ] Integrate isolated changes, pin every dependency and bundle licenses; run required suites and real media tests.
- [ ] Obtain one fresh review, fix important findings with regressions, and validate a freshly extracted package.
- [ ] Publish Korean-first/English-second progress, source, checks and immutable versioned release assets to GitHub; retain historical versions and disclose limitations.
