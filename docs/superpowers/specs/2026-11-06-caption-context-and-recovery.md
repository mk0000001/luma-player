# 문맥 번역과 영상 출력 복구 / Context translation and video-output recovery

## 한국어

사용자가 요청한 한국어 번역은 현재 구간과 같은 세션에 이미 준비된 앞뒤 원문 자막을 참고한다. 표시용으로 나눈 현재 구간만 번역하고, 원문·독음·번역을 섞어 다시 입력하지 않는다. 앞뒤 60초 범위에서 가까운 문맥을 최대 24개와 제한된 UTF-8 크기로 선택한다. 대기열에 넣은 시점이 아니라 실제 추론을 시작할 때 최신 버퍼를 읽는다. 같은 문장도 시간 구간이 다르면 별도 번역이며, 탐색·모델·장치·세션 변경 뒤 오래된 결과는 표시하지 않는다.

실제 로그의 `SwapChain Present failed (hr=0x887A0005)`는 디코더 카운터가 증가해도 영상 출력이 실패할 수 있음을 보여 준다. 검증된 장치 손실 로그만 세션당 한 번 통지하고, UI는 사용자 재생 의도를 유지하면서 네이티브 출력 인스턴스를 한 번 다시 만든다. 본편 타이틀·위치·속도·음량을 복원하며 반복 재시작은 하지 않는다. 캡처 설정 전환만을 장치 손실 원인으로 단정하지 않는다.

개발자 모드를 켤 때 이용 확인을 받는다. 보호 디스크의 영상과 자막에는 기본 캡처 제외를 적용한다.

검증: 합성 원문을 이용한 문맥·출력 범위·취소·캐시 분리 테스트, 개발자 모드 확인과 기본 캡처 제외 UI 테스트, 실제 native callback 경계의 오류·중복·이전 세션 테스트, 소유한 영상의 위치·일시정지 복구와 연결된 디스크의 재실행 확인. 보호 콘텐츠 자체는 공개하지 않는다.

## English

Translate the timed target page using bounded, already-buffered original cues from the same session, up to 60 seconds on each side and 24 nearby cues. Read the latest context when inference starts. Keep target output separate from reference context and isolate repeated phrases by timing; reject stale results after session, seek, model or device changes.

Verified DXGI device-removal/reset Present errors notify once per session. Recreate native video output once while preserving title, position, playback intent, rate and volume; do not infer a capture-toggle cause or loop on failures.

Request acknowledgment when enabling developer mode. Apply default capture exclusion to protected-disc video and captions.

Validate synthetic-context/cancellation/cache behavior, the developer-mode acknowledgment and default capture exclusion, native callback/session isolation, owned-video recovery and a restart of the connected disc. Do not publish protected content.
