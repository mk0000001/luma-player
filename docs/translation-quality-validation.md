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

## English

The quality profile uses Tencent's official [Hy-MT2-7B Q4_K_M](https://huggingface.co/tencent/Hy-MT2-7B-GGUF/tree/ab8472660ac61fac25f1af43fac2599d52a8a775), under the pinned [Apache-2.0 license](https://huggingface.co/tencent/Hy-MT2-7B-GGUF/blob/ab8472660ac61fac25f1af43fac2599d52a8a775/LICENSE.txt). Its original license text is preserved. The table above pins the immutable revision, 4,624,648,896-byte model and hashes. Japanese and Korean are listed in the official model card. The restricted HY-MT1.5 license is a different license and is not used for this profile.

Run `tools/install-translation.ps1 -Profile quality` in an extracted player package to install only the optional 4.62 GB model. Existing default `-Profile fast` behavior remains. File size and SHA-256 are checked before publishing the download. Keep the exact model filename and select it in the player to enable its dedicated plain-response protocol. No Python, cloud inference or API key is required. Developer bootstrap and staging accept `-ModelProfile fast|quality|all`; model-free core packages can continue to use `-SkipModel`.

All examples were written for this test, without commercial media. Native llama.cpp b12125/CUDA 12.4 logs confirmed CUDA0, the RTX 4080 Laptop GPU and 33/33 layers offloaded. The model buffer was approximately 4,403 MiB. NPU inference for this new model is not claimed. The official one-user-message translation prompt was fixed throughout, without answer examples or context. Parameters were temperature .7, top_p .6, top_k 20, repeat_penalty 1.05, seed 42 and a 512-token output limit.

The 16 development examples covered negation, permission/prohibition, intention/requests, fragments, names/time/place, train alighting and musical invitations. Twelve separate held-out examples tested five negations, four intentions/requests/invitations, two fragments and one name/weekday/time. Review compared meaning and omission boundaries, rather than exact reference wording. All 28 outputs ended normally, with no polarity reversals or invented events in fragments. One development sentence lost the explicit return implication of Japanese `行ってくる`, so it was marked partial. All twelve held-out meanings were retained; one had the malformed Korean conjugation `끄어줄래` instead of `꺼 줄래`. The train output also repeated a location particle. This small corpus establishes a measured improvement over the old Qwen3-4B outputs shown above, not universal translation accuracy. ASR errors and missing context remain limitations.

7B warm latency was 240.5 ms median / 433 ms p95 on the development set and 280 ms / 442 ms on held-out text. Cold model startup was approximately 4.2 seconds. The smaller 1.8B Q8 candidate was faster but had larger conjugation and fragment-completion errors and was not promoted. Final native TranslationService verification measured 5,377 ms for the first weather caption, 442 ms for a warm library caption and 0 ms for a duplicate cache hit. It confirmed the actual CUDA backend and suppressed the canceled response.

New tests were observed failing before implementation. The parser suite passed 73 cases, and all three dedicated translation CTest targets passed. The ordinary native CTest is opt-in and skips without its environment; the CUDA proof above was run separately. Service fixtures cover invalid-cue continuation, unchanged source identifiers, cache invalidation and cancellation across an in-flight model change. Eleven installer checks passed, including corrupt/truncated publication protection, profile selection and real-model reuse through packaged Windows PowerShell 5.1 execution. Real models were hard-linked for reuse tests and downloaded zero bytes.
