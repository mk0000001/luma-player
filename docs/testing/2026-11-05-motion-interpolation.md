# 선택적 프레임 보간 / Optional frame interpolation

## 한국어

### 범위와 표시

기본값은 꺼짐이다. 현재 기능은 libmpv 파일 재생 경로에 한정되며 libVLC의 Blu-ray 메뉴·본편 경로에는 적용하지 않는다. 모드와 수치는 다음 의미로 표시한다.

| 모드 | 설정 | 의미와 한계 |
| --- | --- | --- |
| 꺼짐 | `interpolation=no`, `video-sync=audio`, 관리 필터 제거 | 원래 재생 경로를 사용한다. |
| 디스플레이 동기 혼합 | `interpolation=yes`, `video-sync=display-resample` | mpv의 시간축 혼합으로 주사율 차이로 생기는 떨림을 줄인다. 실제 모니터 주사율에 맞추며 지정한 360 fps로 모니터 주사율을 위장하지 않는다. 광학 흐름이나 AI 프레임 생성이 아니다. |
| 광학 흐름 · CPU 실험 기능 | FFmpeg `minterpolate`의 `mci` | 24~360 fps의 **처리 목표**를 설정한다. CPU에서 움직임을 추정하므로 고해상도에서 실시간 성능을 보장하지 않는다. 실제 화면 표시 속도는 필터 처리·GPU·모니터 주사율에 제한된다. |

UI의 권장 목표 선택지는 60/90/120/144/165/240/360 fps이며 목표 선택은 광학 흐름 모드에만 의미가 있다. 소스 FPS, 필터 출력 FPS 추정값, 실제 디스플레이 주사율, 요청한 목표를 서로 구분한다. `estimated-vf-fps`는 시작 직후나 탐색 직후 흔들릴 수 있다.

### 통합 계약

`src/media/motion_options.h`의 `MotionOptions`와 `motionPlan(options, fileBackend)`는 네이티브 엔진을 호출하지 않는 계획 함수다. 알 수 없는 저장 모드는 꺼짐으로 복구하고 광학 흐름 목표는 24~360으로 제한한다. 지원되지 않는 백엔드에서 보간을 요청하면 `supported=false`, 실제 모드 꺼짐과 이유를 반환한다.

360 fps 계획의 `videoFilter`는 다음 **단일 mpv API 인수**다. `mpv_command`에 전달할 때 추가 셸 따옴표를 붙이지 않는다.

```text
@luma_motion:lavfi=[minterpolate=fps=360:mi_mode=mci:mc_mode=aobmc:me_mode=bidir:vsbmc=1]
```

모드 변경 시 `vf remove @luma_motion`으로 앱이 소유한 필터만 제거하고 기존 사용자 필터를 보존한다. 새 광학 흐름 필터는 `vf add`에 위 인수 그대로 전달한다. 실패 반환값과 후속 필터 오류를 성공으로 표시하지 않는다.

`requiresCopyBack=true`이면 CPU가 읽을 수 있는 프레임이 필요하다. 호출자는 `hwdec=auto-copy` 또는 소프트웨어 디코더를 선택하고 모드 해제 시 사용자 디코더 설정을 복원해야 한다. 이는 광학 흐름 계산의 GPU 가속을 뜻하지 않는다. 디스플레이 혼합을 재생 중 켤 때는 하드웨어 디코더 표면 풀이 부족할 수 있으므로 mpv의 `hwdec-extra-frames`와 디코더 재초기화 요건을 따른다.

현재 FFmpeg `minterpolate`의 허용 픽셀 형식은 8비트 YUV/gray다. 따라서 HDR10/Dolby Vision 정밀도·메타데이터 보존을 약속하지 않는다. HDR 패스스루와 동시 사용은 통합 계층에서 차단하거나 명확한 SDR 변환 선택을 먼저 받아야 한다. 이 계획 함수 자체는 HDR 경로를 검사하지 않는다. RIFE, DLSS, 범용 AI 보간, 인증 출력 또는 Blu-ray 메뉴 지원을 구현했다고 표시하지 않는다.

### 검증 결과: 2026-11-05 Windows

- RED: 최소 구현에서 기대한 8개 테스트가 실패했다. GREEN: 별도 MSVC/Qt 6.8.3 빌드의 `MotionTests`가 13 passed / 0 failed / 0 skipped였다(초기화·정리 포함).
- `LUMA_MOTION_FFMPEG`를 네이티브 full FFmpeg 실행 파일로 지정하면 합성 `testsrc2` 96×64/24 fps 입력을 실제 계획 그래프로 처리한다. 120개 출력의 DTS/PTS가 각각 0~119, duration=1, timebase=1/360임을 검사했다. 상용 영상·음성은 사용하지 않았다.
- 고정 mpv `v0.42.0-1-g3138fc8ee`도 같은 관리 필터와 직접 생성한 FFV1 파일을 `vo=null`, 오디오 없음으로 재생했다. 3초 합성 입력에서 `estimated-vf-fps=360`, 소스 `container-fps=24`를 관측했다. 마지막 짧은 구간에서는 추정값이 약315.8로 바뀌었으므로 요청값을 실제값으로 복사하지 않는다. 이 검사는 화면에 360 Hz로 표시했다는 증거가 아니다.
- 한 번의 1920×1080/24 fps 합성 실험에서 360 fps 목표의 출력 30프레임(약0.083초)을 만드는 데 실제21.265초, 약1.4 출력 fps, 최대 RSS 약667 MiB가 필요했다. 다른 작업도 실행 중이던 세션의 수치이며 일반 벤치마크가 아니다. 실시간 고해상도 재생을 보장할 수 없다는 한계는 확인됐다.
- 실제 360 Hz 디스플레이, HDR 광학 흐름 출력, libVLC 보간은 이 작업에서 검증하지 않았다.

실제 필터 검증을 포함한 테스트 실행:

```powershell
$env:LUMA_MOTION_FFMPEG = (Resolve-Path '.deps/ffmpeg-full/ffmpeg-9.0.2-full_build/bin/ffmpeg.exe').Path
./build-motion/motion_tests.exe -o ./build-motion/green.txt,txt
```

공통 CMake/PlaybackEngine/UI 통합은 주 담당이 소유한다. 위 별도 테스트 실행 파일은 통합 전에 충돌 없이 검증하기 위한 로컬 빌드다.

### 근거

- [mpv 공식 매뉴얼: interpolation](https://mpv.io/manual/master/#options-interpolation), [video-sync](https://mpv.io/manual/master/#options-video-sync), [lavfi](https://mpv.io/manual/master/#video-filters-lavfi)
- [고정 mpv 소스의 옵션 문서](https://github.com/mpv-player/mpv/blob/3138fc8eea/DOCS/man/options.rst), [lavfi 프레임 처리](https://github.com/mpv-player/mpv/blob/3138fc8eea/filters/f_lavfi.c)
- [FFmpeg 공식 minterpolate 문서](https://ffmpeg.org/ffmpeg-filters.html#minterpolate), [필터 구현과 픽셀 형식](https://github.com/FFmpeg/FFmpeg/blob/master/libavfilter/vf_minterpolate.c)

## English

### Scope and meaning

Interpolation defaults to off and applies only to the libmpv file backend. The current libVLC Blu-ray/menu backend has no implemented interpolation path.

- **Off:** `interpolation=no`, `video-sync=audio`; remove the application-managed motion filter.
- **Display-synchronized temporal blend:** `interpolation=yes`, `video-sync=display-resample`. This reduces judder at the real display refresh rate. It does not synthesize a claimed 360 Hz display mode, estimate optical motion, or run an AI frame generator.
- **Experimental CPU optical flow:** FFmpeg `minterpolate` with `mi_mode=mci`, a bounded 24–360 fps processing target, adaptive overlapped motion compensation, bidirectional motion estimation, and variable block sizes. A target is not a real-time or presentation guarantee.

Suggested optical target presets are 60/90/120/144/165/240/360 fps. Keep requested target, source FPS, estimated filtered FPS, and display refresh rate separate. A target selector has no effect on display-synchronized blending.

### Integration

`MotionOptions` and `motionPlan(options, fileBackend)` are engine-independent. Unknown persisted modes recover to off. Unsupported backends return `supported=false`, effective off, and an explanation.

The exact labeled filter argument is shown in the Korean section. Pass it as one raw mpv API argument without shell quotes. Remove only `@luma_motion`, preserving unrelated filters. Check command failures and later filter errors. Optical flow requires CPU-readable frames (`auto-copy` or software decode); restore the configured decoder when leaving the mode. Hardware copy-back does not accelerate optical-flow computation. Follow mpv's decoder-surface requirements when enabling display interpolation during playback.

The current minterpolate implementation accepts 8-bit YUV/gray formats. Do not promise HDR10/Dolby Vision precision or metadata preservation. The integration layer must reject simultaneous HDR passthrough or require an explicit SDR conversion choice; the helper itself does not inspect HDR state. No RIFE/DLSS, universal AI, certified output, or libVLC menu interpolation is claimed.

### Observed Windows validation

The test-first stub produced eight expected failures. The isolated MSVC/Qt 6.8.3 suite then passed all 13 cases, including setup/cleanup, with no skips when `LUMA_MOTION_FFMPEG` was set. The native FFmpeg integration test generated 96×64/24 fps test imagery and verified 120 consecutive output timestamps with unit duration and a 1/360 timebase.

Pinned mpv `v0.42.0-1-g3138fc8ee` accepted the same filter and played a generated FFV1 fixture headlessly (`vo=null`, no audio). A three-second input reported 360 estimated filtered fps against 24 source fps, with an end-of-clip estimate near 315.8. This proves filter operation, not 360 Hz physical presentation.

A single 1080p synthetic run produced 30 interpolated output frames (about 0.083 seconds) in 21.265 seconds, about 1.4 output fps, using approximately 667 MiB peak RSS. Concurrent work was running, so this is not a general performance benchmark. It demonstrates why high-resolution real-time performance remains experimental.

No commercial content was used. Physical 360 Hz display output, HDR optical-flow fidelity, and libVLC interpolation remain unverified. Primary owns common CMake, playback, and UI integration. The sources linked above document the underlying options and filter behavior.
