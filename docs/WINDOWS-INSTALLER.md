# Windows 설치 프로그램 / Windows installer

## 한국어

릴리스의 Windows x64 기본 설치 EXE 또는 NVIDIA CUDA 포함 설치 EXE로 LumaPlayer를 설치합니다.

1. 설치 EXE를 실행하고 언어·설치 폴더를 선택합니다. 관리자 권한은 필요하지 않습니다. 기본 위치는 사용자 로컬 앱 폴더입니다.
2. 기본 설치는 오프라인에서 진행하며 두 설치 파일 모두 Turbo 음성 인식 모델을 포함합니다. full-v3 Q5 음성 인식(약 1.08 GB)이나 Hy-MT2 한국어 번역(약 4.62 GB)이 필요하면 다운로드할 모델을 선택하세요. 업데이트할 때도 같은 화면에서 추가 다운로드를 선택할 수 있습니다.
3. 모델의 고정 HTTPS 주소와 SHA-256을 검증합니다. 이미 같은 폴더에 유효한 모델이 있으면 재사용합니다. 실패하면 설치 화면에서 재시도하거나 취소할 수 있습니다. 설치 화면의 다운로드 진행률을 확인하세요.
4. 시작 메뉴에서 LumaPlayer를 실행합니다. **설정 → AI 가속 설정**을 열면 하드웨어를 확인하고 CUDA, Vulkan, OpenVINO를 선택할 수 있습니다. 가속 설치 도우미는 Windows의 PowerShell 실행 정책을 따릅니다. 기존 모델 선택은 유지합니다. 새 설치의 기본 인식기는 Turbo이며 AI 자막 설정의 모델 선택에서 full-v3를 선택할 수 있습니다. 한국어 번역기는 설치된 Hy-MT2를 우선 발견합니다.

플레이어 약 3.63 GB와 추가 모델 약 5.71 GB가 필요하며, 다운로드 임시 파일을 위한 공간도 확보하세요(약 16 GB 여유 권장). 제거 프로그램은 추가 다운로드 모델과 사용자 설정·자막을 보존합니다. 디스크 키·미디어·인식한 상용 자막은 포함하지 않습니다. 설치 EXE는 코드 서명되지 않았습니다.

별도 라이브러리도 함께 설치합니다: CUDA 12 실행 라이브러리(cudart·cuBLAS·cuBLASLt), 음성인식/번역 실행기, Qt 6, Visual C++ 실행 DLL, libVLC·libmpv·FFmpeg, OpenVINO 실행 라이브러리, Noto Sans. 사용자 앱 폴더에 설치하므로 시스템 PATH·드라이버를 바꾸거나 별도 CUDA Toolkit·Visual Studio를 요구하지 않습니다. NVIDIA GPU 가속은 호환 장치 드라이버가 있어야 하며 Intel NPU도 해당 장치 드라이버가 필요합니다. 장치 드라이버는 이 설치기가 자동 교체하거나 재부팅하지 않습니다.

설치기를 빌드하려면 공식 Inno Setup **6.7.4**와 현재 버전의 기본 또는 CUDA 포터블 폴더를 준비하세요. `scripts/build-installer.ps1 -Payload <폴더> -Output <새 출력 폴더> -Compiler <ISCC.exe> -ManifestSha256 <manifest.json의 SHA-256>`를 실행합니다. 패키지 파일의 크기·해시와 소스 커밋을 확인하고 새 출력 폴더에 설치 EXE를 만듭니다. 모델 URL은 저장소 잠금 파일에서 읽습니다.

### 설치가 차단될 때

1. [공식 릴리스](https://github.com/mk0000001/luma-player/releases/latest)에서 받은 같은 설치 파일의 `.sha256` 값을 `Get-FileHash -Algorithm SHA256 -LiteralPath <설치 파일 경로>`의 결과와 대조하세요. 값이 다르면 파일을 실행하지 말고 다시 받으세요.
2. Windows 보안의 보호 기록이나 V3의 진단 로그에서 제품명, 탐지명, 실제 차단된 파일명, LumaPlayer 버전을 확인하세요. 설치 파일을 열 때인지, 설치 중 파일을 풀거나 앱을 처음 열 때인지 함께 적어 주세요.
3. 설치 프로그램 로그가 필요하면 설치 파일에 `/LOG`를 붙여 실행하세요. 공유할 때는 개인 파일 경로와 계정 정보를 가리세요.

스마트스크린의 게시자·평판 경고와 백신의 파일 탐지를 구분합니다. 현재 Windows 설치 파일은 Authenticode 서명이 없으며, 앱의 업데이트 검증 서명은 GitHub에서 받은 파일의 무결성을 확인하는 데 사용합니다. 백신이 정상 파일을 잘못 탐지한 것으로 보이면 [안랩 오진 신고](https://ask.ahnlab.com/hc/ko/articles/900002412366) 또는 [Microsoft 파일 제출](https://www.microsoft.com/en-us/wdsi/filesubmission)에서 실제 차단된 파일의 분석을 요청할 수 있습니다. [Microsoft SmartScreen 게시자 평판 안내](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/smartscreen-reputation)

## English

Choose the standard or NVIDIA CUDA-bundled Windows x64 setup EXE from Releases. Run it and select the language, destination, and components. Installation is per user and needs no administrator rights.

The default setup runs offline and both installers include Turbo. Choose the optional downloads for full-v3 Q5 ASR (1.08 GB), Hy-MT2 Korean translation (4.62 GB), or both during installation or an update. Immutable HTTPS URLs and SHA-256 enforce model integrity. Setup reuses valid models at the destination and offers retry or cancellation if a download fails.

Launch from the Start Menu. Open **Settings → AI acceleration** to check the hardware and choose CUDA, Vulkan, or OpenVINO. The acceleration installer follows the Windows PowerShell execution policy. Existing model choices are preserved. Fresh installations use Turbo by default, and full-v3 is available in AI caption settings. Installed Hy-MT2 is discovered preferentially. Allow approximately 16 GB free for the player, optional models, and download staging. Uninstall preserves downloaded optional models and user settings and captions. The EXE is unsigned.

Build with official Inno Setup 6.7.4 and the current standard or CUDA payload using `scripts/build-installer.ps1 -Payload <folder> -Output <new folder> -Compiler <ISCC.exe> -ManifestSha256 <manifest SHA-256>`. The builder verifies the manifest entries and source commit, reads model pins from the repository lock files, and writes the EXE to a new output folder.

Dependencies are installed alongside the application: CUDA 12 runtime (cudart/cuBLAS/cuBLASLt), ASR/translation runners, Qt 6, Visual C++ runtime DLLs, libVLC/libmpv/FFmpeg, OpenVINO runtime and Noto Sans. No separate CUDA Toolkit/Visual Studio or global PATH change is required. NVIDIA GPU and Intel NPU execution still require compatible device drivers; the installer does not replace drivers or reboot the system.

### If installation is blocked

1. Compare the release's `.sha256` file with `Get-FileHash -Algorithm SHA256 -LiteralPath <installer path>`. Download the file again from the [official release](https://github.com/mk0000001/luma-player/releases/latest) if the values differ.
2. Check Windows Security's Protection History or the V3 detection log. Record the security product, detection name, blocked filename, LumaPlayer version, and whether the block happened before setup, during extraction, or at first launch.
3. Run setup with `/LOG` if an installer log is needed. Redact personal paths and account information before sharing it.

Windows setup files are currently unsigned by Authenticode. The player verifies update metadata signatures to check downloaded file integrity. Analyze a suspected file detection using [AhnLab's false-positive process](https://ask.ahnlab.com/hc/en-us/articles/900003237626-To-Submit-a-Malware-related-Inquiry) or [Microsoft's file submission portal](https://www.microsoft.com/en-us/wdsi/filesubmission). [Microsoft SmartScreen publisher reputation](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/smartscreen-reputation)
