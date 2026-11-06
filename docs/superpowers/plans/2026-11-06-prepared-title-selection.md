# 준비한 타이틀·가속 상태 수정 계획 / Prepared title and accelerator status plan

## 한국어

> 작업자는 `superpowers:subagent-driven-development` 또는 `superpowers:executing-plans`에 따라 실패 재현 → 최소 수정 → 검증 순서로 진행합니다.

**목표(Goal):** 준비한 디스크 자막을 실제 해당 타이틀에서 재생하고, 가속 대체 상태를 설명하고 재시도할 수 있게 합니다.

**구조(Architecture):** libVLC 엔진이 별도의 준비 타이틀 열기 경로에서 모드·native 메타데이터 선택·선택 확인까지의 일시정지를 담당합니다. UI는 완성 프로젝트를 전달하며 이어보기는 모드와 프로젝트를 보존합니다. 자막 준비 controller/dialog는 가속 대체 사유와 동일 요청 재시도를 담당합니다.

**기술(Tech Stack):** C++20, Qt6, libVLC3.0.24, 기존 native 로컬 모델 실행기.

**설계(Spec):** `docs/superpowers/specs/2026-11-06-prepared-title-selection.md`.

### 공통 제약

- 공개 설명은 한국어 다음 영어. 보호 디스크 키·인증서·미디어·메뉴·상용 생성 자막·개인 경로 제외.
- alpha.7/8 태그·파일 불변. 현재 alpha.8 생성·런타임·정적 라이브러리를 건드리지 않고 `build-playlist` 등 새 출력 폴더 사용.
- 길이 선택 오차200ms, 자막 연결1,000ms 유지. ordinary 메뉴·FirstPlay 유지. 모델·캡처 동의 변경 없음.

### 검토 중점

- 정상 본편인 direct index0이 메뉴로 오인되지 않고 이어보기·자막 연결에 사용되는가.
- native metadata 지연·실패·동일 길이 중복에서 다른 타이틀이 재생되지 않는가.
- 선택 확인 전의 수동 일시정지와 재생 재개 의도가 보존되는가.
- 메뉴/직접 모드의 동일 native 번호가 이어보기에서 섞이지 않는가.
- 재시도 후 취소된 세대의 늦은 완료/실패가 새 상태를 바꾸지 않는가.

### 작업1: 준비 타이틀 직접 재생·이어보기

파일: `src/media/playback.h/.cpp`, 필요 시 작은 선택 helper, `src/ui/main_window.h/.cpp`, `src/core/resume_history.h/.cpp`, 대응 playback/resume/UI 검사.

- [x] native 목록0/1·기본1 fixture에서 목표0이 선택되지 않는 실패, ordinary 메뉴 유지, 중복/없음/늦은 metadata·수동 정지 실패를 먼저 확인합니다.
- [x] 명시적 `PlaybackEngine::openPreparedDisc(source,surface,durationMs)`와 strict selection/확인 일시정지를 구현하고 direct index0 가드를 수정합니다.
- [x] 준비 프로젝트·일반 메뉴를 각각 여는 UI 연결과 모드/프로젝트를 보존하는 기존 호환 이어보기를 구현합니다.
- [x] fixture 회귀와 실물00003/index0의 실제 재생·탐색·자막 연결을 확인합니다.

### 작업2: 가속 대체 사유·재시도 표시

파일: `src/subtitles/caption_preparation.h/.cpp`, `src/ui/caption_preparation_dialog.h/.cpp`, 해당 controller/dialog 검사.

- [x] 가속 실패 사유 누락, 취소 후 마지막 장치 표시, 동일 요청 재시도·늦은 세대 거부를 먼저 실패시킵니다.
- [x] 제어된 장치 실패 안내를 전달하고 중지 화면은 마지막 장치로 표시하며 동일 설정·폴더의 checkpoint 재시도를 제공합니다.
- [x] 자체 fixture만으로 회귀 검사하고 실제 GPU 작업은 root의 장치 사용 조정 후 진행합니다. 최초 GPU 실패 원인은 증거 없이 확정하지 않습니다.

### 작업3: 검증·버전 공개

- [ ] 독립 코드 검토와 전체 검사, native/포터블/패키지 검사를 완료합니다.
- [ ] alpha.9 한/영 안내·소스·PR·태그·CUDA ZIP·체크섬·관측 범위 자산을 올립니다. CI 인프라 차단은 통과로 바꾸지 않습니다.

## English

Use the linked spec and RED→GREEN execution. Implement a strict explicit prepared-disc path with mode-aware index0, selection acknowledgment/pause intent and legacy-compatible mode/project resume records. Independently expose controlled fallback reasons, last-used canceled state and checkpoint-safe retry in the preparation controller/dialog. Keep alpha.8 execution/artifacts immutable; build separately. Test delayed/ambiguous native metadata, paused selection, mode-conflicting resume and canceled-generation callbacks, then independently review and verify native/portable behavior before source-matched alpha.9 publication. Public prose remains Korean first, and private disc material stays excluded.
