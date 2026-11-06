# 자막과 재생 사용 안내 / Captions and playback guide

`0.1.0-alpha.7` · 2026-11-06

## 한국어

### AI 자막 준비와 재생

**AI 자동자막 켜기**를 선택하고 **음성 언어**를 맞추세요. 재생 시작 또는 탐색 후에는 현재 위치부터 **10초의 음성 구간**에 대한 인식을 완료한 뒤 재생합니다. 끝까지 10초보다 적게 남았다면 남은 구간을 준비합니다. 이 숫자는 준비된 미디어 분량이며, 인식 작업이 10초 안에 끝난다는 뜻은 아닙니다.

최소 준비량을 채운 뒤에도 작업을 멈추지 않고 **현재 위치에서 60초 앞까지** 계속 준비합니다. 15초 단위 처리와 실제 탐색 위치 보정으로 준비량이 목표를 조금 넘을 수 있습니다. 처음부터 60초를 모두 준비해야 재생하는 방식은 아닙니다. 디스크 메뉴와 시작 안내는 본편 자막 버퍼를 기다리지 않습니다.

준비 중 직접 일시정지하면 준비가 끝나도 일시정지를 유지합니다. 실패하면 상태와 재시도를 표시하며 AI 선택을 자동으로 끄지 않습니다. **자막 다시 시도**로 재시도하거나, **자막 없이 계속**으로 대기를 건너뛸 수 있습니다. 후자를 선택해도 AI는 계속 준비합니다. AI를 직접 끄면 자막 준비 대기도 해제합니다.

### 원문·독음·번역과 긴 자막

**원문**, **일본어 한글 독음**, **한국어 번역**을 각각 켜거나 끌 수 있습니다. 독음과 번역은 원문과 별도로 보관됩니다. 음성 인식과 번역의 장치 설정도 독립적이며, 화면의 실제 사용 장치를 확인하세요. 음성 인식의 NPU 표시는 번역까지 NPU로 처리한다는 뜻이 아닙니다.

처리 순서는 **음성 → 원문 인식 → 원문·문맥을 한국어로 번역**입니다. 번역 모델에 음성을 직접 보내지 않습니다. 앞뒤 60초 안에서 준비된 원문을 참고하고 대상 자막만 번역합니다. 새 원문이 도착하면 앞으로 표시할 번역을 갱신하며, 이미 읽고 있는 번역은 그 구간 동안 유지합니다. 인식한 원문이 틀리면 해석도 틀릴 수 있습니다.

선택 full large-v3 모델은 native 음성 정렬 시각으로 페이지 시작·끝을 정합니다. 토큰 경계와 쉼을 보존하며, 정렬을 얻지 못한 구간과 일반 자막은 구간 시각으로 처리합니다. 가사 전용 강제 정렬이나 완벽한 노래 인식은 보장하지 않습니다. 설치·실측은 [모델 안내](asr-model-validation.md)를 참고하세요.

긴 원문은 짧은 표시 페이지로 나누고, 원래 자막 시간 안에서 차례로 보여 줍니다. 선택한 독음과 번역도 같은 원문 페이지에 맞춥니다. 짧은 시간에 너무 많은 페이지가 번쩍이지 않도록 짧은 조각은 합칠 수 있습니다. 화면 폭을 넘으면 공백 없는 한글·일본어·긴 단어도 줄바꿈하며, 글자 수에 맞춰 배치를 조정합니다. 원본 파일을 잘라 덮어쓰지 않습니다. 결합 문자와 이모지는 하나의 글자 묶음으로 보존합니다.

**표시 자막 저장**은 선택한 표시 조합을 UTF-8 SRT로 저장합니다. 자동 인식·독음·번역의 의미와 정확도는 원문과 함께 확인하세요. 한 번역 구간이 잘리거나 유효하지 않으면 해당 구간을 건너뛰고 다음 번역을 계속할 수 있습니다. 모델이나 실행기 자체의 오류는 별도로 표시합니다. 선택 번역 모델 설치는 [번역 설치 안내](INSTALL-TRANSLATION.md)를 참고하세요.

### 외부 자막 파일

미디어를 연 뒤 **디스크 → 자막 파일 추가**로 로컬 자막을 선택하거나 자막 파일을 플레이어에 놓으세요. 지원하는 텍스트 형식은 **SRT, ASS, SSA, VTT, SMI/SAMI**입니다. 미디어와 같은 폴더에 이름이 정확히 같은 자막이 있으면 자동으로 찾습니다. 예: `movie.mkv`와 `movie.srt`. 언어 접미사가 붙은 `movie.ja.srt`는 직접 선택하세요.

UTF-8과 BOM이 있는 UTF-16 SRT는 엄격하게 읽습니다. 잘못된 바이트나 불완전한 UTF-16 문자를 깨진 글자로 조용히 바꾸지 않습니다. 정상적인 일본어·한글·결합 문자·이모지·음표는 보존합니다. 다른 형식은 로컬 FFmpeg로 시간과 본문을 변환하며 일반 색상·장식 태그는 일반 텍스트로 정리합니다. ASS의 위치·색상·애니메이션을 그대로 재현하는 방식은 아닙니다.

CP949와 Shift-JIS도 변환할 수 있습니다. SMI에 문자 인코딩이 명시되어 있으면 우선하며, 선언이 없으면 추정하므로 모호한 파일은 잘못 해석될 수 있습니다. 글자가 맞지 않으면 원래 인코딩을 지정할 수 있는 편집기에서 열어 **UTF-8로 다시 저장**하세요. 입력과 변환 결과는 각각 8 MiB로 제한하며 변환은 10초가 지나면 중단합니다. 자막을 읽거나 변환하지 못하면 오류를 표시하고 **현재 선택한 음성 언어의 AI 자동자막**을 켭니다. 이때도 음성 인식 모델과 실행기가 필요합니다.

### 화면과 키보드

재생바는 영상 위에 떠 있으며 나타나거나 사라져도 영상 크기와 위치를 바꾸지 않습니다. 전체화면 재생바 높이는 **74 px**입니다. 마우스가 재생바 위에 있더라도 약 **5초** 동안 움직이지 않으면 재생바와 커서를 숨기며, 다시 움직이면 나타납니다. 메뉴 사용·슬라이더 끌기·버튼을 누르고 있는 동안과 키 입력 중에는 조작할 수 있도록 유지합니다. 자막만 재생바 위로 옮겨 겹침을 피합니다.

다음 키는 재생 화면에 초점이 있을 때 사용합니다. 텍스트 입력, 목록, 선택 상자, 슬라이더 등의 기본 키 조작은 유지합니다. 버튼에 초점이 있을 때 Space는 해당 버튼을 누르는 동작입니다.

| 키 | 동작 |
| --- | --- |
| F / Enter / F11 | 전체화면 전환 |
| Esc | 전체화면 나가기 |
| Space | 재생 / 일시정지 |
| ← / → | 10초 뒤로 / 앞으로 |
| ↑ / ↓ | 음량 5% 올리기 / 내리기 |
| M | 음소거 / 이전 음량으로 복원 |
| Ctrl+방향키 | Blu-ray 메뉴의 선택 이동 |
| Ctrl+Enter | Blu-ray 메뉴의 선택 실행 |

디스크 메뉴는 마우스, 화면의 방향/확인 리모컨, **메뉴** 버튼으로도 조작할 수 있습니다. 일반 방향키는 재생 위치와 음량을 조절하므로 디스크 메뉴에는 Ctrl 조합을 사용하세요.

### 이어서 보기

왼쪽의 **이어서 보기**는 최근 유효한 기록을 엽니다. 같은 파일을 다시 열어도 저장한 위치를 복원합니다. 기록은 로컬 사용자 설정에 저장하며, 파일의 경로·크기·수정 시각이 달라지면 기존 기록을 적용하지 않습니다.

디스크는 드라이브 문자만이 아니라 디스크 정보와 본편 타이틀을 구분해 기록합니다. 같은 디스크의 해당 본편 타이틀을 확인한 뒤 위치를 복원하며, 메뉴나 시작 안내의 0초가 본편 기록을 덮어쓰지 않게 합니다. 처음 3초 미만과 끝까지 5초 미만인 지점은 이어보기 대상으로 남기지 않습니다. 저장된 길이와 현재 길이의 호환성 검사도 통과해야 합니다. AI가 켜져 있으면 복원한 위치에서도 같은 최소 자막 준비 규칙을 적용합니다.

### Windows 캡처 제외와 확인 범위

보호 디스크는 지원되는 Windows 캡처 제외 API를 영상 창과 자막 창에 기본 적용합니다. **본창, 설정, 메뉴와 재생바는 캡처할 수 있게 유지**합니다. Parsec 등에서 개발용 영상을 보려면 **설정 → 개발자 모드 → 개발용 영상 캡처 허용**을 사용합니다. 개발자 모드와 캡처 허용은 각각 안내·확인 체크·별도 동의가 필요하며 취소하면 허용되지 않습니다. 개발자 모드를 끄면 허용도 해제됩니다. 안내는 적법한 이용 책임을 설명하며 일괄 법적 면책을 부여하지 않습니다. 화면의 적용 상태를 확인하세요. 이 기능은 DRM·HDCP 인증이나 모든 캡처 방법을 막는 보증과는 별개입니다.

2026-11-06 기준으로 구현과 확인 범위는 다음과 같이 구분합니다.

| 항목 | 확인한 범위 |
| --- | --- |
| 전체화면 재생바 | 소유한 시험 영상의 실제 Windows 앱에서 74 px, 영상 영역의 5초 비활동 숨김, 재표시 시 영상 배치 유지 관찰 |
| 재생바 위 정지 포인터 | 별도 합성 회귀 시험으로 5초 숨김과 움직인 뒤 재표시, 메뉴·끌기·키 입력 유지 확인. 해당 포인터 위치의 네이티브 검증과 구분 |
| 보호 디스크 캡처 | 로컬 실제 관찰에서 영상 부분은 검게 나오고 본창과 재생바는 보임. 상용 영상·메뉴·자막 캡처는 공개하지 않음 |
| 긴 자막 배치 | 합성 시험으로 공백 없는 문자열, 명시적 줄바꿈, 좁은 창, 결합 문자와 이모지의 보존 및 화면 안 배치 확인 |
| 외부 자막 | 소유한 작은 파일의 실제 FFmpeg 변환과 소유 미디어의 가져오기 회귀 통과. 모든 형식·드롭 위치·자동 검색·AI 전환을 실제 앱에서 완료했다는 뜻은 아님 |
| 이어서 보기 | 파일/디스크 식별·저장 규칙과 소유 파일을 닫은 뒤 복원하는 경로 확인. 실물 디스크의 전체 재개 경로 확인은 별도 |
| AI·번역 | 준비 정책과 독립 표시 구현. 모든 언어·모델·장치에서의 인식 정확도, 번역 완전성, 60초 선행의 실제 지속 달성을 확인한 것은 아님 |

## English

### AI caption preparation

Enable **AI 자동자막 켜기** and select the spoken language. Before starting or after seeking, recognition must cover **ten seconds of audio from the current position**, or the remaining duration near the end. This is media coverage, not a promise that inference finishes in ten wall-clock seconds.

After the minimum is ready, work continues toward **60 seconds ahead**. Fifteen-second processing chunks and seek correction may take coverage slightly beyond that target. Playback does not wait for the full 60 seconds initially. Disc menus and startup navigation do not wait for the main-title caption buffer.

A manual pause remains paused when preparation finishes. Failures show status and retry without automatically clearing the AI setting. Use **자막 다시 시도** to retry or **자막 없이 계속** to bypass the wait while AI continues preparing. Manually disabling AI also releases the caption wait.

### Source, reading, translation and long captions

Toggle **원문** (source), **일본어 한글 독음** (Japanese reading in Hangul) and **한국어 번역** (Korean translation) separately. Readings and translations are stored separately from the source. ASR and translation device preferences are independent; check the reported device actually used. An ASR NPU label does not establish NPU translation.

Long source cues become shorter timed display pages within their original cue interval. Readings and translations follow the same source page. Very short pages may merge to avoid flashing. Text wraps to the available video width, including words without spaces, and layout adapts to fit. The source file remains intact. Combining marks and emoji stay together as grapheme clusters.

**표시 자막 저장** saves the selected display combination as UTF-8 SRT. Check generated recognition, reading and translation against the source. A truncated or invalid translation can skip that cue and continue with later cues; model/runtime failures are reported separately. See the [translation installation guide](INSTALL-TRANSLATION.md) for the optional model.

The pipeline is **audio → original-language recognition → original/context text translated into Korean**. Audio is never sent to the translation model. Bounded original captions within 60 seconds on either side inform only the target translation. Newly available originals refresh future translations; currently displayed text remains stable for its cue. Recognition errors can still cause translation errors.

The optional full large-v3 native path carries validated token alignment into page start/end times, preserving token boundaries and pauses. Unaligned segments and external subtitles retain coarse timing. This is not supplied-lyrics forced alignment or a guarantee of singing accuracy. See the [model validation](asr-model-validation.md).

### External subtitle files

Open media, then use **디스크 → 자막 파일 추가**, or drop a subtitle file onto the player. Supported text formats are **SRT, ASS, SSA, VTT and SMI/SAMI**. A subtitle with exactly the same stem in the media folder is found automatically: `movie.mkv` and `movie.srt`. Select language-suffixed names such as `movie.ja.srt` manually.

UTF-8 and BOM-marked UTF-16 SRT are read strictly, rejecting malformed bytes or incomplete UTF-16 instead of silently inserting broken characters. Legitimate Japanese, Hangul, combining marks, emoji and music symbols are preserved. Other formats are converted locally through FFmpeg, retaining words and timing while reducing ordinary styling to plain text. ASS positioning, colors and animation are not reproduced exactly.

CP949 and Shift-JIS conversion is supported. A declared SMI charset takes priority; undeclared legacy encoding uses a heuristic and may be ambiguous. If characters are wrong, open the file with its correct source encoding in an editor and save as **UTF-8**. Input and normalized output are each limited to 8 MiB, with a ten-second conversion timeout. Import failure displays an error and enables AI captions in the currently selected speech language; an ASR model and runtime are still required.

### Screen and keyboard

The transport floats over video; showing or hiding it does not resize or move the video. Its fullscreen height is **74 px**. About **five seconds** without pointer movement hides the bar and cursor, even when the pointer rests over the bar; move again to reveal them. Menus, slider dragging, held buttons and keyboard activity keep controls available. Captions move above the bar without moving the video.

These keys apply with playback focus. Text fields, lists, selectors and sliders keep their normal keyboard behavior. Space activates a focused button.

| Key | Action |
| --- | --- |
| F / Enter / F11 | Toggle fullscreen |
| Esc | Leave fullscreen |
| Space | Play / pause |
| ← / → | Seek back / forward ten seconds |
| ↑ / ↓ | Raise / lower volume by 5% |
| M | Mute / restore previous volume |
| Ctrl+arrow keys | Move the Blu-ray menu selection |
| Ctrl+Enter | Activate the Blu-ray menu selection |

Disc menus also support the mouse, onscreen directional/confirm remote and **메뉴** button. Use Ctrl combinations for disc navigation; ordinary arrows control seeking and volume.

### Resume playback

The left-side **이어서 보기** button opens the latest valid entry. Reopening the same file also restores its position. History stays in local user settings; changed path, size or modification time prevents applying an old file entry.

Disc history distinguishes disc metadata and main titles rather than relying only on drive letters. Restore waits for the matching main title; menu/startup zero positions do not replace its history. Positions before three seconds or within the final five seconds are not retained for resume. Saved and current durations must also pass a compatibility check. With AI enabled, restored playback uses the same minimum caption preparation rule.

### Windows capture exclusion and verification

Protected discs request Windows capture exclusion for the video and caption windows by default. **The main UI, settings, menus and transport remain capturable.** For development through Parsec or capture software, use **설정 → 개발자 모드 → 개발용 영상 캡처 허용**. Developer mode and capture allowance require separately acknowledged dialogs; cancellation grants neither, and disabling developer mode revokes allowance. Notices explain lawful-use responsibilities without blanket legal immunity. Check the displayed status; this is separate from certified DRM/HDCP or a guarantee against every capture method.

Implementation and verification as of 2026-11-06 are distinct:

| Area | Verified scope |
| --- | --- |
| Fullscreen transport | Actual Windows app with an owned test video: 74 px bar, five-second inactivity hide over video, and unchanged video placement when controls return |
| Stationary pointer over transport | Separate synthetic regression verifies five-second hide/reveal and preserved menus/dragging/key activity; this is distinct from native verification at that pointer location |
| Protected-disc capture | Local actual observation showed black video with visible main UI and transport. Commercial video, menu and subtitle captures are not published |
| Long-caption layout | Synthetic tests cover unbroken text, explicit newlines, narrow windows, combining marks/emoji and in-video geometry |
| External subtitles | Actual FFmpeg conversion of small owned files and owned-media import regressions passed. Every format, drop location, discovery and AI-fallback path is not thereby verified in the native app |
| Resume | File/disc identity/storage rules and closing/reopening an owned file were checked; complete physical-disc resume acceptance is separate |
| AI and translation | Preparation policy and independent display are implemented. Accuracy across all languages/models/devices, translation completeness and sustained actual 60-second lookahead are not established by these checks |
