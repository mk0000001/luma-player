# 재생 전 전체 자막 만들기 / Prepare full captions before playback

## 한국어

**AI 자막 → 재생 전 전체 자막 만들기**에서 로컬 파일이나 연결된 Blu-ray를 선택합니다. 영상을 먼저 열 필요가 없습니다. alpha.10은 현재 재생하던 디스크의 확인된 타이틀·오디오를 우선 선택합니다. 정보가 없거나 모호하면 가장 긴 타이틀·첫 오디오를 선택합니다. 가장 긴 타이틀이 코멘터리 등 부가영상일 수도 있으므로 메뉴 항목과 대조해 확인하세요. 타이틀과 오디오 순서는 직접 바꿀 수 있습니다. 파일은 재생 없이 길이와 오디오 목록을 확인합니다.

1. AI 자막 설정에서 음성 언어, 인식 모델·장치와 번역 모델·장치를 선택합니다. 한국어 해석에는 별도 번역 모델이 필요합니다.
2. 사전 생성 창에서 미디어, 타이틀·오디오, 저장 폴더를 확인합니다. 한국어 해석과 일본어 한글 독음 저장은 선택 사항입니다.
3. **전체 자막 만들기**를 누릅니다. 재생을 멈추고 선택한 타이틀 끝까지 원문을 먼저 인식한 다음, 준비된 원문 텍스트로 한국어 해석을 만듭니다. 번역에 음성을 직접 보내지 않습니다. 각 번역은 최대 앞뒤 60초의 원문 중 입력 크기에 맞는 문맥을 참고합니다.
4. 완료 후 **준비한 자막으로 재생**을 누릅니다. 다음 실행에서는 **미리 만든 자막 프로젝트 열기**로 `luma-caption-project.json`을 엽니다. 완성 자막을 사용할 때는 탐색 후 실시간 AI의 최소 10초 준비를 다시 기다리지 않습니다. 디스크 읽기·영상 디코딩 자체에는 시간이 걸릴 수 있습니다.

원문 `original.srt`, 한국어 `korean.srt`, 선택한 원문·독음·한국어 조합 `display.srt`, 원문 토큰 시각과 번역을 보존하는 프로젝트 JSON을 저장합니다. 원본 미디어와 프로젝트·SRT 파일을 유지하세요. 프로젝트는 원래 저장 폴더의 내보내기 파일을 확인합니다. 완성 프로젝트를 재생할 때는 생성에 쓴 모델을 다시 실행하지 않습니다.

alpha.9에서 준비한 Blu-ray는 메뉴를 거치지 않고 해당 길이의 실제 native 타이틀을 선택합니다. 인덱스와 길이가 확인될 때까지 일시정지하며, 200ms 안에서 유일하게 맞는 타이틀을 찾지 못하면 오류를 표시합니다. 같은 번호라도 메뉴 모드와 직접 모드의 타이틀은 다를 수 있습니다. **이어서 보기**는 이 모드와 완성 프로젝트를 함께 저장합니다. 메뉴를 이용하려면 일반 **Blu-ray 재생**을 누르세요. alpha.8은 가장 긴 준비 타이틀과 ALL PLAY가 다르면 완성 자막을 연결하지 못하는 알려진 문제가 있습니다.

취소하면 완료한 구간을 저장합니다. 같은 미디어·타이틀·오디오·모델·옵션·실행기와 저장 폴더로 다시 시작하면 완료 구간을 재사용합니다. 미디어나 생성 조건이 달라지면 이전 결과를 섞지 않고 오류를 표시합니다. 새 조건은 새 폴더에서 생성하세요. 폴더는 한 작업만 사용할 수 있습니다.

가속 장치가 거부되면 알려진 대체 사유를 표시합니다. **가속 다시 시도**는 현재 완료 구간을 저장하고 같은 요청을 새 작업자로 재개합니다. 취소한 상태의 장치명은 마지막 사용 장치이며 실행 중이라는 뜻이 아닙니다. 재시도해도 같은 하드웨어·드라이버·모델 문제가 계속되면 다시 CPU로 대체될 수 있습니다. 실물 장기 생성에서 최초 GPU 거부 원인은 아직 확정하지 못했습니다.

Blu-ray 사전 생성은 재생용 읽기 속도 제한을 적용하지 않으며, 선택한 타이틀의 임시 PCM을 로컬에 저장할 수 있습니다(16 kHz 모노 16비트, 시간당 약 115 MB). 감상 중 실시간 자막의 읽기 제한과 10초/60초 버퍼 정책은 유지합니다. 생성 시간은 모델·장치·음원에 따라 달라집니다. 긴 노래, 배경음, 인식 오류나 정렬 없는 구간에서 정확도·싱크·어투의 한계는 남습니다. 잘못된 번역 구간은 건너뛰고 개수를 표시할 수 있습니다.

입력은 최대 24시간이며 프로젝트 크기와 자막·토큰 수에도 제한이 있습니다. 미디어 파일은 경로·크기·수정 시각, 디스크는 인덱스·플레이리스트와 선택 스트림 정보로 변경을 확인합니다. 전체 영상의 암호학적 무결성 검사는 아닙니다. 원본 디스크가 필요하며 이 기능은 새로운 DRM 인증이나 보호 우회를 추가하지 않습니다. 보호 디스크에서 생성한 상용 자막은 로컬에만 보관하고 저장소·릴리스에 포함하지 않습니다.

명령행에서도 완성 프로젝트를 열 수 있습니다.

```powershell
.\LumaPlayer.exe --prepared-project "D:\MyCaptions\luma-caption-project.json"
```

## English

Open **AI 자막 → 재생 전 전체 자막 만들기** and choose a local file or connected Blu-ray without first playing it. Alpha.10 prefers the verified playing disc title/audio. Without usable unambiguous state it selects the longest title/first audio. Match this against the disc menu: commentary or bonus material can be longer than the main feature. You can choose another title/audio ordinal. File duration and audio tracks are inspected without rendering playback.

Choose language, ASR model/device and translation model/device in the AI settings. Confirm the media, title/audio, output folder and optional Korean/Hangul-reading exports, then select **전체 자막 만들기**. Playback stops. Recognition covers the entire selected title before translation begins. Translation receives recognized source text, with bounded original context from up to 60 seconds on either side; it never receives audio directly.

Select **준비한 자막으로 재생** when complete, or later open `luma-caption-project.json` with **미리 만든 자막 프로젝트 열기** or `--prepared-project`. Seeking with completed captions does not wait for the live ten-second ASR buffer. Disc access and video decoding can still take time.

Exports are `original.srt`, `korean.srt`, the selected source/Hangul/Korean combination in `display.srt`, and a project retaining original token times and translations. Keep the source media and project/SRT exports in their recorded locations. Playing a completed project does not rerun its models.

In alpha.9, prepared discs bypass menus and select the actual native title by a unique duration match within 200 ms, remaining paused until index/duration acknowledgment. Failure or ambiguity produces an error. Native indexes can differ between menu and direct modes; resume retains the mode and completed project. Use ordinary **Blu-ray 재생** for menus. Alpha.8 has a known attachment problem when its longest prepared title differs from ALL PLAY.

Canceling checkpoints completed work. Restart with the same source/title/audio/models/options/runtime and folder to reuse it. Changed inputs are rejected instead of mixing results; use a new folder for new conditions. Only one writer may use an output folder.

Known controlled accelerator rejections are displayed. **가속 다시 시도** checkpoints work and resumes the identical request with a fresh worker. A canceled display names the last-used device, not ongoing execution. Retry may still fall back if the hardware/driver/model issue persists. The initial GPU rejection during the observed long physical-disc run remains unconfirmed.

Offline Blu-ray reads omit the live reader's rate limit and may store temporary PCM locally (16 kHz mono 16-bit, approximately 115 MB per hour). Live reading limits and ten/sixty-second policy remain unchanged. Runtime depends on hardware, models and audio. Singing, background music and recognition/alignment failures can still affect meaning, tone and synchronization; invalid translations can be skipped and counted.

Limits include 24-hour media, project size and cue/token counts. Source identity uses file path/size/mtime or bounded disc index/playlist/selected-stream metadata, not cryptographic verification of the full video. The original disc remains required. This feature adds no DRM certification or protection bypass. Commercial-disc transcripts stay local and are excluded from repository/release assets.
