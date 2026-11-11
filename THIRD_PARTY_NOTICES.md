# 외부 구성요소·라이선스 / Third-party software and source locations

## 한국어

LumaPlayer 소스는 GPL-3.0-or-later입니다. 동적 라이브러리·모델·폰트에는 각 구성요소의 라이선스가 적용됩니다. 버전·다운로드·SHA-256은 `dependencies.lock.json`과 `translation-dependencies.lock.json`, 패키지 파일별 해시는 `manifest.json`에 기록합니다. 디스크 키·인증서·영상·음성·메뉴 이미지·상용 콘텐츠 인식 자막은 배포하지 않습니다.

| 구성요소 | 버전·출처 | 적용 조건·소스 |
|---|---|---|
| Qt Core/Gui/Widgets/Network | 공식 MSVC 6.11.3 | LGPL-3.0/GPL 대안, 아래 영문 표의 정확한 소스와 `LICENSES` 참조 |
| libVLC·libbluray 플러그인 | VLC 3.0.24 | GPL/LGPL 등 각 구성요소 조건, 원본 COPYING 포함 |
| libmpv·FFmpeg | mpv 3138fc8eea / FFmpeg 9.0.2 full | GPL 빌드, 아래 고정 버전 소스 참조 |
| whisper.cpp·GGML·Whisper base 및 OpenVINO 변환 인코더 | b5454 / 고정 모델 해시 | MIT, [Intel 변환 모델](https://huggingface.co/Intel/whisper.cpp-openvino-models), `LICENSES/Whisper-MIT.txt`·`LICENSES/OpenAI-Whisper-MIT.txt` 유지 |
| Whisper large-v3-turbo Q5 모델 | `ggml-large-v3-turbo-q5_0.bin`, `dependencies.lock.json`의 고정 revision/hash | MIT, [고정 GGML 변환 원본](https://huggingface.co/ggerganov/whisper.cpp/tree/5359861c739e955e79d9a303bcbc70fb988958b1), 원본 OpenAI Whisper MIT 고지 유지 |
| 선택 full Whisper large-v3 Q5 모델 | `accurate` 프로필, `ggml-large-v3-q5_0.bin`, revision `fb5ed9369c26eaf08392504d8446da431e0fffb9`, 1,081,140,203바이트 | MIT, [고정 GGML 파일](https://huggingface.co/ggerganov/whisper.cpp/blob/fb5ed9369c26eaf08392504d8446da431e0fffb9/ggml-large-v3-q5_0.bin), `accurateModel`의 SHA-256과 원문 `LICENSES/OpenAI-Whisper-MIT.txt` 유지; 기본 ZIP 외 선택 다운로드 |
| 선택 Kotoba-Whisper v2.0 Q5 일본어 모델 | Kotoba Technologies / Asahi Ushio, revision `e3a0cf6a62b95911703cfb97d819292e058f12c3` | Apache-2.0, [공식 GGML 모델 카드](https://huggingface.co/kotoba-tech/kotoba-whisper-v2.0-ggml/tree/e3a0cf6a62b95911703cfb97d819292e058f12c3), `LICENSES/Kotoba-Whisper-Apache-2.0.txt` 원문과 고정 해시 유지 |
| OpenVINO·oneTBB·NPU 플러그인/컴파일러 | 공식 2026.4.1.22982 Windows SDK, `dependencies.lock.json`의 고정 URL/SHA-256 | 런타임 바이너리는 Intel OpenVINO Distribution License(Version May 2021)와 `redist.txt`, 헤더·해당 오픈소스 구성요소는 Apache-2.0 및 각 제3자 조건. 배포 시 SDK의 `docs/licensing` 전체와 `TBB-LICENSE` 원문 보존 |
| MeCab·IPADIC | 61b90ba6e669dc2d7d533d4a80d206f3b31d52b1 | MeCab BSD 선택 라이선스, IPADIC 원본 COPYING·AUTHORS 유지; [정확한 소스](https://github.com/taku910/mecab/tree/61b90ba6e669dc2d7d533d4a80d206f3b31d52b1) |
| Noto Sans·Noto Sans CJK KR/JP | 고정된 수정 없는 원본 폰트 | SIL OFL 1.1, 상업적 앱에 포함·임베딩 가능. 폰트 단독 판매 제한과 저작권·라이선스 유지; `LICENSES/NotoSans*-OFL.txt` |
| 로컬 한국어 번역 | llama.cpp b12125 / 선택 Qwen3-4B·Hy-MT2-7B Q4_K_M | 실행기 MIT, 두 모델 Apache-2.0. `LICENSES/translation/Qwen3-4B-Apache-2.0.txt`·`Hy-MT2-7B-Apache-2.0.txt` 원문 유지 |
| 선택 CUDA 음성 인식·번역 런타임 | whisper.cpp b5454 / llama.cpp b12125, CUDA 12.4 | MIT 실행기와 NVIDIA CUDA Toolkit EULA·포함 제3자 고지. 코어와 분리; [CUDA 12.4 EULA 원문](https://docs.nvidia.com/cuda/archive/12.4.0/eula/index.html) 보존 |
| AACS·BD+ 읽기 라이브러리 | 아래 MSYS2 버전 | LGPL-2.1-or-later 등 원본 조건. 키·인증서 제공이나 인증 DRM/HDCP 출력을 뜻하지 않음 |
| Microsoft VC143 런타임 | 설치된 공식 Visual Studio 재배포 파일 | 앱과 네이티브 자식 실행기 옆에 수정 없이 복사. [Microsoft 재배포 목록과 조건](https://learn.microsoft.com/en-us/visualstudio/releases/2022/redistribution) 적용 |

Noto 폰트는 시스템에 설치하지 않고 앱에서 로드합니다. 이름과 폰트 라이선스를 변경하지 않습니다. 로고·아이콘·미리보기 그림과 예문은 이 프로젝트에서 독립적으로 작성했습니다. Dolby 상표·인증, 독점 DRM SDK, 그래픽 드라이버 사용 권리를 이 소스 라이선스로 부여하지 않습니다.

선택 CUDA 설치기는 고정한 공식 실행기·재배포 파일을 앱 폴더에 넣으며 시스템 드라이버를 설치하거나 바꾸지 않습니다. NVIDIA 원문 EULA와 해시를 보존하고, CUDA 파일을 GPL/MIT/Apache로 다시 허가한다고 표시하지 않습니다. EULA 사본은 소스의 `LICENSES/translation/cuda/NVIDIA-CUDA-12.4-EULA.html`, 포터블의 `licenses/LICENSES/translation/cuda/NVIDIA-CUDA-12.4-EULA.html`에 있습니다. 번역 가중치 두 종류도 선택 설치이며 원래 모델 라이선스를 따릅니다.

ASR은 OpenVINO SDK의 헤더·import library·런타임을 같은 2026.4.1 핀으로 사용합니다. 수정 없는 Release DLL과 비디버그 oneTBB DLL만 `runtime/ai`에 복사하며, NPU 플러그인·컴파일러·로더·VM 런타임의 같은 폴더 배치를 유지합니다. 번역의 `runtime/translation/*`와 DLL을 섞지 않습니다. SDK의 재배포 목록은 Windows `runtime/bin/intel64/Release/*`와 `runtime/3rdparty/tbb/bin/*`를 포함합니다. SDK 전체가 Apache 또는 앱의 GPL로 재허가된다는 뜻은 아닙니다.

2026.4.1의 EULA 세 형식·재배포 목록·주요 제3자 고지는 기존 `LICENSES/OpenVINO` 포함본과 SHA-256가 같음을 확인했습니다. 배포 단계는 SDK의 최신 원문 전체를 `licenses/LICENSES/OpenVINO`에 복사하고 별도의 `TBB-LICENSE`도 보존합니다. 이 경로에는 Intel EULA, Apache 원문, runtime·oneDNN·oneTBB·문서 제3자 고지와 `redist.txt`가 포함됩니다. 헤더·import library·샘플·Python·디버그 DLL·컴파일 캐시는 포터블 런타임에 넣지 않습니다. 최종 패키지의 파일·라이선스 목록은 배포 전에 다시 확인합니다.

## English

LumaPlayer is GPL-3.0-or-later. The portable package includes dynamically loaded upstream components; their respective notices and terms continue to apply. Exact Windows artifacts and SHA-256 hashes are in `dependencies.lock.json`. No disc video, audio, menu assets, KEYDB.cfg, private host certificate, API credential or generated commercial-disc transcript is distributed.

| Component | Version / provenance | License and source |
|---|---|---|
| Qt Core/Gui/Widgets/Network | 6.11.3, official MSVC 2022 package | LGPL-3.0 / GPL-3.0 alternatives; [qtbase 6.11.3 source](https://download.qt.io/official_releases/qt/6.11/6.11.3/submodules/qtbase-everywhere-src-6.11.3.tar.xz), [Qt licensing](https://www.qt.io/licensing/open-source-lgpl-obligations) |
| VLC / libVLC / libbluray plugins | VLC 3.0.24 official Windows archive | VLC GPL-2.0-or-later and library/plugin component licenses; [VLC source archive](https://download.videolan.org/pub/videolan/vlc/3.0.24/vlc-3.0.24.tar.xz), [source and contrib build recipes](https://code.videolan.org/videolan/vlc/-/tree/3.0.24), COPYING in runtime/vlc |
| libmpv and its linked dependencies | mpv 3138fc8eea; shinchiro 20261105 build | GPL-2.0-or-later build; [mpv exact source](https://github.com/mpv-player/mpv/tree/3138fc8eea), [Windows dependency build recipes](https://github.com/shinchiro/mpv-winbuild-cmake/tree/20261105) |
| FFmpeg full and linked libraries | Gyan FFmpeg 9.0.2 full | GPL-3.0-or-later build; [FFmpeg 9.0.2 source](https://ffmpeg.org/releases/ffmpeg-9.0.2.tar.xz), [Gyan build provenance](https://www.gyan.dev/ffmpeg/builds/), COPYING in runtime/tools |
| whisper.cpp / ggml / CPU and Vulkan backends | upstream b5454 plus the native adapter in `native/ai_worker` | MIT; [exact source](https://github.com/ggml-org/whisper.cpp/tree/b5454), included LICENSES/Whisper-MIT.txt |
| Whisper multilingual base model | ggml-base.bin, SHA-256 60ed5bc3dd14eea856493d334349b405782ddcaf0028d4b5df4088345fba2efe | [OpenAI Whisper MIT license and model provenance](https://github.com/openai/whisper), [GGML conversion/model repository](https://huggingface.co/ggerganov/whisper.cpp) |
| Whisper large-v3-turbo Q5 model | ggml-large-v3-turbo-q5_0.bin, revision 5359861c739e955e79d9a303bcbc70fb988958b1 and hash pinned in `dependencies.lock.json` | MIT; [pinned GGML conversion](https://huggingface.co/ggerganov/whisper.cpp/tree/5359861c739e955e79d9a303bcbc70fb988958b1), raw `LICENSES/OpenAI-Whisper-MIT.txt` preserved |
| Optional full Whisper large-v3 Q5 model | `accurate` profile, `ggml-large-v3-q5_0.bin`, revision `fb5ed9369c26eaf08392504d8446da431e0fffb9`, 1,081,140,203 bytes | MIT; [immutable GGML artifact](https://huggingface.co/ggerganov/whisper.cpp/blob/fb5ed9369c26eaf08392504d8446da431e0fffb9/ggml-large-v3-q5_0.bin), preserve the `accurateModel` SHA-256 and original `LICENSES/OpenAI-Whisper-MIT.txt`; optional download outside the default ZIP |
| Optional Kotoba-Whisper v2.0 Q5 Japanese model | Kotoba Technologies / Asahi Ushio, revision `e3a0cf6a62b95911703cfb97d819292e058f12c3` | Apache-2.0; [official GGML card](https://huggingface.co/kotoba-tech/kotoba-whisper-v2.0-ggml/tree/e3a0cf6a62b95911703cfb97d819292e058f12c3), original `LICENSES/Kotoba-Whisper-Apache-2.0.txt` and hash retained |
| OpenVINO encoder model | Intel conversion pinned in `dependencies.lock.json` | MIT; [official model card](https://huggingface.co/Intel/whisper.cpp-openvino-models) |
| OpenVINO, oneTBB and NPU plugin/compiler | official 2026.4.1.22982 Windows SDK; exact URL/SHA-256 in `dependencies.lock.json` | Runtime binaries: Intel OpenVINO Distribution License (Version May 2021) and `redist.txt`. Headers/applicable open-source components: Apache-2.0 and component-specific terms. Preserve all SDK `docs/licensing` originals and `TBB-LICENSE` when distributing |
| MeCab and IPADIC | source commit 61b90ba6e669dc2d7d533d4a80d206f3b31d52b1, native MSVC build | MeCab BSD option; IPADIC COPYING/AUTHORS preserved; [source](https://github.com/taku910/mecab/tree/61b90ba6e669dc2d7d533d4a80d206f3b31d52b1) |
| Noto Sans, Noto Sans CJK KR/JP | unmodified files pinned in `dependencies.lock.json` | SIL OFL 1.1 permits commercial application bundling/embedding with its conditions; `LICENSES/NotoSans-OFL.txt` and `NotoSansCJK-OFL.txt` |
| llama.cpp and optional Qwen3-4B / Hy-MT2-7B models | b12125 / official Q4_K_M files, pinned translation lock | Runtime MIT; both models Apache-2.0. Preserve raw `LICENSES/translation/Qwen3-4B-Apache-2.0.txt` and `Hy-MT2-7B-Apache-2.0.txt` |
| Optional ASR / translation CUDA runtimes | whisper.cpp b5454 / llama.cpp b12125, CUDA 12.4 | MIT executables plus NVIDIA CUDA Toolkit EULA and bundled third-party notices; preserve the [original CUDA 12.4 EULA](https://docs.nvidia.com/cuda/archive/12.4.0/eula/index.html) |
| libaacs | MSYS2 mingw64 0.12.0-1 | LGPL-2.1-or-later; [VideoLAN source](https://code.videolan.org/videolan/libaacs), [MSYS2 recipe](https://github.com/msys2/MINGW-packages/tree/master/mingw-w64-libaacs) |
| libbdplus | MSYS2 mingw64 0.2.0-2 | LGPL-2.1-or-later; [VideoLAN source](https://code.videolan.org/videolan/libbdplus), [MSYS2 recipe](https://github.com/msys2/MINGW-packages/tree/master/mingw-w64-libbdplus) |
| libgcrypt | MSYS2 mingw64 1.12.4-2 | LGPL-2.1-or-later library; [source](https://gnupg.org/ftp/gcrypt/libgcrypt/), [MSYS2 recipe](https://github.com/msys2/MINGW-packages/tree/master/mingw-w64-libgcrypt) |
| libgpg-error | MSYS2 mingw64 1.61-1 | LGPL-2.1-or-later library; [source](https://gnupg.org/ftp/gcrypt/libgpg-error/), [MSYS2 recipe](https://github.com/msys2/MINGW-packages/tree/master/mingw-w64-libgpg-error) |

Windows packages may also include Microsoft runtime/D3D redistributables deployed by Qt; these are governed by their accompanying redistribution terms and are not application source. The DirectX Shader Compiler upstream license is included in `LICENSES/DirectXShaderCompiler.txt`; [its source and notices](https://github.com/microsoft/DirectXShaderCompiler). Upstream archives retain further dependency notices. See `LICENSES` and each runtime's supplied notices; source references describe the bundled build, not a license for unrelated proprietary DRM SDKs or Dolby trademarks/certification.

VC143 redistributable DLLs are copied unchanged beside native child executables as well as the application; see [Microsoft's redistribution list](https://learn.microsoft.com/en-us/visualstudio/releases/2022/redistribution). Noto remains under OFL and is loaded privately by the app. Branding, preview artwork and preview sentences are independently authored here. The package manifest records each distributed file, with optional translation weights separated from the core archive.

Optional CUDA installers place pinned official executables/redistributables inside the app folder, preserving the original NVIDIA EULA and its hash. They do not install or modify system drivers. CUDA files are not relicensed as GPL, MIT or Apache. The source copy is `LICENSES/translation/cuda/NVIDIA-CUDA-12.4-EULA.html`; the portable copy is `licenses/LICENSES/translation/cuda/NVIDIA-CUDA-12.4-EULA.html`. Both optional translation weight profiles retain their original model terms.

ASR uses headers, import libraries and runtime from the same pinned OpenVINO 2026.4.1 SDK. Unmodified Release DLLs and non-debug oneTBB DLLs are copied to `runtime/ai`, retaining the flat NPU plugin/compiler/loader/VM layout. Translation DLLs remain isolated under `runtime/translation/*`. The vendor redistributable list includes Windows `runtime/bin/intel64/Release/*` and `runtime/3rdparty/tbb/bin/*`. This does not relicense the entire SDK as Apache or the application's GPL.

The 2026.4.1 EULA's three formats, redistributable list and principal third-party notices were SHA-256-identical to the existing `LICENSES/OpenVINO` copies. Staging copies the pinned SDK's complete current originals into `licenses/LICENSES/OpenVINO` and preserves the separate `TBB-LICENSE`: Intel EULA, Apache license, runtime/oneDNN/oneTBB/documentation third-party notices and `redist.txt`. Headers, import libraries, samples, Python, debug DLLs and compiled caches are not portable-runtime payload. The final package file/license inventory is reviewed before release.
