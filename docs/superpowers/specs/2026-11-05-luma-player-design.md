# LumaPlayer design

## Intent and acceptance

Build a clean open source desktop Blu-ray/media player, Windows first and Linux next. The user's primary acceptance criterion is the physical `TUKI_NIPPON_BUDOKAN_BD` currently mounted in the Pioneer USB Blu-ray drive, including disc menu interaction, picture, audio, title/chapter selection and seeking. Disc detection alone is not success. AI automatic subtitles are also required.

The user delegated language and implementation decisions and explicitly requested autonomous execution on 2026-11-05. The choices below are implementation decisions, not user-specified library choices.

## Constraints

- C++20; no Python application or Python runtime.
- Qt 6 Widgets; CMake; Windows x86-64 initial package, Linux architecture/CI from the start.
- GPL-3.0-or-later application. Preserve upstream license notices and source provenance.
- GitHub: `mk0000001/luma-player`. Commit source, design, research, test reports and changes. Use SemVer annotated tags and a GitHub release for every distributed version, including prereleases. Never rewrite released tags.
- Do not upload the commercial disc's video, audio, menu graphics, decryption keys, credentials, private paths, or generated transcripts to GitHub.
- Never advertise universal DRM, certified Dolby Vision output, PotPlayer parity or physical-disc success without evidence.

## Approaches considered

1. Rust UI with native media FFI: portable, but adds glue and more work to Windows video embedding, BD menu input and audio callbacks.
2. C++20/Qt with native media backends (selected by implementer): mature Windows/Linux integration, direct media-library interfaces, simplest path to menu interaction and local AI.
3. Browser wrapper: attractive UI, but browser media/DRM capabilities are not a general Blu-ray playback engine.

## Structure

- `core`: bounded disc metadata inspection and subtitle timeline/generation logic, tested without physical media.
- `media`: libVLC for Blu-ray HDMV/BD-J menus, libmpv for file playback and AV1/HDR processing. Load media engines from a known application runtime directory, then documented system paths on Linux. Do not depend on another player's private DLLs.
- `ui`: native dark window, library/disc sidebar, large video surface, transport bar, chapters, tracks, disc remote, playback diagnostics and AI controls. Keyboard and mouse both work. Preserve aspect ratio; fullscreen and drag/drop.
- `subtitles`: asynchronous local whisper.cpp process, configurable model and language, speech to timed SRT, cancellation and seek-generation invalidation. Korean translation is a separate adapter/milestone, since Whisper translation targets English, not Korean. No cloud API or account required for local inference.

## Protection and HDR

The inserted disc contains AACS files; its `BDJO` and `JAR` directories are empty, consistent with HDMV rather than BD-J navigation. Establish exact runtime behavior experimentally. Open source AACS/BD+ integration depends on available runtime capability/configuration; merely installing libbluray does not make every protected disc playable. Never use a commercial player's private components as redistributable dependencies. If required credentials or licensing are unavailable, show the precise cause and retain the physical-disc acceptance test as failed/pending. Opening PowerDVD is only a baseline diagnostic and never counts as LumaPlayer playback.

Dolby Vision metadata processing/tone mapping, HDR10 output and certified Dolby Vision passthrough are different capabilities. Start with mpv's reported capabilities; do not label the last one supported without a licensed validated path. GPU and display limits remain visible in diagnostics.

## Version milestones

- `0.1.0-alpha.1`: native Windows foundation; disc inspection, libVLC menu/remote controls, file engine, core tests, diagnostic logs and physical-disc test report.
- `0.2`: local automatic transcription and subtitle timeline; local speech fixture and cancellation/seek tests; audio acquisition must be verified for each backend.
- `0.3`: Korean translation adapter, GPU AI acceleration, track/playlist/subtitle improvements.
- `0.4`: AV1/HDR profile matrix and representative fixtures; Linux packaging and native display testing.
- `1.0`: only after disc, playback, accessibility, stability and packaging gates pass. PotPlayer comparison remains an explicit feature checklist, not a claim that alpha has every feature.

## First release validation

Build and test using the real Windows compiler. Verify native window visually. Decode an AV1 fixture and exercise pause/seek/volume. Inspect and attempt the physical disc, test menu navigation if playback starts, and record the exact failure otherwise. Run CI for Windows and Linux. Archive source, checksums, portable package and release notes on GitHub. Report unfinished primary acceptance plainly.
