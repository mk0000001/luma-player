# 재생 전 전체 자막 준비 / Prepare full captions before playback

## 한국어

사용자는 영상 재생 전에 파일과 Blu-ray의 자막을 미리 만드는 기능을 요청했습니다. 기존 10초 최소·60초 선행 실시간 버퍼와 별개의 선택 작업으로 제공합니다. 기존 자율 진행 지시에 따라 C++/Qt와 현재 로컬 인식·번역 실행기를 재사용합니다.

- 파일 또는 연결된 Blu-ray의 특정 플레이리스트를 선택합니다. 파일은 로컬 FFmpeg의 메타데이터 검사로 길이를 확인하며 재생 엔진을 열지 않습니다. 디스크는 MPLS 길이를 사용하고 기본값은 가장 긴 본편입니다. 오디오 트랙도 선택할 수 있습니다.
- 영상을 재생하지 않고 전체 구간의 원문을 순차 생성합니다. 그다음 확보한 전체 원문에서 대상 앞뒤의 제한된 문맥을 골라 한국어를 번역합니다. 음성을 번역 모델에 직접 전달하지 않습니다.
- 진행률·단계·취소를 표시합니다. 완료 구간을 안전하게 저장하고 같은 작업을 다시 시작하면 검증된 중간 결과를 재사용할 수 있게 합니다. 잘못된/다른 미디어·모델 작업의 결과는 재사용하지 않습니다.
- 원문·한국어 SRT와 다시 불러올 수 있는 로컬 프로젝트를 저장합니다. 독음과 선택한 표시 조합도 내보낼 수 있습니다. 완료 후 준비한 자막으로 재생하며 표시 옵션은 독립적으로 유지합니다.
- Blu-ray는 선택한 디스크·플레이리스트가 일치할 때 준비한 자막을 적용합니다. 메뉴/다른 타이틀에는 본편 자막을 붙이지 않습니다. 생성 중 같은 광학 드라이브를 재생과 경쟁해서 읽지 않습니다.
- 실패·취소가 완료 결과로 표시되지 않게 하고, 늦은 응답은 세대 번호로 차단합니다. 보호 미디어·인식 자막은 로컬에만 저장하며 GitHub에 공개하지 않습니다.

## English

Add an optional full-caption preparation workflow, separate from the existing minimum-ten-second/sixty-second live buffer. Inspect local file metadata without opening a playback engine, or select a physical Blu-ray playlist (longest feature by default) and audio track. Recognize the full original first, then translate target pages using bounded neighboring originals. Provide progress, cancellation, validated checkpoints, local SRT/project exports and playback with independent source/reading/Korean options. Bind Blu-ray results to the matching disc/playlist, avoid competing optical reads, and reject stale callbacks or mismatched cached work. Keep protected content and generated transcripts local.
