# Qwen3-ASR 네이티브 비교 / Native Qwen3-ASR comparison

## 한국어

2026-11-06 Windows 11 / RTX 4080 Laptop GPU 12 GB(보고값 12,282 MiB), 드라이버 617.42에서 Qwen3-ASR-1.7B FP32를 별도 네이티브 실행기로 비교했습니다. **실제 CUDA 실행은 확인했지만 현재 full Whisper large-v3 Q5보다 정확하다는 증거는 얻지 못했습니다. 제품 인식기·선택 모델·기본값은 유지합니다.**

[공식 Qwen 카드](https://huggingface.co/Qwen/Qwen3-ASR-1.7B)는 일본어와 노래·배경음 환경을 지원 대상으로 설명하고 Apache-2.0을 선언합니다. 생산자 벤치마크는 이 공연의 정확도 측정이 아닙니다. [sherpa 공식 내보내기 안내](https://k2-fsa.github.io/sherpa/onnx/qwen3-asr/export.html)가 연결하는 커뮤니티 ONNX 변환을 사용했으며 Qwen 공식 ONNX 배포라고 표현하지 않습니다. Python·클라우드 API 없이 C++ 실행기와 ONNX Runtime CUDA EP를 사용했습니다.

### 같은 공개 음성 비교

제공 정답이 있는 일본어 5.08초 한 문장을 같은 16 kHz mono PCM16 WAV로 비교했습니다. 정규화는 Unicode NFKC 후 문장부호 범주 P와 공백 제거, 거리는 Unicode scalar 단위 Levenshtein입니다. 정답은 프롬프트에 넣지 않았습니다.

| 모델 | 정규화 오류/정답 문자 | 초기화 s | 인식 s | 전체 프로세스 s | 관측 전체 GPU 최대 MiB |
|---|---:|---:|---:|---:|---:|
| Qwen3-ASR-1.7B FP32 | 0/25 | 8.760 | 1.938 | 11.210 | 10,406 |
| 현재 full large-v3 Q5_0 | 0/25 | 1.174 | 1.262 | 3.484 | 3,795 |

이 표는 프로파일링을 끈 단회 관측입니다. Qwen은 언어 자동 판별·4 threads, Whisper는 제품 호출과 같은 일본어 고정·8 threads / CUDA0 / beam5 / DTW였습니다. 구현·스레드·파일 캐시·실행 순서가 달라 보편적 속도 순위나 통계가 아닙니다. 짧은 한 문장의 CER을 노래·긴 공연으로 확대하지 않습니다.

별도 Qwen 프로파일의 ORT CUDA 실행 이벤트는 58,453개(MatMul 5,346, FusedMatMul 1,312, Conv 3 포함), CPU 이벤트는 17,875개였습니다. CPU MatMul/FusedMatMul/Gemm/Conv는 0개였지만 shape/index·일부 산술은 CPU에서 실행됐습니다. 이벤트 수는 반복 실행을 포함하며 고유 노드 수·계산량 비율이 아닙니다.

### 비공개 30초와 채택 한계

승인된 비공개 30초 하나에서는 Qwen의 초기화/인식/전체가 8.626/8.320/17.412초, 현재 Whisper는 1.038/3.741/5.608초였습니다. 정답 대본이 없어 CER·정확도 판정을 하지 않았습니다. Qwen의 관측 전체 GPU 최대는 11,400 MiB, Whisper는 3,795 MiB였습니다. 전체 GPU 표본에는 화면·다른 활동이 포함되며 모델 버퍼 크기나 순간 최대가 아닙니다(약 1초 표본). Qwen 관측치는 12,282 MiB 용량에서 882 MiB만 남겨 활성 ASR·번역 동시 실행의 여유를 입증하지 못합니다.

다섯 시험 자식은 모두 종료 코드 0이었고 메모리 감시·시간 제한은 발동하지 않았습니다. 기존 CUDA 12.4 대신 자식 PATH에만 CUDA 12.8.1/cuDNN 9.8.0.87을 사용했습니다. 시스템 PATH·드라이버·설정·기존 앱 런타임을 바꾸지 않았고 모델 프로세스는 종료했습니다. INT8 변환도 준비했지만 CUDA 전체 실행을 가정하거나 제품 채택하지 않았습니다.

시험 native Qwen 출력에는 단어 타임스탬프가 없습니다. [공식 Qwen3-ForcedAligner-0.6B](https://huggingface.co/Qwen/Qwen3-ForcedAligner-0.6B)의 일본어 **Speech** 지원이 노래 정렬·네이티브 통합의 검증은 아닙니다. 기존 Whisper DTW 출력도 발성 종료 정확도 자체를 보장하지 않습니다. 다음 채택에는 다양한 정답 음성·노래 비교와 시간 정렬·동시 메모리 검증이 필요합니다.

고정 출처: sherpa v2.0.1 [`af2d8d81dbfa61c4cea4c568da78f3982edca005`](https://github.com/k2-fsa/sherpa-onnx/tree/af2d8d81dbfa61c4cea4c568da78f3982edca005), ONNX Runtime 1.28.2 [`33ca9628233dc8f002435e868d4c2e9f82766ca1`](https://github.com/microsoft/onnxruntime/tree/33ca9628233dc8f002435e868d4c2e9f82766ca1), [변환 가중치 revision `9c182309f7bb075f241424441add9e16c5086dfb`](https://modelscope.cn/models/zengshuishui/Qwen3-ASR-onnx/tree/9c182309f7bb075f241424441add9e16c5086dfb/model_1.7B), [변환 코드 `0829a577ee741e408e65763dfe01ff4fe75408e9`](https://github.com/Wasser1462/Qwen3-ASR-onnx/tree/0829a577ee741e408e65763dfe01ff4fe75408e9). 원본 카드 revision은 `7278e1e70fe206f11671096ffdd38061171dd6e5`입니다. 후보 가중치·실행기·음성·인식 문자열·프로파일·개인 경로는 이번 릴리스에 포함하지 않습니다.

## English

On 2026-11-06, Qwen3-ASR-1.7B FP32 completed isolated native CUDA execution on Windows 11 / RTX 4080 Laptop GPU (12,282 MiB reported), driver 617.42. **Execution feasibility is established; superiority over current full Whisper large-v3 Q5 is not. Product backends, model selection and defaults remain unchanged.** Official Qwen support/benchmarks do not establish accuracy on this concert. The native C++/ONNX Runtime test used the community export linked by official sherpa documentation, without Python or a cloud API.

For the identical 16 kHz PCM version of one supplied-reference 5.08-second Japanese speech clip, both models had **0/25 normalized character errors**. Normalization was Unicode NFKC, removal of Unicode punctuation P and whitespace, then scalar Levenshtein distance, without reference prompting. Qwen took 8.760 s initialization / 1.938 s recognition / 11.210 s wall; Whisper took 1.174 / 1.262 / 3.484 s. Sampled whole-device GPU peaks were 10,406 and 3,795 MiB. These are unprofiled single observations: Qwen used automatic language/4 threads; Whisper used forced Japanese/8 threads with actual CUDA0/beam5/DTW. Implementations, cache and execution order differ. This one speech clip does not establish singing accuracy or general speed rankings.

A separate Qwen ORT trace recorded 58,453 CUDA events, including 5,346 MatMul, 1,312 FusedMatMul and 3 Conv, plus 17,875 CPU indexing/shape/arithmetic events. No CPU MatMul/FusedMatMul/Gemm/Conv events appeared. Counts include repeated generation and are not computational shares or unique graph nodes.

On one authorized private 30-second probe, Qwen took 8.626 s init / 8.320 s recognition / 17.412 s wall versus Whisper's 1.038 / 3.741 / 5.608 s. There is no gold reference or correctness/CER claim. Sampled whole-GPU peaks were 11,400 versus 3,795 MiB; approximately 1 Hz whole-device sampling includes display/other activity, not model buffers or guaranteed instantaneous peaks. Qwen left only 882 MiB of reported capacity, without establishing concurrent active translation headroom.

All five children exited 0 without memory/time guard activation and were released. Isolated child PATH used CUDA 12.8.1/cuDNN 9.8.0.87; global PATH, drivers, settings and existing runtime were unchanged. INT8 was staged but neither assumed fully CUDA nor adopted. Native Qwen returns no word timestamps. Official ForcedAligner Japanese speech support does not verify singing alignment/native integration; Whisper DTW output alone also does not verify perceptual lyric endpoints. Broader referenced accuracy, alignment and concurrent-memory evidence remain necessary. Exact source/version pins are listed above. Candidate binaries/weights, audio, transcripts, profiles and private paths are excluded from this release.
