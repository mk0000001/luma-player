# 변경 기록 / Changelog

## 한국어

### 0.2.14

- 기본 Windows 설치에 포함된 Turbo로 오프라인 설치를 완료합니다. 추가 인식·번역 모델 다운로드와 AI 가속 설치는 직접 선택합니다.
- 시작 시 업데이트 확인, 내부망 검색·수신, Blu-ray 자동 검색은 사용자가 켜야 실행됩니다.
- Windows와 Android의 자막 배경이 글줄에 더 가깝게 표시됩니다.

### 0.2.13

- Blu-ray 메뉴 모드에서 재생 시계와 영상·음성 출력이 타이틀 끝에 머물면 재생 완료로 전환합니다. 재생 버튼으로 같은 타이틀을 처음부터 다시 볼 수 있습니다.

### 0.2.12

- Blu-ray가 PCM으로 출력하면 음량 조절과 음소거를 사용할 수 있습니다. 실제 비트스트림 출력 중에는 리시버에서 조절합니다.
- 영상 끝에서 재생하면 처음부터 시작합니다. 재생바와 챕터 목록에 현재 챕터를 표시하고 유튜브 방식 재생 단축키를 추가했습니다.
- 원문 자막의 버퍼 대기에 상한을 두고, 계속 재생하는 동안 준비된 자막을 갱신합니다. 대사가 몰리면 곧 표시될 줄부터 번역합니다.
- Android에 실시간 AI 자막 상태와 기기 자막 파일 불러오기를 추가했습니다. PC의 라이브 자막 갱신은 영상 송출과 별도로 전달합니다.
- 개발자 모드, 영상 캡처 허용, 성능 오버레이, 상세 진단 설정을 실행마다 초기화합니다. 상세 진단과 설치 로그는 요청 시에만 사용합니다.

### 0.2.11

- 챕터 위치가 디스크의 영상 인덱스와 일치하는 Blu-ray에서 갤탭 챕터 이동을 앞당겼습니다. 인덱스를 적용할 수 없는 디스크에서는 시간 기준 탐색을 사용합니다.

### 0.2.8

- 페어링한 갤럭시 탭에서 PC의 Blu-ray 메뉴를 조작하고 본편 공유를 시작할 수 있습니다.
- 갤탭에서 챕터를 바꾸면 이전 영상 요청을 종료하고 새 위치의 송출을 시작합니다. PC와 Android 재생바에 재생 버퍼를 표시합니다.
- 설정 화면 위에서 마우스 휠을 돌리면 항목 값 대신 목록이 스크롤됩니다.
- 개발자 모드에서 재생 이벤트와 성능 샘플을 같은 시간순으로 합친 JSON을 저장할 수 있습니다.
- 자동 AI 가속에서 감지한 Radeon Vulkan 장치를 음성 인식과 번역에 선택합니다. 수동 장치 선택은 유지합니다.
- 갤탭에서 PC가 지정한 파일을 고를 수 있습니다. 일반 재생 화면에서는 챕터 위에 컨트롤러를 기본 표시하고, 전체화면에서는 필요할 때 열 수 있습니다.
- Blu-ray 챕터를 번호로 바로 열고, PC에 준비된 표시 자막을 갤탭으로 보냅니다.

### 0.2.7

- Windows에서 LumaPlayer를 켜면 Android에서 PC를 자동으로 찾습니다. 연결이 바뀌어도 PC 목록이 자동으로 갱신됩니다.
- PC에서 허용하면 페어링한 Android 기기에서 PC에 열려 있는 영상의 재생 공유를 시작할 수 있습니다.
- 로컬 자막 번역은 실행할 때마다 새 인증서를 사용하는 루프백 HTTPS 연결로 처리합니다.
- 자막 프로젝트의 로컬 경로 확인, LAN 연결 수 제한, Android의 미디어 요청과 세션 응답 검증을 강화했습니다.

### 0.2.6

- Windows의 인앱 업데이트 확인이나 다운로드가 실패하면 오류 창에서 공식 다운로드 페이지를 열 수 있습니다.
- 자동 UI 테스트에서 실제 광학 드라이브 검색과 저장장치 온도 조회를 차단했습니다.

### 0.2.5

- Android에서 LAN 응답의 JSON 깊이와 WebVTT 응답 크기를 제한하고, 연결 취소 시 진행 중인 메타데이터 읽기도 중단합니다.
- Windows 내부망 서버의 스트리밍 유휴 한도와 페어링 토큰 교체 처리를 보강했습니다. VPN 검색은 요청을 받은 포트에서 응답할 수 있습니다.
- Windows 설치 페이로드 검증, 의존성 캐시와 업데이트 검증을 강화하고, GitHub 빌드와 초안 릴리스 권한을 분리했습니다.
- 자막 검색과 번역 우선순위 처리의 반복 연산을 줄이고 Blu-ray 음성 임시 버퍼의 상한을 적용했습니다.
- Blu-ray 메뉴에서 본편으로 전환할 때 호환 영상 디코딩 경로를 사용해 첫 영상이 멈추는 문제를 수정했습니다.
- Android에서 재생을 유지한 채 설정을 열고, 현재 화질·HDR·재생 속도를 확인할 수 있습니다. 넓은 화면과 폴더블의 창 비율에 맞춰 영상과 챕터를 배치합니다.
- Ubuntu 24.04 x64용 데스크톱 실행 파일을 아카이브로 제공합니다.

### 0.2.4

- Windows 개발자 모드에서 Radeon 영상·음성인식·번역을 하나씩 또는 함께 켜고 CPU 기준과 비교할 수 있습니다.
- 파일 디코더 비교에 D3D11VA 복사, DXVA2 복사, 실험적 Vulkan Video 복사를 추가했습니다. 지정 Vulkan 번역이 실패하면 같은 GPU의 저VRAM 구성으로 한 번 더 시도합니다.
- 진단 파일에 비교 설정 적용 시점, 각 실행기가 감지한 Vulkan 장치, 저VRAM 재시도 결과를 기록합니다.

### 0.2.3

- Windows의 첫 실행과 설정에서 GPU·NPU를 감지해 NVIDIA CUDA, Vulkan, Intel OpenVINO 패키지를 추천하고 선택 설치할 수 있습니다. Radeon의 영상 가속과 로컬 자막 가속은 같은 Windows 빌드에 포함됩니다.
- 테스트 진단에 시스템과 플레이어의 자원 사용량 이력, 디코더 및 AI 백엔드의 선택·실행·대체 정보를 추가했습니다.
- 광학 드라이브가 디스크를 준비하는 동안 창을 계속 사용할 수 있게 했습니다. AACS 설정을 읽을 수 없는 경우 원인을 구체적으로 안내합니다.
- Android의 하단 탐색바, 챕터 목록, 연속 탭 탐색과 전체화면 제스처를 추가했습니다. PC에서도 연결 기기의 화질, 화면 비율과 자막 싱크를 조절할 수 있습니다.
- WireGuard 등으로 연결한 기기에서 사설 IP를 직접 입력하거나 지정한 사설 대역의 LumaPlayer PC를 검색할 수 있습니다.
- Android의 기본 화질을 원본 HDR 우선으로 설정하고 최대 1080p SDR 균형과 최대 480p SDR 데이터 절약 프리셋을 추가했습니다. 호환 디코더가 있는 기기에서 AV1 원본을 재생합니다.
- Android에 홈 화면으로 나갈 때 자동 PiP와 백그라운드 재생을 추가했습니다. 넓은 가로 창에는 영상과 챕터를 나란히 배치합니다.
- Windows와 Android에 공유기 없이 연결하는 Wi‑Fi Direct 경로를 추가했습니다. 임시 Wi‑Fi 암호를 입력하고 양쪽에서 6자리 코드를 확인합니다.
- Windows 성능 오버레이를 영상 위에 유지하고, 지원하는 장치의 온도와 GPU별 VRAM, 네트워크 송수신 속도를 추가했습니다.
- 일본어 한글 독음에서 조사와 보조 표현을 앞말에 붙여 읽기 쉽게 표시합니다.

### 0.2.0-beta.2

- Windows의 **내부망 재생**에서 연결된 기기와 기억한 기기를 확인하고 개별 해제할 수 있습니다. 다른 Windows PC에서도 같은 확인 코드로 연결해 재생합니다.
- Windows 실행 시 GitHub 업데이트를 확인합니다. 설정에서 수동으로 확인하거나 알림을 끌 수 있습니다. 서명과 SHA-256을 확인한 설치 파일을 사용합니다.

### 0.2.0-beta.1

- Android에서 같은 내부망의 Windows LumaPlayer를 자동으로 찾거나 사설 IP로 연결합니다. 양쪽의 확인 코드를 비교해 PC에서 승인하며, 기기별로 매번 승인하거나 다음 연결을 위해 기억할 수 있습니다. 로컬 파일과 Blu-ray 본편에 준비된 PC 자막을 함께 전달합니다.
- Android 보안 영상 표면과 캡처 차단을 적용했습니다. HDR·Dolby Vision·공간 음향은 원본과 기기의 지원 범위에서 재생합니다.
- 일반 설정에 HDR 자동/SDR 전환과 PCM·HDMI 비트스트림을 추가했습니다. 개발자 모드에 시스템·플레이어 성능 비교와 SDR 화면 HDR 렌더링 시험 옵션을 제공합니다.

### 0.1.6 — 2026-11-12 공개 베타

- Linux Qt 6.4 환경에서도 빌드할 수 있도록 창 이동과 자막 코드의 헤더를 수정했습니다.

### 0.1.5 — 2026-11-12 공개 베타

- Windows에서 창을 끌 때 영상, 자막, 조작 바를 같은 이동 처리 단계에서 갱신합니다.
- 항상 위에 뜨는 작은 화면(PiP)을 추가했습니다. 재생 바 또는 `Ctrl+Shift+P`로 전환하고 `Esc`로 돌아옵니다.
- 탐색 바를 클릭해 위치를 바꾸고 자막 메뉴에서 번역 용어집을 열 수 있습니다.
- 화면 구성, 재생, 자막, 입력 처리를 분리하고 Windows Qt를 6.11.3으로 올렸습니다. 라이브러리 검색, 자동 스크립트, 로컬 번역 응답 처리도 강화했습니다.

### 0.1.4 — 2026-11-12 공개 베타

- Blu-ray를 연 상태에서 저장한 번역 용어집을 디스크별로 기억합니다. 같은 디스크를 다시 열거나 전체 자막을 만들 때 자동으로 사용합니다.

### 0.1.3 — 2026-11-12 공개 베타

- **AI 자막 → 번역 용어집**에 `원문 → 번역`을 저장할 수 있습니다. 실시간 한국어 자막과 새로 만드는 전체 자막에서 일치하는 용어를 앞뒤 문맥과 함께 번역 모델에 전달합니다.

### 0.1.2 — 2026-11-12 공개 베타

- **설정 → 자막 모양**에서 글꼴, 크기, 글자·배경 불투명도를 조절합니다. 재생 중 바로 반영하고 다음 실행에도 기억합니다.
- AI 자막, 미리 만든 자막 프로젝트, 불러온 텍스트 자막에 적용합니다. Noto Sans와 기본 크기·불투명도로 복원할 수 있습니다.

### 0.1.1 — 2026-11-12 공개 베타

- Windows Blu-ray에서 GPU 영상 디코딩을 우선 사용하고, 재생 중 영상 프레임이 멈추면 CPU 호환 디코딩으로 같은 위치를 복구합니다.
- 실시간 자막용 Blu-ray 음성을 최대 75초 단위로 읽습니다. 디스크 읽기 대기와 자막 준비 상태를 구분해 표시합니다.
- 시작·탐색은 자막 10초를 준비하고, 재생 중에는 남은 자막이 2초 미만일 때 다시 준비합니다.
- 창을 옮기는 동안 영상·자막·재생 바의 위치를 함께 갱신합니다.
- CUDA 포함 설치 EXE, 자동 모델 다운로드, 제품 소개, 권장·테스트 사양 및 문제 해결 안내를 제공합니다. [베타 기록](docs/releases/0.1.0-beta.1.md)

공개 배포에는 불변 태그·GitHub Release·체크섬을 남깁니다. 로컬 시험 빌드와 실제 배포를 구분하며, 아래 프리뷰의 최종 검증은 각 릴리스 기록을 따릅니다.

### 0.1.0-alpha.10 — 2026-11-06 프리뷰

- 사전 생성 창이 현재 재생하던 디스크·유일하게 확인된 타이틀·오디오 순서를 우선 선택. 정지 전에 상태를 전달하고 다른 디스크·모호한 길이·범위 밖 입력을 거부.
- 재생 정보가 없으면 가장 긴 타이틀을 선택하되 본편으로 단정하지 않고 코멘터리 등 부가영상 여부를 메뉴와 대조하도록 안내. 실물 ALL PLAY와 더 긴 코멘터리 선택판을 구분.
- [alpha.10 검증·배포 기록](docs/releases/0.1.0-alpha.10.md). 생성·모델·프로젝트 형식과 alpha.9 이하 태그·파일은 유지.

### 0.1.0-alpha.9 — 2026-11-06 프리뷰

- 준비한 Blu-ray 타이틀을 메뉴 없는 실제 native 목록에서 길이로 선택하고, 선택·길이가 확인될 때까지 일시정지. 모호하거나 맞지 않는 타이틀에는 자막을 강제로 적용하지 않음. 일반 메뉴 재생 유지.
- 직접 재생의 본편 index 0을 지원하고 메뉴/직접 모드·완성 프로젝트를 이어보기에 저장. 미디어 전환·영상 출력 복구 때 완성 원문·번역을 유지하고 변경된 원본은 충돌 없이 거부.
- 사전 생성의 제어된 가속 대체 사유와 마지막 장치 표시, 동일 조건·저장 구간을 유지하는 가속 재시도. 최초 GPU 대체 원인은 미확정으로 유지.
- 실물 104분 전체 생성 완료와 준비 타이틀의 영상 진행·탐색을 확인. [alpha.9 검증](docs/releases/0.1.0-alpha.9.md) 및 [Qwen3 네이티브 비교와 미채택 근거](docs/research/qwen3-asr-native-evaluation.md). alpha.8 이하 태그·파일은 보존.

### 0.1.0-alpha.8 — 2026-11-06 프리뷰

- 파일·연결된 Blu-ray의 선택 타이틀 전체를 영상 재생 없이 원문 인식한 뒤 원문 문맥으로 한국어 번역. 진행률·취소·중간 저장·같은 조건의 이어하기 제공.
- 원문/한국어 SRT, 선택 독음 조합 SRT와 재사용 가능한 자막 프로젝트. 완성 프로젝트 재생·탐색은 실시간 AI 준비를 기다리지 않으며 타이틀·오디오가 달라지면 잘못된 자막을 적용하지 않음.
- Blu-ray 사전 생성만 읽기 속도 제한 해제, 감상 중 기존 10초/60초 정책 유지. 이미 켜진 AI의 외부 자막 실패 전환을 수정.
- [사용 안내](docs/PREPARE-CAPTIONS.md)와 [검증·배포 기록](docs/releases/0.1.0-alpha.8.md). 이전 태그·배포 파일 보존.

### 0.1.0-alpha.7 — 2026-11-06 프리뷰

- 유효한 음성 토큰 시각을 자막 페이지에 전달. 긴 가사를 글자 수에 따라 다시 배분하지 않고 토큰·결합 문자 경계와 쉼을 보존하며, 무효/없는 정렬 정보는 기존 구간 시간으로 처리.
- 선택 full Whisper large-v3 Q5 모델의 고정 다운로드·MIT 고지 추가. 이 구조의 native 자동 모드는 beam 5와 DTW를 사용하며 Kotoba/Turbo의 기존 자동 설정은 유지. CPU native 실행 뒤 독립 CLI 대체 경로도 보존.
- 번역 중 새 원문 문맥이 도착하면 낡은 응답을 표시하지 않고 재요청. 미래의 완료 번역도 문맥에 맞게 갱신하고 현재 읽는 자막은 유지. 번역 입력은 인식된 원문 텍스트와 앞뒤 원문이며 음성 직접 번역을 사용하지 않음.
- 공식 Qwen3-8B 후보는 12개 합성 원문 비교에서 부정·이름 오류가 있어 채택하지 않음. Hy-MT2 유지와 최신 문맥 처리 수정을 구분하며 [번역 기록](docs/translation-quality-validation.md)에 한계를 명시.
- 전체 Qt 검사 22/22, native 검사 9/9 및 실제 CUDA/정렬 소비 검사 통과. 실물 디스크·최종 배포 상태는 [alpha.7 기록](docs/releases/0.1.0-alpha.7.md)을 따름. alpha.6 태그·ZIP은 보존.

### 0.1.0-alpha.6 — 2026-11-06 프리뷰 작업

- 고정 Kotoba Q5 가중치에서 FP16 OpenVINO 인코더를 만드는 C++ 변환기·수치 비교·공유 정체성 검증 CLI 추가. 실제 인코더 호출 수·시간을 기록하며, 추론을 건너뛴 짧은 음성을 가속 성공으로 인정하지 않음.
- OpenVINO 2026.4.1로 업데이트하고 설치 시 합성 16초 입력을 연속 네 번 실제 인코딩해 자동 NPU 사용 여부를 결정. 모델·IR·worker·런타임·드라이버가 바뀌거나 느림·오류가 있으면 GPU→CPU로 대체. 연산량 하한과 제품 TFLOPS는 [성능 기록](docs/PERFORMANCE-KOTOBA.md)에서 구분.
- 버퍼된 앞뒤 원문으로 한국어 문맥 번역. 실제 HyMT2의 문맥 JSON 출력 문제를 공식 배경/원문 프롬프트로 수정하고, 같은 문장도 시간 구간별로 결과를 분리.
- 개발자 모드에 이용 확인을 추가했습니다. 보호 디스크 영상과 자막에는 기본 캡처 차단을 적용합니다.
- 그래픽 장치 손실 시 네이티브 출력을 한 번 복구하고 위치·불러온 자막·최신 재생 의도를 유지. 반복 오류는 재생을 멈추고 다시 열기 안내를 유지. 디스크 초기 안내·허용된 메뉴 동작을 존중.
- AACS/BD+ 상세 오류가 일반 오류로 덮이지 않게 보존하고 재열기 때 초기화. 실패 후 잘못된 일시정지 아이콘·재생 의도를 정리. 실제 연결 디스크의 시작 안내→메뉴 진입은 확인했으나 과거 AACS 실패 원인은 미확정.
- 최종 패키지·CI·장치 확인은 [alpha.6 기록](docs/releases/0.1.0-alpha.6.md)을 따름. 이전 버전·태그·ZIP은 보존.

### 0.1.0-alpha.5 — 2026-11-06 프리뷰 작업

- 정지 포인터의 VLC 가운데 클릭을 감지하기 위해 현재 프로세스의 캡처 시작·종료 WinEvent만 관찰. GUI 스레드에서는 이벤트 번호·창만 기록하고, 100ms 확인 시 전경 창·캡처 창·포인터가 플레이어 소유인지 검사해 창/전체화면 재생바를 깨움. 전역 마우스 훅·Raw Input 등록·세션 유휴 시각은 사용하지 않음.
- 비활성 창·다른 창·팝업/모달의 이벤트 번호도 소비해 늦은 재처리를 방지. 기존 5개 추적 회귀는 이벤트 번호 변화·순환을 다루며, 새 실제 Win32 알림·수명 검사 2개는 RED 2건 뒤 선택 실행 4/4 통과(초기화·정리 포함). 최종 전체 재생바·CTest·네이티브·패키지·CI 검증은 [alpha.5 기록](docs/releases/0.1.0-alpha.5.md)의 대기 상태를 따름.
- CUDA 배포 구성, 기본 Turbo·선택 Kotoba와 번역 Q4 프로필, 10초 최소·60초 선행 정책은 유지. `v0.1.0-alpha.4`와 기존 ZIP·체크섬을 보존.

### 0.1.0-alpha.4 — 2026-11-06 프리뷰

- `-IncludeCuda`로 `windows-x64-cuda` ZIP에 음성 인식·번역 CUDA 12.4를 함께 준비. 동일 NVIDIA DLL 3개는 `runtime/cuda`로 공유하고 엔진별 GGML은 격리. 드라이버 변경 없이 CPU/Vulkan 경로 유지.
- 기존 `windows-x64` 코어는 CUDA 생략을 유지. 수동 릴리스 워크플로의 `include_cuda` 기본값은 `true`; 구성별 manifest·해시와 새로 푼 패키지 검증을 구분.
- 재생바 입력·겹침 순서를 보완하고 네 가지 회귀 및 Windows 파일 화면의 창/전체화면 복원을 확인. 최종 VLC 검사에서는 이미 포커스된 영상의 정지 포인터·유휴 후 가운데 클릭 실패가 2회 남아 모든 클릭 복구 완료로 취급하지 않음. 10초 최소·60초 선행 정책 유지.
- 번역 디코딩·Q6·일반 자막 지시 196개 요청 비교에서 확실한 의미 개선이 없어 Hy-MT2-7B Q4와 기존 설정 유지. 선택 일본어 Kotoba-Whisper v2.0 Q5 설치기·해시·Apache-2.0 원문 추가. 배포 기본 Turbo Q5는 유지하며 정확도와 실행 검증을 구분.
- 최종 ZIP 크기·CI·네이티브 실행·공개 완료는 [alpha.4 기록](docs/releases/0.1.0-alpha.4.md)의 확인 상태와 공개 검증 자산을 따름.

### 0.1.0-alpha.3 — 2026-11-06 프리뷰 작업

- 긴 원문을 시간 안에서 짧게 나누고 원문·독음·번역을 같은 페이지로 표시. 실제 글자 배치로 공백 없는 문자열도 줄바꿈하며 결합 문자·이모지를 보존.
- 재생·탐색 후 최소 10초 준비와 60초 선행 작업을 분리. 실패로 최소 버퍼를 자동 우회하지 않고 수동 일시정지를 유지하며 재시도/자막 없이 계속을 제공.
- 엄격한 UTF-8/UTF-16 SRT와 로컬 FFmpeg의 ASS/SSA/VTT/SMI, CP949/Shift-JIS 추정 변환. 같은 이름 검색·드롭·실패 시 현재 언어 AI 전환, 파일/디스크 본편별 로컬 이어보기 추가.
- 영상과 재생바를 독립 창으로 겹쳐 숨김 때 영상 배치를 유지. 74 px 전체화면 바는 정지 포인터가 바 위에 있어도 5초 후 숨고 메뉴·끌기·키 조작은 유지. Windows 캡처 제외는 영상·자막만 적용하며 본창·메뉴·재생바는 보임.
- F/Enter/F11 전체화면, Esc 해제, 좌우 10초, 위아래 음량 5%, M 음소거. 디스크 메뉴는 Ctrl+방향키/Ctrl+Enter; 입력 위젯의 키 유지.
- 코어에 MIT Whisper large-v3-turbo Q5 모델 추가. CUDA 음성 인식·번역은 원문 NVIDIA CUDA 12.4 EULA를 보존하는 선택 수동 설치로 제공하며 드라이버를 변경하지 않음.
- 선택 번역 프로필: fast Qwen3-4B 약 2.50 GB / quality Hy-MT2-7B 약 4.624 GB(Apache-2.0). 설치한 quality는 명시한 모델 설정이 없을 때만 시작 기본값. 한 cue의 잘린/무효 번역은 건너뛰고 후속 번역 계속.
- 소유 파일의 실제 키·자막 가져오기·이어서보기 검사와 개발 PC CUDA 실행 확인은 최종 CI·포터블 검증과 구분. [사용 안내](docs/CAPTIONS-AND-PLAYBACK.md)와 [alpha.3 기록](docs/releases/0.1.0-alpha.3.md)에 확인 범위를 기재하며 배포 완료를 미리 주장하지 않음.

### 0.1.0-alpha.2 — Windows 프리뷰

- TUKI 메뉴→본편 안정화: 소프트웨어 디코딩과 10초 디스크 캐시를 기본으로 선택. 새 연속 재생·네 번 탐색·챕터·메뉴 검사를 통과했으며 하드웨어 디코더의 표면 할당 오류는 재현·기록.
- 원문·한글 독음·한국어 번역을 분리해 표시. 네이티브 MeCab/IPADIC과 선택 Qwen3-4B 번역 추가. 포터블 코어에서는 약 2.50 GB 모델을 분리 설치.
- ASR NPU 인코더/OpenVINO 2024.1, CPU 디코더, 선택 Vulkan GPU와 CPU 대체. 번역은 확인된 Arc Vulkan/CPU 경로를 기본 배포하고 실패한 번역 NPU/OpenVINO GPU를 성공으로 표시하지 않음.
- AI 모델 백그라운드 준비, 탐색 간 모델 상주, 최신 요청 우선. 15초 구간·최소 10초 준비·약 30~45초 선행, 수동 일시정지 유지와 2/4/8초 재시도. 디스크는 실제 음성 2초 선행 읽기로 탐색 뒤 시계 보정에 대응. 준비된 NPU 캐시에서 소유 파일 2,127ms/탐색 1,780ms 관측이며 콜드 시작·광학 탐색 시간과 구분.
- 기본 다크/선택 라이트, 자체 SVG 로고·컨트롤, Noto 글꼴, 약 74px 전체화면 바와 5초 유휴 숨김·상호작용 유지. 진단을 개발자 모드로 이동.
- 파일 출력 GPU 선택과 별도 AI 장치 선택, Windows 본창·자막 창의 캡처 제외 상태 표시, HDR/SDR 렌더링 진단.
- 파일 전용 프레임 보간: 디스플레이 혼합과 실험적 CPU 광학 흐름 24~360 fps 목표. 합성 360 fps 타임스탬프 확인; 1080p 실시간 성능은 충족하지 못함. HDR·미확인 메타데이터에서는 광학 흐름을 차단.
- 캡처·HDR/무손실·신규 ASR 지연·최종 포터블·CI 완료 여부는 [alpha.2 릴리스 기록](docs/releases/0.1.0-alpha.2.md)에 별도로 기록.

### 0.1.0-alpha.1 — 2026-11-05 로컬 개발 스냅샷

- C++20/Qt 6 Windows 기반, libVLC Blu-ray 메뉴·리모컨, libmpv 파일 재생, 로컬 자막 작업과 초기 배포 스크립트.
- 공개된 불변 릴리스로 취급하지 않는 초기 로컬 빌드. 일부 실행에서 메뉴→본편 전환 후 약20초 정지를 관측했으므로 초기 누적 재생 수치만으로 장기 안정성을 판단하지 않음.
- 당시 기록은 이력이며 alpha.2의 새 빌드·패키지 검증을 대신하지 않음.

## English

### 0.2.14

- Finish the default Windows setup offline with bundled Turbo. Choose additional recognition and translation model downloads and AI acceleration setup explicitly.
- Enable startup update checks, LAN discovery and listening, and automatic Blu-ray detection to run them.
- Caption backgrounds fit the text lines more closely on Windows and Android.

### 0.2.13

- Detect when the playback clock and audio/video output stop at the end of a Blu-ray title in menu mode. Press Play to start that title again from the beginning.

### 0.2.12

- Restore volume and mute controls when Blu-ray plays through PCM. Active bitstream output uses receiver volume.
- Replay from the beginning after playback ends. See the current chapter in the timeline and chapter list and use YouTube-style playback shortcuts.
- Limit how long playback waits for original captions and update cues as playback continues. Translate imminent captions first when dialogue gets dense.
- See live AI caption progress and import device subtitle files on Android. Live host captions update independently of the video stream.
- Reset Developer Mode, video capture allowance, performance overlay, and detailed diagnostics at every launch. Detailed diagnostics and installer logs are opt-in.

### 0.2.11

- Chapter changes on Android start sooner when the Blu-ray chapter matches the disc's video index. Discs that cannot use the index continue to use timestamp-based seeking.

### 0.2.8

- Use a paired Galaxy Tab to navigate the PC's Blu-ray menu and start sharing the feature.
- Changing chapters on Android closes the previous stream before requesting the new position. Desktop and Android playback bars display buffering information.
- Scrolling in desktop settings scrolls the panel without changing the selected values.
- Developer Mode exports playback events and performance samples in one chronological JSON log.
- Automatic AI acceleration selects detected Radeon Vulkan devices for speech recognition and translation while preserving manual device selections.
- Choose PC-approved files on the tablet. The controller appears above chapters by default in the regular playback layout and opens on demand in fullscreen.
- Open Blu-ray chapters directly by number and share captions already prepared on the PC.

### 0.2.7

- Android automatically finds running Windows players and refreshes its list as the connection changes.
- A paired Android device can start sharing the media currently open on its PC when the PC enables remote start.
- Local caption translation uses loopback HTTPS with a new certificate for each worker launch.
- Hardened caption project path checks, LAN connection limits, and validation of Android media requests and session responses.

### 0.2.6

- The Windows updater offers a link to the official downloads when a check or download fails.
- Automated UI tests skip physical optical-drive discovery and storage temperature queries.

### 0.2.5

- Bound LAN JSON depth and WebVTT response sizes on Android, and cancel active metadata reads when disconnecting.
- Hardened Windows LAN stream idle handling and pairing-token rotation. VPN discovery can reply from its listening port.
- Tightened Windows installer payload, dependency cache, and updater validation, and isolated GitHub build permissions from draft publishing.
- Reduced repeated caption lookup and translation prioritization work, and bounded temporary Blu-ray audio buffering.
- Use a compatible video decoding path when switching from a Blu-ray menu to the feature.
- Open Android playback settings without interrupting playback. The panel shows current quality, HDR and speed settings, and video and chapters adapt to wide and foldable windows.
- Provide a desktop build archive for Ubuntu 24.04 x64.

### 0.2.4

- Windows Developer Mode can compare a CPU baseline with Radeon video, speech recognition, and translation separately or together.
- File playback adds D3D11VA copy-back, DXVA2 copy-back, and experimental Vulkan Video copy-back options. A failed explicitly selected Vulkan translation backend retries on the same GPU with a lower-VRAM configuration.
- Diagnostics record the applied profile, Vulkan devices seen by each worker, and low-VRAM retry results.

### 0.2.3

- Windows detects GPUs and NPUs at first launch and in Settings, then recommends NVIDIA CUDA, Vulkan, and Intel OpenVINO packages for optional installation. Radeon video and local caption acceleration are part of the same Windows build.
- Tester diagnostics include resource history for the system and player, along with requested, active, and fallback decoder and AI backend information.
- The window remains responsive while the optical drive prepares a disc. The player gives a specific error when it cannot read the AACS configuration.
- Added an Android bottom seek bar, portrait chapters, repeated-tap seeking, and fullscreen gestures. The host PC can adjust streaming quality, screen fit, and subtitle timing for a connected receiver.
- Devices connected through WireGuard or a similar VPN can enter a private IP directly or discover LumaPlayer hosts within an explicitly entered private subnet.
- Android defaults to original quality with source HDR. Added Balanced up to 1080p SDR and Data Saver up to 480p SDR. Compatible devices can play the original AV1 stream.
- Added automatic PiP on Home and background playback on Android. Wide landscape windows place the chapter list beside the video.
- Added router-free Wi‑Fi Direct playback between Windows and Android. Enter the temporary Wi‑Fi password and compare the six-digit code on both devices.
- Kept the Windows performance overlay above the video and added available device temperatures, per-GPU VRAM, and network throughput.
- Korean readings of Japanese captions now keep particles and auxiliary forms with the preceding word.

### 0.2.0-beta.2

- View, disconnect, and forget LAN devices in the Windows player. A second Windows PC can connect and play media using the same code approval flow.
- Check GitHub for Windows updates at launch or from settings. Automatic notifications can be turned off. Installation verifies the signed release manifest and the installer SHA-256.

### 0.2.0-beta.1

- Discover LumaPlayer PCs on the LAN from Android or enter a private IP. Compare the code on both screens and approve the connection on the PC. Choose per-device approval or remember the device for next time. Prepared PC captions can accompany local files and Blu-ray titles.
- Secure Android video surfaces and capture protection are enabled. HDR, Dolby Vision, and spatial audio depend on the original media and device capabilities.
- HDR auto/SDR switching, PCM, and HDMI bitstream are available in Settings. Developer mode adds system/player performance comparisons and forced HDR rendering for SDR display tests.

### 0.1.6 — 2026-11-12 public beta

- Use Qt 6.4-compatible headers when compiling window movement and captions on Linux.

### 0.1.5 — 2026-11-12 public beta

- Update the video, caption, and transport windows during the same Windows drag event.
- Add always-on-top picture-in-picture. Toggle it from the transport or with `Ctrl+Shift+P`, and return with `Esc`.
- Click the seek track to jump and open the translation glossary from the captions menu.
- Separate window construction, playback, captions, and input. Update Windows Qt to 6.11.3 and harden library search, automatic scripts, and local translation response handling.

### 0.1.4 — 2026-11-12 public beta

- Save a separate translation glossary for an open Blu-ray. The player loads it automatically when reopening the disc or preparing its captions.

### 0.1.3 — 2026-11-12 public beta

- Save `source → translation` entries under **AI Captions → Translation glossary**. Live Korean translation and new prepared caption jobs send matching terms to the model alongside nearby context.

### 0.1.2 — 2026-11-12 public beta

- Change the font, size, and text and background opacity in **Settings → Caption appearance**. Changes take effect during playback and persist across launches.
- The settings apply to AI captions, prepared caption projects, and imported text subtitles. Reset them to Noto Sans and the default size and opacity.

### 0.1.1 — 2026-11-12 public beta

- Prefer GPU video decoding for Blu-ray on Windows and recover a stalled picture at the same position with software decoding.
- Read Blu-ray audio for live captions in windows of up to 75 seconds. Show disc buffering and caption preparation status.
- Prepare ten seconds of captions at startup and after seeks. During playback, refill when less than two seconds remain.
- Update the video, captions, and transport positions as the main window moves.
- Provide a CUDA installer, automatic model downloads, a product introduction, hardware recommendations, and troubleshooting. [Beta record](docs/releases/0.1.0-beta.1.md)

Published distributions receive immutable tags, GitHub Releases and checksums. Local test builds are distinct from distributed versions; each release record owns its final validation status.

### 0.1.0-alpha.10 — preview, 2026-11-06

- Prefer the playing disc, uniquely identified title and audio ordinal when opening preparation. Snapshot before stop and reject other-disc, ambiguous-duration or out-of-bounds preferences.
- Without usable playback state, keep longest-title selection without labeling it the main feature; explain commentary/bonus selection against the disc menu. Distinguish actual ALL PLAY from the longer commentary variant.
- See alpha.10 validation/release evidence. Generation, models, project schema and earlier tags/assets remain unchanged.

### 0.1.0-alpha.9 — preview, 2026-11-06

- Select the prepared Blu-ray feature through the actual menu-free native title list, pausing until unique duration/selection acknowledgment. Never force captions onto ambiguous or mismatched titles; retain ordinary authored menus.
- Support direct-mode content index 0 and mode/project-aware resume. Preserve completed originals/translations across source switches and video-output recovery; reject changed sources without a crash.
- Show controlled preparation fallback reasons and last-used devices, with checkpoint-preserving same-request accelerator retry. The initial GPU rejection cause remains unconfirmed.
- Observed complete 104-minute preparation and matching-feature video progress/seek. See alpha.9 validation and the native Qwen3 comparison/retention decision. Earlier tags and files remain immutable.

### 0.1.0-alpha.8 — preview, 2026-11-06

- Prepare all original captions for a file or connected Blu-ray title without playback, then translate recognized text with original context. Progress, cancellation, checkpoints and compatible resume.
- Original/Korean/display SRT exports and reusable caption projects. Completed-project playback/seeking avoids live AI holds and validates the title/audio before attachment.
- Offline-only unthrottled disc reading; existing live ten/sixty-second policy retained. Fixed invalid-subtitle fallback when AI was already enabled.
- See the preparation guide and alpha.8 validation/release record. Earlier tags and assets remain immutable.

### 0.1.0-alpha.7 — preview, 2026-11-06

- Carry validated speech-token times into caption pages. Preserve token/grapheme boundaries and pauses instead of redistributing long lyrics by character count; missing or invalid alignment retains coarse timing.
- Add a pinned optional full Whisper large-v3 Q5 download and MIT notice. Native auto mode uses beam 5/DTW for that architecture while retaining Kotoba/Turbo auto settings. Preserve an independent legacy CLI after native CPU failure.
- Requeue in-flight translations when new original context arrives, refresh completed future captions and keep currently displayed text stable. Translation receives recognized original text and neighboring originals, without direct audio translation.
- Reject the official Qwen3-8B candidate after negation/name errors in twelve synthetic source comparisons. Retain Hy-MT2; context correctness is separate from model wording quality, as documented in the translation validation record.
- Qt 22/22, native 9/9 and actual CUDA/timing-consumer checks passed. Follow the alpha.7 release record for physical-disc/final-package status. Earlier alpha.6 tags and archives remain immutable.

### 0.1.0-alpha.6 — preview work, 2026-11-06

- Added native C++ export, parity and shared identity verification for a pinned Q5-derived FP16 Kotoba encoder, plus actual per-request encoder counts/timing. Skipped short inputs cannot acknowledge acceleration.
- Upgrade to OpenVINO 2026.4.1 and qualify automatic NPU use with four genuine encodes of owned 16-second synthetic input. Source/IR/worker/runtime/driver changes, slow runs and failures select GPU then CPU. Distinguish analytical operation-rate bounds from product TFLOPS in the performance report.
- Translate targets using buffered original context; fix real HyMT2 context-JSON echoes through the official background/source prompt. Keep identical phrases separate by timed cue.
- Added a developer-mode acknowledgment. Capture exclusion defaults to on for protected-disc video and captions.
- Recreate native output once after graphics-device loss while preserving position, imported captions and latest playback intent. Repeated loss stops playback with persistent reopen guidance. Respect authored disc initialization and permitted menu actions.
- Preserve specific AACS/BD+ errors, reset them when reopening and clear failed playback intent. The connected disc reached its interactive menu; the original AACS failure's root cause is unconfirmed.
- Follow the alpha.6 release record for final package, CI and device status. Earlier versions and artifacts remain immutable.

### 0.1.0-alpha.5 — preview work, 2026-11-06

- Observe capture-start/end WinEvents scoped to the current process for stationary VLC middle clicks. The GUI observer records only an event serial/window; a 100 ms poll checks foreground, capture-window and pointer ownership before waking windowed/fullscreen controls. No global mouse hook, Raw Input registration or session-idle timestamp is used.
- Consume background, foreign-window and popup/modal serials without stale replay. Retain five serial-change/wrap tracker regressions. Two new real Win32 notification/lifetime tests first failed, then passed a selected four-case run including setup/cleanup. Final full transport, CTest, native, package and CI verification remain pending in the [alpha.5 record](docs/releases/0.1.0-alpha.5.md).
- Keep CUDA flavors, default Turbo/optional Kotoba, Q4 translation profiles and the ten-second minimum/sixty-second lookahead. Preserve `v0.1.0-alpha.4` and its archives/checksums.

### 0.1.0-alpha.4 — preview, 2026-11-06

- Add `-IncludeCuda` for a `windows-x64-cuda` ZIP with both ASR and translation CUDA 12.4. Share three identical NVIDIA DLLs under `runtime/cuda`, keeping engine-specific GGML isolated. Preserve CPU/Vulkan paths without driver changes.
- Keep the CUDA-free `windows-x64` core. The manual release workflow's `include_cuda` input defaults to `true`; distinguish each flavor's manifest, hashes and extracted-package verification.
- Improve transport input/stacking with four regressions and native windowed/fullscreen file restoration checks. Final VLC testing still reproduced two stationary middle-click misses after idle on already-focused video, so this is not complete click recovery. Preserve ten-second minimum and sixty-second lookahead.
- Compare 196 decoding, Q6 and general subtitle-style requests without a clear semantic gain, retaining Hy-MT2-7B Q4 and current settings. Add optional Kotoba-Whisper v2.0 Q5 installation and native CUDA compatibility evidence; multilingual Turbo Q5 remains the default. Execution does not establish accuracy.
- Final ZIP size, CI, native execution and publication follow the [alpha.4 record](docs/releases/0.1.0-alpha.4.md) and published validation assets.

### 0.1.0-alpha.3 — preview work, 2026-11-06

- Split long source cues into timed pages and align source/readings/translations. Measured text layout wraps unbroken strings while preserving combining marks and emoji.
- Separate the strict ten-second playback/seek minimum from continued 60-second lookahead. Preserve manual pause; failure does not automatically bypass readiness. Provide retry and explicit continue-without-captions actions.
- Add strict UTF-8/UTF-16 SRT, local FFmpeg ASS/SSA/VTT/SMI import and heuristic CP949/Shift-JIS conversion. Add same-stem discovery, dropping, current-language AI fallback and local per-file/disc-title resume.
- Float video/transport without reflow. The 74 px fullscreen bar hides after five idle seconds even with the pointer over it, while menus/dragging/key activity remain usable. Exclude only video/captions from Windows capture; main UI, menus and transport stay visible.
- Add F/Enter/F11 fullscreen, Esc exit, ten-second arrows, 5% volume arrows and M mute. Use Ctrl+arrows/Ctrl+Enter for disc menus while preserving input-control keys.
- Include the MIT Whisper large-v3-turbo Q5 model in core. Offer manual optional ASR/translation CUDA installation, retaining original NVIDIA CUDA 12.4 EULA and leaving drivers unchanged.
- Offer optional fast Qwen3-4B (~2.50 GB) and quality Hy-MT2-7B (~4.624 GB, Apache-2.0) translation. Installed quality becomes the startup default only without an explicit saved model. Skip an invalid/truncated cue translation and continue later cues.
- Owned-media native keys/import/resume checks and development-PC CUDA execution are distinct from final CI/portable validation. The [guide](docs/CAPTIONS-AND-PLAYBACK.md) and [alpha.3 record](docs/releases/0.1.0-alpha.3.md) state scope without claiming publication is complete.

### 0.1.0-alpha.2 — Windows preview

- Stabilize the tested TUKI menu/main-title path with software decoding and a ten-second disc cache. A fresh playback/four-seek/chapter/menu sequence passed; hardware surface-allocation failures were reproduced and recorded.
- Separate original captions, Japanese Hangul readings and Korean translation. Add native MeCab/IPADIC and optional Qwen3-4B translation; install its approximately 2.50 GB model separately from the portable core.
- Use native NPU encoder/OpenVINO 2024.1 with CPU decoding, selected Vulkan GPU and CPU fallback for ASR. Default translation packaging uses verified Arc Vulkan/CPU; failed translation NPU/OpenVINO GPU runs are not reported as acceleration success.
- Add background ASR warmup, resident models and newest-request priority. Use 15-second chunks, a ten-second minimum, about 30–45 seconds ahead, preserved manual pause and bounded 2/4/8-second retries. Read two seconds of real disc preroll to cover post-seek clock correction. Owned-file NPU preparation measured 2,127ms/1,780ms after seek with a warm compiled cache, distinct from cold startup and optical seeking.
- Add dark/light custom controls, SVG branding, Noto fonts, an approximately 74 px fullscreen bar with five-second inactivity behavior, and opt-in developer diagnostics.
- Add file output-adapter and separate AI device choices, capture-affinity status for both windows, and HDR/SDR diagnostics.
- Add file-only display blending and experimental 24–360 fps CPU optical-flow targets. Synthetic 360 fps timestamps passed; 1080p real-time throughput did not. Optical flow rejects HDR/unknown metadata.
- Keep final capture, HDR/lossless, ASR timing, portable package and CI results explicit in the [alpha.2 record](docs/releases/0.1.0-alpha.2.md).

### 0.1.0-alpha.1 — local development snapshot, 2026-11-05

- Establish native C++20/Qt 6, libVLC disc menus, libmpv files, local subtitle jobs and initial packaging scripts.
- This early local snapshot is not a published immutable release. A main-title freeze around 20 seconds was observed in some runs; cumulative intro counters did not establish sustained main-title stability.
- Historical records do not substitute for alpha.2 build/package validation.
