# 음성 인식 모델 비교 / Speech-recognition model comparison

## 한국어

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

### 채택한 범위

[공식 모델 카드](https://huggingface.co/kotoba-tech/kotoba-whisper-v2.0)는 일본어 방송 자료로 학습한 증류 모델의 평가와 영역별 차이를 공개합니다. 방송 시험에서는 교사 large-v3보다 나았지만 다른 일본어 시험에서는 항상 우세하지 않습니다. 콘서트 노래에 대한 검증이나 Turbo Q5와의 직접 정확도 비교를 대신하지 않습니다. v2.2는 같은 v2.0 가중치에 추가 파이프라인을 연결한 구성이므로 Python 구성 요소를 앱에 넣지 않았습니다.

Windows 11 / RTX 4080 Laptop GPU에서 고정 whisper.cpp b5454가 32개 인코더·2개 디코더 레이어의 Q5 파일을 읽고 일본어 추론을 완료했습니다. 공식 공개 일본어 샘플의 16초 구간에서 Kotoba 세 번은 390/274/273ms, Turbo는 946/451/426ms였습니다. 이는 짧은 한 샘플의 실행 시간이며, 완성 자막의 CER 또는 노래 정확도 수치가 아닙니다. 실제 `CUDA0` 추론 완료 표식을 확인했습니다. NPU용 일치 인코더는 제공하지 않습니다.

### 채택하지 않은 변경

자체 작성 음성을 합성한 영어·한국어, 잡음·무음 자료로 먼저 비교했습니다. 발음한 숫자와 숫자 표기의 차이도 원시 오류 수에 포함했습니다. 기존 작업 모델과 실험 어댑터의 greedy 1/1 출력이 일치하는 것을 먼저 확인했습니다.

- Turbo beam 5/best-of 5는 개발·별도 음성의 오류 수를 줄이지 못하고 준비된 음성 처리 중앙값을 약 186→288ms로 늘렸습니다. 기본 greedy 1/1을 유지합니다.
- 일반 large-v3 Q5는 별도 영어의 원시 오류가 4/125→3/125로 줄었으나 잡음 포함 한국어 오류는 72/135→75/135였습니다. 음성 처리 중앙값은 약 193→610ms로 늘어 기본 모델로 승격하지 않았습니다.
- 직접 worker를 실행한 무음·잡음에서는 두 모델 모두 환각을 보였습니다. 앱은 이미 정확히 무음인 PCM 구간을 별도로 처리하지만 배경음 속 음성을 완전히 분리하거나 모든 환각을 막지는 않습니다.

일본어 전용 모델의 동시 ASR·번역 메모리, 실물 디스크와 최종 배포본 확인은 [alpha.4 릴리스 기록](releases/0.1.0-alpha.4.md) 및 외부 검증 자산에 구분합니다. 공개 자료에는 상용 디스크 음성이나 인식 자막을 넣지 않습니다.

## English

alpha.4 retains multilingual Whisper large-v3-turbo Q5 and adds optional **Kotoba-Whisper v2.0 Q5** for Japanese. Increasing model size or decoding candidates alone is not treated as an accuracy improvement. The table above and [`japaneseModel` lock](../dependencies.lock.json) pin the official revision, 537,819,875-byte file and full SHA-256. Apache-2.0 text is retained. The installer checks the license and bytes before replacing a model; see [installation](INSTALL-TRANSLATION.md).

The [official model card](https://huggingface.co/kotoba-tech/kotoba-whisper-v2.0) describes Japanese broadcast training and domain-dependent results. It improves over its large-v3 teacher on the broadcast test, while other Japanese tests show tradeoffs. These results establish neither concert-song accuracy nor a direct comparison against Turbo Q5. v2.2 reuses v2.0 weights with additional pipeline components; no Python pipeline is bundled.

On Windows 11 / RTX 4080 Laptop GPU, pinned whisper.cpp b5454 loaded the 32-encoder/2-decoder-layer Q5 model and completed explicit Japanese inference on CUDA0. A 16-second official public Japanese sample took 390/274/273ms for Kotoba and 946/451/426ms for Turbo across three runs. These are sample processing times, not CER/accuracy measurements. No matching NPU encoder is provided.

Owned synthesized English/Korean speech and synthetic silence/noise were used before Japanese evaluation. Greedy experimental-adapter output first matched the existing worker. Spoken-number versus digit differences remain in the raw error counts. Turbo beam5/best-of5 preserved the same errors while increasing warm speech median from about 186 to 288ms, so greedy1/1 remains. Non-Turbo large-v3 Q5 changed held-out English raw errors from 4/125 to 3/125, but Korean-with-noise errors from 72/135 to 75/135, at approximately 610ms median. It was not promoted. Direct-worker non-speech hallucinations remained; the application already handles exact-zero PCM separately, without promising complete speech separation or hallucination prevention.

Concurrent ASR/translation memory, physical-disc and final-portable observations belong in the [alpha.4 release record](releases/0.1.0-alpha.4.md) and its validation assets. Commercial-disc audio and recognized transcripts are never published.
