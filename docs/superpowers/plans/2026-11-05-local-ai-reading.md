# 로컬 AI·독음 구현 계획 / Local AI and readings implementation plan

> **구현 담당:** superpowers:executing-plans 절차로 항목별 구현·검증을 기록합니다. 이 초기 계획 이후 기본 테마는 다크/선택 라이트로 확장됐습니다.
> **For agentic workers:** Use superpowers:executing-plans task by task. The later refinement plan supersedes the initial light-only theme.

**Goal:** 일본어 원문+한글 독음, NPU→GPU→CPU 로컬 인식, Noto Sans와 흰색 UI를 실제 재생 앱에 제공한다. / Deliver Japanese originals with Hangul readings, NPU→GPU→CPU local recognition, Noto Sans and a light UI in the native player.

**Architecture:** MeCab dictionary readings feed a pure kana transliterator. A transparent Qt caption overlay uses bundled fonts. The asynchronous recognition worker owns ordered accelerator retries and reports the backend confirmed by inference output.

**Tech Stack:** C++20, Qt 6, MeCab/IPADIC, whisper.cpp/OpenVINO, libVLC/libmpv.

**Spec:** `docs/superpowers/specs/2026-11-05-local-ai-reading-design.md`

## Global Constraints / 공통 제약

- Korean first, English second in public descriptions. Upstream licenses stay verbatim.
- Keep exact MPLS/audio mapping, generation cancellation and bounded subprocess deadlines.
- NPU must actually execute inference before it is reported as used; failures fall back to GPU then CPU.
- No commercial disc content, keys or certificates in Git/release artifacts.
- Preserve Windows TUKI playback; keep Linux compiling.

## Review Focus / 검토 항목

- Cancellation during first-use NPU compilation must terminate the worker and not publish stale cues.
- A worker that silently falls back to CPU must never be labeled NPU/GPU.
- Unknown kanji, Latin-only and Korean captions must retain their original text.
- The caption overlay must follow resize/fullscreen and not intercept disc remote/mouse/keyboard input.
- Bundled fonts and native runtime dependencies must work from a freshly extracted portable folder.

### Task 1: 일본어 독음 / Japanese readings

- [ ] Add tests for kana digraphs, long vowels, sokuon, nasal, kanji/particles via an actual UTF-8 dictionary, and non-Japanese text; observe failures.
- [ ] Add `src/subtitles/japanese_reading.{h,cpp}` with `kanaToHangul(QString)` and `JapaneseReading::annotate(QString)`; bound input and handle missing dictionary honestly.
- [ ] Pin MeCab, IPADIC source, private font assets and licenses. Compile the UTF-8 dictionary without shipping Python.
- [ ] Run reading/core tests and commit. Expected: all pass and an actual dictionary resolves kanji.

### Task 2: NPU 우선 인식 / NPU-first recognition

- [ ] Test ordered candidates, actual-backend confirmation, failure retry, timeout and cancellation using a deterministic native child fixture; observe failures.
- [ ] Add `src/subtitles/ai_backend.{h,cpp}`; extend `SubtitleJob` to try NPU→GPU→CPU and expose selected backend.
- [ ] Build pinned native whisper.cpp with OpenVINO, lock runtime/model artifacts, and stage redistributable runtime and notices.
- [ ] Run actual inference on NPU/GPU and record success or concrete fallback evidence; run worker suite and commit.

### Task 3: 자막·Noto·화면 / Captions, Noto and UI

- [ ] Test managed-caption timing and original/reading switching; observe failures.
- [ ] Add `src/ui/caption_overlay.{h,cpp}` and `src/ui/theme.{h,cpp}`. Load Noto fonts privately, style the light native UI, add reading controls and actual accelerator status.
- [ ] Route generated and imported SRT captions through the managed overlay; preserve external ASS/PGS engine support. Save the selected original/reading form.
- [ ] Verify menu keyboard routing and overlay click-through; run suites and commit.

### Task 4: 검증·공개 / Verification and publication

- [ ] Repeat physical TUKI menu/main title/chapter/seek/shutdown and AV1/real AI checks.
- [ ] Update Korean-first/English-second README, changelog, feature matrix, PR/issue/release descriptions and test evidence.
- [ ] Obtain one fresh whole-change code review, fix important findings and run required checks.
- [ ] Build a new portable package in a new output directory, test it, push, verify CI, merge the reviewed PR and publish an immutable version tag/release.
