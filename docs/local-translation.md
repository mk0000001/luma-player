# 로컬 한국어 번역 / Local Korean translation

## 한국어

### 설치와 배포

번역은 선택 기능이다. 원문 일본어와 한글 독음 데이터는 번역 결과와 분리되며, 모델이 없어도 재생·원문·독음 기능을 사용할 수 있다. 실행 중에는 인터넷, 외부 API 계정·키, Python이 필요하지 않다. 설치 스크립트를 실행할 때만 공개 모델·런타임을 내려받는다.

포터블 코어 사용자는 패키지의 `tools/install-translation.ps1`로 모델만 설치한다. [사용자 설치 안내](INSTALL-TRANSLATION.md)에 명령과 검증·재시도 동작을 설명한다. 아래 `bootstrap`·`stage` 명령은 개발·패키징용이다.

```powershell
./scripts/bootstrap-translation.ps1
./scripts/stage-translation.ps1 -RuntimeDirectory ./dist/LumaPlayer/runtime
```

`-SkipModel`은 기본 Qwen3-4B 약 2.50 GB(2.33 GiB) 모델 다운로드·복사를 생략한다. 선택 Hy-MT2-7B Q4_K_M은 약 4.62GB다. `-SkipOpenVino`는 선택적인 OpenVINO 경로를 생략한다. Vulkan 번들은 CPU 대체 경로도 포함한다. ASR의 OpenVINO **2026.4.1**은 `runtime/ai`, 번역은 `runtime/translation/*`에 두며 같은 버전이더라도 DLL 폴더를 합치지 않는다.

정확한 기본 모델 경로는 `runtime/translation/models/Qwen3-4B-Q4_K_M.gguf`이다. 공식 파일 2,497,280,256바이트이며 SHA-256은 `7485fe6f11af29433bc51cab58009521f205840f5b4ae3a32fa7f92e8534fdf5`이다. 모델과 런타임의 불변 URL·전체 해시는 `translation-dependencies.lock.json`에 고정한다. 모델을 앱 소스 저장소에 커밋하지 않는다.

Windows 네이티브 서버에는 MSVCP140/VCRUNTIME140/VCRUNTIME140_1와 시스템 UCRT가 필요하다. 공통 배포 단계가 허용된 Microsoft 재배포 파일을 각 번역 실행기 폴더에도 제공하거나 자식 프로세스가 찾을 수 있도록 해야 한다. 검증 PC에는 해당 런타임이 설치되어 있었으며 미설치된 새 Windows 환경은 별도로 검증해야 한다.

상업 이용·재배포 조건은 [공식 Qwen 라이선스](https://huggingface.co/Qwen/Qwen3-4B-GGUF/blob/bc640142c66e1fdd12af0bd68f40445458f3869b/LICENSE), [고정한 llama.cpp 라이선스](https://github.com/ggml-org/llama.cpp/blob/7d516e25ead4653169236b3f90d13f7bd8629724/LICENSE), 공식 바이너리의 LLVM OpenMP와 OpenVINO 고지에서 확인했다. 원문과 관련 제3자 고지를 `LICENSES/translation`에 보존하고 배포 시 함께 복사한다. OpenVINO에는 Apache 2.0 이외에도 Intel Distribution License와 재배포 목록 조건이 적용되는 바이너리가 포함된다.

### 장치와 실제 확인 결과

기본 자동 순서는 설치된 OpenVINO NPU → 선택한 GPU → CPU이다. 수동 NPU/GPU 모드는 지정 장치 → CPU, CPU 모드는 CPU만 사용한다. `Vulkan0`, `OpenVINO:GPU.0` 같은 식별자를 그대로 지정하며, 지정 GPU가 실패하면 다른 GPU를 임의로 고르지 않는다. 장치 목록만으로 성공을 표시하지 않고, 실제 모델 실행 로그와 완성된 번역 응답을 함께 확인한다.

2026-11-05 Windows 11, Core Ultra 9 185H, Intel Arc, Intel AI Boost 드라이버 31.0.100.1688에서 직접 작성한 일본어 문장만 검증했다. 상용 디스크 음성·자막·키는 검증 자료에 넣지 않았다.

| 경로 | 관측한 결과 |
| --- | --- |
| Qwen3-4B / Vulkan0 Intel Arc | 실제 GPU 레이어 오프로딩과 한국어 응답 확인. 네이티브 서비스 첫 결과 5.59초, 다음 문장 1.75초, 중복 캐시 0ms. 별도 배포 폴더에서도 첫 결과 6.69초, 다음 문장 1.76초 확인. |
| Qwen3-4B / OpenVINO NPU | 당시 드라이버 `31.0.100.1688`에서 `NPU_COMPILER_DYNAMIC_QUANTIZATION` 옵션 지원 오류. NPU 성공으로 표시하지 않음. 수동 NPU 모드가 이유를 알리고 CPU로 대체하여 첫 결과 50.97초, 다음 문장 2.89초, 캐시 0ms. |
| Qwen3-4B / OpenVINO GPU.1 NVIDIA RTX 4080 Laptop | 장치 목록에는 나타났지만 실제 실행기는 접근 위반 `-1073741819`로 종료. 해당 GPU 실패를 알리고 CPU로 대체. 첫 결과 9.95초, 다음 문장 3.01초. NVIDIA 번역 가속 성공으로 표시하지 않음. |
| Qwen2.5-1.5B Q4_K_M 후보 | 빠르지만 일부 문장을 일본어 그대로 반환하거나 일본어와 한국어를 혼합하여 기본 모델에서 제외. |
| Qwen3-1.7B Q8_0 후보 | 모든 예시를 한국어로 반환했지만 `도서관 전에서`, `당신이 있어서 정말 잘했어요`처럼 의미·어법 오류를 관측. 기본 모델에서 제외. |

이 시간은 위 과거 장치·설정·예시의 측정치이며 동시 재생 부하에 따라 달라진다. [상류 OpenVINO 문서](https://github.com/ggml-org/llama.cpp/blob/7d516e25ead4653169236b3f90d13f7bd8629724/docs/backend/OPENVINO.md)의 지원 표가 해당 조합의 성공을 뜻하지 않는다. 2026-11-05의 이 시험에서는 드라이버나 전역 설정을 변경하지 않았다. 이후 **2026-11-06 사용자가 공식 NPU 드라이버 `32.0.100.5540`을 수동 설치했으며 재부팅하지 않았다**. 새 OpenVINO 2026.4.1 ASR worker는 공개 16초 음성의 실제 NPU 네 요청을 완료했지만 이는 별도 모델·실행기의 결과다. 번역 NPU의 재검증이나 Hy-MT2 번역 품질 통과로 세지 않는다. [ASR 측정](PERFORMANCE-KOTOBA.md)을 참고한다.

현재 검증 PC의 빠른 기본 배포는 `-SkipOpenVino`를 사용해 확인된 Arc Vulkan/CPU 경로만 묶을 수 있다. 이 경우 없는 NPU 실행기를 즉시 알리고 GPU/CPU로 진행하여 실패하는 NPU 컴파일 대기를 피한다. OpenVINO 패키지는 지원되는 다른 드라이버·장치에서 별도로 검증할 수 있도록 선택 기능으로 남긴다.

선택한 4B 모델도 번역이 완벽하지 않다. `明日の午後三時に、図書館の前で会いましょう。`는 `내일 오후 3시에 도서관 앞에서 만나요.`로 옮겼지만, 전철 하차 문장을 `철도를 내려주세요`로 옮기는 어색한 표현도 관측했다. 원문을 보존하고 함께 표시하며, 검증된 전문 자막으로 취급하지 않는다. 모델별 응답 형식 오류·잘린 생성·일본어 잔존·한국어가 없는 결과는 성공 자막으로 내보내지 않는다.

### 최신 버퍼 문맥과 모델별 응답

alpha.6의 문맥 경로는 대상 cue의 전후 최대 60초를 **최신 원문 자막 버퍼**에서 선택하고 대상 cue만 번역한다. 원문·타임스탬프·한글 독음은 바꾸지 않는다. 가까운 cue를 우선하되 최대 24개, 문맥 JSON 2,048바이트, 전체 prompt 6,144 UTF-8바이트의 보수적 예산으로 제한한다. 미래 cue가 아직 없으면 기다리지 않으며 이후 요청을 보낼 때 갱신된 버퍼를 사용한다. 시간·원문이 다른 반복 구절은 별도 cue로 식별한다.

Qwen은 기존 `korean` JSON 응답을 사용하고 Hy-MT2는 배경 문맥과 번역 대상 문자열을 분리한 모델별 prompt·일반 텍스트 응답 처리를 사용한다. 최초 실제 Hy-MT2 문맥 시험의 입력 JSON 되풀이 문제를 수정한 뒤 단위 **80/80**, 서비스 **27/27**, 실제 CUDA의 직접 작성한 무문맥·문맥 입력 검사 각각을 통과했다. 이 예제의 통과를 모든 가사·대화의 의미 정확도 보장으로 기록하지 않는다. 이 기록에는 보호 콘텐츠의 원문·번역·화면을 싣지 않는다.

### 통합 계약과 수명

`luma::TranslationService`는 Qt 이벤트 루프에서 동작한다. 기본 런타임은 실행 파일 옆 `runtime`이며, Qt6 Core/Network를 링크한다. 네이티브 llama-server와 DLL은 `translation/vulkan`, 선택적인 `translation/openvino`에 각각 격리한다. 로컬 프로세스는 `127.0.0.1`의 임시 포트에만 바인딩하고 실행마다 생성한 내부 토큰을 사용한다. 프록시·리다이렉트·외부 모델 다운로드·웹 UI는 사용하지 않는다. 자막이나 요청 로그를 파일에 쓰지 않는다.

- `setPreferences(AiPreferences)`: 장치 선택 변경 시 기존 세대와 프로세스를 취소한다.
- `setModel(QString)`: 모델 변경 시 세대를 바꾸고 캐시를 비운다. 빈 값은 기본 경로다.
- `setContextCues(QVector<SubtitleCue>)`, `requestCues(QVector<SubtitleCue>)`: 원문 버퍼를 교체한 뒤 가까운 대상 cue부터 요청한다. 실제 전송 시 최신 문맥을 선택하며 cue의 시간·원문으로 요청을 식별한다. `translatedCue(generation, cueKey, source, korean)`는 원래 cue에 결과를 매핑한다.
- 기존 `request(QStringList nearestFirst)`도 유지한다. 대기 64개·동시 추론 1개·캐시 1,024개로 제한하며 800자가 넘는 대상은 자르지 않고 `skipped`/`skippedCue`로 알린다. 한 대상의 거부가 남은 큐 전체를 중단하지 않는다.
- `cancel()`: 세대를 바꾸고 대기·진행 중 자막 요청을 취소한다. 같은 모델의 캐시와 모델 초기화는 재사용한다. 탐색 중 모델 로딩을 반복하지 않는다.
- `generation()`, `translated(generation, source, korean)`: 호출자는 현재 세대만 적용하고 동일한 원문을 가진 cue에 번역을 매핑한다. 타이밍·독음·원문은 호출자가 보관한다.
- `progress`, `failed`, `backendSelected`: 누락한 파일, 장치 실패·CPU 대체 이유, 확인한 실제 백엔드를 알린다. 시작 제한 75초·요청 제한 45초이며 대체 시도 횟수는 후보 목록으로 제한한다.

의미 있는 단위·프로세스 검사는 `translation_tests`, `translation_service_tests`에 있고 실제 모델 검사는 `translation_native_tests`로 분리했다. 후자는 `LUMA_TRANSLATION_RUNTIME`을 설정해야 실행되며 기본 테스트에서는 건너뛴다. `LUMA_TRANSLATION_MODE`, `LUMA_TRANSLATION_DEVICE`, `LUMA_TRANSLATION_EXPECT_BACKEND`로 특정 장치와 실제 백엔드를 확인한다. 생성 예시·시간·로그는 무시되는 `.deps`에만 둔다.

## English

### Installation and distribution

Translation is optional. Japanese source, Hangul reading and Korean translation remain separate. Playback/source/reading work without a translation model. Runtime inference needs no internet, external API account/key or Python. Only the explicit bootstrap step downloads public assets.

Portable-core users install only the model with `tools/install-translation.ps1`; see the [end-user installation guide](INSTALL-TRANSLATION.md). The bootstrap/stage commands below are for development and packaging.

```powershell
./scripts/bootstrap-translation.ps1
./scripts/stage-translation.ps1 -RuntimeDirectory ./dist/LumaPlayer/runtime
```

Use `-SkipModel` to omit the default Qwen3-4B model, approximately 2.50 GB/2.33 GiB; optional Hy-MT2-7B Q4_K_M is approximately 4.62GB. `-SkipOpenVino` omits the optional OpenVINO backend. Vulkan includes CPU fallback. ASR OpenVINO **2026.4.1** remains in `runtime/ai`, isolated from `runtime/translation/*`, even when runtime versions match.

The exact default is `runtime/translation/models/Qwen3-4B-Q4_K_M.gguf`: 2,497,280,256 bytes, SHA-256 `7485fe6f11af29433bc51cab58009521f205840f5b4ae3a32fa7f92e8534fdf5`. Immutable model/runtime URLs and hashes are in `translation-dependencies.lock.json`. Do not commit model binaries to source control.

The Windows native server imports MSVCP140/VCRUNTIME140/VCRUNTIME140_1 and system UCRT. Shared packaging must supply permitted Microsoft redistributables in each translation executable folder or ensure the child process can locate them. They were installed on the validation PC; a Windows system lacking that runtime needs separate verification.

Commercial use and redistribution were checked against the [official Qwen license](https://huggingface.co/Qwen/Qwen3-4B-GGUF/blob/bc640142c66e1fdd12af0bd68f40445458f3869b/LICENSE), [pinned llama.cpp license](https://github.com/ggml-org/llama.cpp/blob/7d516e25ead4653169236b3f90d13f7bd8629724/LICENSE), and the official binaries' LLVM OpenMP/OpenVINO notices. Preserve `LICENSES/translation` in distributions. Some OpenVINO binaries are subject to the Intel Distribution License and redistributable list in addition to open-source notices.

### Devices and observed results

Automatic mode tries installed OpenVINO NPU, the selected GPU, then CPU. Manual NPU/GPU mode tries that device then CPU; CPU mode uses CPU only. Exact IDs such as `Vulkan0` and `OpenVINO:GPU.0` are preserved. A failed selected GPU never silently selects another GPU. Success requires actual execution evidence and a complete translation response, not inventory alone.

On 2026-11-05, Windows 11/Core Ultra 9 185H/Intel Arc/Intel AI Boost driver 31.0.100.1688, only independently written Japanese sentences were tested. No commercial disc audio/subtitles/keys are included.

- Qwen3-4B/Arc Vulkan0: actual GPU layer offload and Korean responses; native service first result 5.59s including load, next sentence 1.75s, duplicate cache 0ms. A separately staged runtime also passed: 6.69s first, 1.76s next.
- Qwen3-4B/OpenVINO NPU: the then-installed `31.0.100.1688` driver rejected `NPU_COMPILER_DYNAMIC_QUANTIZATION`. Manual NPU mode reported the reason and fell back to actual CPU: 50.97s first including the failed NPU attempt, 2.89s next, cache 0ms. No translation NPU success is claimed.
- Explicit OpenVINO GPU.1/NVIDIA RTX 4080 Laptop: inventory listed the device, but the process exited with access violation `-1073741819`. The service reported that selected-device failure and fell back to actual CPU: 9.95s first, 3.01s next. No NVIDIA translation acceleration is claimed.
- Qwen2.5-1.5B Q4_K_M was rejected for unchanged Japanese/mixed-language outputs. Qwen3-1.7B Q8_0 produced Korean but more semantic/grammar errors on the sample set.

Times apply to those historical examples/settings and vary with playback load. [Upstream OpenVINO support tables](https://github.com/ggml-org/llama.cpp/blob/7d516e25ead4653169236b3f90d13f7bd8629724/docs/backend/OPENVINO.md) do not establish success for that combination. The 2026-11-05 test did not change driver/global settings. **The user subsequently manually installed official NPU driver `32.0.100.5540` on 2026-11-06, without rebooting.** A new OpenVINO 2026.4.1 ASR worker completed four actual NPU requests for public 16-second speech. That separate model/worker result is neither translation NPU revalidation nor Hy-MT2 quality acceptance; see [ASR performance](PERFORMANCE-KOTOBA.md).

For a fast default package on this tested PC, `-SkipOpenVino` can ship the verified Arc Vulkan/CPU paths. Missing NPU runtime is reported immediately before GPU/CPU selection, avoiding failed NPU compilation delays. Optional OpenVINO remains available for separately verified compatible drivers/devices.

The selected 4B model remains imperfect. It correctly translated the meeting-in-front-of-the-library example, but produced awkward rail/alighting wording. Preserve/show the original and do not treat generated text as verified professional captions. Invalid model-specific response formats, truncated generations, remaining Japanese kana and outputs lacking Korean are rejected.

### Latest-buffer context and model-specific responses

The alpha.6 context path selects up to 60 seconds on either side of a target from the **latest original-caption buffer** and translates only the target cue. Source, timing and Hangul readings remain intact. Nearby cues are preferred, bounded to 24 cues, 2,048 context-JSON bytes and a conservative 6,144 UTF-8-byte total prompt budget. Missing future cues do not block a request; later dispatches use the updated buffer. Repeated phrases at different times are separate cue identities.

Qwen retains the `korean` JSON response contract. Hy-MT2 uses a model-specific prompt separating background context from the target string, with plain-text response handling. After correcting the initial real Hy-MT2 JSON echo, **80/80 unit**, **27/27 service** and actual CUDA tests with independently written raw/context inputs passed. These examples do not guarantee semantic accuracy across all lyrics/dialogue. Protected source text, translations and screenshots are excluded from this record.

### Integration and lifecycle

`luma::TranslationService` runs on its owning Qt event-loop thread and links Qt6 Core/Network. The default root is `runtime` beside the executable. Native server/DLL folders are isolated per backend. The server binds only to `127.0.0.1` on an ephemeral port with an internally generated session token. No proxy, redirect, external model download or web UI is used. Caption/request logs are not written to disk.

`setPreferences` cancels generations/processes on device changes. `setModel` changes generation and clears cache. `setContextCues(QVector<SubtitleCue>)` replaces the source buffer; `requestCues(QVector<SubtitleCue>)` queues nearby targets using timing/source identities and chooses current context at dispatch. Apply `translatedCue(generation, cueKey, source, korean)` to its original cue only when the generation matches.

The legacy `request(QStringList nearestFirst)` API remains. Pending work is bounded to 64, inference concurrency to one and cache to 1,024. Targets over 800 characters are skipped rather than truncated; `skipped`/`skippedCue` does not cancel the remaining queue. `cancel()` changes generation and cancels pending/active caption work while preserving model initialization/cache, avoiding repeated model loads on seeks. Legacy `translated(generation, source, korean)` remains available. The caller retains timing/reading/source. `progress`, `failed`, `backendSelected` report missing assets, device/fallback reasons and actual backend. Startup/request deadlines are 75s/45s; fallback is bounded by the candidate list.

`translation_tests` and `translation_service_tests` cover parsing, bounds, duplicates, cancellation, cache, private launch, device honesty, failures, deadlines and startup reuse. `translation_native_tests` is opt-in using `LUMA_TRANSLATION_RUNTIME`; `LUMA_TRANSLATION_MODE`, `LUMA_TRANSLATION_DEVICE`, and `LUMA_TRANSLATION_EXPECT_BACKEND` select/verify a backend. Synthetic outputs/timings/logs remain in ignored `.deps` only.
