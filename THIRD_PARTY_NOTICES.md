# 외부 구성요소·라이선스 / Third-party software and source locations

## 한국어

LumaPlayer 소스는 GPL-3.0-or-later입니다. 동적 라이브러리·모델·폰트에는 각 구성요소의 라이선스가 적용됩니다. 버전·다운로드·SHA-256은 `dependencies.lock.json`과 `translation-dependencies.lock.json`, 패키지 파일별 해시는 `manifest.json`에 기록합니다. 디스크 키·인증서·영상·음성·메뉴 이미지·상용 콘텐츠 인식 자막은 배포하지 않습니다.

| 구성요소 | 버전·출처 | 적용 조건·소스 |
|---|---|---|
| Qt Core/Gui/Widgets/Network | 공식 MSVC 6.8.3 | LGPL-3.0/GPL 대안, 아래 영문 표의 정확한 소스와 `LICENSES` 참조 |
| libVLC·libbluray 플러그인 | VLC 3.0.24 | GPL/LGPL 등 각 구성요소 조건, 원본 COPYING 포함 |
| libmpv·FFmpeg | mpv 3138fc8eea / FFmpeg 9.0.2 full | GPL 빌드, 아래 고정 버전 소스 참조 |
| whisper.cpp·GGML·Whisper base 및 OpenVINO 변환 인코더 | b5454 / 고정 모델 해시 | MIT, [Intel 변환 모델](https://huggingface.co/Intel/whisper.cpp-openvino-models), 원본 Whisper 조건 유지 |
| OpenVINO·oneTBB·포함 플러그인 | 공식 2024.1 Windows 런타임 | Apache-2.0 및 포함 외부 구성요소·재배포 조건, `LICENSES/OpenVINO` 원문 전체 포함 |
| MeCab·IPADIC | 61b90ba6e669dc2d7d533d4a80d206f3b31d52b1 | MeCab BSD 선택 라이선스, IPADIC 원본 COPYING·AUTHORS 유지; [정확한 소스](https://github.com/taku910/mecab/tree/61b90ba6e669dc2d7d533d4a80d206f3b31d52b1) |
| Noto Sans·Noto Sans CJK KR/JP | 고정된 수정 없는 원본 폰트 | SIL OFL 1.1, 상업적 앱에 포함·임베딩 가능. 폰트 단독 판매 제한과 저작권·라이선스 유지; `LICENSES/NotoSans*-OFL.txt` |
| 로컬 한국어 번역 | llama.cpp b12125 / Qwen3-4B Q4_K_M | MIT / Apache-2.0, 선택 설치 모델 및 런타임 원문은 `LICENSES/translation` 참조 |
| AACS·BD+ 읽기 라이브러리 | 아래 MSYS2 버전 | LGPL-2.1-or-later 등 원본 조건. 키·인증서 제공이나 인증 DRM/HDCP 출력을 뜻하지 않음 |
| Microsoft VC143 런타임 | 설치된 공식 Visual Studio 재배포 파일 | 앱과 네이티브 자식 실행기 옆에 수정 없이 복사. [Microsoft 재배포 목록과 조건](https://learn.microsoft.com/en-us/visualstudio/releases/2022/redistribution) 적용 |

Noto 폰트는 시스템에 설치하지 않고 앱에서 로드합니다. 이름과 폰트 라이선스를 변경하지 않습니다. 로고·아이콘·미리보기 그림과 예문은 이 프로젝트에서 독립적으로 작성했습니다. Dolby 상표·인증, 독점 DRM SDK, 그래픽 드라이버 사용 권리를 이 소스 라이선스로 부여하지 않습니다.

## English

LumaPlayer is GPL-3.0-or-later. The portable package includes dynamically loaded upstream components; their respective notices and terms continue to apply. Exact Windows artifacts and SHA-256 hashes are in `dependencies.lock.json`. No disc video, audio, menu assets, KEYDB.cfg, private host certificate, API credential or generated commercial-disc transcript is distributed.

| Component | Version / provenance | License and source |
|---|---|---|
| Qt Core/Gui/Widgets/Network | 6.8.3, official MSVC 2022 package | LGPL-3.0 / GPL-3.0 alternatives; [qtbase 6.8.3 source](https://download.qt.io/official_releases/qt/6.8/6.8.3/submodules/qtbase-everywhere-src-6.8.3.tar.xz), [Qt licensing](https://www.qt.io/licensing/open-source-lgpl-obligations) |
| VLC / libVLC / libbluray plugins | VLC 3.0.24 official Windows archive | VLC GPL-2.0-or-later and library/plugin component licenses; [VLC source archive](https://download.videolan.org/pub/videolan/vlc/3.0.24/vlc-3.0.24.tar.xz), [source and contrib build recipes](https://code.videolan.org/videolan/vlc/-/tree/3.0.24), COPYING in runtime/vlc |
| libmpv and its linked dependencies | mpv 3138fc8eea; shinchiro 20261105 build | GPL-2.0-or-later build; [mpv exact source](https://github.com/mpv-player/mpv/tree/3138fc8eea), [Windows dependency build recipes](https://github.com/shinchiro/mpv-winbuild-cmake/tree/20261105) |
| FFmpeg full and linked libraries | Gyan FFmpeg 9.0.2 full | GPL-3.0-or-later build; [FFmpeg 9.0.2 source](https://ffmpeg.org/releases/ffmpeg-9.0.2.tar.xz), [Gyan build provenance](https://www.gyan.dev/ffmpeg/builds/), COPYING in runtime/tools |
| whisper.cpp / ggml / CPU and Vulkan backends | upstream b5454 plus the native adapter in `native/ai_worker` | MIT; [exact source](https://github.com/ggml-org/whisper.cpp/tree/b5454), included LICENSES/Whisper-MIT.txt |
| Whisper multilingual base model | ggml-base.bin, SHA-256 60ed5bc3dd14eea856493d334349b405782ddcaf0028d4b5df4088345fba2efe | [OpenAI Whisper MIT license and model provenance](https://github.com/openai/whisper), [GGML conversion/model repository](https://huggingface.co/ggerganov/whisper.cpp) |
| OpenVINO encoder model | Intel conversion pinned in `dependencies.lock.json` | MIT; [official model card](https://huggingface.co/Intel/whisper.cpp-openvino-models) |
| OpenVINO and oneTBB | official 2024.1 runtime | Apache-2.0 and bundled dependency/redistribution terms; complete upstream notices in `LICENSES/OpenVINO` |
| MeCab and IPADIC | source commit 61b90ba6e669dc2d7d533d4a80d206f3b31d52b1, native MSVC build | MeCab BSD option; IPADIC COPYING/AUTHORS preserved; [source](https://github.com/taku910/mecab/tree/61b90ba6e669dc2d7d533d4a80d206f3b31d52b1) |
| Noto Sans, Noto Sans CJK KR/JP | unmodified files pinned in `dependencies.lock.json` | SIL OFL 1.1 permits commercial application bundling/embedding with its conditions; `LICENSES/NotoSans-OFL.txt` and `NotoSansCJK-OFL.txt` |
| llama.cpp and optional Qwen3-4B model | b12125 / official Q4_K_M, pinned translation lock | MIT / Apache-2.0; `LICENSES/translation` and `docs/local-translation.md` |
| libaacs | MSYS2 mingw64 0.12.0-1 | LGPL-2.1-or-later; [VideoLAN source](https://code.videolan.org/videolan/libaacs), [MSYS2 recipe](https://github.com/msys2/MINGW-packages/tree/master/mingw-w64-libaacs) |
| libbdplus | MSYS2 mingw64 0.2.0-2 | LGPL-2.1-or-later; [VideoLAN source](https://code.videolan.org/videolan/libbdplus), [MSYS2 recipe](https://github.com/msys2/MINGW-packages/tree/master/mingw-w64-libbdplus) |
| libgcrypt | MSYS2 mingw64 1.12.4-2 | LGPL-2.1-or-later library; [source](https://gnupg.org/ftp/gcrypt/libgcrypt/), [MSYS2 recipe](https://github.com/msys2/MINGW-packages/tree/master/mingw-w64-libgcrypt) |
| libgpg-error | MSYS2 mingw64 1.61-1 | LGPL-2.1-or-later library; [source](https://gnupg.org/ftp/gcrypt/libgpg-error/), [MSYS2 recipe](https://github.com/msys2/MINGW-packages/tree/master/mingw-w64-libgpg-error) |

Windows packages may also include Microsoft runtime/D3D redistributables deployed by Qt; these are governed by their accompanying redistribution terms and are not application source. The DirectX Shader Compiler upstream license is included in `LICENSES/DirectXShaderCompiler.txt`; [its source and notices](https://github.com/microsoft/DirectXShaderCompiler). Upstream archives retain further dependency notices. See `LICENSES` and each runtime's supplied notices; source references describe the bundled build, not a license for unrelated proprietary DRM SDKs or Dolby trademarks/certification.

VC143 redistributable DLLs are copied unchanged beside native child executables as well as the application; see [Microsoft's redistribution list](https://learn.microsoft.com/en-us/visualstudio/releases/2022/redistribution). Noto remains under OFL and is loaded privately by the app. Branding, preview artwork and preview sentences are independently authored here. The package manifest records each distributed file, with optional translation weights separated from the core archive.
