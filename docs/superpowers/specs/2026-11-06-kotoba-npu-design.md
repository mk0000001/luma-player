# Kotoba NPU·성능 검증 설계 / Kotoba NPU and performance design

## 한국어

사용자 요청은 현재 Kotoba 모델의 NPU 인코더를 만들어 NPU를 우선 사용하고, 현재 자막 성능에 필요한 최소 연산량을 확인하는 것입니다. 추가로 실제 Blu-ray의 AACS 설정 오류를 확인하고 재생을 복구합니다. 기존 자율 진행 권한에 따라 아래 구현 방식을 개발자가 선택합니다.

- C++/Qt 애플리케이션을 유지합니다. 변환 도구도 C++로 만들고 Python을 실행 의존성에 추가하지 않습니다.
- 기존 Q5 모델의 고정 SHA-256과 Whisper b5454 로더를 재사용해 인코더 텐서를 읽고 OpenVINO 정적 IR을 생성합니다. FP16 IR은 원래 Q5 가중치에서 유도되며 실제 NPU 연산 정밀도는 FP16입니다.
- 대안인 별도 Hugging Face FP16 모델 다운로드/Python 변환은 가중치 불일치와 추가 실행 환경 때문에 우선 선택하지 않습니다. 재학습이나 INT8 재양자화로 정확도를 바꾸지 않습니다.
- 네이티브 텐서 접근 경계, 인코더 그래프, 내보내기 CLI를 분리합니다. 입력은 `[1,128,3000]`, 출력은 `[1,1500,1280]`이며 32개 인코더 블록·20개 헤드를 유지합니다.
- CPU 기준 임베딩 및 실제 CUDA/NPU 음성 인식을 비교합니다. CPU Q5 활성값 양자화·FP16 근사·flash attention의 패딩 차이 때문에 비트 단위 동일성을 약속하지 않습니다.
- NPU는 명시한 장치에서 컴파일과 실제 추론이 성공해야 사용 확인으로 표시합니다. OpenVINO 인코더의 실행 실패를 무시하지 않고 GPU/CPU 대체를 허용합니다. 기존 최소 10초·목표 60초 선행 정책과 수동 일시정지를 유지합니다.
- 성능 보고는 콜드 준비·워밍업·정상상태, 인코더·CPU 디코더·번역·광학 읽기를 구분합니다. INT8 TOPS와 FP16/FP32 TFLOPS를 혼용하지 않습니다. 한 장치의 결과를 모든 제품의 최소 사양으로 확대하지 않습니다.
- AACS 오류는 현재 실행과 과거 로그를 구분해 조사합니다. 키·인증서 본문은 출력하거나 공개하지 않으며 새 키를 내려받지 않습니다. 기존의 적절한 로컬 설정이 실제로 열리는지를 확인하고 한국어 오류 안내를 제공합니다.
- 기존 공개 태그·바이너리는 보존하며 변경은 새 버전에 기록합니다. 공개 설명은 한국어 다음 영어 순서입니다.

## English

Build a Kotoba NPU encoder, prefer an actually usable NPU, measure the compute needed for current captions, and investigate the newly reported AACS failure. Existing autonomous authorization covers routine implementation choices.

Keep C++/Qt and use a native C++ converter with the pinned Q5 model/Whisper loader. Export a static FP16 OpenVINO encoder without retraining or INT8 requantization. Preserve `[1,128,3000]` to `[1,1500,1280]`, 32 blocks and 20 heads. A separate FP16 model/Python conversion is a fallback design, not the initial choice.

Prove numerical behavior and explicit-device inference; account for Q5 activation quantization, FP16 rounding and flash-attention padding rather than claiming bit identity. Propagate failed OpenVINO inference, preserve GPU/CPU fallback and strict caption readiness. Separate cold/warm timing, encoder/CPU decoder/translation/disc delays, precision-specific operation rates and theoretical versus observed limits. Inspect AACS safely without exposing or downloading keys. Preserve historical releases and Korean-first public explanations.
