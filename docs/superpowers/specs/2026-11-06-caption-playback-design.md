# 자막과 재생 조작 개선 / Caption and playback improvements

## 한국어

사용자가 자율 진행을 요청한 후, 긴 자막 줄바꿈·시간 분할, 이상 문자·한국어 오역, 영상 위 재생바, 영상만 캡처 제외, 더 좋은 음성 모델, 10초 최소·60초 선행, 외부 자막 실패 시 자동 인식, 단축키와 이어서보기를 추가 요청했습니다. 기존 C++20/Qt6 구조와 Windows 우선·Linux 호환을 유지합니다.

자막은 원본을 보관하고 표시용으로 문장·단어·Unicode grapheme 경계를 사용해 분할합니다. 기본 표시 길이는 CJK 2/그 외 1의 약60단위이며 시간은 원래 cue 안에서 글자 비율로 나눕니다. 900ms보다 짧은 조각을 강제하지 않고 모든 글자를 유지합니다. 같은 조각에서 원문·독음·번역을 함께 바꿉니다. QTextLayout으로 측정과 그리기를 공유해 공백 없는 문자열도 폭 안에서 줄바꿈합니다. 깨진 인코딩과 모델 제어 표식은 입력·번역 경계에서 검사하며 정상 이름·음표·결합 문자·emoji는 보존합니다.

재생은 최소10초(끝까지 남은 시간이 짧으면 남은 분량)를 준비하고 이후60초 이상을 목표로 계속 인식합니다. 15초 처리 단위 때문에 목표를 약간 넘을 수 있습니다. 실패로 최소 버퍼를 자동 해제하지 않으며 사용자가 ‘자막 없이 계속’을 선택할 수 있습니다. 외부 SRT/ASS/SSA/VTT/SMI는 로컬에서 읽고 실패 시 현재 언어의 AI를 켭니다.

재생바와 영상은 독립 창으로 겹쳐 그려 영상 anchor가 hide/show로 변하지 않게 합니다. 캡처 제외는 영상과 그 위 자막에만 적용하고 본창·설정·재생바는 보입니다. 단축키는 F/Enter 전체화면, 좌우10초, 위아래5% 음량, M 음소거이며 입력 위젯의 키를 보존합니다. 디스크 조작은 마우스·리모컨과 별도 수정키로 제공합니다.

로컬 재생 위치는 사용자 설정에 저장합니다. 파일은 다시 열 때 복원하고, 메뉴가 있는 디스크는 본편 타이틀이 확인된 뒤 같은 디스크·타이틀의 위치를 복원합니다. 메뉴 자체의 0초를 본편 위치로 덮어쓰지 않습니다. 끝 가까운 지점과 길이 변경은 이어서보기 대상에서 제외합니다.

번역 장치 설정은 ASR과 분리합니다. 선택 CUDA 런타임은 고정 해시·별도 NVIDIA 고지·앱 폴더 설치로 제공하며 기존 시스템 드라이버를 바꾸지 않습니다. 더 좋은 ASR 모델은 고정 배포물과 실제 backend·지연·정확도를 확인한 뒤 기본값을 정합니다. 의미 개선은 소유 예문의 부정·허가/금지·화자 의도를 비교하며 형식 검사만으로 정확도 개선을 주장하지 않습니다.

## English

Keep the native C++20/Qt6 architecture. Preserve original captions and build grapheme-safe presentation pages around60 weighted units (CJK2/other1), assigning proportional times inside each original cue without forcing sub900ms pages or dropping text. Share QTextLayout measurement/drawing and validate encoding/model artifacts at their boundaries.

Require ten seconds before playback, bounded by the remaining duration, then continually prefetch at least60seconds in fifteen-second chunks. Failure does not silently bypass readiness. Import ordinary subtitle formats locally and enable current-language ASR if import fails.

Float video and controls without changing the video anchor. Exclude only video/caption windows from capture, keeping UI visible. Add requested playback keys while preserving editable/control keyboard input. Store local resume positions; restore files and matching disc main titles without overwriting them with menu positions.

Separate translation device preferences from ASR, verify optional app-local CUDA without driver changes, and evaluate a better pinned ASR model. Grade owned translation examples for meaning before changing prompts. Retain bilingual versioned source/releases and never publish commercial media, transcripts or keys.
