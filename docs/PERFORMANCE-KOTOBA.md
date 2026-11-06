# Kotoba 연산량과 성능 검증 / Kotoba compute and performance validation

## 한국어

### 계산 범위

이 문서의 TFLOP/s는 모델의 주요 dense 연산량을 처리 시간으로 나눈 **유효 연산율 상당값**입니다. 특정 GPU/NPU 제품의 FP32 TFLOPS 최소 사양을 확정한 결과가 아닙니다. 15초 자막을 계속 준비하는 조건과 과거 CUDA 요청의 274ms 지연을 재현하는 조건을 구분합니다.

원본은 [고정 Kotoba-Whisper v2.0 Q5 모델](../dependencies.lock.json)의 537,819,875바이트, SHA-256 `4a3b92192b5d3578ff854a5876213e2e27af0c2d357492c2d14271e82c303658`입니다. 네이티브 변환기는 이 Q5 가중치를 역양자화해 FP16 가중치의 OpenVINO 인코더를 만듭니다. CPU에서 수행한 FP16 가중치 IR 비교와 NPU의 실제 연산 정밀도·성능은 별도로 해석합니다.

[공식 모델 구성](https://huggingface.co/kotoba-tech/kotoba-whisper-v2.0/blob/main/config.json)과 [Whisper 인코더 구현](https://github.com/openai/whisper/blob/main/whisper/model.py)에 따라 hidden 폭 1280, encoder 32층, decoder 2층, head 20개, mel 128개를 사용합니다. 입력 `[1,128,3000]`은 convolution 뒤 `[1,1500,1280]`이 됩니다. 현재 경로는 15초 음성에도 30초 크기의 고정 인코더 입력을 사용합니다.

[1 MAC=2 FLOPs 관례](https://docs.nvidia.com/deeplearning/performance/dl-performance-matrix-multiplication/index.html)로 convolution, attention의 주요 행렬곱과 MLP를 합산하면 다음과 같습니다.

```text
d = 1280, S = 1500, L = 32
F_encoder = 2*(3000*d*128*3 + S*d*d*3)
            + L*(24*S*d*d + 4*S*S*d)
          = 2,273,771,520,000 ops/call
          = 2.27377152 TFLOP 상당/call
F_chunk   = 실제 encoderCalls * F_encoder
```

이는 주요 연산의 이론 계산입니다. 정규화·GELU·softmax·양자화/역양자화·메모리 복사·커널 실행·패딩·음성 읽기·디코더·번역·영상 재생·모델 로드/컴파일은 별도 비용입니다. 타임스탬프·재시도·구간 처리에 따라 인코더가 여러 번 실행될 수 있으므로 chunk당 호출 횟수를 반드시 계측해야 합니다.

### 목표 시간에 따른 하한

아래 표는 인코더가 한 번 실행되고, 시간 예산 전체를 인코더에 쓸 수 있다고 가정한 하한입니다.

| 목표 | 시간 예산 | 필요한 유효 연산율 하한 |
|---|---:|---:|
| 15초 chunk의 장기 평균 처리량 유지 | 15초 | 0.152 TFLOP/s 상당 |
| 첫 15초 준비 후 최소 10초 버퍼 유지 | 약 5초 | 0.455 TFLOP/s 상당 |
| 디스크 seek에서 2초 preroll을 포함한 같은 조건 | 약 3초 | 0.758 TFLOP/s 상당 |
| 과거 CUDA 전체 요청 274ms 재현 | 0.274초 | 8.30 TFLOP/s 상당 |

5초와 3초는 [`CaptionCoverage`](../src/core/subtitle.h)의 15초 chunk·10초 최소와 [디스크 preroll](../src/ui/main_window.cpp)에서 계산한 최초 여유입니다. 실제로는 이 안에 음성 준비·디코딩·스케줄링도 끝내야 합니다. 목표 60초 선행 버퍼가 최초 재생부터 확보되는 것은 아닙니다. 또한 ASR coverage 준비 조건은 한국어 번역 완료를 기다리지 않으므로 번역은 cue 시작 전 준비 여부를 따로 확인해야 합니다.

274ms는 [과거 공개 일본어 16초 표본](asr-model-validation.md)의 CUDA 전체 요청 시간입니다. 동일 조건에서 encoder 개별 시간이 274ms였다는 뜻이 아니며, 두 warm 관측으로 지연 분포나 모든 하드웨어의 최소 사양을 확정하지 않습니다.

### NPU peak와 비교할 때

[Intel 185H 공식 사양](https://www.intel.co.kr/content/www/kr/ko/products/sku/236849/intel-core-ultra-9-processor-185h-24m-cache-up-to-5-10-ghz/specifications.html)의 NPU peak는 **11 INT8 TOPS**입니다. [Meteor Lake U/H 데이터시트의 NCE tile 설명](https://edc.intel.com/content/www/us/en/design/products/platforms/details/meteor-lake-u-p/core-ultra-processor-datasheet-volume-1-of-2/001/nce-tile/)은 고정 2 tiles이며 tile당 같은 1GHz에서 4 INT8 TOPS 또는 2 FP16 TFLOPS라고 명시합니다.

이 비율을 같은 peak/동작 조건에 적용하면 FP16 peak를 **약 5.5 TFLOPS**로 추론할 수 있습니다. 반올림된 사양을 이용한 추정이며 설치된 장치의 실측 처리량이 아닙니다. INT8 TOPS를 FP32/FP16 TFLOPS로 그대로 읽어서는 안 됩니다.

```text
이상적인 FP16 인코더 시간 = 2.27377152 / 5.5 ≈ 0.413초
```

같은 dense 인코더 전체를 이 FP16 NPU만으로 처리한다는 조건에서는 인코더만 약 413ms이므로 전체 요청 274ms 재현은 불가능하다는 계산입니다. 이 조건부 추론은 3~5초의 재생 준비 예산을 충족하는지에 대한 실제 NPU 검증을 대신하지 않습니다.

### 번역과 메모리 비용

[Hy-MT2-7B](https://huggingface.co/tencent/Hy-MT2-7B/blob/main/config.json)는 자막을 순차 생성하는 별도 모델입니다. Q4_K_M 파일은 [잠금 정보](../translation-dependencies.lock.json)에서 4,624,648,896바이트이며, Kotoba Q5 파일과 합하면 5.162GB(4.808GiB)입니다. 이는 파일 크기의 합일 뿐 필요한 RAM/VRAM이 아닙니다. KV cache·작업 버퍼·FP16 NPU 인코더 사본·CPU decoder·영상과 운영체제의 메모리가 추가됩니다.

번역의 단일 요청 순차 생성은 가중치 재사용과 메모리 대역폭의 영향을 받습니다. 예를 들어 과거 85 tokens/2.002초 관측에 생성 토큰마다 4.625GB를 읽는 단순 가정을 적용하면 약 196GB/s입니다. 실제 메모리 트래픽이나 decode 처리량의 측정값이 아니며 prompt 처리·캐시·커널에 따라 달라집니다. [NVIDIA의 연산/메모리 제한 설명](https://docs.nvidia.com/deeplearning/performance/dl-performance-matrix-multiplication/index.html)처럼 연산량만으로 전체 지연을 설명할 수 없으므로, 현재 모델 조합의 제품 최소 TFLOPS를 하나의 정수로 단언하지 않습니다.

### 현재 검증 상태: 2026-11-06

FP16 인코더 내보내기를 완료했습니다. XML은 **1,139,082바이트**, BIN은 **1,273,938,066바이트**입니다. 작은 native fixture 검증과 실제 모델 내보내기, CPU의 수치 비교는 NPU 실행 성공과 구분합니다.

합성 mel 입력에서 native Q5 기준의 flash attention을 끄고 수행한 CPU 비교 관측은 다음과 같습니다.

| 지표 | 관측값 |
|---|---:|
| Cosine similarity | 0.993151422356 |
| Normalized RMSE | 0.11703439058 |
| Maximum absolute difference | 13.0189685822 |

Q5 경로의 activation 양자화, FP16 가중치 반올림, TANH GELU의 연산/반올림 차이가 수치 차이에 영향을 줄 수 있습니다. 이 원인별 기여를 분리한 결과는 아니며, 정해진 임계값에 합격했다고 판정하지 않습니다. 합성 embedding 비교로 실제 ASR 정확도나 노래 인식 품질을 입증하지 않습니다. 실제 ASR 품질 비교는 검증 중입니다.

새 CUDA 측정에서 공개 16초 일본어 음성의 모델 준비는 1,748ms, 첫 추론은 416.635ms, 이후 세 요청은 250.957/253.515/267.540ms였습니다. 인코더는 첫 161.007ms, 이후 59.047/58.988/56.124ms이며 **모든 요청의 원시 인코더 호출 수는 1회**입니다. 이 표본에서는 NPU 컴파일이 동시에 진행 중이었으므로 독립·최저 지연 측정으로 보지 않습니다.

NPU 전체 인코더는 OpenVINO2024.1·드라이버31.0.100.1688에서 **30분18초 내 컴파일이 끝나지 않아 해당 시험을 종료**했고 캐시는 생성되지 않았습니다. 컴파일·실제 NPU 추론·NPU 자막 버퍼 유지는 미통과입니다. 과정에서 프로세스 working set 약 **9.75GiB**가 관측되었으며, 이는 NPU 전용 메모리나 peak VRAM 수치가 아닙니다. 구형 드라이버가 지연의 유일한 원인인지는 입증하지 않았습니다. [공식 최신 NPU 문서](https://docs.openvino.ai/2026/openvino-workflow/running-inference/inference-devices-and-modes/npu-device.html)는 Meteor Lake3720의 v2565 미만 드라이버에서 새 plugin compiler blob 실행이 비호환이라고 명시하므로 런타임만 교체하는 방법도 현재 장치의 해결책으로 단정할 수 없습니다. CUDA 경로는 유지합니다.

### 재현 측정

저장소 루트에서 최신 timing marker를 출력하는 worker와 공개 또는 소유한 mono 16kHz PCM WAV를 사용합니다. 다음 경로는 설치 위치에 맞게 바꿉니다. XML에는 같은 stem의 BIN과 identity JSON이 필요합니다. CUDA와 NPU는 각각 새 출력 폴더로 순서대로 실행하고 다른 벤치 부하와 겹치지 않게 합니다.

```powershell
$worker = 'C:\LumaPlayer\runtime\ai\luma-whisper-worker.exe'
$model = 'C:\LumaPlayer\runtime\models\ggml-kotoba-whisper-v2.0-q5_0.bin'
$encoder = 'C:\LumaModels\kotoba-encoder.xml'
$wave = 'C:\OwnedFixtures\ja-15s.wav'

.\scripts\measure-asr.ps1 -Worker $worker -Model $model -Wave $wave `
  -Device CUDA0 -Language ja -Threads 6 -Requests 4 -TimeoutSeconds 300 `
  -OutputDirectory '.\.superpowers\sdd\npu-kotoba\measurements\cuda-001'

.\scripts\measure-asr.ps1 -Worker $worker -Model $model -Wave $wave `
  -Device NPU -Encoder $encoder -Language ja -Threads 6 -Requests 4 `
  -TimeoutSeconds 900 `
  -OutputDirectory '.\.superpowers\sdd\npu-kotoba\measurements\npu-001'
```

[`measure-asr.ps1`](../scripts/measure-asr.ps1)의 필수 인자는 `Worker`, `Model`, `Wave`, `OutputDirectory`입니다. 선택 인자는 `Device`(기본 NPU), `Encoder`, `Language`(ja), `Threads`(6, 범위 1~16), `Requests`(4, 범위 2~20), `TimeoutSeconds`(300, 범위 5~900)입니다. 대기 제한을 늘리는 것은 성공 판정이 아닙니다.

`metrics.json`의 `readyMs`는 모델 로드/컴파일까지만 포함하며 첫 실제 추론과 분리합니다. `runs[0].firstInference=true`가 첫 추론이며 이후 요청은 같은 resident worker의 후속 관측입니다. 각 요청의 `requestWallMs`, `inferenceMs`, `audioMs`, 실제 `backend`/`device`, `encoderTotalMs`, `encoderCalls`와 입력 해시를 기록합니다. `--warmup`의 `LUMA_READY`만으로 warm 추론 성공을 판단하지 않습니다. 콜드 준비를 비교할 때에는 출력 `cache` 폴더를 포함한 캐시 조건도 기록합니다.

측정 폴더에는 `worker.stdout.txt`, `worker.stderr.txt` 및 프로토콜이 생성한 `request-*.srt`가 남을 수 있습니다. `metrics.json`에는 오디오나 인식문을 넣지 않습니다. 입력 오디오·인식 자막·컴파일 캐시는 공개하거나 커밋하지 않고, 공개 보고에는 확인한 장치와 시간·수치 범위만 기록합니다. 번역 동시 실행·디스크 읽기·실제 재생/seek 검증은 단독 worker 측정과 구분해 보고합니다.

## English

### Scope and operation count

TFLOP/s values here are **effective major-operation equivalents**: the model's dense operation count divided by a time budget. They are not a verified minimum FP32 TFLOPS specification for a GPU/NPU product. Sustaining 15-second caption chunks and matching the old 274ms CUDA request are separate targets.

The native converter derives FP16 encoder weights from the [pinned Kotoba Q5 file](../dependencies.lock.json), 537,819,875 bytes, SHA-256 `4a3b92192b5d3578ff854a5876213e2e27af0c2d357492c2d14271e82c303658`. An FP16-weight IR comparison on CPU is distinct from the NPU's actual arithmetic and performance.

The [official model configuration](https://huggingface.co/kotoba-tech/kotoba-whisper-v2.0/blob/main/config.json) and [Whisper implementation](https://github.com/openai/whisper/blob/main/whisper/model.py) specify width 1280, 32 encoder layers, two decoder layers, 20 heads and 128 mel bins. The fixed `[1,128,3000]` input becomes `[1,1500,1280]`. The current path retains that 30-second encoder shape for 15-second audio.

Using [one multiply-add as two operations](https://docs.nvidia.com/deeplearning/performance/dl-performance-matrix-multiplication/index.html), the convolutions, attention matrix multiplications and MLP total **2,273,771,520,000 major ops per encoder call**. The formula is given above. Multiply it by the measured `encoderCalls` for each chunk; windowing, timestamps and retries can cause repeated calls. Normalization, GELU, softmax, quantization, copies, launches, padding, audio extraction, decoding, translation, video playback and cold preparation add costs.

### Deadline-derived lower bounds

Assuming one encoder call and the entire budget available to it:

| Target | Time budget | Ideal effective lower bound |
|---|---:|---:|
| Sustain 15-second chunks on average | 15 seconds | 0.152 TFLOP/s equivalent |
| Preserve the 10-second minimum after the first 15-second chunk | About 5 seconds | 0.455 TFLOP/s equivalent |
| The same condition with two seconds of disc-seek preroll | About 3 seconds | 0.758 TFLOP/s equivalent |
| Match the old 274ms complete CUDA request | 0.274 seconds | 8.30 TFLOP/s equivalent |

The five/three-second margins follow the current [15-second chunk and 10-second gate](../src/core/subtitle.h) and [disc preroll](../src/ui/main_window.cpp). Extraction, decoding and scheduling must also finish within them. The 60-second prefetch target is not present immediately at startup. Korean translation is outside the ASR coverage gate and needs separate before-cue readiness checks.

The [historical 274ms result](asr-model-validation.md) was a complete request on one public 16-second Japanese sample, not a measured encoder-only duration. Two old warm observations cannot establish a latency distribution or a universal product minimum.

### Interpreting the NPU peak

[Intel's 185H specification](https://www.intel.co.kr/content/www/kr/ko/products/sku/236849/intel-core-ultra-9-processor-185h-24m-cache-up-to-5-10-ghz/specifications.html) lists **11 INT8 TOPS** for the NPU. The [Meteor Lake U/H NCE datasheet](https://edc.intel.com/content/www/us/en/design/products/platforms/details/meteor-lake-u-p/core-ultra-processor-datasheet-volume-1-of-2/001/nce-tile/) describes two fixed tiles and, per tile at the same 1GHz, four INT8 TOPS or two FP16 TFLOPS.

Applying that ratio under matching peak conditions suggests **approximately 5.5 FP16 TFLOPS**. This inference from rounded specifications is not measured throughput. At that ideal rate, the encoder alone takes `2.27377152 / 5.5`, approximately **413ms**. Conditional on executing this entire dense encoder solely on that FP16 NPU, matching a 274ms complete request is therefore impossible. This calculation does not establish whether real NPU execution meets the separate three/five-second playback budgets.

### Translation and memory

[Hy-MT2-7B](https://huggingface.co/tencent/Hy-MT2-7B/blob/main/config.json) is a separate autoregressive translation model. Its [pinned Q4_K_M file](../translation-dependencies.lock.json) is 4,624,648,896 bytes; together with Kotoba Q5 the files total 5.162GB (4.808GiB). That is not sufficient RAM/VRAM: KV caches, working buffers, FP16 NPU copies, CPU decoding, video and the operating system need additional memory.

Sequential generation also depends on memory bandwidth and weight reuse. As an illustration, reading 4.625GB for every generated token at the old 85-token/2.002-second observation implies approximately 196GB/s. This is neither a traffic measurement nor a decode-throughput benchmark; prompt processing, caches and kernels affect it. [Arithmetic intensity and memory limits](https://docs.nvidia.com/deeplearning/performance/dl-performance-matrix-multiplication/index.html) prevent reducing this model combination's practical minimum to a single product TFLOPS number.

### Validation status: 2026-11-06

FP16 export completed: XML **1,139,082 bytes**, BIN **1,273,938,066 bytes**. Native fixture checks, real-model export and CPU numerical comparison are distinct from successful NPU execution.

For synthetic mel input, CPU comparison against the native Q5 reference with flash attention disabled observed cosine similarity **0.993151422356**, normalized RMSE **0.11703439058**, and maximum absolute difference **13.0189685822**. Q5 activation quantization, FP16 weight rounding and TANH GELU arithmetic/rounding may contribute; their individual contributions were not isolated. No threshold pass is asserted. Synthetic embeddings do not prove real ASR or singing accuracy; ASR quality validation remains in progress.

For the public 16-second Japanese fixture, a new CUDA sample took 1,748ms to prepare, 416.635ms for first inference and 250.957/253.515/267.540ms for three subsequent requests. Encoder timing was 161.007ms initially and 59.047/58.988/56.124ms subsequently, with **one raw encoder invocation per request**. NPU compilation was running concurrently, so this is not an isolated minimum-latency measurement.

Full NPU compilation with OpenVINO2024.1 and driver31.0.100.1688 did not complete within **30 minutes18 seconds**, after which the test was terminated; no cache was created. Successful compilation, genuine NPU inference and NPU caption-buffer coverage remain unverified. Observed process working set was approximately **9.75GiB**, not dedicated NPU memory or peak VRAM. The driver has not been established as the sole cause of this delay. The [current official NPU documentation](https://docs.openvino.ai/2026/openvino-workflow/running-inference/inference-devices-and-modes/npu-device.html) states that plugin-compiler blobs are incompatible with Meteor Lake3720 drivers older than v2565, so a runtime-only replacement cannot be presumed to solve this PC's issue. CUDA remains available.

### Reproducing measurements

Run the PowerShell examples above from the repository root using a current worker with timing markers and a public/owned mono 16kHz PCM WAV. Adapt the paths. An encoder XML needs matching same-stem BIN and identity JSON files. Run CUDA and NPU sequentially into fresh output directories, without other benchmark jobs.

[`measure-asr.ps1`](../scripts/measure-asr.ps1) requires `Worker`, `Model`, `Wave` and `OutputDirectory`. Optional parameters are `Device` (default NPU), `Encoder`, `Language` (ja), `Threads` (6, allowed 1–16), `Requests` (4, allowed 2–20), and `TimeoutSeconds` (300, allowed 5–900). A longer timeout does not establish success.

`metrics.json` separates load/compile-only `readyMs` from the first real inference, flagged by `runs[0].firstInference=true`, and subsequent requests on the same resident worker. It records request/inference/audio times, actual backend/device, total encoder time, encoder calls and input hashes. `LUMA_READY` alone is not evidence of successful warm inference. Record cache conditions, including the output `cache` directory, when comparing cold preparation.

The private output directory can retain `worker.stdout.txt`, `worker.stderr.txt` and protocol-generated `request-*.srt`. The metrics JSON contains no audio or recognized text. Audio, recognized subtitles and compiled caches are not published or committed; public reports contain verified devices, timing and numerical scope. Report concurrent translation, disc extraction and actual playback/seek acceptance separately from isolated worker measurements.
