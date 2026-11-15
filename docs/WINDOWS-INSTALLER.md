# Windows 설치 프로그램 / Windows installer

## 한국어

릴리스의 Windows x64 설치 EXE로 LumaPlayer를 설치합니다.

1. 설치 EXE를 실행하고 언어·설치 폴더를 선택합니다. 관리자 권한은 필요하지 않습니다. 기본 위치는 사용자 로컬 앱 폴더입니다.
2. 설치 화면에서 GPU·드라이버·메모리를 확인해 추천 항목을 체크합니다. 호환 NVIDIA GPU를 감지하면 CUDA를 추천합니다. RAM 16 GB급과 전용 VRAM 4 GB급부터 Whisper Turbo를, 그 아래는 Whisper Base와 OpenVINO 인코더를 추천합니다. RAM 32 GB급과 전용 VRAM 12 GB급부터 한국어 번역 모델 Hy-MT2도 추천합니다. 필요한 항목을 직접 바꾸거나 **플레이어만 · 오프라인**을 선택할 수 있습니다.
3. 설치 프로그램은 고정 HTTPS 주소에서 선택한 항목을 받아 SHA-256을 확인합니다. 같은 폴더에 검증된 모델과 CUDA 라이브러리가 있으면 재사용합니다. 앱에서 시작한 업데이트도 설치 화면에서 항목을 직접 추가할 수 있습니다. 자동 선택은 일반 설치 화면에서만 적용됩니다.
4. 시작 메뉴에서 LumaPlayer를 실행합니다. **AI 자막 → 음성 인식 모델 선택**에서 설치한 모델을 바꿀 수 있습니다. 한국어 번역기는 설치된 Hy-MT2를 찾습니다. **설정 → AI 가속 설정**에서 별도 가속 라이브러리를 설치할 수도 있습니다.

Whisper Base와 인코더는 약 172 MB, Turbo는 약 575 MB, full-v3 Q5는 약 1.08 GB, Hy-MT2는 약 4.62 GB입니다. CUDA 크기와 설치에 필요한 공간은 설치 화면에 표시됩니다. 제거 프로그램은 다운로드한 모델과 사용자 설정·자막을 보존합니다. 설치 EXE는 코드 서명되지 않았습니다.

CPU·Vulkan·Intel OpenVINO 실행기, Qt 6, Visual C++ 실행 DLL, libVLC·libmpv·FFmpeg과 Noto Sans는 기본 설치에서 제공합니다. 선택한 CUDA 12 런타임은 설치 폴더에 내려받아 사용합니다. CUDA Toolkit이나 Visual Studio 설치는 필요 없으며 가속에는 해당 장치의 호환 드라이버가 필요합니다.

설치기를 빌드하려면 공식 Inno Setup **6.7.4**, 네이티브 Windows 빌드와 잠금 파일에 지정한 Base 인코더·CUDA 개발 캐시를 준비하세요. `scripts/package-windows.ps1 -StageOnly -ForInstaller`로 만든 폴더를 `scripts/build-installer.ps1 -Payload <폴더> -Output <새 출력 폴더> -Compiler <ISCC.exe> -ManifestSha256 <manifest.json의 SHA-256>`에 전달합니다. 설치 EXE와 별도 CUDA 아카이브·체크섬을 같은 버전의 GitHub 릴리스에 올립니다. 0.2.15 설치 EXE의 `-cuda` 접미사는 0.2.14 이하 업데이트와 호환하기 위해 사용합니다.

### 설치가 차단될 때

1. [공식 릴리스](https://github.com/mk0000001/luma-player/releases/latest)에서 받은 같은 설치 파일의 `.sha256` 값을 `Get-FileHash -Algorithm SHA256 -LiteralPath <설치 파일 경로>`의 결과와 대조하세요. 값이 다르면 파일을 실행하지 말고 다시 받으세요.
2. Windows 보안의 보호 기록이나 V3의 진단 로그에서 제품명, 탐지명, 실제 차단된 파일명, LumaPlayer 버전을 확인하세요. 설치 파일을 열 때인지, 설치 중 파일을 풀거나 앱을 처음 열 때인지 함께 적어 주세요.
3. 설치 프로그램 로그가 필요하면 설치 파일에 `/LOG`를 붙여 실행하세요. 공유할 때는 개인 파일 경로와 계정 정보를 가리세요.

스마트스크린의 게시자·평판 경고와 백신의 파일 탐지를 구분합니다. 현재 Windows 설치 파일은 Authenticode 서명이 없으며, 앱의 업데이트 검증 서명은 GitHub에서 받은 파일의 무결성을 확인하는 데 사용합니다. 백신이 정상 파일을 잘못 탐지한 것으로 보이면 [안랩 오진 신고](https://ask.ahnlab.com/hc/ko/articles/900002412366) 또는 [Microsoft 파일 제출](https://www.microsoft.com/en-us/wdsi/filesubmission)에서 실제 차단된 파일의 분석을 요청할 수 있습니다. [Microsoft SmartScreen 게시자 평판 안내](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/smartscreen-reputation)

## English

Run the Windows x64 setup EXE from Releases, then select the language, destination and components. Installation is per user and needs no administrator rights.

Interactive setup checks the GPU, driver, and memory and preselects suitable components. It recommends CUDA on supported NVIDIA hardware, Whisper Turbo from the 16 GB RAM and 4 GB dedicated VRAM class, and Whisper Base with its OpenVINO encoder on smaller systems. Hy-MT2 is recommended from the 32 GB RAM and 12 GB dedicated VRAM class. Change selections or choose **Player only (offline)**. App-initiated updates offer manual selections.

Setup downloads selected components over pinned HTTPS URLs and checks their SHA-256 hashes. It reuses verified models and CUDA libraries already at the destination. Whisper Base with its encoder is about 172 MB, Turbo about 575 MB, full-v3 Q5 about 1.08 GB, and Hy-MT2 about 4.62 GB. Setup shows the CUDA download size. Uninstall preserves downloaded models, user settings and captions. The setup EXE is unsigned.

Build with official Inno Setup 6.7.4, the native Windows build, and the pinned Base encoder and CUDA development caches. Stage with `scripts/package-windows.ps1 -StageOnly -ForInstaller`, then call `scripts/build-installer.ps1 -Payload <folder> -Output <new folder> -Compiler <ISCC.exe> -ManifestSha256 <manifest SHA-256>`. Publish the installer, CUDA archive, and checksums together on the versioned GitHub release. The `-cuda` filename suffix in 0.2.15 preserves compatibility with updaters through 0.2.14.

The standard payload contains CPU, Vulkan and Intel OpenVINO runners, Qt 6, the Visual C++ runtime, libVLC/libmpv/FFmpeg, and Noto Sans. Select CUDA to install its runtime into the LumaPlayer directory. GPU and NPU acceleration require compatible device drivers. Use **AI 자막 → 음성 인식 모델 선택** to switch between installed models, and **설정 → AI 가속 설정** to manage additional runtimes.

### If installation is blocked

1. Compare the release's `.sha256` file with `Get-FileHash -Algorithm SHA256 -LiteralPath <installer path>`. Download the file again from the [official release](https://github.com/mk0000001/luma-player/releases/latest) if the values differ.
2. Check Windows Security's Protection History or the V3 detection log. Record the security product, detection name, blocked filename, LumaPlayer version, and whether the block happened before setup, during extraction, or at first launch.
3. Run setup with `/LOG` if an installer log is needed. Redact personal paths and account information before sharing it.

Windows setup files are currently unsigned by Authenticode. The player verifies update metadata signatures to check downloaded file integrity. Analyze a suspected file detection using [AhnLab's false-positive process](https://ask.ahnlab.com/hc/en-us/articles/900003237626-To-Submit-a-Malware-related-Inquiry) or [Microsoft's file submission portal](https://www.microsoft.com/en-us/wdsi/filesubmission). [Microsoft SmartScreen publisher reputation](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/smartscreen-reputation)
