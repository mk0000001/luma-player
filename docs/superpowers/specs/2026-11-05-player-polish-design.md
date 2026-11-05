# 재생 안정화와 플레이어 개편 / Playback reliability and player refinement

## 한국어

우선순위는 현재 츠키 Blu-ray에서 마우스로 ALL PLAY를 누른 뒤 20초에 검은 화면으로 멈추는 문제다. 기존 창은 본편 타이틀 1·길이6294288ms로 바뀌었지만 오디오 트랙이 사라지고 시계가 정지했다. 새 클린 빌드에서는 같은 클릭으로 7분 이상 본편 영상·음성이 진행됐으며, 탐색 중 GPU 디코더의 영상 버퍼 할당 오류도 관측했다. 단정하지 않고 동일 조건과 반복 전환으로 비교한다. 성공 판정은 타이틀 변경 후 새 영상·오디오 카운터 증가, 실제 시간 진행, 오디오 트랙 유지다. 인트로 누적 수치만으로 통과시키지 않는다.

사용자는 다음을 추가로 요청했다. 기본 다크 모드와 라이트 모드 전환, 직접 디자인한 컨트롤(기본 콤보 상자 모양 제거), 낮은 전체화면 하단 바, 마우스가 5초 정지하면 바·커서 숨김, 원문·한글 독음·한국어 번역 병행, 빠른 자막 생성, 가속에 사용할 GPU 선택, 독립 로고, 캡처 차단, HDR/Dolby Vision·무손실 음원 지원 확인. 이전의 삼성닷컴 참고 디자인과 Google Noto Sans 요구는 유지한다. 작업은 위임됐고 다시 설계 승인을 묻지 않는다.

화면: 검정·흰색 중심의 삼성닷컴 참고 레이아웃을 독립적으로 작성하고 파란 강조색을 쓴다. SVG 로고와 자체 아이콘·버튼을 쓰고 Noto Sans를 앱 전용으로 로드한다. 전체화면의 하단 바는 약64~76 논리 픽셀, 5초 유휴 시 숨김, 움직임·호버·키보드 조작 시 표시, 편집 메뉴·드래그 중에는 유지한다. 원문·독음·번역은 별도 데이터로 보존해 토글과 내보내기가 원문을 손상시키지 않는다.

AI: 음성 인식은 가능한 NPU를 먼저 시도하며 GPU와 CPU로 대체한다. 수동 모드와 GPU 선택을 제공하고 실제 사용 백엔드를 표시한다. 짧은 구간·우선순위·추출 및 모델 재사용으로 지연을 줄이고 구간 취소·재시도·정확한 MPLS와 오디오 선택은 유지한다. 한국어 번역은 재배포·상업 이용 조건을 확인한 로컬 모델과 네이티브 런타임으로 처리한다. 모델별 지원 장치를 구분하고 NPU를 지원하지 않는 번역 경로를 NPU라고 표시하지 않는다. 클라우드 API나 Python 런타임은 배포하지 않는다.

보호: AACS 디스크 읽기, Windows 화면 캡처 차단, 인증 DRM/HDCP 출력은 별개로 기록한다. 지원되는 Windows에서 보호 디스크 재생 시 캡처 차단을 기본 적용하고 자막의 별도 창에도 적용한다. 실제 API 결과를 표시하며 이를 DRM 인증이나 모든 캡처 경로의 완전 차단이라고 부르지 않는다. 검증 이미지에는 상용 디스크 화면·가사·자막을 포함하지 않는다.

영상·음성: 실제 디스플레이의 HDR 지원과 활성 상태를 조사하고 지원되는 파일 경로에 HDR 패스스루 또는 SDR 톤 매핑을 적용한다. Dolby Vision 메타데이터 처리와 인증 Dolby Vision 출력은 구분한다. 무손실 PCM/LPCM·FLAC·ALAC 재생을 검증하고 공유 모드 출력과 비트 퍼펙트/독점 출력의 차이를 명시한다. 확인하지 않은 기술을 지원 완료로 표시하지 않는다.

독립 작업 경계: UI 담당은 `src/ui`, `assets`, UI 테스트를 소유한다. 번역 담당은 새 번역 모듈과 전용 의존성 잠금·스크립트·테스트를 소유한다. 주 담당은 재생·플랫폼 기능·음성 인식·공통 빌드/배포·통합을 소유한다. CMake와 공통 의존성 잠금은 주 담당만 수정한다. GitHub 자료는 한국어 다음 영어로 공개하며 키·인증서·상용 콘텐츠는 배포하지 않는다.

## English

The first priority is the physical TUKI Blu-ray freezing on a black picture at 20 seconds after clicking ALL PLAY. The old process reached title1/duration6294288ms but lost audio tracks and stopped advancing. A fresh clean build advanced picture and audio beyond seven minutes with the same mouse action. GPU decoder surface-allocation failures were also observed during seeking. Compare conditions instead of assuming one cause. Require fresh per-title video/audio counter increases, advancing time and retained audio tracks; cumulative intro counters cannot prove main-title playback.

Additional requirements: default dark and optional light themes, custom controls, a compact fullscreen bar that hides with the cursor after five seconds of mouse inactivity, Japanese original/Hangul reading/Korean translation together, faster captions, GPU selection, an independent logo, capture protection, and verified HDR/Dolby Vision/lossless support. Retain the Samsung.com-inspired styling and private Noto Sans fonts. Implementation decisions are delegated.

Use independent black/white styling with blue accents and SVG branding. Fullscreen transport should be about64–76 logical pixels and remain visible during menus, dragging and keyboard interaction. Preserve original, phonetic and translated text separately for toggles and export.

Try usable NPU speech inference first, then the selected GPU, then CPU; also provide manual selection and honest backend status. Improve latency through shorter/prioritized chunks and extraction/model reuse without weakening cancellation, retry, exact MPLS or audio selection. Use a redistributable local translation model and native runtime, with commercial-use terms checked. Report acceleration per model. Ship neither Python nor cloud API requirements.

Treat AACS reading, Windows capture exclusion and certified DRM/HDCP as separate capabilities. Default to supported Windows capture exclusion for protected discs, including the separate caption window. Report actual API success, not DRM certification or an absolute guarantee against every capture method. Use only owned/generated content in published UI validation screenshots.

Inspect actual display HDR capabilities/state. Configure supported file rendering for HDR passthrough or SDR tone mapping. Distinguish Dolby Vision metadata handling from certified Dolby Vision output. Validate PCM/LPCM, FLAC and ALAC playback; distinguish shared-mode output from bit-perfect/exclusive output. Never mark untested technology complete.

Ownership: UI worker owns `src/ui`, `assets` and UI tests; translation worker owns new translation modules, its dedicated dependency lock/scripts/tests; primary owns media/platform, recognition, common build/packaging and integration. Only primary edits CMake/common lock. Public descriptions are Korean first, then English; no keys, certificates or commercial content in distribution.
