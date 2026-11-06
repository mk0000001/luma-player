# Kotoba NPU 준비 / Kotoba NPU setup

## 한국어

상태: alpha.6 개발 안내입니다. 합성 네 요청 적합성 판정과 자동 장치 선택을 구현하고 PowerShell 7·5.1 설치 fixture 각각 25개를 통과했습니다. 공개 음성 네 요청과 별도 소유 합성 입력 네 요청도 실제 NPU로 완료했습니다. 최종 포터블의 설치 profile·GUI 10/60초 버퍼·공개 배포 검증은 대기 중입니다.

Kotoba-Whisper v2.0 Q5는 일본어용 선택 모델입니다. C++ 변환기는 고정된 원본에서 OpenVINO FP16 인코더를 만들며, 인식·변환에 Python을 사용하지 않습니다. 약 538MB의 원본 외에 약 1.275GB의 XML/BIN, 장치별 컴파일 캐시와 작업 메모리가 필요합니다. 인코더만 NPU에서 실행하고 디코더는 CPU를 사용합니다. 번역의 장치 선택은 별개입니다.

플레이어 압축을 푼 폴더에서 다음 순서로 실행하세요.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ".\tools\install-asr-model.ps1" -Profile japanese
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ".\tools\install-asr-encoder.ps1" -PrepareNpu
```

첫 줄은 크기·SHA-256·Apache-2.0 원문을 확인한 선택 모델을 받습니다. 둘째 줄은 로컬에서 인코더를 만들고, 설치기와 실행기가 공유하는 C++ 검증 함수로 원본·XML·BIN·manifest를 검사합니다. `-PrepareNpu`는 **현재 사용자의 앱 캐시에서 별도 컴파일을 준비한 뒤 실제 반복 추론으로 적합성을 검사**하는 경로입니다. 파일만 변환하려면 이 인자를 생략합니다. 최초 컴파일 준비 제한은 30분이며 진행 상황을 표시합니다. 실패해도 유효한 변환 파일은 보존하므로 같은 명령으로 다시 검사할 수 있습니다.

적합성 검사의 요구조건은 다음과 같습니다. `LUMA_READY`나 장치 열기만으로 성공을 표시하지 않습니다.

1. 소유한 합성 **16초 PCM**을 만들고 같은 resident worker에 실제 요청을 **4회** 보냅니다.
2. 네 요청 모두 실제 NPU 실행 장치, 양수인 원시 인코더 호출 수와 유한한 양수 시간을 확인합니다.
3. 첫 요청과 분리한 후속 **3회 각각**의 전체 추론 `inferenceMs`와 벽시계 `requestWallMs`가 모두 **3,000ms 이하**여야 합니다.
4. 원본·XML/BIN·worker·runtime·드라이버/장치 정체성과 결합한 로컬 calibration을 저장하고 자동 선택 시 일치 여부를 검사합니다. 불합격·부재·불일치이면 GPU, GPU가 불가능하면 CPU를 사용합니다.

앱의 일반 준비 제한은 60초입니다. 큰 인코더를 처음 사용하기 전 별도 준비를 완료하세요. 드라이버·런타임·장치·인코더·worker가 바뀌면 캐시 재컴파일이나 적합성 재검사가 필요할 수 있습니다. 컴파일 캐시는 해당 PC용이며 배포물에 넣지 않습니다. 스크립트는 드라이버와 시스템 보안 설정을 변경하지 않습니다.

앱의 AI 자막에서 `runtime/models/ggml-kotoba-whisper-v2.0-q5_0.bin`을 선택합니다. 원본과 같은 폴더의 `*-encoder-openvino.xml`, `.bin`, `.json`이 한 세트입니다. **자동 Kotoba NPU 선택에는 유효한 파일과 현재 환경에 맞는 합격 calibration이 필요**합니다. 아키텍처 이름이나 광고 TOPS로 선택하지 않습니다. 기존 base 모델 경로와 명시적 NPU 수동 선택은 유지하며 수동 선택 자체가 적합성 합격을 뜻하지 않습니다. 실제 요청의 실행 증거와 단순 인코더 로드를 구분합니다.

현재 검증 PC는 사용자가 공식 NPU 드라이버 **`32.0.100.5540`을 수동 설치했고 재부팅하지 않은 상태**입니다. 구형 `31.0.100.1688`의 1,818초 컴파일 타임아웃과 OpenVINO `2024.1`의 두 번째 요청 `DEVICE_LOST`는 비교 이력입니다. 이후 동일 원본/IR의 **OpenVINO `2026.4.1` resident worker는 공개 일본어 16초 음성의 실제 NPU 네 요청을 완료**했습니다. 후속 세 요청은 추론 약 2,480~2,554ms, 벽시계 약 2,485~2,559ms였고 모든 요청의 원시 호출 수는 1회였습니다. 콜드 준비는 약 299,730ms이므로 별도 최초 준비가 필요합니다. 해당 표본의 CUDA 인식문과 일치했지만 정답 대비 정확도·장시간 안정성·10/60초 자막 버퍼 통과를 뜻하지 않으며, 설치기의 소유 합성 입력 검사는 별도로 수행해야 합니다.

정상 빌드 worker의 별도 합성 16초 시험도 네 요청 모두 실제 NPU·원시 호출 1회였습니다. 캐시 사용 준비 9,797.446ms, 첫 추론 3,452.014ms, 후속 추론 2,052.547/2,055.236/2,060.860ms, 후속 최대 벽시계 2,064.406ms를 확인했습니다. 이 실행·시간 증거가 최종 포터블의 profile 저장·무효화·자동 선택 검증을 대신하지는 않습니다.

ASR의 앱 내부 SDK 핀은 **OpenVINO `2026.4.1.22982`**로 갱신했습니다. 정상 AI 빌드의 네이티브 CPU 검사 7/7을 통과했고 Whisper b5454와 일반 CPU/Vulkan 경로를 유지합니다. 배포 단계는 같은 SDK의 NPU 플러그인·컴파일러·로더·VM DLL과 원문 라이선스를 함께 복사하며 번역 DLL 폴더와 섞지 않습니다. [외부 구성요소 고지](../THIRD_PARTY_NOTICES.md)를 따릅니다. 최근 40+ INT8 TOPS Intel NPU도 제품명으로 합격시키지 않고 위 실측 조건을 적용합니다.

기존 유효 파일은 재사용하며, 잘못된 기존 인코더는 자동 덮어쓰기하지 않습니다. 모델 원본·manifest·인코더를 서로 다른 버전에서 섞지 마세요. 일반 GPU/CPU 경로와 최소 10초 자막 준비·60초 선행 정책은 유지합니다. 합성 적합성 검사는 인식 품질이나 재생 중 버퍼 유지를 보장하지 않습니다. 수치와 한계는 [성능 기록](PERFORMANCE-KOTOBA.md)을 확인하세요.

개발 저장소에서는 `scripts/install-asr-encoder.ps1 -Development -PrepareNpu`로 `.deps/models`를 사용합니다. 변환기와 worker는 먼저 `scripts/build-local-ai.ps1`로 빌드합니다. 수치 비교·파일 검증용 CLI 도움말은 `luma-whisper-encoder.exe --help`입니다.

## English

Status: alpha.6 development guidance. Synthetic four-request qualification and automatic selection are implemented; all 25 installer fixtures passed under each PowerShell 7/5.1. Four public-speech and four separate owned-synthetic requests completed on actual NPU. Final portable-installer profile, GUI 10/60-second coverage and publication checks remain pending.

Kotoba-Whisper v2.0 Q5 is an optional Japanese model. The native C++ converter derives an OpenVINO FP16 encoder from its pinned source without Python. In addition to the approximately 538MB source, allow approximately 1.275GB for XML/BIN, device-specific compile cache and working memory. The NPU runs the encoder; the decoder remains on CPU. Translation device selection is independent.

From the extracted player folder, run the two commands above: install the pinned Japanese model, then run `install-asr-encoder.ps1 -PrepareNpu`. The installer checks the source, XML, BIN and manifest through the same strict native validator as the worker. `-PrepareNpu` separately prepares compilation in the current user's actual app cache, then qualifies actual repeated inference. Omit it for conversion only. Initial compilation preparation has a 30-minute deadline and reports progress. Valid converted files remain available after failure for a later explicit retry.

Qualification requires more than `LUMA_READY` or opening a device:

1. Submit **four actual requests** for an owned synthetic **16-second PCM** input to one resident worker.
2. Verify an actual NPU execution device, positive raw encoder calls and finite positive timings for every request.
3. **Each of the three subsequent requests** must have complete `inferenceMs` and `requestWallMs` both **at most 3,000ms**.
4. Bind local calibration to source, XML/BIN, worker, runtime and driver/device identities, then check that binding during automatic selection. Failed, missing or mismatched calibration selects GPU, with CPU fallback if GPU is unavailable.

The app's normal preparation deadline is 60 seconds. Complete separate first-time preparation before using the large encoder. Encoder, worker, runtime, driver or device changes may require recompilation or qualification again. Compiled blobs remain local and are not distributed. The installer does not modify drivers or system security settings.

Select `runtime/models/ggml-kotoba-whisper-v2.0-q5_0.bin` in AI captions. Its same-directory XML/BIN/JSON form one set. **Automatic Kotoba NPU selection requires valid artifacts and passing calibration matching the current environment.** Architecture names and advertised TOPS do not qualify a device. The legacy base-model path and explicit manual NPU override remain available; a manual choice is not a qualification pass. Actual request execution is distinct from loading an encoder.

The user **manually installed official NPU driver `32.0.100.5540`, without rebooting**. The 1,818-second timeout on `31.0.100.1688` and OpenVINO `2024.1` second-request `DEVICE_LOST` are historical comparisons. An **OpenVINO `2026.4.1` resident worker using the same source/IR subsequently completed four genuine NPU requests for a public 16-second Japanese sample**. Subsequent inference was approximately 2,480–2,554ms and wall time 2,485–2,559ms, with one raw encoder call for every request. Cold readiness took approximately 299,730ms, requiring separate first-time preparation. Recognized text matched the CUDA sample, but this is not ground-truth accuracy, long-run stability or 10/60-second coverage. The installer's owned synthetic-input qualification remains a separate check.

The normal-build worker also completed four actual NPU requests for the generated 16-second synthetic input, one raw encoder call each: cached readiness 9,797.446ms, first inference 3,452.014ms, subsequent inference 2,052.547/2,055.236/2,060.860ms, maximum subsequent wall time 2,064.406ms. This execution/timing evidence does not replace final portable-profile persistence, invalidation and automatic-selection checks.

App-local ASR is now pinned to **OpenVINO `2026.4.1.22982`**. The normal AI build passed 7/7 native CPU tests, retaining Whisper b5454 and the normal CPU/Vulkan path. Staging copies matching NPU plugin/compiler/loader/VM DLLs and original notices together, isolated from translation DLL folders; see [third-party notices](../THIRD_PARTY_NOTICES.md). Recent 40+ INT8 TOPS Intel NPUs must satisfy the measured contract rather than qualifying by product name.

Existing valid artifacts are reused; unexpected existing artifacts are preserved rather than overwritten. Keep matching source, manifest and encoder versions. GPU/CPU paths and the minimum 10-second caption buffer/60-second lookahead remain. Synthetic qualification does not establish recognition quality or playback coverage. See the [performance report](PERFORMANCE-KOTOBA.md) for observations and limits. Development uses `scripts/install-asr-encoder.ps1 -Development -PrepareNpu` after building native tools with `scripts/build-local-ai.ps1`; native validation/comparison help is available through `luma-whisper-encoder.exe --help`.
