# 재생 전 자막 준비 계획 / Full caption preparation plan

## 한국어

1. 원문 전체 생성 → 전체 원문 문맥 번역의 별도 controller와 bounded 프로젝트/checkpoint 저장. 합성 fixture로 구간 순서·시간·진행·취소·캐시 정체성·번역 완료 조건을 먼저 실패시켜 검증.
2. 로컬 파일 길이/오디오 정보 검사와 파일/Blu-ray 선택·진행·내보내기 dialog. 재생 엔진을 열지 않고 준비하며 기존 다크 UI를 사용. Qt controller와 dialog를 통합하고 미리 생성한 원문/독음/번역을 올바른 소스·타이틀에 적용.
3. 전체 Qt·native·설치/패키지 검사, 독립 코드 검토, 소유 짧은 파일 및 연결 디스크의 실제 생성/재생 검사. alpha.7 불변 배포는 별도 작업으로 마무리.
4. alpha.8 소스·한/영 문서·PR·불변 CUDA 패키지·체크섬과 검증 자산 공개. GitHub Actions가 실행되지 않으면 그 상태를 명시하며 검사를 통과했다고 기록하지 않음.

## English

Implement the independent original-first/translation-second controller and validated local project/checkpoint persistence with regression fixtures. Add a no-playback metadata probe and styled file/Blu-ray preparation dialog, then bind prepared results to the correct source/title without duplicate ASR or translation. Run complete local checks, independent review and real owned-file/disc preparation; preserve alpha.7 and publish alpha.8 with immutable source-matched artifacts and honest CI status.
