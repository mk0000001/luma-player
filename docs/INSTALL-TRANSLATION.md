# 선택 번역 모델·CUDA 설치 / Optional translation models and CUDA

## 한국어

포터블 코어에는 번역 실행기와 Whisper large-v3-turbo Q5 음성 인식 모델이 들어갑니다. **번역 가중치와 CUDA는 별도 선택 설치**입니다. 번역 모델 없이도 영상·음성 재생, 원문 자막과 일본어 한글 독음을 사용할 수 있습니다. 설치할 때 인터넷을 쓰며, 완료 후 인식·번역은 로컬에서 실행합니다. 외부 API 계정·키·Python은 필요하지 않습니다.

### 번역 모델 선택

| 프로필 | 파일 | 다운로드 크기 |
| --- | --- | --- |
| `fast` (설치 기본값) | Qwen3-4B-Q4_K_M.gguf | 2,497,280,256바이트 · 약 2.50 GB / 2.33 GiB |
| `quality` (선택) | Hy-MT2-7B-Q4_K_M.gguf | 4,624,648,896바이트 · 약 4.624 GB / 4.31 GiB |

두 모델 모두 Apache 2.0이며 파일 크기·SHA-256·공식 배포 revision을 [번역 잠금 파일](../translation-dependencies.lock.json)에 고정합니다. 프로필 이름은 선택 기준이며 모든 문장의 정확도나 모든 장치의 속도를 보장하지 않습니다. 설치하려는 파일 크기보다 여유 공간을 더 확보하세요. 두 모델을 함께 두려면 합계 약 7.12 GB에 설치 여유 공간이 추가로 필요합니다.

압축을 푼 플레이어 폴더에서 앱을 종료한 뒤 실행하세요. 빠른 프로필:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ".\tools\install-translation.ps1"
```

품질 프로필:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ".\tools\install-translation.ps1" -Profile quality
```

다른 위치에서 실행한다면 플레이어 폴더를 지정할 수 있습니다.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "C:\Downloads\LumaPlayer\tools\install-translation.ps1" -PlayerDirectory "D:\Apps\LumaPlayer" -Profile quality
```

실행 정책 옵션은 해당 PowerShell 프로세스에만 적용됩니다. 기본 설치 위치는 스크립트가 있는 `tools` 폴더의 부모이며, 개발 저장소의 `scripts`에서 실행하면 저장소 폴더입니다. 모델은 `runtime/translation/models`에 저장합니다. 설치기는 UI에 저장한 모델 선택을 바꾸지 않습니다.

설치한 **quality 모델은 명시한 번역 모델 설정이 없을 때만 다음 앱 시작의 기본값**입니다. 앱이 열려 있었다면 재시작하세요. 이전 모델을 직접 선택한 적이 있다면 **AI 자막 → 번역 모델 선택**에서 `Hy-MT2-7B-Q4_K_M.gguf`를 직접 선택하세요. fast로 돌아갈 때도 같은 곳에서 Qwen 파일을 선택할 수 있습니다. 음성 인식 모델 선택과 번역 모델 선택은 서로 독립적입니다.

### 선택 NVIDIA CUDA 실행기

코어 ZIP은 CUDA를 포함하지 않습니다. 호환 NVIDIA 드라이버가 있는 **Windows x64**에서 필요할 때만 다음을 설치하세요. 번역 CUDA:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ".\tools\install-translation-cuda.ps1"
```

음성 인식 CUDA:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ".\tools\install-asr-cuda.ps1"
```

두 명령 모두 `-PlayerDirectory "D:\Apps\LumaPlayer"`를 지원합니다. 번역 실행기는 `runtime/translation/cuda`, 음성 인식 플러그인은 `runtime/ai`에 들어갑니다. 공식 고정 CUDA 12.4 배포물의 크기와 해시를 검사하고, [NVIDIA EULA 원문](https://docs.nvidia.com/cuda/archive/12.4.0/eula/index.html)과 포함 고지를 보존합니다. 포터블의 원문은 `licenses/LICENSES/translation/cuda/NVIDIA-CUDA-12.4-EULA.html`에도 있습니다. 시스템 드라이버·PATH·SDK를 설치하거나 변경하지 않습니다. 이 추가 파일과 설치 임시 파일을 위한 저장 공간도 필요합니다.

설치 후 앱을 다시 열고 **음성 인식 가속/AI GPU** 또는 **번역 가속/번역 GPU**에서 해당 GPU를 선택하세요. 모델과 장치에 따라 지원 경로가 다르므로 화면에 표시한 **실제 백엔드**를 확인하세요. 설치 성공과 가속 실행 성공은 구분합니다. CUDA를 설치하지 않은 코어의 Vulkan/CPU 경로와 별도 ASR OpenVINO 런타임도 유지합니다. Turbo 가중치가 있다고 모든 NPU 경로가 지원되는 것은 아닙니다.

### 검증과 실패 시 동작

설치기는 플레이어 또는 `licenses` 폴더의 잠금 파일을 읽고 지정된 공식 배포물만 받습니다. 기존 파일의 크기와 SHA-256이 일치하면 다시 받지 않습니다. 다운로드가 끊기거나 검증이 실패하면 기존 모델을 교체하지 않습니다. 다시 실행하면 완성된 임시 파일을 검증해 재사용하거나 불완전한 다운로드를 버리고 다시 받습니다. 파일 사용 중 또는 공간 부족 오류가 나면 앱을 닫고 공간을 확보한 뒤 다시 실행하세요.

번역이 실패해도 원문·독음과 AI 선택을 유지합니다. 잘린/유효하지 않은 한 구간은 건너뛰고 다음 구간을 계속 처리할 수 있으며, 모델 누락이나 실행기 오류는 별도로 표시합니다. [자막·재생 안내](CAPTIONS-AND-PLAYBACK.md)와 [alpha.3 검증 기록](releases/0.1.0-alpha.3.md)을 참고하세요. 개발 PC에서 확인한 CUDA 실행이나 소유 예문 결과를 모든 장치의 호환성·완전한 번역·최종 CI/패키지 통과로 확대하지 않습니다.

## English

The portable core includes the translation runtime and Whisper large-v3-turbo Q5 ASR model. **Translation weights and CUDA are optional additions.** Playback, source captions and Japanese Hangul readings work without translation weights. Installation uses the internet; subsequent recognition/translation runs locally, without an external API account/key or Python.

### Choose a translation model

| Profile | File | Download size |
| --- | --- | --- |
| `fast` (installer default) | Qwen3-4B-Q4_K_M.gguf | 2,497,280,256 bytes · about 2.50 GB / 2.33 GiB |
| `quality` (optional) | Hy-MT2-7B-Q4_K_M.gguf | 4,624,648,896 bytes · about 4.624 GB / 4.31 GiB |

Both models are Apache 2.0. Exact official revisions, sizes and SHA-256 hashes are pinned in the [translation lock](../translation-dependencies.lock.json). Profile names do not guarantee every sentence's accuracy or every device's speed. Allow installation headroom beyond the model size; keeping both needs about 7.12 GB plus headroom.

Close the app and run from its extracted folder:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ".\tools\install-translation.ps1"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ".\tools\install-translation.ps1" -Profile quality
```

Choose the command for the desired profile; both are not required. When running elsewhere, add `-PlayerDirectory "D:\Apps\LumaPlayer"`. The execution-policy option applies only to this PowerShell process. The default destination is the parent of the script's `tools` folder, or the repository root when invoked from `scripts`. Models go in `runtime/translation/models`; installation does not change a saved UI model choice.

An installed **quality model becomes the next startup default only without an explicit saved translation-model selection**. Restart if the app was open. With a previous explicit selection, use **AI 자막 → 번역 모델 선택** to choose `Hy-MT2-7B-Q4_K_M.gguf`. Choose the Qwen file there to return to fast. ASR and translation model selections are independent.

### Optional NVIDIA CUDA runtimes

Core ZIPs omit CUDA. On **Windows x64 with a compatible NVIDIA driver**, install either optional component as needed:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ".\tools\install-translation-cuda.ps1"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ".\tools\install-asr-cuda.ps1"
```

Both support `-PlayerDirectory "D:\Apps\LumaPlayer"`. Translation installs into `runtime/translation/cuda`; the ASR plugin installs into `runtime/ai`. The installers verify pinned official CUDA 12.4 artifacts and retain the [original NVIDIA EULA](https://docs.nvidia.com/cuda/archive/12.4.0/eula/index.html) and third-party notices. A portable copy is at `licenses/LICENSES/translation/cuda/NVIDIA-CUDA-12.4-EULA.html`. They do not install or change system drivers, PATH or SDKs. Allow additional space for the runtimes and temporary installation files.

Restart and select the GPU through the independent ASR or translation acceleration/GPU controls. Check the **actual backend** displayed: installing a runtime does not establish accelerated execution. Model/device support varies. Vulkan/CPU alternatives and the separate ASR OpenVINO runtime remain available. Bundling Turbo does not imply every NPU path supports that model.

### Verification and recovery

Installers read the lock from the player or `licenses` folder and download only its designated official artifacts. A valid existing size/hash skips downloading. Interrupted downloads or failed checks preserve existing models; rerunning verifies a complete temporary file or discards an incomplete download before retrying. Close the app and provide enough space if replacement or disk-space errors occur.

Translation failures preserve source/readings and the AI preference. A truncated/invalid cue can be skipped while later cues continue; missing models/runtime failures are reported separately. See the [caption/playback guide](CAPTIONS-AND-PLAYBACK.md) and [alpha.3 record](releases/0.1.0-alpha.3.md). Development-PC CUDA runs and owned examples do not establish all-device compatibility, complete translation or final CI/package success.
