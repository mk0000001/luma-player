# Kotoba NPU 준비 / Kotoba NPU setup

## 한국어

Kotoba-Whisper v2.0 Q5는 일본어용 선택 모델입니다. C++ 변환기는 고정된 원본에서 OpenVINO FP16 인코더를 만들며, 인식·변환에 Python을 사용하지 않습니다. 약 538MB의 원본 외에 약 1.275GB의 XML/BIN, 장치별 컴파일 캐시와 작업 메모리가 필요합니다. 인코더만 NPU에서 실행하고 디코더는 CPU를 사용합니다. 번역의 장치 선택은 별개입니다.

플레이어 압축을 푼 폴더에서 다음 순서로 실행하세요.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ".\tools\install-asr-model.ps1" -Profile japanese
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ".\tools\install-asr-encoder.ps1" -PrepareNpu
```

첫 줄은 크기·SHA-256·Apache-2.0 원문을 확인한 선택 모델을 받습니다. 둘째 줄은 로컬에서 인코더를 만들고, 설치기와 실행기가 공유하는 C++ 검증 함수로 원본·XML·BIN·manifest를 검사한 뒤 **현재 사용자의 앱 캐시에 NPU 컴파일을 준비**합니다. 파일만 변환하려면 `-PrepareNpu`를 생략합니다. 준비 제한은 30분이며 진행 상황을 표시합니다. 준비 실패 시 변환 파일을 보존하고 오류를 알리므로, 같은 명령으로 준비를 다시 시도할 수 있습니다.

앱의 일반 준비 제한은 60초입니다. 큰 인코더를 처음 사용하기 전 위의 별도 준비를 완료해야 합니다. 드라이버·런타임·장치·인코더가 바뀌면 캐시를 다시 만들 수 있습니다. 캐시는 해당 PC용이며 배포물에 넣지 않습니다. 스크립트는 드라이버와 시스템 보안 설정을 변경하지 않습니다.

앱의 **AI 자막 → 음성인식 모델 선택**에서 `runtime/models/ggml-kotoba-whisper-v2.0-q5_0.bin`을 선택하고, **자동 · NPU 우선** 또는 **NPU 우선 · 실패 시 GPU**를 사용하세요. 원본과 같은 폴더의 `*-encoder-openvino.xml`, `.bin`, `.json`이 한 세트입니다. 인코더를 로드했다는 사실과 실제 NPU 추론 성공은 구분합니다. 실제 요청에서 인코더 호출을 확인한 뒤에만 UI에 NPU 실행 확인을 표시합니다. 사용할 수 없으면 GPU, 다음 CPU로 대체합니다.

기존 유효 파일은 재사용하며, 잘못된 기존 인코더는 자동 덮어쓰기하지 않습니다. 모델 원본·manifest·인코더를 서로 다른 버전에서 섞지 마세요. 일반 GPU/CPU 재생 경로와 최소 10초 자막 준비·60초 선행 정책은 유지합니다. 현재 장치의 실측 및 연산량의 한계는 [성능 기록](PERFORMANCE-KOTOBA.md)을 확인하세요.

개발 저장소에서는 `scripts/install-asr-encoder.ps1 -Development -PrepareNpu`로 `.deps/models`를 사용합니다. 변환기와 worker는 먼저 `scripts/build-local-ai.ps1`로 빌드합니다. 수치 비교·파일 검증용 CLI 도움말은 `luma-whisper-encoder.exe --help`입니다.

## English

Kotoba-Whisper v2.0 Q5 is an optional Japanese model. The native C++ converter derives an OpenVINO FP16 encoder from its pinned source without Python. In addition to the approximately 538MB source, allow approximately 1.275GB for XML/BIN, device-specific compile cache and working memory. The NPU runs the encoder; the decoder remains on CPU. Translation device selection is independent.

From the extracted player folder, run the two commands above: install the pinned Japanese model, then run `install-asr-encoder.ps1 -PrepareNpu`. The installer checks the source, XML, BIN and manifest through the same strict native validator as the worker, then prepares the NPU in the current user's actual app cache. Omit `-PrepareNpu` for file conversion only. Preparation has a 30-minute deadline and reports progress. If it fails, valid converted files remain available so the same command can retry preparation.

The app's normal warmup deadline is 60 seconds. Complete this separate first-time preparation before selecting the large NPU encoder. Changes to the encoder, runtime, driver or device may require recompilation. Compiled blobs remain local and are not distributed. The tool does not modify drivers or system security settings.

Select `runtime/models/ggml-kotoba-whisper-v2.0-q5_0.bin` in AI captions and choose automatic NPU priority or NPU priority with GPU fallback. Its same-directory XML/BIN/JSON form one set. Readiness means loading/compilation completed; NPU execution is acknowledged only after a real request completes encoder inference. Unavailable NPU paths fall back to GPU, then CPU.

Existing valid artifacts are reused; unexpected existing artifacts are preserved rather than overwritten. Keep matching source, manifest and encoder versions. The minimum 10-second caption buffer and 60-second lookahead remain. See the [performance report](PERFORMANCE-KOTOBA.md) for observed results and precision-aware operation-rate limits. Development uses `scripts/install-asr-encoder.ps1 -Development -PrepareNpu` after building native AI tools.
