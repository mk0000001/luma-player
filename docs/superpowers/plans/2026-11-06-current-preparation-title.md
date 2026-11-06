# 현재 타이틀로 사전 생성 / Prepare the current title

## 한국어

실물 디스크의 가장 긴 MPLS 00003은 코멘터리 선택판이며 ALL PLAY는 00002입니다. alpha.9의 길이·타이틀 연결은 정확하지만 생성 창이 현재 재생하던 타이틀을 버리고 가장 긴 타이틀로 초기화했습니다. alpha.10은 현재 재생하던 디스크·유일한 플레이리스트·오디오 순서를 정지 전에 전달합니다. 디스크가 같고 유일한 길이 일치와 입력 범위가 확인될 때만 사용합니다. 불완전·메뉴·다른 소스의 정보는 적용하지 않습니다.

재생 정보가 없으면 기존 가장 긴 타이틀 선택을 유지하되 이를 본편으로 단정하지 않고 디스크 메뉴와 대조하도록 안내합니다. 현재 입력·수동 선택, 프로젝트 형식, 완성 프로젝트 연결, 모델·생성 정책은 유지합니다. alpha.9 이하 소스·태그·배포물을 교체하지 않으며 별도 빌드 폴더를 사용합니다.

1. 현재 선택 00002와 더 긴 00003의 합성 회귀 실패를 확인합니다. 다른 디스크·없는 타이틀·모호한 길이·무효 오디오를 검사합니다.
2. 정지 전 현재 정보 전달과 대화상자의 엄격한 선호 선택, 기본 안내를 최소 수정합니다.
3. 영향 검사·전체 검사·독립 검토와 최종 포터블 실행을 확인하고 alpha.10 소스·태그·배포 기록을 남깁니다. 보호 자료·개인 경로는 제외합니다.

## English

The longest observed playlist 00003 is commentary; ALL PLAY is 00002. Alpha.9 binds the selected title correctly but its preparation dialog discards the playing selection and chooses the longest. Alpha.10 passes current disc/unique playlist/audio ordinal before stopping, accepting only same-source, unambiguous duration and bounded input. Incomplete/navigation/other-source state is not used. Without usable playback state, retain longest-title fallback while explicitly avoiding a main-feature assumption. Preserve schema, models, manual selection and earlier tags/assets. Reproduce synthetic preferred-title failures and invalid/ambiguous cases, make the minimal change, independently review and verify full/native/portable behavior before a separate release. Private disc material remains local.
