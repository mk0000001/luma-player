# 선택 번역 모델·CUDA 설치 / Optional translation models and CUDA

## 한국어

포터블에는 번역 실행기와 Whisper large-v3-turbo Q5 음성 인식 모델이 들어갑니다. `windows-x64` 코어는 CUDA를 생략하고, `windows-x64-cuda`는 음성 인식·번역 CUDA 실행기를 함께 포함합니다. **번역 가중치는 두 구성 모두 별도 선택 설치**입니다. 번역 모델 없이도 영상·음성 재생, 원문 자막과 일본어 한글 독음을 사용할 수 있습니다. 설치할 때 인터넷을 쓰며, 완료 후 인식·번역은 로컬에서 실행합니다. 외부 API 계정·키·Python은 필요하지 않습니다.

Windows 패키지의 첫 일반 실행이나 설정의 **AI 가속 설정**에서 GPU와 사용 가능한 NPU를 확인하고 라이브러리를 설치할 수 있습니다. NVIDIA에는 CUDA, AMD Radeon과 Intel GPU에는 Vulkan, Intel GPU와 NPU에는 OpenVINO를 권장합니다. 권장 여부와 관계없이 설치할 수 있습니다. Vulkan·OpenVINO 음성 인식 런타임은 패키지에 포함되며, 이 두 항목의 설치 버튼은 번역 런타임을 추가하거나 복구합니다. 설치 후 LumaPlayer를 다시 열어 장치를 확인하세요.

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

### 선택 일본어 음성 인식 모델

Kotoba-Whisper v2.0 Q5는 일본어용 선택 모델입니다. [고정 공식 배포](https://huggingface.co/kotoba-tech/kotoba-whisper-v2.0-ggml/tree/e3a0cf6a62b95911703cfb97d819292e058f12c3)의 537,819,875바이트와 SHA-256, Apache-2.0 원문을 확인합니다. 설치 후의 네이티브 실행에는 Python이나 인터넷이 필요하지 않습니다.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ".\tools\install-asr-model.ps1" -Profile japanese
```

`-PlayerDirectory`로 다른 플레이어 폴더를 지정할 수 있습니다. 설치기는 저장된 모델 선택을 바꾸지 않습니다. **AI 자막 → 음성 인식 모델 선택**에서 `runtime/models/ggml-kotoba-whisper-v2.0-q5_0.bin`을 선택하고 음성 언어를 일본어로 맞추세요. 다른 언어에는 기존 Turbo 모델을 다시 선택할 수 있습니다. 이 모델에 맞는 OpenVINO NPU 인코더는 포함하지 않으며, 실제 지원되는 GPU/CPU 경로와 [표본 검증 범위](asr-model-validation.md)를 확인하세요.

### alpha.7 선택 full large-v3 음성 인식 모델

`accurate` 프로필은 **full Whisper large-v3 Q5**를 별도로 설치합니다. [고정 공식 파일](https://huggingface.co/ggerganov/whisper.cpp/blob/fb5ed9369c26eaf08392504d8446da431e0fffb9/ggml-large-v3-q5_0.bin)은 revision `fb5ed9369c26eaf08392504d8446da431e0fffb9`, 1,081,140,203바이트, MIT이며, 전체 SHA-256은 [잠금 파일](../dependencies.lock.json)의 `accurateModel`과 [검증 안내](asr-model-validation.md)에 기록합니다. 이 가중치는 기본 ZIP에 포함하지 않습니다.

앱을 종료한 뒤 압축을 푼 플레이어 폴더에서 실행하세요.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ".\tools\install-asr-model.ps1" -Profile accurate
```

다른 플레이어 폴더에는 `-PlayerDirectory "D:\Apps\LumaPlayer"`를 추가합니다. 설치기는 원문 MIT 고지, 고정 revision·크기·SHA-256을 검증하며, 저장된 모델 선택은 바꾸지 않습니다. 설치 후 **AI 자막 → 음성 인식 모델 선택**에서 `runtime/models/ggml-large-v3-q5_0.bin`을 직접 선택하세요. 기존 Turbo 또는 Kotoba 파일을 다시 선택하면 해당 모델로 돌아갑니다. 기본 Turbo와 Kotoba의 자동 디코딩 경로는 유지합니다.

full-v3의 자동 모드는 모델 구조를 확인하고 beam 5와 공식 DTW token 시각을 사용합니다. 유효한 시각은 자막 페이지까지 전달하며, 정렬 정보 검증을 통과하지 못한 cue는 coarse 시간에 폴백합니다. 고정 지연 보정을 추가하거나 이미 알고 있는 가사를 강제 정렬하는 기능은 아닙니다. `accurate`라는 이름이 모든 문구·노래의 정확도 향상을 보장하지 않습니다. 공개 일본어 16초 한 구간의 CUDA 실행·부분 정렬 관측과 수동 발성 시각 검증의 차이는 [모델 검증 안내](asr-model-validation.md)를 확인하세요. 이 full-v3 모델용 NPU 인코더는 포함하지 않습니다.

### CUDA 포함판과 코어의 선택 설치

`windows-x64-cuda` ZIP에는 두 CUDA 실행기가 이미 들어 있습니다. 전체 `runtime` 폴더를 함께 유지하고, 아래 추가 설치 명령은 CUDA를 생략한 코어에서 사용하세요. 호환 NVIDIA 드라이버가 있는 **Windows x64**가 대상입니다. 번역 CUDA:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ".\tools\install-translation-cuda.ps1"
```

음성 인식 CUDA:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ".\tools\install-asr-cuda.ps1"
```

두 명령 모두 `-PlayerDirectory "D:\Apps\LumaPlayer"`를 지원합니다. 번역 실행기는 `runtime/translation/cuda`, 음성 인식 플러그인은 `runtime/ai`에 들어갑니다. CUDA 포함 ZIP은 동일한 NVIDIA `cudart64_12.dll`·`cublas64_12.dll`·`cublasLt64_12.dll`을 `runtime/cuda`에 한 번만 두고, 엔진별 GGML DLL은 분리합니다. 수동 설치기는 기존 엔진별 배치도 지원합니다. DLL을 다른 폴더로 옮기거나 서로 교체하지 마세요.

공식 고정 CUDA 12.4 배포물의 크기와 해시를 검사하고, [NVIDIA EULA 원문](https://docs.nvidia.com/cuda/archive/12.4.0/eula/index.html)과 포함 고지를 보존합니다. 포터블의 원문은 `licenses/LICENSES/translation/cuda/NVIDIA-CUDA-12.4-EULA.html`에도 있습니다. 시스템 드라이버·전역 PATH·SDK를 설치하거나 변경하지 않습니다. 추가 파일과 설치 임시 파일을 위한 저장 공간도 필요합니다.

설치 후 앱을 다시 열고 **음성 인식 가속/AI GPU** 또는 **번역 가속/번역 GPU**에서 해당 GPU를 선택하세요. 모델과 장치에 따라 지원 경로가 다르므로 화면에 표시한 **실제 백엔드**를 확인하세요. 설치 성공과 가속 실행 성공은 구분합니다. CUDA를 설치하지 않은 코어의 Vulkan/CPU 경로와 별도 ASR OpenVINO 런타임도 유지합니다. Turbo 가중치가 있다고 모든 NPU 경로가 지원되는 것은 아닙니다.

개발 저장소에서 [Windows 소스 빌드](../README.md#소스에서-빌드)를 마친 뒤 CUDA 포함 ZIP을 만들려면 다음을 실행합니다.

```powershell
pwsh -File scripts/install-asr-cuda.ps1 -Development
pwsh -File scripts/install-translation-cuda.ps1 -Development
pwsh -File scripts/package-windows.ps1 -BuildDirectory build-next -IncludeCuda
```

패키징 스크립트에서 `-IncludeCuda`를 생략하면 코어를 만듭니다. 수동 릴리스 워크플로의 `include_cuda` 입력은 기본 `true`이며, 두 실행기를 검증해 CUDA 포함 구성을 준비합니다.

### 검증과 실패 시 동작

설치기는 플레이어 또는 `licenses` 폴더의 잠금 파일을 읽고 지정된 공식 배포물만 받습니다. 기존 파일의 크기와 SHA-256이 일치하면 다시 받지 않습니다. 다운로드가 끊기거나 검증이 실패하면 기존 모델을 교체하지 않습니다. 다시 실행하면 완성된 임시 파일을 검증해 재사용하거나 불완전한 다운로드를 버리고 다시 받습니다. 파일 사용 중 또는 공간 부족 오류가 나면 앱을 닫고 공간을 확보한 뒤 다시 실행하세요.

번역이 실패해도 원문·독음과 AI 선택을 유지합니다. 잘린/유효하지 않은 한 구간은 건너뛰고 다음 구간을 계속 처리할 수 있으며, 모델 누락이나 실행기 오류는 별도로 표시합니다. 최소 10초 준비·60초 선행 인식 정책은 유지합니다. [자막·재생 안내](CAPTIONS-AND-PLAYBACK.md), [번역 비교](translation-quality-validation.md), [alpha.4 검증 상태](releases/0.1.0-alpha.4.md)를 참고하세요. 새 ZIP의 최종 크기·CI·네이티브 검증을 완료한 것으로 미리 표시하지 않습니다.

## English

The portable player includes the translation runtime and Whisper large-v3-turbo Q5 ASR model. The `windows-x64` core omits CUDA, while `windows-x64-cuda` bundles both ASR and translation CUDA runtimes. **Translation weights remain optional in both.** Playback, source captions and Japanese Hangul readings work without translation weights. Installation uses the internet; subsequent recognition/translation runs locally, without an external API account/key or Python.

On the first normal launch of a Windows package, or under **AI 가속 설정** in Settings, LumaPlayer scans GPU and available NPU hardware and offers acceleration packages. It recommends CUDA for NVIDIA, Vulkan for AMD Radeon and Intel GPUs, and OpenVINO for Intel GPUs and NPUs. Every package remains selectable regardless of the recommendation. Vulkan and OpenVINO recognition libraries are bundled, and their install buttons add or repair the translation runtime. Reopen LumaPlayer after installation to refresh device detection.

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

### Optional Japanese speech-recognition model

Kotoba-Whisper v2.0 Q5 is an optional Japanese model. The installer verifies the [pinned official distribution](https://huggingface.co/kotoba-tech/kotoba-whisper-v2.0-ggml/tree/e3a0cf6a62b95911703cfb97d819292e058f12c3), 537,819,875-byte size, SHA-256 and original Apache-2.0 text. Native inference requires neither Python nor internet.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ".\tools\install-asr-model.ps1" -Profile japanese
```

Use `-PlayerDirectory` for another player folder. Installation preserves the saved model choice: select `runtime/models/ggml-kotoba-whisper-v2.0-q5_0.bin` through **AI 자막 → 음성 인식 모델 선택**, with Japanese speech selected. Switch back to Turbo for other languages. No matching OpenVINO NPU encoder is included; check the actual GPU/CPU route and [validation scope](asr-model-validation.md).

### alpha.7 optional full large-v3 speech-recognition model

The `accurate` profile separately installs **full Whisper large-v3 Q5**. The [immutable official artifact](https://huggingface.co/ggerganov/whisper.cpp/blob/fb5ed9369c26eaf08392504d8446da431e0fffb9/ggml-large-v3-q5_0.bin) is revision `fb5ed9369c26eaf08392504d8446da431e0fffb9`, 1,081,140,203 bytes, under MIT. Its full SHA-256 is recorded in [`accurateModel` in the lock](../dependencies.lock.json) and [validation notes](asr-model-validation.md). These weights are not included in the default ZIP.

Close the app and run from its extracted folder:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ".\tools\install-asr-model.ps1" -Profile accurate
```

Add `-PlayerDirectory "D:\Apps\LumaPlayer"` for another player folder. The installer verifies the original MIT notice, pinned revision, size and SHA-256, while preserving the saved model choice. After installation, manually select `runtime/models/ggml-large-v3-q5_0.bin` through **AI 자막 → 음성 인식 모델 선택**. Select the existing Turbo or Kotoba file to return to it. Their automatic decoding paths remain unchanged.

Full-v3 automatic mode verifies the architecture, then uses beam 5 and the official DTW token points. Valid timing reaches caption pages, with coarse-time fallback for cues whose alignment metadata fails validation. It adds no fixed-delay correction or forced alignment of supplied lyrics. The `accurate` name does not guarantee better wording or song recognition. See [model validation](asr-model-validation.md) for the public Japanese 16-second CUDA/partial-alignment observations and their distinction from manual phonetic QA. No NPU encoder for this full-v3 model is included.

### Bundled CUDA and optional installation for core

The `windows-x64-cuda` ZIP already includes both runtimes. Keep its complete `runtime` folder. Use the following optional installers with core ZIPs, on **Windows x64 with a compatible NVIDIA driver**:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ".\tools\install-translation-cuda.ps1"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ".\tools\install-asr-cuda.ps1"
```

Both support `-PlayerDirectory "D:\Apps\LumaPlayer"`. Translation installs into `runtime/translation/cuda`; the ASR plugin installs into `runtime/ai`. CUDA ZIPs keep one verified copy of NVIDIA's `cudart64_12.dll`, `cublas64_12.dll` and `cublasLt64_12.dll` in `runtime/cuda`, while keeping engine-specific GGML DLLs isolated. Manual installers also support the existing per-engine layout. Do not move or interchange these DLLs.

Pinned official CUDA 12.4 artifacts retain the [original NVIDIA EULA](https://docs.nvidia.com/cuda/archive/12.4.0/eula/index.html) and third-party notices; a portable copy is at `licenses/LICENSES/translation/cuda/NVIDIA-CUDA-12.4-EULA.html`. Installers leave system drivers, global PATH and SDKs unchanged. Allow space for runtimes and temporary installation files.

Restart and select the GPU through the independent ASR or translation acceleration/GPU controls. Check the **actual backend** displayed: installing a runtime does not establish accelerated execution. Model/device support varies. Vulkan/CPU alternatives and the separate ASR OpenVINO runtime remain available. Bundling Turbo does not imply every NPU path supports that model.

After the Windows source build, use both `install-asr-cuda.ps1 -Development` and `install-translation-cuda.ps1 -Development`, then `package-windows.ps1 -BuildDirectory build-next -IncludeCuda`, as shown above. Omitting the packaging switch preserves core. The manual release workflow's `include_cuda` input defaults to `true`.

### Verification and recovery

Installers read the lock from the player or `licenses` folder and download only its designated official artifacts. A valid existing size/hash skips downloading. Interrupted downloads or failed checks preserve existing models; rerunning verifies a complete temporary file or discards an incomplete download before retrying. Close the app and provide enough space if replacement or disk-space errors occur.

Translation failures preserve source/readings and the AI preference. A truncated/invalid cue can be skipped while later cues continue; missing models/runtime failures are reported separately. The ten-second minimum and continued sixty-second lookahead remain. See the [caption/playback guide](CAPTIONS-AND-PLAYBACK.md), [translation comparisons](translation-quality-validation.md) and [alpha.4 status](releases/0.1.0-alpha.4.md). Final ZIP size, CI and native-package success are not claimed before verification.
