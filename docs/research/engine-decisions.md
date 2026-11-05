# Engine decisions and primary references

## Why C++20 / Qt 6

The user delegated language choice and prioritized Windows with Linux portability. Qt's native window interfaces and the C interfaces of libVLC/libmpv make C++20 a direct integration path. Rust remains a viable future helper language; this application has no Python runtime.

- [Qt supported platforms](https://doc.qt.io/qt-6/supported-platforms.html).
- [libVLC public media-player API](https://videolan.videolan.me/vlc-3.0/group__libvlc__media__player.html).

## Disc menus versus file rendering

libVLC/libbluray expose disc navigation, overlays, title/chapter state and HDMV/BD-J support. Java support is a runtime requirement for applicable BD-J titles. The current TUKI disc reports two HDMV titles and zero BD-J titles. mpv is used for file playback, AV1 and the libplacebo gpu-next rendering path; choosing a capable decoder alone does not implement the disc menu UI.

- [libbluray feature and protected-disc notes](https://images.videolan.org/developers/libbluray.html).
- [VLC Blu-ray implementation](https://github.com/videolan/vlc/blob/3.0.x/modules/access/bluray.c).
- [mpv reference manual](https://mpv.io/manual/stable/).

## AACS runtime discovery

An observed Windows packaging failure was traced to libbluray's `dl_dlopen`: its flags restrict lookup to the host EXE directory and System32. Adding the VLC directory with AddDllDirectory loads libVLC but does not override this separate loader. The portable staging step therefore places the four AACS/BD+ dependency DLLs beside the host EXE as well as beside the FFmpeg CLI used for local audio acquisition.

- [Windows loader implementation (VideoLAN-derived source mirror)](https://github.com/ShiftMediaProject/libbluray/blob/master/src/file/dl_win32.c).
- [AACS adapter and LIBAACS_PATH](https://github.com/ShiftMediaProject/libbluray/blob/master/src/libbluray/disc/aacs.c).

This is library discovery, not a promise that every protected disc has usable access credentials. AACS and online DRM ecosystems also have their own licensing/certification paths: [AACS licensing](https://aacsla.com/license-aacs/). No commercial player's private DLLs or private credentials are used as application dependencies.

## HDR terminology

mpv documents Dolby Vision metadata processing and tone mapping. Its dynamic HDR metadata mode describes an HDR10 output using per-scene luminance, not full Dolby Vision metadata passthrough. Report source metadata, rendering path and actual output validation separately. [mpv HDR/color controls](https://mpv.io/manual/stable/#options-target-colorspace-hint).

## Local automatic subtitles

whisper.cpp provides native local inference and timed SRT output. Recognize the source language first; Korean translation needs a separate model/adapter. Requests are argument lists, never shell commands. Short audio windows stay local and are removed with their owned temporary directory. Completed work is recorded only after matching successful output; cancellation invalidates the generation and allows retry.

- [whisper.cpp source and build instructions](https://github.com/ggml-org/whisper.cpp/tree/b5454).
- [Whisper source/model licensing](https://github.com/openai/whisper).

Physical-disc observations and fixture latency are recorded in the test report, not inferred from these upstream capabilities.

### Disc-caption source alignment

The physical disc's first-play title rejects title-search commands during its author-defined intro. Caption acquisition therefore uses the FFmpeg full build's libbluray protocol with an explicit MPLS playlist and selected MPEG-TS audio PID. It does not run the interactive menu a second time or assume that the longest playlist is the current title: this disc's commentary playlist is slightly longer than its main concert. Matching the active duration must identify a unique playlist within 200 ms; an ambiguous match fails visibly. [FFmpeg Blu-ray protocol](https://ffmpeg.org/ffmpeg-protocols.html#bluray), [Gyan full-build libraries](https://www.gyan.dev/ffmpeg/builds/).
