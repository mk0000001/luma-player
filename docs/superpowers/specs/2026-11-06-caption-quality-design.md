# 가사 시간 정렬과 원문 번역 / Lyric timing and text translation

## 한국어

요구: 가사가 먼저 나타나거나 먼저 사라지는 문제와 원문 인식·한국어 어투를 개선한다. 일본어 인식 결과 텍스트를 번역 모델에 보내고, 이미 확보한 앞뒤 원문을 활용한다. 기존 C++/Qt, Windows 우선·Linux 호환, 로컬 추론과 버전별 GitHub 배포를 유지한다. 사용자는 자율 진행을 위임했다.

구현 선택: 기존 Kotoba 속도 경로를 보존하고 선택 모델 full Whisper large-v3 Q5를 정확도 후보로 추가한다. full-v3에는 공식 DTW head와 beam 5를 적용한다. DTW는 token 시점을 별도 출력하므로 segment SRT에 옵션만 켜는 방법으로 처리하지 않는다. 원문 토큰의 시간 정보를 페이지 나누기까지 전달한다. 외부 SRT와 정렬 정보가 없는 결과는 기존 호환 경로를 유지한다. 일정한 지연 조절만으로 문장마다 다른 오차를 해결했다고 보지 않는다.

번역은 계속 원문 텍스트만 입력한다. 뒤 원문이 들어오면 아직 표시되지 않은 완료 자막을 유효 문맥 fingerprint에 따라 갱신하며, 표시 중이거나 지난 완료 자막은 바꾸지 않는다. 추론 도중 문맥이 달라지면 낡은 결과를 공개하지 않고 새 문맥으로 처리한다. 일반 번역 지시를 추가해도 자동으로 개선이라고 보지 않고 직접 작성한 대조 문장으로 의미·부정·요청·말투를 비교한다.

계약: native `.timings.json`은 schema 1, `format=luma-asr-timing-v1`, `alignment=dtw|token`, `audioMs`, `segments`를 가진다. 각 segment는 `startMs`, `endMs`, UTF-8 `text`, `tokens`를 가지며 token은 UTF-8 `byteStart`(포함), `byteEnd`(제외), WAV 기준 `startMs`, `endMs`를 가진다. UTF-8 중간에서 나누지 않고 유효 범위·단조 시간을 검증한다. token 끝은 다음 token 시점/segment 끝을 이용한 구간이며 DTW가 정확한 발성 종료를 직접 측정한다는 의미는 아니다. Qt의 `SubtitleTokenTiming{textStart,textLength,startMs,endMs}`는 UTF-16 범위와 미디어 기준 시간을 사용한다.

제약: NPU에 잘못된 full/turbo head를 Kotoba 2층 decoder에 적용하지 않는다. 기존 Kotoba NPU 검사 정책은 그대로이며 실행 파일이 바뀐 profile은 유효하지 않다. 정확도 모델 가중치는 ZIP 크기 제한 때문에 선택 설치한다. 보호된 디스크의 키·음성·가사·화면은 공개하지 않고 로컬 검증에만 사용한다. 모든 품질·속도 주장은 실제 관측 범위를 적는다. 기존 alpha.6 태그·ZIP은 변경하지 않는다.

## English

Improve early/short lyric presentation and ASR/Korean wording while retaining the native local Windows/Linux architecture. Preserve Kotoba; evaluate optional full large-v3 Q5 with official DTW and beam 5. Export genuine token timing and preserve it through presentation paging; retain SRT compatibility. DTW token points do not directly measure vocal end times.

Translation continues to receive recognized original text only. Refresh future completed cues when their bounded original context changes, freeze displayed/past completed cues, and suppress stale in-flight context results. Measure semantic prompt candidates rather than claiming generic instruction changes improve quality. The timing schema and Qt token contract above define the producer/consumer boundary. Preserve NPU/model compatibility, immutable prior releases and private protected-media evidence.
