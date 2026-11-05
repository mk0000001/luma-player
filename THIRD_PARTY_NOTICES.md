# Third-party software and corresponding source locations

LumaPlayer is GPL-3.0-or-later. The portable package includes dynamically loaded upstream components; their respective notices and terms continue to apply. Exact Windows artifacts and SHA-256 hashes are in `dependencies.lock.json`. No disc video, audio, menu assets, KEYDB.cfg, private host certificate, API credential or generated commercial-disc transcript is distributed.

| Component | Version / provenance | License and source |
|---|---|---|
| Qt Core/Gui/Widgets | 6.8.3, official MSVC 2022 package | LGPL-3.0 / GPL-3.0 alternatives; [qtbase 6.8.3 source](https://download.qt.io/official_releases/qt/6.8/6.8.3/submodules/qtbase-everywhere-src-6.8.3.tar.xz), [Qt licensing](https://www.qt.io/licensing/open-source-lgpl-obligations) |
| VLC / libVLC / libbluray plugins | VLC 3.0.24 official Windows archive | VLC GPL-2.0-or-later and library/plugin component licenses; [VLC source archive](https://download.videolan.org/pub/videolan/vlc/3.0.24/vlc-3.0.24.tar.xz), [source and contrib build recipes](https://code.videolan.org/videolan/vlc/-/tree/3.0.24), COPYING in runtime/vlc |
| libmpv and its linked dependencies | mpv 3138fc8eea; shinchiro 20261105 build | GPL-2.0-or-later build; [mpv exact source](https://github.com/mpv-player/mpv/tree/3138fc8eea), [Windows dependency build recipes](https://github.com/shinchiro/mpv-winbuild-cmake/tree/20261105) |
| FFmpeg full and linked libraries | Gyan FFmpeg 9.0.2 full | GPL-3.0-or-later build; [FFmpeg 9.0.2 source](https://ffmpeg.org/releases/ffmpeg-9.0.2.tar.xz), [Gyan build provenance](https://www.gyan.dev/ffmpeg/builds/), COPYING in runtime/tools |
| whisper.cpp / ggml / CPU backends | upstream b5454 | MIT; [exact source](https://github.com/ggml-org/whisper.cpp/tree/b5454), included LICENSES/Whisper-MIT.txt |
| Whisper multilingual base model | ggml-base.bin, SHA-256 60ed5bc3dd14eea856493d334349b405782ddcaf0028d4b5df4088345fba2efe | [OpenAI Whisper MIT license and model provenance](https://github.com/openai/whisper), [GGML conversion/model repository](https://huggingface.co/ggerganov/whisper.cpp) |
| libaacs | MSYS2 mingw64 0.12.0-1 | LGPL-2.1-or-later; [VideoLAN source](https://code.videolan.org/videolan/libaacs), [MSYS2 recipe](https://github.com/msys2/MINGW-packages/tree/master/mingw-w64-libaacs) |
| libbdplus | MSYS2 mingw64 0.2.0-2 | LGPL-2.1-or-later; [VideoLAN source](https://code.videolan.org/videolan/libbdplus), [MSYS2 recipe](https://github.com/msys2/MINGW-packages/tree/master/mingw-w64-libbdplus) |
| libgcrypt | MSYS2 mingw64 1.12.4-2 | LGPL-2.1-or-later library; [source](https://gnupg.org/ftp/gcrypt/libgcrypt/), [MSYS2 recipe](https://github.com/msys2/MINGW-packages/tree/master/mingw-w64-libgcrypt) |
| libgpg-error | MSYS2 mingw64 1.61-1 | LGPL-2.1-or-later library; [source](https://gnupg.org/ftp/gcrypt/libgpg-error/), [MSYS2 recipe](https://github.com/msys2/MINGW-packages/tree/master/mingw-w64-libgpg-error) |

Windows packages may also include Microsoft runtime/D3D redistributables deployed by Qt; these are governed by their accompanying redistribution terms and are not application source. The DirectX Shader Compiler upstream license is included in `LICENSES/DirectXShaderCompiler.txt`; [its source and notices](https://github.com/microsoft/DirectXShaderCompiler). Upstream archives retain further dependency notices. See `LICENSES` and each runtime's supplied notices; source references describe the bundled build, not a license for unrelated proprietary DRM SDKs or Dolby trademarks/certification.
