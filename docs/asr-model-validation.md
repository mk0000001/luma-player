# 음성 인식 모델 비교 / Speech-recognition model comparison

## 한국어

### alpha.7 선택 full large-v3와 token 시간

alpha.7은 **full Whisper large-v3 Q5**를 `accurate` 선택 설치로 추가합니다. 기본 ZIP의 Turbo Q5와 선택 Kotoba Q5를 자동으로 교체하지 않으며, 두 모델의 `auto` 디코딩은 기존 fast 경로를 유지합니다. full-v3 파일을 직접 선택하면 native worker가 GGML 헤더의 정확한 구조를 확인한 뒤 `auto`에서 beam 5와 공식 full-v3 DTW head를 사용합니다. 이때 flash attention을 끄고 길이 48자 사전 분할을 사용하지 않습니다. `accurate`는 비교할 처리 프로필 이름이며, 모든 일본어·노래에서 더 정확하다는 보장은 아닙니다.

| 선택 full-v3 모델 | 고정 정보 |
|---|---|
| 배포자 | `ggerganov/whisper.cpp`, [고정 GGML 배포 파일](https://huggingface.co/ggerganov/whisper.cpp/blob/fb5ed9369c26eaf08392504d8446da431e0fffb9/ggml-large-v3-q5_0.bin) |
| revision | `fb5ed9369c26eaf08392504d8446da431e0fffb9` |
| 파일 | `ggml-large-v3-q5_0.bin` |
| 크기 | 1,081,140,203바이트 |
| SHA-256 | `d75795ecff3f83b5faa89d1900604ad8c780abd5739fae406de19f23ecd98ad1` |
| 라이선스 | MIT, 원문 `LICENSES/OpenAI-Whisper-MIT.txt` 유지 |

[`accurateModel` 잠금](../dependencies.lock.json)이 설치기의 기준입니다. 포터블 폴더에서 `tools/install-asr-model.ps1 -Profile accurate`로 설치하고 **AI 자막 → 음성 인식 모델 선택**에서 `runtime/models/ggml-large-v3-q5_0.bin`을 직접 선택합니다. 전체 명령과 다른 설치 위치는 [설치 안내](INSTALL-TRANSLATION.md)를 따릅니다. 가중치는 선택 다운로드이며 기본 ZIP에 추가하지 않습니다.

native worker는 호환 SRT와 함께 schema 1 `luma-asr-timing-v1`의 `.timings.json`을 출력합니다. WAV 기준 ms와 정확한 UTF-8 byte 범위를 Qt의 미디어 시각·UTF-16 범위로 전달하고, 페이지도 유효한 token 경계를 사용합니다. 일괄 고정 지연 보정은 추가하지 않습니다. DTW의 `t_dtw`는 token 시점이며, token 구간의 끝은 다음 시점이나 마지막 coarse segment 끝에서 정합니다. 정확한 발성 종료를 직접 측정한 값은 아닙니다.

UTF-8 분할 token과 같은 시점 token은 합치고, 범위·단조 시간 검증을 통과하지 못한 cue는 `tokens:[]`로 coarse 시간에 폴백합니다. 부분 정렬을 전체 정렬로 표시하지 않습니다. 외부 SRT와 token 정보가 없는 결과도 호환 경로를 유지합니다. 이 기능은 이미 알고 있는 가사를 음성에 강제 정렬하는 기능이 아니며, 잘못 인식한 문구를 DTW가 고치지는 않습니다.

#### 2026-11-06 공개 일본어 16초 실행 비교

Windows 11 / RTX 4080 Laptop GPU 12GB, 실제 `CUDA0`, 일본어·6 threads에서 같은 공개 16초 WAV를 프로필별로 4회 요청했습니다. 첫 요청과 warm 3회 중앙값을 구분합니다. 세 비교 모두 Hy-MT2-7B가 GPU에 상주한 유휴 상태였으며, 다른 추론은 동시에 실행하지 않았습니다. 활성 ASR·번역 동시 추론이나 peak VRAM 측정은 아닙니다.

| 프로필 | 첫 추론 ms | warm 3회 중앙값 ms | 요청당 유효 token 정렬 cue / 전체 cue | 요청당 token 구간 |
|---|---:|---:|---:|---:|
| full-v3 Q5 `--quality auto` · beam 5 / DTW | 3059.315 | 2686.771 | 4 / 6 | 54 |
| Kotoba Q5 `--quality auto` · greedy / token | 446.546 | 270.070 | 3 / 7 | 36 |
| full-v3 Q5 `--quality fast` · greedy / token | 1765.589 | 1622.024 | 2 / 7 | 27 |

같은 실행 파일과 같은 WAV 해시를 사용했고, 각 프로필 안에서 SRT·sidecar는 4회 모두 같은 해시였습니다. 서로 다른 문장 4개를 시험한 결과가 아닙니다. 신뢰할 원문 reference가 없어 CER이나 일본어 정확도 순위를 계산하지 않았습니다. 프로필 사이에 문구와 끝부분 출력도 달랐으므로, 실행 성공이나 정렬 cue 수로 정확도 향상을 주장하지 않습니다. Beam·DTW·flash·분할 설정이 함께 바뀌는 비교여서 각 설정의 효과도 분리하지 않았습니다. 노래·느린 음절·콘서트 음성의 품질 근거로 확대하지 않습니다.

full-v3 auto의 실제 native sidecar 4개를 제품과 같은 Qt parser·pager에 넣은 검사는 **24 segments, 16 aligned cues, 216 token 구간, 24 pages**를 통과했습니다. cue 2와 4는 각 요청에서 token이 없는 폴백이었습니다. 이는 byte 범위·시간 경계·부분 정렬 소비에 대한 소프트웨어 계약 검사입니다. 수동 청취로 정한 발성 시작·종료 reference와의 정확도 검사는 아직 아닙니다. 이번 실측은 CUDA0 범위이며 CPU/NPU나 새 배포 ZIP의 실물 검증을 대신하지 않습니다.

### alpha.4 모델 선택과 검증 기록

alpha.4는 다국어 기본 모델인 Whisper large-v3-turbo Q5를 유지하고, 일본어용 **Kotoba-Whisper v2.0 Q5**를 선택 설치로 추가합니다. 모델 크기나 디코딩 후보 수만 늘려 정확도가 좋아졌다고 표시하지 않습니다.

| 선택 모델 | 고정 정보 |
|---|---|
| 배포자 | Kotoba Technologies / Asahi Ushio, `kotoba-tech/kotoba-whisper-v2.0-ggml` |
| revision | `e3a0cf6a62b95911703cfb97d819292e058f12c3` |
| 파일 | `ggml-kotoba-whisper-v2.0-q5_0.bin` |
| 크기 | 537,819,875바이트 |
| SHA-256 | `4a3b92192b5d3578ff854a5876213e2e27af0c2d357492c2d14271e82c303658` |
| 라이선스 | Apache-2.0, 원문 `LICENSES/Kotoba-Whisper-Apache-2.0.txt` |

정확한 전체 해시는 [의존성 잠금](../dependencies.lock.json)의 `japaneseModel`이 기준입니다. 설치기는 원문 라이선스와 모델 크기·해시를 검증하고, 실패한 다운로드가 기존 모델을 교체하지 않게 합니다. 설치와 선택은 [설치 안내](INSTALL-TRANSLATION.md)를 따릅니다.

#### 채택한 범위

[공식 모델 카드](https://huggingface.co/kotoba-tech/kotoba-whisper-v2.0)는 일본어 방송 자료로 학습한 증류 모델의 평가와 영역별 차이를 공개합니다. 방송 시험에서는 교사 large-v3보다 나았지만 다른 일본어 시험에서는 항상 우세하지 않습니다. 콘서트 노래에 대한 검증이나 Turbo Q5와의 직접 정확도 비교를 대신하지 않습니다. v2.2는 같은 v2.0 가중치에 추가 파이프라인을 연결한 구성이므로 Python 구성 요소를 앱에 넣지 않았습니다.

Windows 11 / RTX 4080 Laptop GPU에서 고정 whisper.cpp b5454가 32개 인코더·2개 디코더 레이어의 Q5 파일을 읽고 일본어 추론을 완료했습니다. 공식 공개 일본어 샘플의 16초 구간에서 Kotoba 세 번은 390/274/273ms, Turbo는 946/451/426ms였습니다. 이는 짧은 한 샘플의 실행 시간이며, 완성 자막의 CER 또는 노래 정확도 수치가 아닙니다. 실제 `CUDA0` 추론 완료 표식을 확인했습니다. NPU용 일치 인코더는 제공하지 않습니다.

#### 채택하지 않은 변경

자체 작성 음성을 합성한 영어·한국어, 잡음·무음 자료로 먼저 비교했습니다. 발음한 숫자와 숫자 표기의 차이도 원시 오류 수에 포함했습니다. 기존 작업 모델과 실험 어댑터의 greedy 1/1 출력이 일치하는 것을 먼저 확인했습니다.

- Turbo beam 5/best-of 5는 개발·별도 음성의 오류 수를 줄이지 못하고 준비된 음성 처리 중앙값을 약 186→288ms로 늘렸습니다. 기본 greedy 1/1을 유지합니다.
- 일반 large-v3 Q5는 별도 영어의 원시 오류가 4/125→3/125로 줄었으나 잡음 포함 한국어 오류는 72/135→75/135였습니다. 음성 처리 중앙값은 약 193→610ms로 늘어 기본 모델로 승격하지 않았습니다.
- 직접 worker를 실행한 무음·잡음에서는 두 모델 모두 환각을 보였습니다. 앱은 이미 정확히 무음인 PCM 구간을 별도로 처리하지만 배경음 속 음성을 완전히 분리하거나 모든 환각을 막지는 않습니다.

일본어 전용 모델의 동시 ASR·번역 메모리, 실물 디스크와 최종 배포본 확인은 [alpha.4 릴리스 기록](releases/0.1.0-alpha.4.md) 및 외부 검증 자산에 구분합니다. 공개 자료에는 상용 디스크 음성이나 인식 자막을 넣지 않습니다.

## English

### alpha.7 optional full large-v3 and token timing

alpha.7 adds **full Whisper large-v3 Q5** through the optional `accurate` installer profile. It does not replace bundled Turbo Q5 or optional Kotoba Q5; their `auto` decoding preserves the existing fast path. After the user selects full-v3, the native worker checks the exact GGML architecture before choosing beam 5 and the official full-v3 DTW heads in `auto`. This disables flash attention and the 48-character pre-alignment split. `accurate` names a candidate processing profile, not a guarantee of better recognition for every Japanese utterance or song.

| Optional full-v3 model | Immutable pin |
|---|---|
| Distributor | `ggerganov/whisper.cpp`, [pinned GGML artifact](https://huggingface.co/ggerganov/whisper.cpp/blob/fb5ed9369c26eaf08392504d8446da431e0fffb9/ggml-large-v3-q5_0.bin) |
| Revision | `fb5ed9369c26eaf08392504d8446da431e0fffb9` |
| File | `ggml-large-v3-q5_0.bin` |
| Size | 1,081,140,203 bytes |
| SHA-256 | `d75795ecff3f83b5faa89d1900604ad8c780abd5739fae406de19f23ecd98ad1` |
| License | MIT; preserve the original `LICENSES/OpenAI-Whisper-MIT.txt` |

The installer uses the [`accurateModel` lock](../dependencies.lock.json). Run `tools/install-asr-model.ps1 -Profile accurate` from the portable folder, then manually select `runtime/models/ggml-large-v3-q5_0.bin` through **AI 자막 → 음성 인식 모델 선택**. See [installation](INSTALL-TRANSLATION.md) for the complete command and an alternate destination. The weights remain an optional download outside the default ZIP.

The native worker writes compatible SRT plus a schema 1 `luma-asr-timing-v1` `.timings.json` sidecar. WAV-relative milliseconds and exact UTF-8 byte spans become Qt media times and UTF-16 spans, and pages use valid token boundaries. No global fixed-delay correction is added. DTW `t_dtw` values are token points; interval ends use the next point or the final coarse segment end. They do not directly measure precise vocal endings.

Split UTF-8 tokens and tokens sharing one point are merged. Cues failing range or monotonic-time validation retain coarse times with `tokens:[]`; partial alignment is reported honestly. External SRT and results without token metadata retain their compatible fallback. This is not forced alignment of supplied lyrics, and DTW does not correct misrecognized wording.

#### Public 16-second Japanese comparison, 2026-11-06

The same public 16-second WAV was requested four times per profile on Windows 11 / RTX 4080 Laptop GPU 12GB, with actual `CUDA0`, Japanese and six threads. The first request is separate from the median of three warm requests. Hy-MT2-7B was resident but idle on the GPU in all three comparisons, with no other inference active. These runs do not measure active concurrent ASR/translation or peak VRAM.

| Profile | First inference ms | Median of 3 warm requests ms | Valid token-aligned cues / total per request | Token spans per request |
|---|---:|---:|---:|---:|
| full-v3 Q5 `--quality auto`, beam 5 / DTW | 3059.315 | 2686.771 | 4 / 6 | 54 |
| Kotoba Q5 `--quality auto`, greedy / token | 446.546 | 270.070 | 3 / 7 | 36 |
| full-v3 Q5 `--quality fast`, greedy / token | 1765.589 | 1622.024 | 2 / 7 | 27 |

The executable and WAV hashes matched across profiles. All four SRT hashes and sidecar hashes matched within each profile. These are repetitions of one utterance, not four independent samples. No trusted reference transcript was available, so no CER or Japanese-accuracy ranking was calculated. Wording and suffixes also differed: successful inference or more aligned cues do not establish an accuracy gain. Beam, DTW, flash and segmentation settings change together, so this comparison does not isolate their effects. It establishes no song, sustained-syllable or concert-audio quality claim.

Feeding the four actual full-v3 auto sidecars through the product's Qt parser and pager passed with **24 segments, 16 aligned cues, 216 token spans and 24 pages**. Cues 2 and 4 fell back without tokens in every request. This verifies the software contract for byte spans, time boundaries and partial-alignment consumption. It is not manual phonetic QA against independently labeled vocal starts and ends. These observations cover CUDA0, not CPU/NPU execution or a newly packaged portable release.

### alpha.4 model-selection and validation record

alpha.4 retains multilingual Whisper large-v3-turbo Q5 and adds optional **Kotoba-Whisper v2.0 Q5** for Japanese. Increasing model size or decoding candidates alone is not treated as an accuracy improvement. The alpha.4 Japanese-model table in the Korean section and [`japaneseModel` lock](../dependencies.lock.json) pin the official revision, 537,819,875-byte file and full SHA-256. Apache-2.0 text is retained. The installer checks the license and bytes before replacing a model; see [installation](INSTALL-TRANSLATION.md).

The [official model card](https://huggingface.co/kotoba-tech/kotoba-whisper-v2.0) describes Japanese broadcast training and domain-dependent results. It improves over its large-v3 teacher on the broadcast test, while other Japanese tests show tradeoffs. These results establish neither concert-song accuracy nor a direct comparison against Turbo Q5. v2.2 reuses v2.0 weights with additional pipeline components; no Python pipeline is bundled.

On Windows 11 / RTX 4080 Laptop GPU, pinned whisper.cpp b5454 loaded the 32-encoder/2-decoder-layer Q5 model and completed explicit Japanese inference on CUDA0. A 16-second official public Japanese sample took 390/274/273ms for Kotoba and 946/451/426ms for Turbo across three runs. These are sample processing times, not CER/accuracy measurements. No matching NPU encoder is provided.

Owned synthesized English/Korean speech and synthetic silence/noise were used before Japanese evaluation. Greedy experimental-adapter output first matched the existing worker. Spoken-number versus digit differences remain in the raw error counts. Turbo beam5/best-of5 preserved the same errors while increasing warm speech median from about 186 to 288ms, so greedy1/1 remains. Non-Turbo large-v3 Q5 changed held-out English raw errors from 4/125 to 3/125, but Korean-with-noise errors from 72/135 to 75/135, at approximately 610ms median. It was not promoted. Direct-worker non-speech hallucinations remained; the application already handles exact-zero PCM separately, without promising complete speech separation or hallucination prevention.

Concurrent ASR/translation memory, physical-disc and final-portable observations belong in the [alpha.4 release record](releases/0.1.0-alpha.4.md) and its validation assets. Commercial-disc audio and recognized transcripts are never published.
