# 준비한 디스크 타이틀 선택 / Prepared disc title selection

## 한국어

실물 디스크에서 기본 사전 생성 타이틀은 MPLS 00003(6,296,832ms)이지만 메뉴 ALLPLAY는 MPLS 00002(6,294,288ms)를 엽니다. alpha.8의 엄격한 연결 검사는 올바르게 다른 자막을 거부하지만 준비한 타이틀을 직접 열지 못합니다. libVLC의 메뉴 없는 목록에서는 00003이 native index 0, 00002가 index 1이고 기본 선택은 index 1입니다. 인덱스 0은 이 모드에서 정상 본편입니다.

alpha.9의 ‘준비한 자막으로 재생’은 `:no-bluray-menu`로 열고 실제 native 목록에서 준비한 길이와 유일하게 맞는 타이틀을 선택합니다. native 인덱스와 MPLS 번호는 동일하다고 가정하지 않습니다. 선택이 확인될 때까지 음성·영상을 일시정지하고, 사용자의 일시정지 의도를 유지합니다. 일치하지 않거나 모호하거나 메타데이터를 얻지 못하면 명확한 오류를 표시합니다. 일반 Blu-ray 재생은 기존 메뉴·FirstPlay 경로를 유지합니다. 길이 검사의 허용 오차는 200ms이며 기존 자막 연결의 1,000ms 허용치를 늘리지 않습니다.

직접 재생의 인덱스 0은 탐색·자막·이어서 보기에서 본편으로 취급합니다. 이어보기 기록은 메뉴 모드와 직접 모드를 구분하고 선택적으로 완성 프로젝트 경로를 보관합니다. 기존 기록은 메뉴 모드로 읽습니다. 파일 재생과 기존 메뉴 탐색·수동 일시정지·출력 복구 동작을 보존합니다.

사전 생성에서 가속 장치를 사용할 수 없을 때 사용자가 이유를 볼 수 있게 하고 같은 저장 조건으로 가속 재시도를 제공합니다. 중지된 화면에는 마지막 사용 장치임을 표시합니다. 최초 GPU 전환의 실제 원인은 아직 미확정이므로 고쳤다고 주장하지 않습니다. 원문·번역 내용은 진단 메시지에 추가하지 않습니다.

공개 설명은 한국어 다음 영어입니다. 보호 디스크 자료와 개인 경로는 배포하지 않습니다. alpha.7/8 태그·배포 파일은 교체하지 않습니다. 생성 중인 alpha.8 프로세스·라이브러리·런타임은 그대로 유지하며 alpha.9는 별도 빌드 폴더를 사용합니다. 모델 기본값과 캡처 동의는 변경하지 않습니다.

## English

Physical evidence shows default prepared MPLS 00003 at 6,296,832ms differs from menu ALLPLAY's 00002 at 6,294,288ms. Strict alpha.8 binding correctly rejects it, but cannot select the prepared title. Direct-mode native index0 is 00003 and index1 is 00002; default selection remains1.

Explicit prepared playback must use `:no-bluray-menu`, select the unique actual native duration match within200ms, hold playback until acknowledgment, honor manual pause and fail clearly on ambiguity/missing metadata. Do not equate native indexes with MPLS numbers or weaken the existing1,000ms caption-binding check. Ordinary disc playback retains authored menus/FirstPlay. Direct index0 is content. Resume records distinguish modes, optionally reference completed projects, and retain legacy menu-mode compatibility. File playback and output recovery remain valid.

Expose controlled accelerator fallback reasons and compatible checkpointed retry; label canceled displays as last-used devices. The initiating GPU rejection remains unconfirmed. Preserve published tags/artifacts, private disc data and current alpha.8 runtime/build; build alpha.9 separately. Do not change model defaults or capture consent.
