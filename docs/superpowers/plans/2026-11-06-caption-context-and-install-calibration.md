# 문맥 자막·출력 복구·설치 검사 / Caption context, recovery and installation calibration

## 한국어

설계: [문맥과 복구](../specs/2026-11-06-caption-context-and-recovery.md). 기존 Kotoba NPU 계획을 이어서 진행하며, 사용자 요청에 따라 설치 때 실제 장치를 측정해 자동 가속을 결정한다. 명목 INT8 TOPS를 FP16/FP32 TFLOPS나 실제 모델 지연으로 해석하지 않는다.

- [x] 최신 원문 버퍼를 추론 시작 때 읽고, 대상 구간과 문맥을 분리한다. 문맥·취소·캐시·반복 문장·출력 검증의 RED→GREEN을 확인한다.
- [x] 실제 HyMT2 CUDA 시험에서 문맥 JSON 전체를 번역하는 문제를 재현하고, 공식 배경/원문 구분 프롬프트로 수정한다. 합성 문장 두 종류와 기존 단일 원문 경로를 검증한다.
- [x] 개발자 모드 이용 확인과 보호 디스크의 기본 캡처 제외를 검증한다.
- [x] 검증된 DXGI 장치 손실을 세션당 한 번 알리고 네이티브 출력을 한 번 다시 만든다. 소유 영상으로 중간/끝부분·재생/일시정지·복구 중 입력·새 미디어로의 이전 콜백 차단을 검증한다.
- [ ] 연결된 디스크의 초기 안내와 허용된 메뉴 동작을 거쳐 타이틀·위치·불러온 자막·일시정지를 복원한다. 디스크가 금지한 타이틀 탐색을 우회하지 않는다.
- [ ] 설치기에서 소유한 합성 16초 PCM으로 NPU worker를 연속 네 번 실행한다. 실제 NPU·양수 인코더 호출·유효 시간·후속 세 번의 전체/왕복 지연을 검사한다. 모델·IR·worker·런타임·드라이버에 묶인 결과를 원자적으로 저장한다.
- [ ] 유효한 검사 결과가 있고 세 번의 최대 지연이 3초 이내인 Kotoba만 자동 NPU를 사용한다. 미검사·변경·느림·오류는 GPU, 다음 CPU를 사용한다. 명시적 NPU 선택과 기존 base 모델의 동작은 구분한다.
- [ ] 격리한 OpenVINO 2026.4.1의 실제 반복 추론을 확인하고 배포 런타임 변경 여부를 결정한다. 합성 입력은 음성 인식 정확도 검증을 대신하지 않는다.
- [ ] 전체 관련 테스트·독립 코드 재검토·Windows/Linux CI를 완료하고 새 불변 패키지를 독립 추출해 실행한다. 새 동의 절차와 현재 위치에서의 재실행을 확인한 뒤 GitHub에 소스·한국어/영어 설명·체크섬·검증 범위를 공개한다.

## English

Continue the existing Kotoba plan with latest-buffer target-only translation, developer-mode acknowledgment and default capture exclusion, one bounded native-output recovery, and measured installation-time NPU selection. Do not equate advertised INT8 TOPS with FP16/FP32 TFLOPS or model latency.

Completed checks above cover context/cancellation/cache isolation, the real HyMT2 JSON-echo regression and official background/source prompt correction, the developer-mode acknowledgment and capture exclusion, and owned-video recovery cases. Remaining gates are the connected disc's permitted navigation and full pause/caption restoration, four-request synthetic NPU calibration bound to all relevant identities, conservative automatic selection, an isolated modern-runtime inference assessment, full tests/review/CI, immutable packaging and an observed restart. Synthetic audio is a hardware-path timing probe, not speech-quality evidence. Public artifacts exclude protected-disc content.
