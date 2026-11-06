# 로컬 일본어→한국어 번역 검증 / Local Japanese-to-Korean translation validation

검증일: 2026-11-06 (한국 표준시). 상용 미디어 대신 직접 작성한 일본어 문장만 사용했습니다. 이 검증은 작은 자막 표본의 의미 보존을 확인한 것이며, 모든 콘텐츠의 번역 정확도를 보장하지 않습니다.

## 한국어

### 선택한 모델과 배포 조건

품질 프로필은 Tencent의 공식 [Hy-MT2-7B-GGUF](https://huggingface.co/tencent/Hy-MT2-7B-GGUF/tree/ab8472660ac61fac25f1af43fac2599d52a8a775)의 `Hy-MT2-7B-Q4_K_M.gguf`입니다. 일본어와 한국어는 공식 [모델 설명](https://huggingface.co/tencent/Hy-MT2-7B-GGUF/blob/ab8472660ac61fac25f1af43fac2599d52a8a775/README.md)의 지원 언어에 포함되어 있습니다. 해당 리비전의 [라이선스 원문](https://huggingface.co/tencent/Hy-MT2-7B-GGUF/blob/ab8472660ac61fac25f1af43fac2599d52a8a775/LICENSE.txt)은 Apache-2.0이며, 저장소에 원문을 그대로 보관합니다. HY-MT1.5의 지역 제한 라이선스와 혼동하지 않습니다.

| 항목 | 고정 값 |
|---|---|
| 공식 저장소 | `tencent/Hy-MT2-7B-GGUF` |
| 리비전 | `ab8472660ac61fac25f1af43fac2599d52a8a775` |
| 파일 | `Hy-MT2-7B-Q4_K_M.gguf` |
| 크기 | 4,624,648,896바이트, 약 4.62 GB / 4.31 GiB |
| SHA-256 | `9f96256500f3fc1ab4d64336b58f52a949a95ad7516b0c229476eef782f9f77b` |
| 라이선스 파일 SHA-256 | `746750afa6af28fe4f8b326751ad2a40c700d2e5c459c0a1f6a2e76d99ace224` |
| 네이티브 런타임 | llama.cpp b12125, 커밋 `7d516e25ead4653169236b3f90d13f7bd8629724` |

기존 Qwen3-4B 빠른 프로필은 유지합니다. `tools/install-translation.ps1 -Profile quality`는 잠금 파일의 품질 모델 하나만 다운로드하며, 크기와 SHA-256을 확인한 후 최종 파일로 교체합니다. 기본 `-Profile fast` 동작은 바뀌지 않습니다. 모델은 선택 설치 항목이고, 번역에는 Python·클라우드·API 키가 필요하지 않습니다. 모델 파일명을 유지하고 플레이어에서 그 파일을 선택해야 전용 프로토콜이 적용됩니다.

개발용 다운로드는 `scripts/bootstrap-translation.ps1 -ModelProfile quality`, 전체 로컬 패키지의 두 모델 준비는 `scripts/stage-translation.ps1 -RuntimeDirectory <경로> -ModelProfile all -IncludeCuda`를 사용합니다. 모델 없는 코어 패키지는 `-SkipModel`을 유지할 수 있습니다.

### 의미 검토와 실제 속도

Windows 11, NVIDIA GeForce RTX 4080 Laptop GPU, llama.cpp CUDA 12.4에서 검사했습니다. 실행 로그가 `CUDA0`, 실제 장치 이름, `offloaded 33/33 layers to GPU`를 확인했습니다. 모델 GPU 버퍼는 약 4,403 MiB였습니다. 이 새 모델의 NPU 실행은 통과로 기록하지 않습니다.

공식 기본 번역 지시를 사용자 메시지 하나로 전달하고, JSON 출력을 강제하지 않았습니다. 생성 설정은 temperature 0.7, top_p 0.6, top_k 20, repeat_penalty 1.05, seed 42, 최대 출력 512토큰입니다. 문맥이나 정답 예문을 프롬프트에 추가하지 않았습니다. 각 요청은 독립된 자막이며 원문 문자열은 그대로 결과 식별자로 반환합니다.

| 검사 | 표본 | 중앙값 | 95백분위 | 모델 시작 |
|---|---:|---:|---:|---:|
| Hy-MT2-1.8B Q8 비교 후보 | 개발 문장 16개 | 119.5 ms | 211 ms | 3,659 ms |
| 채택한 Hy-MT2-7B Q4 | 개발 문장 16개 | 240.5 ms | 433 ms | 4,315 ms |
| 같은 7B, 고정 설정의 별도 문장 | 보류 문장 12개 | 280 ms | 442 ms | 4,186 ms |

개발 표본은 부정·허용·금지·의도·부탁 8개, 미완성 구절 4개, 장소·시간·인명 2개, 하차 안내 1개, 음악 기호가 있는 권유 1개입니다. 별도 12개는 개발 결과를 본 뒤 프롬프트를 바꾸지 않고 검사했습니다. 부정 5개, 권유·본인의 의도·부탁 4개, 미완성 구절 2개, 인명·요일·시각 1개입니다. 문자열 일치 대신 각 문장의 행동, 부정 범위와 생략된 부분을 검토했습니다.

7B의 28개 출력 모두 정상 종료했으며, 부정·허용·금지의 뒤집힘이나 구절의 새 사건 추가는 없었습니다. 개발 문장 1개에는 귀환의 뉘앙스가 빠져 완전한 의미 보존으로 세지 않았습니다. 별도 12개는 핵심 의미를 보존했지만 1개에 잘못된 한국어 활용이 남았습니다.

| 직접 작성한 원문 | 기존 Qwen3-4B 출력 | Hy-MT2-7B 출력 |
|---|---|---|
| `もう一度、最初からやり直そう。` | 한 번 더 처음부터 다시 해볼게. | 다시 한 번, 처음부터 시작해 보자. |
| `待っていてくれる？` | 내려 주는 거요? | 기다려 줄래? |
| `次の駅で電車を降りてください。` | 다음 역에서 철도를 내려주세요. | 다음 역에서 기차에서 내리세요. |
| `田中さんと Alice は、大阪で会います。` | 타카자와 씨와 알리스는 오사카에서 만나요. | 타나카 씨와 앨리스는 오사카에서 만납니다. |
| `♪ 一緒に歌おう ♪` | ♪ 함께 노자요 ♪ | ♪ 함께 노래해요 ♪ |

미완성 `それでも、君のそばに。`는 7B에서 `그래도, 네 곁에.`로, 별도 표본 `誰にも言わなかった理由を`는 `아무에게도 말하지 않은 이유를`로 유지됐습니다. `急がなくても大丈夫。`는 `서두르지 않아도 괜찮아요.`, `忘れないとは約束できない。`는 `잊지 않겠다고 약속할 수는 없어.`로 옮겼습니다.

남은 한계는 구체적입니다. `僕が先に行ってくる。`가 `내가 먼저 갈게.`가 되어 다녀온다는 뉘앙스가 약해졌습니다. `明かりを消してくれる？`는 뜻은 맞지만 `불을 끄어줄래?`로 활용을 틀렸습니다. 하차 안내에는 `역에서 기차에서`라는 중복이 있습니다. 1.8B 후보는 하차 동사 활용과 구절 완성 오류가 더 커서 품질 프로필로 채택하지 않았습니다. ASR 원문 자체의 오류나 앞뒤 문맥 부족도 번역의 한계로 남습니다.

최종 C++ TranslationService 경로의 별도 실제 검사에서는 첫 날씨 문장 5,377 ms, 다음 도서관 문장 442 ms, 중복 캐시 0 ms를 기록했습니다. 실제 백엔드는 `GPU / CUDA0 / NVIDIA GeForce RTX 4080 Laptop GPU`였고 취소한 요청의 늦은 결과는 표시되지 않았습니다.

### 자동 검증 범위

파서는 손상된 유니코드, 제어 문자, 모델 역할·사고 표식, 이중 이스케이프, 잘린 생성과 형식 오류를 거부합니다. 정상 따옴표·JSON 이스케이프·한글 자모·고유명사·음악 기호·결합 문자·ZWJ는 보존합니다. 전용 평문 모드도 같은 검사를 적용하며, JSON이나 코드 블록을 임의로 벗겨서 성공으로 처리하지 않습니다.

테스트를 먼저 추가해 기존 구현의 실패를 확인한 뒤 수정했습니다. 파서 73개 검사가 통과했고, 별도 번역 CTest 3개 대상이 통과했습니다. 일반 CTest의 실제 모델 항목은 환경 변수가 없으면 건너뛰므로, 위 CUDA 실제 검사는 별도로 실행했습니다. 서비스 fixture는 잘못된 자막 뒤의 계속 처리, 원문 보존, 모델별 캐시 초기화와 진행 중 모델 교체 취소를 확인합니다. 설치기 11개 검사는 변조·미완성 파일의 교체 방지, 프로필 선택, 기존 실제 모델 재사용 및 Windows PowerShell 5.1의 패키지 실행을 확인했습니다. 실제 모델 재사용 검사는 하드 링크를 사용했고 다운로드는 0바이트였습니다.

### alpha.4 추가 비교: 번역 모델과 설정 유지

CUDA 포함 패키지 작업 중 같은 모델·장치로 196개 요청을 추가 비교했습니다. 기존 개발 16문장과 이전 별도 12문장을 이번에는 회귀 표본으로 사용했습니다. 온도·반복 페널티 비교 112개, Q6 비교 28개, 일반 자막 스타일 지시 A/B 56개이며 모두 `stop`으로 종료했고 전송 실패는 없었습니다. 비교 서버는 한 번에 하나씩 실행했으며 ASR 검사와 GPU 사용 시간을 나눴습니다.

온도 0.7/0과 repeat_penalty 1.05/1.0의 네 조합에서 top_p 0.6, top_k 20, seed 42, 공식 기본 번역 지시를 유지했습니다. 개발 문장 16개는 네 조건에서 모두 같은 출력이었습니다. 이전 별도 문장에서는 두 문장의 말투만 달라졌고 `불을 끄어줄래?`와 귀환 뉘앙스 생략은 고쳐지지 않았습니다. 따라서 디코딩 설정을 바꾸지 않았습니다.

더 높은 정밀도 후보는 같은 공식 저장소·리비전·Apache-2.0 라이선스의 [HY-MT2-7B-Q6_K.gguf](https://huggingface.co/tencent/Hy-MT2-7B-GGUF/blob/ab8472660ac61fac25f1af43fac2599d52a8a775/HY-MT2-7B-Q6_K.gguf)였습니다. 6,164,482,720바이트를 실제 다운로드하고 SHA-256 `88ef0aba59952a4cfe4be36cb5baf797dbb370bc60e9dcbd7297036021e52831`을 확인했습니다. 같은 프롬프트·설정의 Q4와 비교해 28개 중 3개만 달라졌습니다. `불을 끄어줄래?`는 문법상 올바른 `불을 끌 수 있어?`가 됐지만, 명시적인 부탁이 능력을 묻는 표현으로 바뀌어 명확한 의미 개선으로 세지 않았습니다. 다른 두 변화는 말투와 과거 표현이었고, 귀환 뉘앙스와 하차 안내의 중복은 그대로였습니다.

| 같은 CUDA0 장치의 비교 | Q4 현재 기준 | Q6 후보 |
|---|---:|---:|
| 모델 파일 크기 | 4.62 GB | 6.16 GB |
| 실제 GPU 모델 버퍼 | 4,403.24 MiB | 5,871.74 MiB |
| 28문장 응답 중앙값 / 95백분위 | 266 / 435 ms | 338 / 544 ms |
| GPU 전체 사용량 / 남은 양, 모델 로드 후 | 5,596 / 6,400 MiB | 7,142 / 4,854 MiB |

GPU 전체 사용량은 해당 실행의 Windows 작업까지 포함한 값이며 ASR 동시 실행 측정값은 아닙니다. 두 후보 모두 로그에서 CUDA0의 33/33 레이어 오프로드를 확인했습니다. Q6는 파일이 1.54 GB 커지고 응답이 느려졌지만 확실한 의미 개선이 없어 배포 모델·설치기에 추가하지 않았습니다. Q8은 더 큰 다운로드와 메모리 사용을 정당화할 근거가 없어 다운로드하지 않았습니다.

마지막으로 Q4의 공식 기본 지시를 유지하면서 자연스러운 한국어 자막, 부정·화자 의도·이름·동작·미완성 구절 보존을 요청하는 일반 지시 한 가지를 비교했습니다. 정답이나 특정 원문·한국어 단어를 프롬프트에 넣지 않았습니다. 28개 중 9개 출력이 바뀌었지만 잘못된 활용은 `불을 끄어줄 수 있어?`로 남았고, 귀환 뉘앙스와 하차 안내 중복도 유지됐습니다. 중앙값은 기존 273 ms, 후보 281 ms였습니다. 이 지시도 채택하지 않았습니다.

승격할 후보가 없어 새로 준비한 보류 문장에 대한 승격 검사는 진행하지 않았습니다. 앱의 Q4 모델·기본 프롬프트·디코딩·설치 잠금 정보는 그대로 유지했습니다. 이 비교 결과를 번역 정확도가 개선됐다는 주장에 사용하지 않습니다.

### alpha.7 추가 비교: Qwen3-8B 후보 제외

자연스러운 한국어 어투를 확인하기 위해 공식 [Qwen3-8B-GGUF](https://huggingface.co/Qwen/Qwen3-8B-GGUF/tree/7c41481f57cb95916b40956ab2f0b139b296d974)의 Q4_K_M을 현재 Hy-MT2 Q4와 비교했습니다. 아래 후보는 개발용으로만 다운로드·검증했으며 앱 프로필·설치기·배포 모델에 추가하지 않았습니다.

| Qwen 후보 고정 값 | 확인한 내용 |
|---|---|
| 리비전 | `7c41481f57cb95916b40956ab2f0b139b296d974` |
| 파일 | `Qwen3-8B-Q4_K_M.gguf` |
| 크기 / SHA-256 | 5,027,783,488바이트 / `d98cdcbd03e17ce47681435b5150e34c1417f50b5c0019dd560e4882c5745785` |
| 라이선스 | 같은 리비전의 공식 [Apache-2.0 원문](https://huggingface.co/Qwen/Qwen3-8B-GGUF/raw/7c41481f57cb95916b40956ab2f0b139b296d974/LICENSE) |
| 라이선스 SHA-256 | `5de36594c10839788a8c589443a8ef9d8b8d17c65a1b5807206ae037fc36c6bd` |

추론 전에 독립적으로 작성·고정한 합성 일본어 자막 12개를 두 모델에 동일하게 전달했습니다. 부정 범위, 허용·금지, 부탁과 가능의 구별, 존대·반말, 미완성 구절, 인명·시각·장소, 앞 원문에 따른 중의성 해소를 검토했습니다. 의미 검토 메모나 한국어 정답 예문은 프롬프트에 넣지 않았습니다. 상용 콘텐츠와 비공개 전체 표본은 공개하지 않습니다.

입력은 실제 `translationPayload` C++ 코드를 호출해 생성하고 그 JSON 바이트를 그대로 로컬 HTTP 서버에 전달했습니다. Hy-MT2는 현재 제품의 Background Information / JSON 문자열 Source Text 지시, 앞뒤 원문 선택·입력 한도, temperature 0.7, top_p 0.6, top_k 20, repeat_penalty 1.05, seed 42, 최대 출력 512토큰과 프롬프트 캐시를 그대로 사용했습니다. Qwen은 현재 `LegacyQwenJson`의 시스템·사용자 지시와 JSON schema, `enable_thinking:false`, 최대 출력 256토큰·캐시를 유지하되 [공식 비사고 모드 권장값](https://huggingface.co/Qwen/Qwen3-8B-GGUF/raw/7c41481f57cb95916b40956ab2f0b139b296d974/README.md)에 따라 temperature 0.7, top_p 0.8, top_k 20, min_p 0, presence_penalty 1.5를 적용하고 seed 42를 고정했습니다. `/no_think` 문구만 추가한 실험이 아닙니다. 기존 빠른 프로필의 디코딩 설정을 바꾼 것도 아닙니다.

같은 Windows 11·RTX 4080 Laptop GPU에서 llama.cpp b12125 (`7d516e25ead4653169236b3f90d13f7bd8629724`) CUDA 서버를 순차 실행했습니다. 24개 요청 모두 HTTP 200과 `stop`으로 끝났고 전송·추출 실패는 없었습니다.

| 이번 12개 요청의 실제 측정 | Hy-MT2 Q4 현재 기준 | Qwen3-8B Q4 후보 |
|---|---:|---:|
| 응답 중앙값 / 95백분위 | 300.631 / 578.755 ms | 315.514 / 613.216 ms |
| 첫 요청 | 376.243 ms | 470.886 ms |
| CUDA0 레이어 오프로드 | 33/33 | 37/37 |
| GPU 모델 버퍼 | 4,403.24 MiB | 4,455.34 MiB |
| GPU FP16 KV 버퍼, 8192토큰 | 1,024.00 MiB | 1,152.00 MiB |

중앙값은 정렬한 가운데 두 값의 평균이며, 95백분위는 nearest-rank 방식입니다. 12개에서는 최댓값이 95백분위가 됩니다. 응답 시간은 모델 로드 후 첫 요청을 포함한 클라이언트 측 HTTP 경과 시간이며 모델 시작 시간은 아닙니다. 모델·KV 버퍼는 각각의 서버 로그 값으로, Windows를 포함한 GPU 전체 사용량이나 ASR·DTW 동시 실행의 최대 사용량이 아닙니다.

| 직접 작성한 원문 | Hy-MT2 Q4 | Qwen3-8B Q4 |
|---|---|---|
| `そこに立つな。` | 거기에 서지 마. | 그곳에 서 있어. |
| `知らないとは言っていない。` | 모른다고는 말하지 않았어요. | 모르는 것은 말하지 않았다. |
| `ここでちょっと待って。` | 여기서 잠시 기다려요. | 여기서 잠깐 기다려줘. |

Qwen은 마지막 예처럼 일부 부탁의 반말을 더 잘 유지했고, 정중한 기다림 요청도 더 간결했습니다. 그러나 금지를 긍정 명령으로 뒤집고 중첩 부정의 범위를 바꿨습니다. 다른 문장에서는 열쇠를 잠금장치로 바꾸고, `山田`를 `야마다` 대신 `산다`로 옮겼습니다. 자연스러운 일부 표현이 이러한 의미·인명 오류를 상쇄하지 못해 후보를 제외했습니다. 별도로 남겨 둔 보류 표본에는 추론을 실행하지 않았으며, 이 결과를 전체 번역 품질 개선으로 주장하지 않습니다.

이번 표본에서 Hy-MT2는 핵심 의미를 유지했지만, 일부 편한 말투를 `-요`·`-세요`로 높이는 한계가 남았습니다. 앱은 계속 ASR이 인식한 원문 **텍스트**를 로컬 한국어 번역 모델에 보내고, 이미 확보한 앞뒤 원문만 배경으로 사용합니다. 새 원문에 따른 미래 자막의 문맥 갱신과 오래된 진행 중 응답 제외는 별도의 처리 정확성 수정입니다. 그 수정이나 이 비교가 자연스러운 한국어를 완전히 보장하지는 않습니다. 실제 오디오 인식 오류, 부족한 문맥과 화자 관계도 별도로 검토해야 합니다.

## English

The quality profile uses Tencent's official [Hy-MT2-7B Q4_K_M](https://huggingface.co/tencent/Hy-MT2-7B-GGUF/tree/ab8472660ac61fac25f1af43fac2599d52a8a775), under the pinned [Apache-2.0 license](https://huggingface.co/tencent/Hy-MT2-7B-GGUF/blob/ab8472660ac61fac25f1af43fac2599d52a8a775/LICENSE.txt). Its original license text is preserved. The table above pins the immutable revision, 4,624,648,896-byte model and hashes. Japanese and Korean are listed in the official model card. The restricted HY-MT1.5 license is a different license and is not used for this profile.

Run `tools/install-translation.ps1 -Profile quality` in an extracted player package to install only the optional 4.62 GB model. Existing default `-Profile fast` behavior remains. File size and SHA-256 are checked before publishing the download. Keep the exact model filename and select it in the player to enable its dedicated plain-response protocol. No Python, cloud inference or API key is required. Developer bootstrap and staging accept `-ModelProfile fast|quality|all`; model-free core packages can continue to use `-SkipModel`.

All examples were written for this test, without commercial media. Native llama.cpp b12125/CUDA 12.4 logs confirmed CUDA0, the RTX 4080 Laptop GPU and 33/33 layers offloaded. The model buffer was approximately 4,403 MiB. NPU inference for this new model is not claimed. The official one-user-message translation prompt was fixed throughout, without answer examples or context. Parameters were temperature .7, top_p .6, top_k 20, repeat_penalty 1.05, seed 42 and a 512-token output limit.

The 16 development examples covered negation, permission/prohibition, intention/requests, fragments, names/time/place, train alighting and musical invitations. Twelve separate held-out examples tested five negations, four intentions/requests/invitations, two fragments and one name/weekday/time. Review compared meaning and omission boundaries, rather than exact reference wording. All 28 outputs ended normally, with no polarity reversals or invented events in fragments. One development sentence lost the explicit return implication of Japanese `行ってくる`, so it was marked partial. All twelve held-out meanings were retained; one had the malformed Korean conjugation `끄어줄래` instead of `꺼 줄래`. The train output also repeated a location particle. This small corpus establishes a measured improvement over the old Qwen3-4B outputs shown above, not universal translation accuracy. ASR errors and missing context remain limitations.

7B warm latency was 240.5 ms median / 433 ms p95 on the development set and 280 ms / 442 ms on held-out text. Cold model startup was approximately 4.2 seconds. The smaller 1.8B Q8 candidate was faster but had larger conjugation and fragment-completion errors and was not promoted. Final native TranslationService verification measured 5,377 ms for the first weather caption, 442 ms for a warm library caption and 0 ms for a duplicate cache hit. It confirmed the actual CUDA backend and suppressed the canceled response.

New tests were observed failing before implementation. The parser suite passed 73 cases, and all three dedicated translation CTest targets passed. The ordinary native CTest is opt-in and skips without its environment; the CUDA proof above was run separately. Service fixtures cover invalid-cue continuation, unchanged source identifiers, cache invalidation and cancellation across an in-flight model change. Eleven installer checks passed, including corrupt/truncated publication protection, profile selection and real-model reuse through packaged Windows PowerShell 5.1 execution. Real models were hard-linked for reuse tests and downloaded zero bytes.

### alpha.4 follow-up: retain the current translation model and settings

The CUDA-package work added 196 controlled requests: 112 decoding comparisons, 28 Q6 comparisons and 56 subtitle-style prompt comparisons. The previous 16 development and 12 held-out sentences were used as regression data in this round. Every request finished with `stop`, without transport failures. Only one benchmark server ran at a time, with GPU leases separated from ASR tests.

A two-by-two comparison varied temperature .7/0 and repeat_penalty 1.05/1.0 while retaining top_p .6, top_k 20, seed 42 and the official default prompt. All sixteen development outputs were identical across all four conditions. Two older held-out sentences changed politeness or equivalent wording, without fixing the malformed light-switch conjugation or the missing return implication. No decoding change was adopted.

The official [HY-MT2-7B-Q6_K](https://huggingface.co/tencent/Hy-MT2-7B-GGUF/blob/ab8472660ac61fac25f1af43fac2599d52a8a775/HY-MT2-7B-Q6_K.gguf) candidate uses the same pinned revision and Apache-2.0 license. The downloaded 6,164,482,720-byte file matched SHA-256 `88ef0aba59952a4cfe4be36cb5baf797dbb370bc60e9dcbd7297036021e52831`. Only three of 28 outputs differed from Q4. The malformed request became a grammatical ability question, which was not counted as a clear semantic gain. The other changes concerned politeness and past-tense phrasing; the return nuance and repetitive train instruction remained.

Native CUDA logs confirmed 33/33 layers offloaded for both quants. Q4/Q6 GPU model buffers were 4,403.24/5,871.74 MiB. The 28-caption wall-time medians were 266/338 ms, with p95 435/544 ms. Total observed GPU use after load was 5,596/7,142 MiB, leaving 6,400/4,854 MiB; those figures include Windows activity and do not represent concurrent ASR tests. The extra 1.54 GB download and slower responses were not justified by the observed meaning changes, so Q6 was not added to the application profile, lock or installer. Q8 was not downloaded.

One final Q4 prompt variant kept the official default instruction and added general requirements for faithful, natural subtitles, negation, intention, names, actions and incomplete phrases. It contained no answers or source-specific vocabulary. Nine outputs changed, but the malformed conjugation persisted and the remaining known issues were not fixed. Median latency was 273 ms for the current prompt and 281 ms for the candidate. That prompt was also rejected. Since no candidate qualified for promotion, the newly prepared untouched held-out set was not used for a promotion test. The shipped Q4 model, prompt, decoding and installer pins remain unchanged; these negative results do not establish improved translation accuracy.

### alpha.7 follow-up: reject the Qwen3-8B candidate

The official [Qwen3-8B Q4_K_M](https://huggingface.co/Qwen/Qwen3-8B-GGUF/tree/7c41481f57cb95916b40956ab2f0b139b296d974) was compared with the current Hy-MT2 Q4. Revision `7c41481f57cb95916b40956ab2f0b139b296d974`, the 5,027,783,488-byte model, SHA-256 and the official [Apache-2.0 license](https://huggingface.co/Qwen/Qwen3-8B-GGUF/raw/7c41481f57cb95916b40956ab2f0b139b296d974/LICENSE) are pinned in the Korean table above. The downloaded license matched SHA-256 `5de36594c10839788a8c589443a8ef9d8b8d17c65a1b5807206ae037fc36c6bd`. This remained a development candidate; no application profile, installer or distributed model was added.

Twelve independently written Japanese subtitle cases were fixed before inference. They covered negation scope, permission/prohibition, requests versus possibility, polite/casual speech, fragments, names/time/place and context disambiguation. Both models received the same fixtures, without scoring notes or Korean answer examples. Commercial transcripts and the complete private corpus are not published.

The harness called the actual production `translationPayload` C++ builder and sent its serialized JSON bytes unchanged. Hy-MT2 retained the current background/source framing, bounded raw-original context, temperature .7, top_p .6, top_k 20, repeat_penalty 1.05, seed 42, 512-token limit and prompt caching. Qwen retained the current `LegacyQwenJson` prompts, JSON schema, hard `enable_thinking:false`, 256-token limit and caching. Only its candidate sampling changed to the [official non-thinking recommendation](https://huggingface.co/Qwen/Qwen3-8B-GGUF/raw/7c41481f57cb95916b40956ab2f0b139b296d974/README.md): temperature .7, top_p .8, top_k 20, min_p 0 and presence_penalty 1.5, with seed 42 fixed for reproducibility. This was not merely a `/no_think` prompt suffix, and the shipped fast profile's decoding was not changed.

Sequential llama.cpp b12125 (`7d516e25ead4653169236b3f90d13f7bd8629724`) CUDA runs used the same Windows 11/RTX 4080 Laptop GPU. All 24 requests returned HTTP 200 and finished with `stop`, without transport or extraction failures. Hy-MT2/Qwen client HTTP medians were 300.631/315.514 ms; nearest-rank p95 values were 578.755/613.216 ms. With twelve samples, that percentile is the maximum. The first loaded-model requests took 376.243/470.886 ms and are included; these are not model-startup measurements. Logs confirmed 33/33 versus 37/37 GPU layers, 4,403.24 versus 4,455.34 MiB GPU model buffers, and 1,024 versus 1,152 MiB FP16 KV buffers at 8192 tokens. These separate buffers are not whole-device GPU use or concurrent ASR/DTW peak measurements.

Qwen sometimes retained casual requests more naturally and shortened a polite waiting request. However, it reversed the prohibition `そこに立つな。` into the affirmative command `그곳에 서 있어.`, changed the embedded negation in `知らないとは言っていない。`, substituted a locking mechanism for a key, and rendered Yamada as `산다`. Those meaning/name regressions outweighed the local wording gains. The candidate was rejected before any held-out inference. No overall quality improvement is claimed.

Hy-MT2 retained the core meanings in this small set but still elevated some casual lines into polite Korean. The application continues to send ASR-recognized original **text** to the local translation model, using available surrounding originals only as background. Refreshing future translations when that context changes and suppressing stale in-flight results are separate processing-correctness fixes. Neither those fixes nor this model comparison guarantees fully natural Korean; ASR errors, missing context and speaker relationships remain separate limitations.
