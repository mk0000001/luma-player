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

[Intel 185H 공식 사양](https://www.intel.co.kr/content/www/kr/ko/products/sku/236849/intel-core-ultra-9-processor-185h-24m-cache-up-to-5-10-ghz/specifications.html)의 NPU peak는 **11 INT8 TOPS**입니다. 더 최신 제품의 40+ TOPS도 정밀도를 확인해야 합니다. 예를 들어 [Intel 258V 공식 사양](https://www.intel.com/content/www/us/en/products/sku/240957/intel-core-ultra-7-processor-258v-12m-cache-up-to-4-80-ghz/specifications.html)은 NPU peak를 **47 INT8 TOPS**로 명시합니다. 이 숫자를 FP16/FP32 TFLOPS나 Kotoba 처리량으로 읽지 않습니다.

2026-11-06 드라이버 `32.0.100.5540` 설치 후, OpenVINO `2026.4.1` 메타데이터 조회에서 `NPU.3720`의 `DEVICE_GOPS`는 FP16 **5,734.4 GOPS**, INT8 **11,468.8 GOPS**를 보고했습니다. FP16 환산값은 **5.7344 TFLOPS**입니다. 이는 런타임이 보고한 장치 연산 능력이며 실제 모델의 지속 처리량 측정값이 아닙니다. 이전 ASR 런타임인 OpenVINO `2024.1`에서는 이 속성 조회를 지원하지 않았습니다.

```text
보고된 FP16 능력을 모두 사용하는 이상적 시간
  = 2.27377152 / 5.7344 ≈ 0.39651초 = 396.51ms
```

이 계산은 주요 dense 연산만 포함하며 제품 최소 사양이나 실측 최소 지연이 아닙니다. 이 이상 모델의 인코더 시간도 과거 CUDA 전체 요청 274ms보다 깁니다. 이전 문서의 약 **5.5 FP16 TFLOPS / 413ms**는 185H의 반올림된 INT8 peak와 [Meteor Lake NCE tile의 INT8/FP16 비율](https://edc.intel.com/content/www/us/en/design/products/platforms/details/meteor-lake-u-p/core-ultra-processor-datasheet-volume-1-of-2/001/nce-tile/)에서 추론한 값입니다. 현재 조회값과 구분하며, 어느 값도 장치 선택이나 3초 준비 조건 통과를 대신하지 않습니다.

### 번역과 메모리 비용

[Hy-MT2-7B](https://huggingface.co/tencent/Hy-MT2-7B/blob/main/config.json)는 자막을 순차 생성하는 별도 모델입니다. Q4_K_M 파일은 [잠금 정보](../translation-dependencies.lock.json)에서 4,624,648,896바이트이며, Kotoba Q5 파일과 합하면 5.162GB(4.808GiB)입니다. 이는 파일 크기의 합일 뿐 필요한 RAM/VRAM이 아닙니다. KV cache·작업 버퍼·FP16 NPU 인코더 사본·CPU decoder·영상과 운영체제의 메모리가 추가됩니다.

번역의 단일 요청 순차 생성은 가중치 재사용과 메모리 대역폭의 영향을 받습니다. 예를 들어 과거 85 tokens/2.002초 관측에 생성 토큰마다 4.625GB를 읽는 단순 가정을 적용하면 약 196GB/s입니다. 실제 메모리 트래픽이나 decode 처리량의 측정값이 아니며 prompt 처리·캐시·커널에 따라 달라집니다. [NVIDIA의 연산/메모리 제한 설명](https://docs.nvidia.com/deeplearning/performance/dl-performance-matrix-multiplication/index.html)처럼 연산량만으로 전체 지연을 설명할 수 없으므로, 현재 모델 조합의 제품 최소 TFLOPS를 하나의 정수로 단언하지 않습니다.

번역은 최신 자막 버퍼에서 대상 cue 전후 최대 60초의 문맥을 제공하고 **대상 cue만 번역**합니다. 최초 실제 Hy-MT2 실행의 입력 JSON 되풀이 문제는 모델별 prompt·응답 처리로 수정했습니다. 단위 80개·서비스 27개와 실제 CUDA의 직접 작성한 무문맥·문맥 입력 검사를 통과했습니다. 이 예제들의 통과나 과거 속도 관측을 모든 가사·대화의 번역 품질 보장으로 기록하지 않습니다.

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

구형 드라이버 `31.0.100.1688`과 OpenVINO `2024.1`의 전체 인코더 컴파일은 **1,818초(30분18초)**에 시험을 종료했으며 캐시가 없었습니다. 약 **9.75GiB**의 프로세스 working set이 관측됐지만 NPU 전용 메모리나 peak VRAM은 아닙니다. 이는 업데이트 전의 과거 기록입니다.

이후 사용자가 공식 NPU 드라이버 **`32.0.100.5540`을 수동 설치했으며 재부팅은 하지 않았습니다**. 같은 OpenVINO `2024.1` 경로에서 다음 실제 실행을 확인했습니다.

| 시험 | 관측 | 판정 범위 |
|---|---|---|
| 전체 인코더 수치 비교 | 실제 장치 `NPU.3720`, 컴파일 **355,621.030ms**, 첫 추론 **3,307.984ms** | 전체 NPU 컴파일·추론 확인 |
| 합성 mel의 native Q5 기준 비교 | cosine **0.984372474598**, normalized RMSE **0.176712698545** | 임계값 합격·실제 ASR 품질을 주장하지 않음 |
| 캐시 사용 resident worker의 16초 음성 request 0 | 실제 NPU, 전체 추론 **3,985.201ms**, 인코더 **3,380.651ms**, 원시 호출 **1회** | 한 요청의 실제 NPU 실행 확인 |
| 같은 worker의 request 1 | `ZE_RESULT_ERROR_DEVICE_LOST` (`0x70000001`), 종료 **22** | 연속 실행 실패 |

이후 동일한 Q5/IR과 Whisper b5454를 사용하는 OpenVINO **`2026.4.1` 별도 빌드**에서 같은 공개 일본어 16초 입력을 resident worker에 네 번 보냈습니다. 기본 compiler 선택을 사용했으며 명시적 `PLUGIN` 강제 시험으로 기록하지 않습니다.

| 요청 | 전체 추론 ms | 인코더 ms | 요청 벽시계 ms | 실행 증거 |
|---|---:|---:|---:|---|
| 첫 요청 | 4,177.404 | 3,667.474 | 4,193.115 | 실제 NPU, 원시 호출 1회 |
| 후속 1 | 2,480.040 | 1,970.969 | 2,484.615 | 실제 NPU, 원시 호출 1회 |
| 후속 2 | 2,482.592 | 1,954.427 | 2,486.966 | 실제 NPU, 원시 호출 1회 |
| 후속 3 | 2,553.997 | 1,982.019 | 2,558.898 | 실제 NPU, 원시 호출 1회 |

콜드 모델 준비·컴파일은 **299,730.433ms**로 실제 추론과 별도입니다. 네 요청을 모두 완료했고 후속 세 요청은 전체 추론·벽시계 모두 3초 이내였습니다. SRT 네 개는 바이트가 같았으며 정규화한 인식문 128자도 기존 CUDA 표본과 정확히 일치했습니다. **이 표본에서의 반복 실행과 텍스트 일치**이며 정답 대비 WER, 가사 전체의 품질, 장시간 안정성이나 10초 최소/60초 선행 버퍼를 입증하지 않습니다. 공개 보고에는 인식문 자체를 싣지 않습니다.

정상 `.deps/build-ai`의 새 worker에서도 설치기가 만든 **소유 합성 16초 PCM**으로 네 요청을 확인했습니다. 캐시 사용 준비 **9,797.446ms**, 첫 추론 **3,452.014ms**, 후속 추론 **2,052.547/2,055.236/2,060.860ms**, 후속 벽시계 **2,056.746/2,059.609/2,064.406ms**이며 모두 실제 NPU·원시 호출 1회였습니다. 따라서 공개 음성 4회와 합성 입력 4회가 실제 인코딩을 수행했습니다. 합성 입력이 무음/미실행 경로로 잘못 합격하는지 확인한 결과이며 인식 정확도 검사는 아닙니다. 최종 포터블 설치기의 profile 생성·선택과 실제 GUI 10/60초 버퍼는 별도 검증 대기입니다.

이 근거로 ASR의 앱 내부 SDK 핀을 **OpenVINO `2026.4.1.22982`**로 갱신했습니다. 정상 AI 빌드의 네이티브 CPU 검사도 **7/7** 통과했으며 Whisper b5454와 CUDA가 없는 일반 CPU/Vulkan 빌드를 유지합니다. 최근 Intel NPU용 라이브러리·컴파일러를 포함하지만 40+ INT8 TOPS 제품 전체의 실물 통과를 주장하지 않습니다. [공식 NPU 호환성 문서](https://docs.openvino.ai/2026/openvino-workflow/running-inference/inference-devices-and-modes/npu-device.html)에 따라 runtime/compiler/driver 조합을 확인하고 설치기에서 해당 PC를 실측해야 합니다. 구형 SDK·드라이버 시험은 비교 이력으로 보존하며 GPU 경로도 유지합니다.

### 자동 NPU 선택의 실측 조건

설치기·앱은 다음 적합성 계약을 구현했으며 PowerShell 7·5.1 설치 fixture 각각 25개와 앱 회귀 검사를 통과했습니다. 최종 포터블 설치기의 profile·GUI 검증은 대기 중입니다. 장치 세대나 광고 TOPS로 자동 선택하지 않습니다.

1. 소유한 합성 **16초 PCM** 입력으로 한 resident worker에 실제 요청을 **4회** 보냅니다. 준비 완료 표식만으로 합격시키지 않습니다.
2. 네 요청 모두 실제 NPU 장치, 양수인 원시 인코더 호출 수, 유한한 양수 실행 시간을 확인합니다.
3. 첫 요청과 분리한 후속 **3회 모두** 전체 추론 시간 `inferenceMs`와 요청 벽시계 시간 `requestWallMs`가 각각 **3,000ms 이하**여야 합니다.
4. 유효한 원본·IR·worker·runtime·드라이버/장치 정체성에 결합한 calibration이 있어야 자동 Kotoba NPU를 선택합니다. 실패·부재·불일치이면 GPU를 사용하며 GPU가 불가능하면 CPU로 대체합니다.

기존 base 모델 경로와 사용자의 명시적 NPU 수동 선택은 유지합니다. 수동 선택은 자동 적합성 합격을 뜻하지 않습니다. 이 합성 검사는 실행·지연 검사용이며 ASR 품질이나 실제 재생 중 버퍼 유지를 입증하지 않습니다.

### 재현 측정

저장소 루트에서 최신 timing marker를 출력하는 worker와 공개 또는 소유한 mono 16kHz PCM WAV를 사용합니다. 다음 경로는 설치 위치에 맞게 바꿉니다. XML에는 같은 stem의 BIN과 identity JSON이 필요합니다. NPU 예제는 먼저 [별도 설치 준비](INSTALL-KOTOBA-NPU.md)의 `-PrepareNpu`를 완료하고 같은 앱 캐시를 사용합니다. 일반 worker의 60초 준비 제한으로 콜드 5분 컴파일을 수행하지 않습니다. CUDA와 NPU는 각각 새 출력 폴더로 순서대로 실행하고 다른 벤치 부하와 겹치지 않게 합니다.

```powershell
$worker = 'C:\LumaPlayer\runtime\ai\luma-whisper-worker.exe'
$model = 'C:\LumaPlayer\runtime\models\ggml-kotoba-whisper-v2.0-q5_0.bin'
$encoder = 'C:\LumaPlayer\runtime\models\ggml-kotoba-whisper-v2.0-q5_0-encoder-openvino.xml'
$wave = 'C:\OwnedFixtures\ja-16s.wav'
$cache = Join-Path $env:LOCALAPPDATA 'LumaPlayer/LumaPlayer/cache/openvino'

.\scripts\measure-asr.ps1 -Worker $worker -Model $model -Wave $wave `
  -Device CUDA0 -Language ja -Threads 6 -Requests 4 -TimeoutSeconds 300 `
  -OutputDirectory '.\.superpowers\sdd\npu-kotoba\measurements\cuda-001'

.\scripts\measure-asr.ps1 -Worker $worker -Model $model -Wave $wave `
  -Device NPU -Encoder $encoder -Language ja -Threads 6 -Requests 4 `
  -CacheDirectory $cache -ReadyTimeoutSeconds 60 -TimeoutSeconds 900 `
  -OutputDirectory '.\.superpowers\sdd\npu-kotoba\measurements\npu-001'
```

[`measure-asr.ps1`](../scripts/measure-asr.ps1)의 필수 인자는 `Worker`, `Model`, `Wave`, `OutputDirectory`입니다. `Device`, `Encoder`, `Language`, `Threads`, `Requests`, `TimeoutSeconds`로 요청 조건을 지정하고 `CacheDirectory`와 `ReadyTimeoutSeconds`로 캐시·준비 조건을 별도로 지정합니다. 대기 제한을 늘리는 것은 성공 판정이 아닙니다. 위의 음성 예제는 일반 성능 비교이며 설치기의 소유 합성 입력 적합성 검사와 구분합니다.

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

[Intel's 185H specification](https://www.intel.co.kr/content/www/kr/ko/products/sku/236849/intel-core-ultra-9-processor-185h-24m-cache-up-to-5-10-ghz/specifications.html) lists **11 INT8 TOPS** for the NPU. Newer 40+ TOPS figures also require a precision label: the [official 258V specification](https://www.intel.com/content/www/us/en/products/sku/240957/intel-core-ultra-7-processor-258v-12m-cache-up-to-4-80-ghz/specifications.html) lists **47 INT8 TOPS**. These are not FP16/FP32 TFLOPS or Kotoba throughput figures.

After driver `32.0.100.5540` was installed on 2026-11-06, OpenVINO `2026.4.1` reported `NPU.3720` `DEVICE_GOPS` of **5,734.4 FP16 GOPS** and **11,468.8 INT8 GOPS**. The FP16 equivalent is **5.7344 TFLOPS**, a reported capacity rather than measured sustained model throughput. The previous ASR runtime, OpenVINO `2024.1`, did not support this property query.

Using all that reported FP16 capacity ideally gives `2.27377152 / 5.7344`, approximately **396.51ms** for major encoder operations alone. This is neither a product minimum nor measured minimum latency; even that ideal encoder estimate exceeds the historical 274ms complete CUDA request. The earlier **5.5 FP16 TFLOPS / 413ms** estimate came from the rounded 185H INT8 peak and the [Meteor Lake NCE tile's INT8/FP16 ratio](https://edc.intel.com/content/www/us/en/design/products/platforms/details/meteor-lake-u-p/core-ultra-processor-datasheet-volume-1-of-2/001/nce-tile/). Neither estimate qualifies a device or replaces an actual three-second readiness test.

### Translation and memory

[Hy-MT2-7B](https://huggingface.co/tencent/Hy-MT2-7B/blob/main/config.json) is a separate autoregressive translation model. Its [pinned Q4_K_M file](../translation-dependencies.lock.json) is 4,624,648,896 bytes; together with Kotoba Q5 the files total 5.162GB (4.808GiB). That is not sufficient RAM/VRAM: KV caches, working buffers, FP16 NPU copies, CPU decoding, video and the operating system need additional memory.

Sequential generation also depends on memory bandwidth and weight reuse. As an illustration, reading 4.625GB for every generated token at the old 85-token/2.002-second observation implies approximately 196GB/s. This is neither a traffic measurement nor a decode-throughput benchmark; prompt processing, caches and kernels affect it. [Arithmetic intensity and memory limits](https://docs.nvidia.com/deeplearning/performance/dl-performance-matrix-multiplication/index.html) prevent reducing this model combination's practical minimum to a single product TFLOPS number.

Translation supplies up to 60 seconds of context on each side of the target from the latest caption buffer and translates **only the target cue**. The initial real Hy-MT2 JSON echo was corrected through model-specific prompt/response handling. All 80 unit and 27 service checks passed, as did actual CUDA tests with independently written raw/context inputs. These examples and historical timings do not guarantee general lyrics/dialogue quality.

### Validation status: 2026-11-06

FP16 export completed: XML **1,139,082 bytes**, BIN **1,273,938,066 bytes**. Native fixture checks, real-model export and CPU numerical comparison are distinct from successful NPU execution.

For synthetic mel input, CPU comparison against the native Q5 reference with flash attention disabled observed cosine similarity **0.993151422356**, normalized RMSE **0.11703439058**, and maximum absolute difference **13.0189685822**. Q5 activation quantization, FP16 weight rounding and TANH GELU arithmetic/rounding may contribute; their individual contributions were not isolated. No threshold pass is asserted. Synthetic embeddings do not prove real ASR or singing accuracy; ASR quality validation remains in progress.

For the public 16-second Japanese fixture, a new CUDA sample took 1,748ms to prepare, 416.635ms for first inference and 250.957/253.515/267.540ms for three subsequent requests. Encoder timing was 161.007ms initially and 59.047/58.988/56.124ms subsequently, with **one raw encoder invocation per request**. NPU compilation was running concurrently, so this is not an isolated minimum-latency measurement.

The historical full compile with OpenVINO `2024.1` and old driver `31.0.100.1688` was terminated at **1,818 seconds (30 minutes18 seconds)** with no cache. Approximately **9.75GiB** process working set was observed; this was not dedicated NPU memory or peak VRAM.

The user subsequently **manually installed the official NPU driver `32.0.100.5540`, without rebooting**. The same OpenVINO `2024.1` path then produced these observations:

| Test | Observation | What it establishes |
|---|---|---|
| Full encoder numerical comparison | Actual `NPU.3720`, compile **355,621.030ms**, first inference **3,307.984ms** | Full NPU compilation and inference completed |
| Synthetic mel against native Q5 | Cosine **0.984372474598**, normalized RMSE **0.176712698545** | No threshold or real-ASR quality pass asserted |
| Cached resident worker, 16-second request 0 | Actual NPU, complete inference **3,985.201ms**, encoder **3,380.651ms**, raw calls **1** | One genuine NPU request completed |
| Same worker, request 1 | `ZE_RESULT_ERROR_DEVICE_LOST` (`0x70000001`), exit **22** | Repeated execution failed |

An OpenVINO **`2026.4.1` isolated build**, using the same Q5/IR and Whisper b5454, subsequently completed four requests for the same public 16-second Japanese input in one resident worker. It used default compiler selection; this was not an explicitly forced `PLUGIN` test.

| Request | Complete inference ms | Encoder ms | Request wall ms | Execution evidence |
|---|---:|---:|---:|---|
| First | 4,177.404 | 3,667.474 | 4,193.115 | Actual NPU, one raw call |
| Subsequent 1 | 2,480.040 | 1,970.969 | 2,484.615 | Actual NPU, one raw call |
| Subsequent 2 | 2,482.592 | 1,954.427 | 2,486.966 | Actual NPU, one raw call |
| Subsequent 3 | 2,553.997 | 1,982.019 | 2,558.898 | Actual NPU, one raw call |

Cold load/compilation was **299,730.433ms**, separate from inference. All four requests completed; all three subsequent requests had inference and wall times below three seconds. The four SRT files were byte-identical, and normalized recognized text matched the existing CUDA sample exactly at 128 characters. This establishes **repeated execution and text agreement for this sample**, not ground-truth WER, general lyrics quality, long-run stability or 10/60-second coverage. Recognized text is excluded from the public report.

The normal `.deps/build-ai` worker also completed four actual requests for the installer's **owned synthetic 16-second PCM**: cached readiness **9,797.446ms**, first inference **3,452.014ms**, subsequent inference **2,052.547/2,055.236/2,060.860ms**, subsequent wall time **2,056.746/2,059.609/2,064.406ms**, all actual NPU with one raw call each. Thus four public-speech and four synthetic requests performed real encoding; the synthetic test did not falsely pass through silence/no-encode handling. It is not an accuracy test. Final portable-installer profile generation/selection and actual GUI 10/60-second coverage remain pending.

Based on that evidence, app-local ASR is now pinned to **OpenVINO `2026.4.1.22982`**. The normal AI build also passed **7/7 native CPU tests**, retaining Whisper b5454 and the normal CUDA-free CPU/Vulkan build. Modern Intel NPU libraries/compiler are included; this is not a hardware pass for every 40+ INT8 TOPS product. Follow [official NPU compatibility guidance](https://docs.openvino.ai/2026/openvino-workflow/running-inference/inference-devices-and-modes/npu-device.html) for runtime/compiler/driver combinations and qualify each PC through the installer. Old SDK/driver results remain historical comparisons. GPU remains available.

### Measured automatic NPU qualification

The installer/app qualification contract below is implemented; all 25 installer fixtures passed under each PowerShell 7/5.1, alongside app regressions. Final portable-installer profile/GUI validation is pending. Architecture names and advertised TOPS do not select the device.

1. Submit **four actual requests** for an owned synthetic **16-second PCM** input to one resident worker. A readiness marker alone is insufficient.
2. Require an actual NPU execution device, positive raw encoder calls and finite positive timings for all four requests.
3. For **each of the three subsequent requests**, both complete `inferenceMs` and `requestWallMs` must be **at most 3,000ms**.
4. Automatic Kotoba NPU selection requires valid calibration bound to the source, IR, worker, runtime and driver/device identities. Missing, mismatched or failed calibration selects GPU, with CPU fallback if GPU is unavailable.

The legacy base-model path and explicit manual NPU override remain available. Manual selection is not automatic qualification. This synthetic check establishes execution/timing only, not ASR quality or caption coverage during playback.

### Reproducing measurements

Run the PowerShell examples above from the repository root using a current worker with timing markers and a public/owned mono 16kHz PCM WAV. Adapt the paths. An encoder XML needs matching same-stem BIN and identity JSON files. The NPU example first requires the separate [`-PrepareNpu` setup](INSTALL-KOTOBA-NPU.md) and the same app cache; the normal worker's 60-second readiness deadline is not a five-minute cold-compile path. Run CUDA and NPU sequentially into fresh output directories, without other benchmark jobs.

[`measure-asr.ps1`](../scripts/measure-asr.ps1) requires `Worker`, `Model`, `Wave` and `OutputDirectory`. Set request conditions with `Device`, `Encoder`, `Language`, `Threads`, `Requests` and `TimeoutSeconds`; `CacheDirectory` and `ReadyTimeoutSeconds` separately control cache/preparation conditions. A longer timeout does not establish success. The speech examples above are general comparisons, separate from the installer's owned synthetic-input qualification.

`metrics.json` separates load/compile-only `readyMs` from the first real inference, flagged by `runs[0].firstInference=true`, and subsequent requests on the same resident worker. It records request/inference/audio times, actual backend/device, total encoder time, encoder calls and input hashes. `LUMA_READY` alone is not evidence of successful warm inference. Record cache conditions, including the output `cache` directory, when comparing cold preparation.

The private output directory can retain `worker.stdout.txt`, `worker.stderr.txt` and protocol-generated `request-*.srt`. The metrics JSON contains no audio or recognized text. Audio, recognized subtitles and compiled caches are not published or committed; public reports contain verified devices, timing and numerical scope. Report concurrent translation, disc extraction and actual playback/seek acceptance separately from isolated worker measurements.
