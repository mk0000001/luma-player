# 자막과 재생 사용 안내 / Captions and playback guide

`0.1.0-alpha.7` · 2026-11-06

## 한국어

### 영상 재생 전 전체 자막 생성

**AI 자막 → 재생 전 전체 자막 만들기**에서 파일 또는 연결된 Blu-ray 타이틀 전체를 준비할 수 있습니다. 전체 원문 인식을 끝낸 뒤 원문 문맥으로 한국어를 번역하고, SRT와 다시 열 수 있는 프로젝트를 저장합니다. 완성 프로젝트는 탐색할 때 실시간 자막 버퍼를 기다리지 않습니다. [사용·취소·이어하기 안내](PREPARE-CAPTIONS.md)를 참고하세요.

### AI 자막 준비와 재생

**AI 자동자막 켜기**를 선택하고 **음성 언어**를 맞추세요. 재생 시작 또는 탐색 후에는 현재 위치부터 **10초의 음성 구간**에 대한 인식을 완료한 뒤 재생합니다. 끝까지 10초보다 적게 남았다면 남은 구간을 준비합니다. 이 숫자는 준비된 미디어 분량이며, 인식 작업이 10초 안에 끝난다는 뜻은 아닙니다.

최소 준비량을 채운 뒤에도 작업을 멈추지 않고 **현재 위치에서 60초 앞까지** 계속 준비합니다. 15초 단위 처리와 실제 탐색 위치 보정으로 준비량이 목표를 조금 넘을 수 있습니다. 처음부터 60초를 모두 준비해야 재생하는 방식은 아닙니다. 디스크 메뉴와 시작 안내는 본편 자막 버퍼를 기다리지 않습니다.

준비 중 직접 일시정지하면 준비가 끝나도 일시정지를 유지합니다. 실패하면 상태와 재시도를 표시하며 AI 선택을 자동으로 끄지 않습니다. **자막 다시 시도**로 재시도하거나, **자막 없이 계속**으로 대기를 건너뛸 수 있습니다. 후자를 선택해도 AI는 계속 준비합니다. AI를 직접 끄면 자막 준비 대기도 해제합니다.

### Windows AI 가속 설치

처음 실행할 때 Windows에서 감지한 GPU와 NPU에 맞춰 AI 가속 라이브러리를 추천합니다. 이후 **설정 → AI 가속 설정**에서도 설치할 수 있습니다. NVIDIA CUDA, AMD Radeon을 포함한 Vulkan, Intel OpenVINO 중 원하는 항목을 직접 선택할 수 있습니다. 설치는 LumaPlayer 폴더에 적용되며, 가속 사용은 장치와 드라이버가 해당 백엔드를 지원할 때 시작됩니다. Radeon은 Windows의 D3D11 영상 디코딩과 Vulkan 음성 인식·번역 경로를 사용합니다.

### Radeon 가속 비교

Windows **설정 → 개발자 모드 → Radeon 가속 비교**에서 **CPU 기준**, **영상만**, **음성인식만**, **번역만**, **세 가지 동시**를 적용할 수 있습니다. 각각의 Vulkan 장치 목록에 Radeon이 여러 개 있으면 **AI 자막** 탭에서 음성인식 GPU와 번역 GPU를 따로 고르세요. GPU 번호는 두 실행기에서 다를 수 있습니다. 재생 중인 파일은 현재 위치에서 다시 열리며, Blu-ray는 디코더를 바꾼 뒤 직접 다시 엽니다.

AI 경로를 시험할 때는 사전 생성 자막이나 외부 자막을 사용하지 않는 파일에서 **AI 자동자막**과 **한국어 번역**을 켜고 같은 구간을 반복하세요. 프리셋을 적용하면 실시간 자막 버퍼를 다시 채웁니다. 지정한 Vulkan 번역이 실패하면 같은 GPU에서 오프로딩을 12개 레이어로 줄이고 Flash Attention을 끈 뒤 한 번 재시도합니다. **영상 디코딩과 색상**에서 D3D11VA 복사, DXVA2 복사, 실험적 Vulkan Video 복사도 비교할 수 있습니다. 복사 디코더는 드라이버가 선택한 장치를 쓸 수 있으며, 파일 영상 GPU 설정은 D3D11 렌더러 선택입니다.

**테스트 진단 JSON 저장**에는 프리셋 적용 시점과 각 실행기의 Vulkan 장치, 실제 디코더, 가속 재시도가 기록됩니다. 모델을 그대로 두고 먼저 CPU 기준, 다음으로 각 가속 경로, 마지막으로 동시 가속을 비교하세요. 비교가 5분을 넘으면 각 단계 직후에 저장하세요. CPU 기준은 소프트웨어 디코딩과 CPU 추론을 사용하며 영상 표시에는 GPU가 쓰일 수 있습니다.

### 원문·독음·번역과 긴 자막

**원문**, **일본어 한글 독음**, **한국어 번역**을 각각 켜거나 끌 수 있습니다. 독음과 번역은 원문과 별도로 보관됩니다. 음성 인식과 번역의 장치 설정도 독립적이며, 화면의 실제 사용 장치를 확인하세요. 음성 인식의 NPU 표시는 번역까지 NPU로 처리한다는 뜻이 아닙니다.

처리 순서는 **음성 → 원문 인식 → 원문·문맥을 한국어로 번역**입니다. 번역 모델에 음성을 직접 보내지 않습니다. 앞뒤 60초 안에서 준비된 원문을 참고하고 대상 자막만 번역합니다. 새 원문이 도착하면 앞으로 표시할 번역을 갱신하며, 이미 읽고 있는 번역은 그 구간 동안 유지합니다. 인식한 원문이 틀리면 해석도 틀릴 수 있습니다.

선택 full large-v3 모델은 native 음성 정렬 시각으로 페이지 시작·끝을 정합니다. 토큰 경계와 쉼을 보존하며, 정렬을 얻지 못한 구간과 일반 자막은 구간 시각으로 처리합니다. 가사 전용 강제 정렬이나 완벽한 노래 인식은 보장하지 않습니다. 설치·실측은 [모델 안내](asr-model-validation.md)를 참고하세요.

긴 원문은 짧은 표시 페이지로 나누고, 원래 자막 시간 안에서 차례로 보여 줍니다. 선택한 독음과 번역도 같은 원문 페이지에 맞춥니다. 짧은 시간에 너무 많은 페이지가 번쩍이지 않도록 짧은 조각은 합칠 수 있습니다. 화면 폭을 넘으면 공백 없는 한글·일본어·긴 단어도 줄바꿈하며, 글자 수에 맞춰 배치를 조정합니다. 원본 파일을 잘라 덮어쓰지 않습니다. 결합 문자와 이모지는 하나의 글자 묶음으로 보존합니다.

**표시 자막 저장**은 선택한 표시 조합을 UTF-8 SRT로 저장합니다. 자동 인식·독음·번역의 의미와 정확도는 원문과 함께 확인하세요. 한 번역 구간이 잘리거나 유효하지 않으면 해당 구간을 건너뛰고 다음 번역을 계속할 수 있습니다. 모델이나 실행기 자체의 오류는 별도로 표시합니다. 선택 번역 모델 설치는 [번역 설치 안내](INSTALL-TRANSLATION.md)를 참고하세요.

### 번역 용어집

**AI 자막 → 번역 용어집**에서 한 줄에 하나씩 `원문 → 번역`을 저장합니다. 예를 들어 `青空 → 푸른 하늘`로 지정하면 해당 표현이 나오는 자막을 번역할 때 모델에 이 뜻을 전달합니다. 음성 인식에서 반복되는 오기가 있다면 실제로 표시된 원문을 왼쪽에 넣을 수 있습니다.

Blu-ray를 연 상태에서 편집하면 그 디스크에만 저장됩니다. 같은 디스크를 다시 열거나 자막을 미리 만들면 해당 용어집을 자동으로 불러옵니다. 디스크를 열지 않은 상태에서 편집한 기본 용어집은 일반 파일과 전용 용어집이 없는 디스크에 사용됩니다.

실시간 번역은 현재 위치부터 다시 만들고, **재생 전 전체 자막 만들기**에는 생성 시작 시의 용어집이 적용됩니다. 기존 완료 프로젝트의 번역을 바꾸려면 용어집을 저장하고 새 프로젝트를 생성하세요. 용어집은 로컬 설정에 저장됩니다.

### 외부 자막 파일

미디어를 연 뒤 **디스크 → 자막 파일 추가**로 로컬 자막을 선택하거나 자막 파일을 플레이어에 놓으세요. 지원하는 텍스트 형식은 **SRT, ASS, SSA, VTT, SMI/SAMI**입니다. 미디어와 같은 폴더에 이름이 정확히 같은 자막이 있으면 자동으로 찾습니다. 예: `movie.mkv`와 `movie.srt`. 언어 접미사가 붙은 `movie.ja.srt`는 직접 선택하세요.

UTF-8과 BOM이 있는 UTF-16 SRT는 엄격하게 읽습니다. 잘못된 바이트나 불완전한 UTF-16 문자를 깨진 글자로 조용히 바꾸지 않습니다. 정상적인 일본어·한글·결합 문자·이모지·음표는 보존합니다. 다른 형식은 로컬 FFmpeg로 시간과 본문을 변환하며 일반 색상·장식 태그는 일반 텍스트로 정리합니다. ASS의 위치·색상·애니메이션을 그대로 재현하는 방식은 아닙니다.

CP949와 Shift-JIS도 변환할 수 있습니다. SMI에 문자 인코딩이 명시되어 있으면 우선하며, 선언이 없으면 추정하므로 모호한 파일은 잘못 해석될 수 있습니다. 글자가 맞지 않으면 원래 인코딩을 지정할 수 있는 편집기에서 열어 **UTF-8로 다시 저장**하세요. 입력과 변환 결과는 각각 8 MiB로 제한하며 변환은 10초가 지나면 중단합니다. 자막을 읽거나 변환하지 못하면 오류를 표시하고 **현재 선택한 음성 언어의 AI 자동자막**을 켭니다. 이때도 음성 인식 모델과 실행기가 필요합니다.

### 자막 모양

**설정 → 자막 모양**에서 시스템에 설치된 글꼴, 기본 대비 **50–200%** 크기, 글자·배경 불투명도를 각각 조절할 수 있습니다. **0%**는 투명입니다. 변경은 화면에 바로 반영되며 앱을 다시 열어도 유지됩니다. **자막 모양 기본값 복원**으로 Noto Sans와 기본 크기·불투명도로 돌아갈 수 있습니다.

AI 자막, 미리 만든 자막 프로젝트, 불러온 텍스트 자막에 적용됩니다. 디스크의 PGS 이미지 자막은 디스크에 저장된 모양으로 표시됩니다.

### 화면과 키보드

재생바는 영상 위에 떠 있으며 나타나거나 사라져도 영상 크기와 위치를 바꾸지 않습니다. 전체화면 재생바 높이는 **74 px**입니다. 마우스가 재생바 위에 있더라도 약 **5초** 동안 움직이지 않으면 재생바와 커서를 숨기며, 다시 움직이면 나타납니다. 메뉴 사용·슬라이더 끌기·버튼을 누르고 있는 동안과 키 입력 중에는 조작할 수 있도록 유지합니다. 자막만 재생바 위로 옮겨 겹침을 피합니다.

다음 키는 재생 화면에 초점이 있을 때 사용합니다. 텍스트 입력, 목록, 선택 상자, 슬라이더 등의 기본 키 조작은 유지합니다. 버튼에 초점이 있을 때 Space는 해당 버튼을 누르는 동작입니다.

| 키 | 동작 |
| --- | --- |
| F / Enter / F11 | 전체화면 전환 |
| Ctrl+Shift+P | 작은 화면(PiP) 전환 |
| Ctrl+Shift+D | 성능 오버레이 전환 · 개발자 모드 |
| Esc | 전체화면 또는 작은 화면 나가기 |
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

### 개발자 모드의 성능 진단

**설정 → 개발자 모드 → 성능 오버레이**에서 텍스트·그래프·혼합과 왼쪽 또는 오른쪽 위를 선택하세요. 오버레이는 창 모드, 전체화면, 작은 화면에서 영상 위에 표시됩니다. 1초 간격으로 측정하고 최근 90초의 추이를 보여 줍니다.

CPU·GPU·NPU·RAM은 **시스템 전체 / 플레이어** 순으로 비교합니다. 그래프에서 실선은 전체, 점선은 플레이어입니다. Windows의 플레이어 집계에는 자막 작업 등 하위 프로세스가 포함됩니다. RAM은 프로세스별 상주 메모리의 합으로, 공유 메모리가 중복 집계될 수 있습니다. GPU·NPU 수치는 각 범위에서 가장 바쁜 엔진 기준이며 GPU의 영상 디코딩 부하도 별도로 표시합니다. Linux에서는 시스템과 플레이어 본체의 CPU·RAM을 비교합니다. 수집할 수 없는 수치는 `—`로 표시합니다. Blu-ray I/O는 libVLC의 입력 또는 디먹싱 처리량이고, Blu-ray FPS는 약 3초 동안 실제로 표시한 프레임을 바탕으로 계산합니다. 일반 파일 FPS는 필터 출력 추정치입니다.

**진단 요약 복사**는 재생 상태와 최근 수치의 JSON을 복사합니다. **성능 기록 CSV 저장**은 최근 샘플을 저장하고, **성능 기록 초기화**와 **재생 로그 지우기**는 각 화면의 기록을 비웁니다. JSON과 CSV에는 파일 경로, 자막 본문, 재생 로그가 들어가지 않습니다.

가속 문제를 재현하기 전에 **설정 → 개발자 모드**를 켜고, 재현한 뒤 앱을 닫기 전에 **테스트 진단 JSON 저장**을 누르세요. 보고서에는 지원되는 시스템·플레이어 자원 사용량 이력, GPU 하드웨어 ID와 드라이버 버전, 선택한 설정, 실제 디코더와 AI 장치, 가속 라이브러리 설치 결과와 최근 오류 코드가 들어갑니다. 미디어 경로, 원본 로그, 자막과 네트워크 주소는 제외합니다. 테스트 담당자가 검토한 JSON 파일을 직접 공유할 수 있습니다.

**설정 → 파일 HDR 출력**의 기본값은 자동입니다. Windows HDR이 활성화된 화면에서 파일 렌더러가 HDR 출력을 협상합니다. **끔 · SDR 출력**은 HDR 파일을 SDR로 톤 매핑합니다. 두 선택은 재생 중에도 바꿀 수 있습니다. 화면을 옮기면 디스플레이 상태가 갱신됩니다.

**개발자 모드 → 영상 디코딩과 색상**에서는 SDR, HDR10(PQ), HLG를 지정할 수 있습니다. **SDR 화면에서도 HDR 렌더링 강제**는 출력 협상 결과와 별도로 파일 렌더러의 HDR 타깃을 강제하고 SDR 소스에 역톤 매핑을 허용합니다. 실제 모니터 모드는 설정의 디스플레이 상태에서 확인할 수 있습니다. 디코딩 장치와 파일 디코더 우선순위, Dolby Vision 메타데이터 옵션은 미디어를 다시 열면 적용됩니다. Blu-ray 메뉴 재생은 디스크 엔진의 영상 출력을 사용합니다. [mpv 옵션 문서](https://mpv.io/manual/master/)

Windows 시험에서 츠키 무도관 Blu-ray의 D3D11VA 재생, 합성 AV1 파일의 `libdav1d` 선택, 25G64에서 합성 HEVC HDR10 파일의 PQ / BT.2020 출력을 확인했습니다. 25G64의 240Hz 연결에서 Windows는 채널당 8비트를 보고했습니다. Dolby Vision 디스플레이 출력은 미검증입니다. 미디어 입력 검증을 드래그와 재생 엔진에 적용했고, 네트워크 주소를 파일 엔진의 로그에서 가립니다.

### 오디오 출력과 공간 음향

**시스템 오디오 · PCM**은 Windows에서 출력 장치의 공간 음향 설정을 사용합니다. **Windows 공간 음향 설정**에서 Windows Sonic, Dolby Atmos for Headphones, DTS Headphone:X를 선택할 수 있습니다. 파일 PCM은 자동 채널, 스테레오, 5.1/7.1 우선 또는 야간 모드를 선택할 수 있습니다. 야간 모드는 큰 소리를 완화합니다. [Windows 공간 음향](https://support.microsoft.com/en-us/windows/hardware/drivers/how-to-turn-on-spatial-sound-in-windows)

**HDMI 비트스트림 · AV 리시버**는 호환 장치에 Dolby Digital, Dolby Digital Plus, TrueHD, DTS, DTS-HD 비트스트림을 요청합니다. Dolby Atmos 또는 DTS:X는 해당 메타데이터가 포함된 원본 트랙과 이를 해석하는 리시버가 필요합니다. 비트스트림 출력 시 리시버에서 음량을 조절합니다. 파일 엔진은 출력 형식과 PCM 전환 여부를 표시합니다. Blu-ray 메뉴 재생에서는 리시버에서 실제 출력 형식을 확인하세요. 오디오 설정은 미디어를 다시 열면 적용됩니다. Windows USB DAC에서 Dolby Digital Plus 비트스트림 거부 후 WASAPI PCM 재생을 확인했습니다. Atmos·DTS:X 리시버 출력은 미검증입니다. [mpv 오디오 옵션](https://mpv.io/manual/master/) · [VideoLAN WASAPI 지원](https://github.com/videolan/vlc-3.0/blob/master/NEWS)

### Windows 캡처 제외와 확인 범위

보호 디스크를 재생하면 Windows에서 지원하는 캡처 제외를 영상 창과 자막 창에 기본 적용합니다. 본창, 설정, 메뉴와 재생바는 계속 캡처할 수 있습니다. 적용 상태는 플레이어 설정에 표시됩니다.

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

### Full captions before playback

Use **AI 자막 → 재생 전 전체 자막 만들기** to prepare an entire file or connected Blu-ray title. Recognition finishes before original-context Korean translation. Save SRT exports and a reusable project; completed projects bypass live ASR buffering after seeks. See the [preparation/resume guide](PREPARE-CAPTIONS.md).

### AI caption preparation

Enable **AI 자동자막 켜기** and select the spoken language. Before starting or after seeking, recognition must cover **ten seconds of audio from the current position**, or the remaining duration near the end. This is media coverage, not a promise that inference finishes in ten wall-clock seconds.

After the minimum is ready, work continues toward **60 seconds ahead**. Fifteen-second processing chunks and seek correction may take coverage slightly beyond that target. Playback does not wait for the full 60 seconds initially. Disc menus and startup navigation do not wait for the main-title caption buffer.

A manual pause remains paused when preparation finishes. Failures show status and retry without automatically clearing the AI setting. Use **자막 다시 시도** to retry or **자막 없이 계속** to bypass the wait while AI continues preparing. Manually disabling AI also releases the caption wait.

### Windows AI acceleration setup

On first launch, LumaPlayer recommends AI libraries based on the GPUs and NPUs detected on Windows. Open **설정 → AI 가속 설정** to install them later. NVIDIA CUDA, Vulkan (including AMD Radeon), and Intel OpenVINO are also available for manual selection. Installation is scoped to the LumaPlayer directory. Acceleration requires a device and driver that support the backend. Radeon uses Windows D3D11 for video decoding and Vulkan for speech recognition and translation.

### Comparing Radeon acceleration

In Windows **Settings → Developer Mode → Radeon acceleration comparison**, apply a CPU baseline, video only, recognition only, translation only, or all three. If either worker detects multiple Radeon GPUs, select the recognition and translation devices separately under **AI captions**. Their Vulkan indices can differ. An open file resumes from the current position after applying a profile. Reopen a Blu-ray to change its decoder.

For a live AI comparison, enable automatic captions and Korean translation, use a file with no prepared or external subtitles, and repeat the same interval. Applying a profile refills the live caption buffer. A failed explicitly selected Vulkan translation retries on the same GPU with 12 offloaded layers and Flash Attention disabled. **Video decoding and color** also offers D3D11VA copy-back, DXVA2 copy-back, and experimental Vulkan Video copy-back. Copy-back decoders may use the device selected by their driver. The file video GPU setting selects the D3D11 renderer.

**Save tester diagnostics JSON** records profile transitions, Vulkan device lists per worker, the observed decoder, and acceleration retries. Keep the model fixed and compare the CPU baseline, each accelerated path, and then simultaneous acceleration. If the comparison takes more than five minutes, save after each step. The CPU baseline uses software decoding and CPU inference, with the GPU still available for rendering.

### Source, reading, translation and long captions

Toggle **원문** (source), **일본어 한글 독음** (Japanese reading in Hangul) and **한국어 번역** (Korean translation) separately. Readings and translations are stored separately from the source. ASR and translation device preferences are independent; check the reported device actually used. An ASR NPU label does not establish NPU translation.

Long source cues become shorter timed display pages within their original cue interval. Readings and translations follow the same source page. Very short pages may merge to avoid flashing. Text wraps to the available video width, including words without spaces, and layout adapts to fit. The source file remains intact. Combining marks and emoji stay together as grapheme clusters.

**표시 자막 저장** saves the selected display combination as UTF-8 SRT. Check generated recognition, reading and translation against the source. A truncated or invalid translation can skip that cue and continue with later cues; model/runtime failures are reported separately. See the [translation installation guide](INSTALL-TRANSLATION.md) for the optional model.

The pipeline is **audio → original-language recognition → original/context text translated into Korean**. Audio is never sent to the translation model. Bounded original captions within 60 seconds on either side inform only the target translation. Newly available originals refresh future translations; currently displayed text remains stable for its cue. Recognition errors can still cause translation errors.

The optional full large-v3 native path carries validated token alignment into page start/end times, preserving token boundaries and pauses. Unaligned segments and external subtitles retain coarse timing. This is not supplied-lyrics forced alignment or a guarantee of singing accuracy. See the [model validation](asr-model-validation.md).

### Translation glossary

In **AI Captions → Translation glossary**, enter one `source → translation` pair per line. For example, `青空 → 푸른 하늘` sends the preferred meaning to the model when the expression appears. Use the displayed source text on the left to handle a recurring recognition error.

Editing while a Blu-ray is open saves the glossary for that disc. Reopening the disc or preparing its captions loads the glossary automatically. Edit without an open disc to set the default glossary for files and discs without a dedicated glossary.

Live captions are translated again from the current position. New preparation jobs use the glossary saved when the job starts. To revise the translation in a completed project, save the glossary and generate a new project. The glossary is stored locally.

### External subtitle files

Open media, then use **디스크 → 자막 파일 추가**, or drop a subtitle file onto the player. Supported text formats are **SRT, ASS, SSA, VTT and SMI/SAMI**. A subtitle with exactly the same stem in the media folder is found automatically: `movie.mkv` and `movie.srt`. Select language-suffixed names such as `movie.ja.srt` manually.

UTF-8 and BOM-marked UTF-16 SRT are read strictly, rejecting malformed bytes or incomplete UTF-16 instead of silently inserting broken characters. Legitimate Japanese, Hangul, combining marks, emoji and music symbols are preserved. Other formats are converted locally through FFmpeg, retaining words and timing while reducing ordinary styling to plain text. ASS positioning, colors and animation are not reproduced exactly.

CP949 and Shift-JIS conversion is supported. A declared SMI charset takes priority; undeclared legacy encoding uses a heuristic and may be ambiguous. If characters are wrong, open the file with its correct source encoding in an editor and save as **UTF-8**. Input and normalized output are each limited to 8 MiB, with a ten-second conversion timeout. Import failure displays an error and enables AI captions in the currently selected speech language; an ASR model and runtime are still required.

### Caption appearance

Open **설정 → 자막 모양** (Settings → Caption appearance) to choose an installed font, scale captions from **50–200%** of the default size, or adjust text and background opacity independently. **0%** is transparent. Changes apply immediately and persist after restarting. **자막 모양 기본값 복원** restores Noto Sans and the default size and opacity.

These settings apply to AI captions, prepared caption projects, and imported text subtitles. The disc's PGS image subtitles retain their authored appearance.

### Screen and keyboard

The transport floats over video; showing or hiding it does not resize or move the video. Its fullscreen height is **74 px**. About **five seconds** without pointer movement hides the bar and cursor, even when the pointer rests over the bar; move again to reveal them. Menus, slider dragging, held buttons and keyboard activity keep controls available. Captions move above the bar without moving the video.

These keys apply with playback focus. Text fields, lists, selectors and sliders keep their normal keyboard behavior. Space activates a focused button.

| Key | Action |
| --- | --- |
| F / Enter / F11 | Toggle fullscreen |
| Ctrl+Shift+P | Toggle picture-in-picture |
| Ctrl+Shift+D | Toggle the performance overlay in developer mode |
| Esc | Leave fullscreen or picture-in-picture |
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

### Developer performance diagnostics

Go to **Settings → Developer mode → Performance overlay** and choose text, graphs, or both and the upper left or right corner. The overlay sits over the video in windowed, fullscreen, and picture-in-picture modes. It samples once per second and shows the last 90 seconds.

CPU, GPU, NPU, and RAM show **system / player**. Solid graph lines show the system and dashed lines show the player. The Windows player figures include child processes such as caption workers. The resident-memory sum may count shared pages more than once. GPU and NPU figures use the busiest engine in each scope, and video decode load is shown separately. On Linux, CPU and RAM compare the system with the main player process. A dash marks an unavailable measurement. Blu-ray I/O measures libVLC input or demux throughput. Blu-ray FPS uses frames presented over about three seconds of continuous playback. File FPS uses the estimated filter output rate.

**Copy diagnostics** copies a JSON snapshot of playback and current metrics. **Save performance CSV** saves recent samples. **Reset performance history** and **Clear playback log** clear their respective records. JSON and CSV omit file paths, subtitle text, and playback logs.

Enable **설정 → 개발자 모드** before reproducing an acceleration issue, then select **테스트 진단 JSON 저장** before closing the app. The report includes available system and player resource history, GPU hardware IDs and driver versions, selected settings, active decoders and AI devices, acceleration installation results, and recent error codes. It omits media paths, raw logs, caption text, and network addresses. The tester can review and share the saved JSON directly.

**Settings → File HDR output** defaults to Auto. The file renderer negotiates HDR when the Windows display has HDR active. **Off · SDR output** tone maps HDR files to SDR. Either option can be changed during playback. The display status follows the monitor as you move the window.

**Developer mode → Video decoding and color** can request SDR, HDR10 (PQ), or HLG. **Force HDR rendering on an SDR display** overrides the renderer target independently of the negotiated display mode and enables inverse tone mapping for SDR sources. Settings separately reports the monitor's active mode. Decoder device, file decoder preference, and Dolby Vision metadata choices take effect when you reopen media. Blu-ray menu playback uses the disc engine's video output. [mpv option reference](https://mpv.io/manual/master/)

Windows tests observed D3D11VA playback on the Tuki Budokan Blu-ray, `libdav1d` selection on a generated AV1 file, and PQ / BT.2020 output for a generated HEVC HDR10 file on a 25G64. Windows reported 8 bits per channel at 240 Hz on that connection. Dolby Vision display output remains unverified. Drag and drop and engine input validate media sources, and file engine logs mask network addresses.

### Audio output and spatial sound

**System audio · PCM** uses the Windows output device's spatial sound settings. Use **Windows spatial sound settings** to select Windows Sonic, Dolby Atmos for Headphones, or DTS Headphone:X. File PCM profiles include automatic layout, stereo, preferred 5.1/7.1 layouts, and a night mode that compresses loud peaks. [Windows spatial sound](https://support.microsoft.com/en-us/windows/hardware/drivers/how-to-turn-on-spatial-sound-in-windows)

**HDMI bitstream · AV receiver** requests Dolby Digital, Dolby Digital Plus, TrueHD, DTS, and DTS-HD passthrough on compatible devices. Dolby Atmos and DTS:X require source tracks containing the relevant metadata and a receiver that can decode it. Adjust bitstream volume on the receiver. The file engine reports the active output format and PCM fallback. For Blu-ray menu playback, check the receiver for the active format. Reopen the media to apply audio preferences. A Windows USB DAC rejected Dolby Digital Plus bitstream and resumed playback as WASAPI PCM. Atmos and DTS:X receiver output remains unverified. [mpv audio options](https://mpv.io/manual/master/) · [VideoLAN WASAPI support](https://github.com/videolan/vlc-3.0/blob/master/NEWS)

### Windows capture exclusion and verification

On supported Windows configurations, protected discs request capture exclusion for the video and caption windows by default. The main UI, settings, menus and transport remain capturable. The player settings show the current capture status.

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
