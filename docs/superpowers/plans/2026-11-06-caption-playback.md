# 자막과 재생 조작 구현 계획 / Caption and playback implementation plan

## 한국어

**목표:** 긴 자막·번역·화면 조작 문제를 수정하고 모델·외부 자막·이어보기를 새 프리뷰로 검증합니다.
**구조:** core는 시간 분할·coverage, UI는 독립 영상/자막/재생바, subtitle 서비스는 외부 파일과 ASR·번역 장치 경계를 담당합니다.
**기술:** C++20, Qt6, libVLC/libmpv, FFmpeg, whisper.cpp, llama.cpp. Python·외부 추론 API 없음.
**설계:** [설계 문서](../specs/2026-11-06-caption-playback-design.md). 사용자에게 받은 자율 진행 방식으로 계속합니다.

### 1. 자막 분할과 선행 버퍼 — root

- [x] tests/core_tests.cpp에 10초 준비 후에도60초까지 이어지는 실패 검사, 문장/장문/결합 문자 분할과 시간 보존 검사를 작성하고 RED 확인.
- [x] src/core/subtitle.*에 최소10초와 선행60초를 분리하고 표시용 `captionPages`를 구현. 원본을 변경하지 않음.
- [x] src/subtitles/pcm_buffer.cpp의 초기 burst를75초로 맞추고 main_window의 페이지별 독음·번역·엄격한 준비 정책에 연결.

### 2. 화면 배치와 캡처 — ui_polish, root 통합

- [x] caption_layout.*의 실제 그리기·긴 문자열·좁은 창 RED/GREEN.
- [x] fullscreen_controls.*와 video_surface.*에서 native 영상/재생바를 겹쳐 배치하고 숨김 전후 anchor geometry가 같은지 검사.
- [x] root는 main_window에서 native 영상 핸들·캡처 대상·자막 하단 여백을 연결. 본창과 재생바를 캡처 대상에서 제외하지 않음.

### 3. 번역 품질과 GPU — local_translation, review_player, root 통합

- [x] parser의 깨진 Unicode/제어 표식 RED/GREEN과 다음 cue 계속 처리 검증.
- [x] 같은 모델·장치의 소유 예문 A/B로 부정·화자·미완 문장 의미와 지연을 평가한 뒤 prompt 선택.
- [x] 선택 CUDA 설치기·번역 전용 장치 탐색/대체 경로를 검사하고 root가 별도 설정 UI에 연결.

### 4. 외부 자막·키·이어서보기·ASR — root

- [x] 외부 형식·잘못된 인코딩·자동 AI 전환, 입력 위젯 키 보존, 음소거 복원, 파일/디스크 위치 보존의 실패 검사 작성.
- [x] 로컬 자막 import, 재생 단축키, 사용자 설정 기반 resume, 실제 고품질 ASR 모델 설치·장치 실행을 구현.
- [ ] 전체 CTest, 소유 파일, 현재 TUKI의 메뉴/본편/탐색/키/캡처/버퍼/모델을 확인. 새 버전·소스·불변 태그·ZIP·체크섬·검증 기록을 한국어 다음 영어로 공개.

**검토 초점:** grapheme 분할 손상, 짧은 cue의 시간 역전, 메뉴가 resume를 덮어씀, native Tool 포커스·캡처 범위, 모델 실패가 가속 성공으로 잘못 표시됨을 각 소유 task에서 검사합니다.

## English

> For agentic workers: use superpowers:executing-plans or subagent-driven-development task by task. Independent UI and translation/backend tasks are delegated; root integrates common files and verifies the whole branch.

Goal: implement the linked design as a tested Windows preview with Linux compatibility. Root owns core paging/coverage, MainWindow integration, subtitle import, keys, history and ASR. UI work owns measured layout/native floating windows; translation workers own validation, meaning A/B and optional CUDA. Each component needs a reproduced RED and GREEN, followed by full-suite and physical acceptance before immutable release publication. Preserve valid Unicode and original text, keep timing inside source cues, never let menus overwrite main-title history, retain control keyboard input, and report actual backend/fallback rather than device inventory alone.
