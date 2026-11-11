# Windows 설치 프로그램 / Windows installer

## 한국어

Windows x64 CUDA 설치 EXE로 LumaPlayer를 설치합니다. 버전은 각 릴리스에서 확인할 수 있습니다.

1. 설치 EXE를 실행하고 언어·설치 폴더를 선택합니다. 관리자 권한은 필요하지 않습니다. 기본 위치는 사용자 로컬 앱 폴더입니다.
2. 권장 설치는 full-v3 Q5 음성인식(약 1.08 GB)과 Hy-MT2 한국어 번역(약 4.62 GB)을 자동 다운로드합니다. 인터넷 없이 설치하려면 기본 Turbo만 선택합니다. CUDA 실행기와 Turbo는 EXE에 포함됩니다.
3. 모델의 고정 HTTPS 주소와 SHA-256을 검증합니다. 이미 같은 폴더에 유효한 모델이 있으면 재사용합니다. 실패하면 설치 화면에서 재시도하거나 취소할 수 있습니다. 설치 화면의 다운로드 진행률을 확인하세요.
4. 시작 메뉴에서 LumaPlayer를 실행합니다. 기존 모델 선택은 유지합니다. 새 설치의 기본 인식기는 Turbo이며 AI 자막 설정의 모델 선택에서 full-v3를 선택할 수 있습니다. 한국어 번역기는 설치된 Hy-MT2를 우선 발견합니다.

플레이어 약 3.63 GB와 추가 모델 약 5.71 GB가 필요하며, 다운로드 임시 파일을 위한 공간도 확보하세요(약 16 GB 여유 권장). 제거 프로그램은 추가 다운로드 모델과 사용자 설정·자막을 보존합니다. 디스크 키·미디어·인식한 상용 자막은 포함하지 않습니다. 설치 EXE는 코드 서명되지 않았습니다.

별도 라이브러리도 함께 설치합니다: CUDA 12 실행 라이브러리(cudart·cuBLAS·cuBLASLt), 음성인식/번역 실행기, Qt 6, Visual C++ 실행 DLL, libVLC·libmpv·FFmpeg, OpenVINO 실행 라이브러리, Noto Sans. 사용자 앱 폴더에 설치하므로 시스템 PATH·드라이버를 바꾸거나 별도 CUDA Toolkit·Visual Studio를 요구하지 않습니다. NVIDIA GPU 가속은 호환 장치 드라이버가 있어야 하며 Intel NPU도 해당 장치 드라이버가 필요합니다. 장치 드라이버는 이 설치기가 자동 교체하거나 재부팅하지 않습니다.

설치기를 빌드하려면 공식 Inno Setup **6.7.4**와 현재 버전의 CUDA 포터블 폴더를 준비하세요. `scripts/build-installer.ps1 -Payload <폴더> -Output <새 출력 폴더> -Compiler <ISCC.exe> -ManifestSha256 <manifest.json의 SHA-256>`를 실행합니다. 패키지 파일의 크기·해시와 소스 커밋을 확인하고 새 출력 폴더에 설치 EXE를 만듭니다. 모델 URL은 저장소 잠금 파일에서 읽습니다.

## English

The Windows x64 CUDA setup EXE installs LumaPlayer. Choose your version from Releases, run the EXE, and select the language, destination, and components. Installation is per user and needs no administrator rights.

Recommended installation automatically downloads full-v3 Q5 ASR (1.08 GB) and Hy-MT2 Korean translation (4.62 GB). Bundled-Turbo-only installation works offline; CUDA runtimes and Turbo are embedded. Immutable HTTPS URLs and SHA-256 enforce model integrity. Valid existing models at the destination are reused; failures offer retry/cancel.

Launch from the Start Menu. Existing model choices are preserved. Fresh installations retain Turbo as the recognition default; full-v3 can be selected from AI caption settings. Installed Hy-MT2 is discovered preferentially. Allow approximately 16 GB free for the 3.63 GB player, 5.71 GB optional models and download staging. Uninstall preserves downloaded optional models and user settings/captions. No protected keys/media/commercial captions are bundled. The EXE is unsigned.

Build with official Inno Setup 6.7.4 and the current CUDA payload using `scripts/build-installer.ps1 -Payload <folder> -Output <new folder> -Compiler <ISCC.exe> -ManifestSha256 <manifest SHA-256>`. The builder verifies the manifest entries and source commit, reads model pins from the repository lock files, and writes the EXE to a new output folder.

Dependencies are installed alongside the application: CUDA 12 runtime (cudart/cuBLAS/cuBLASLt), ASR/translation runners, Qt 6, Visual C++ runtime DLLs, libVLC/libmpv/FFmpeg, OpenVINO runtime and Noto Sans. No separate CUDA Toolkit/Visual Studio or global PATH change is required. NVIDIA GPU and Intel NPU execution still require compatible device drivers; the installer does not replace drivers or reboot the system.
