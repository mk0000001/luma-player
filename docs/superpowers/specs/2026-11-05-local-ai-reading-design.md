# 로컬 AI 가속·한글 독음·화면 개편 / Local AI acceleration, Hangul readings and UI

## 한국어

사용자는 츠키 실물 Blu-ray 메뉴·본편 재생이라는 기존 합격 기준에 더해 일본어 자막 아래 한글 발음 표시, 삼성닷컴을 참고한 UI, Google Noto Sans, NPU 우선 로컬 AI를 요청했다. 사용자는 구현 결정을 위임했으며 GitHub 설명은 한국어 다음 영어로 작성한다.

선택: 기존 C++20/Qt 6 앱을 유지한다. 화면에는 흰색·연회색, 굵은 검은 제목, 둥근 버튼과 넉넉한 여백을 적용하고 재생 영역은 검게 둔다. 삼성의 로고·사진·전용 글꼴은 사용하지 않는다. Noto Sans와 한국어·일본어 CJK 글꼴을 앱 전용으로 로드하고 SIL OFL 원문을 배포한다.

독음은 MeCab/IPADIC로 한자의 발음을 구한 뒤 가나를 한글로 변환한다. 일본어 원문은 그대로 보존한다. 켜기/끄기, SRT 저장을 제공하며 기존 SRT와 자동 생성 자막에 적용한다. 고유명사·신조어의 독음과 발음 표기는 근사치다. 다른 언어 자막에 독음을 붙이지 않는다.

AI는 네이티브 whisper.cpp에 OpenVINO 인코더를 연결한다. 사용 가능한 NPU를 먼저 시도하고 실패하면 GPU, 마지막으로 CPU를 사용한다. 실제로 실행한 백엔드만 표시한다. OpenVINO가 담당하는 부분은 음성 인코더이며 디코더는 CPU일 수 있다. 장치·드라이버를 임의로 바꾸지 않는다. 취소, 세대 번호, 구간 제한, 음성 트랙·MPLS 매칭을 유지한다. 초기 장치 컴파일에는 별도 긴 제한을 적용하고 실패한 가속기는 같은 세션에서 반복하지 않는다.

대안: 단순 가나 치환만으로는 한자를 읽지 못하므로 제외했다. 클라우드 인식은 로컬 실행 요구와 맞지 않는다. NPU 존재만 감지하고 실제로 CPU를 쓰는 구현은 통과로 간주하지 않는다. Qt 오버레이로 관리 자막을 표시하면 앱 글꼴을 확실히 적용하고 VLC 외부 트랙 누적을 피할 수 있다. 영상/메뉴 입력을 가로채지 않는 별도 투명 오버레이로 구현한다.

검증: 가나·한자·조사·장음·촉음·다른 언어 보존, 독음 토글, 가속기 실패·시간 제한·취소·백엔드 표시를 자동 검증한다. 실물 디스크 메뉴/챕터/본편/종료, AV1 재생, 실제 음성 인식, 휴대 배포 파일과 CI를 다시 확인한다. PotPlayer 전체 기능, 모든 DRM, 인증 Dolby Vision 출력은 기존 공개 로드맵으로 유지하며 완료라고 표기하지 않는다.

## English

The user added Hangul phonetic readings below Japanese captions, a Samsung.com-inspired UI, Google Noto Sans, and NPU-first local AI to the existing physical TUKI Blu-ray menu and main-title acceptance criteria. Implementation choices are delegated; public GitHub descriptions must place Korean before English.

Keep C++20/Qt 6. Use white/light-gray surfaces, bold black headings, rounded buttons and generous spacing, with a black playback surface. Do not copy Samsung logos, imagery or proprietary fonts. Load Noto Sans and Korean/Japanese CJK fonts privately and ship their original SIL OFL notices.

Use MeCab/IPADIC readings for kanji followed by kana-to-Hangul conversion. Preserve Japanese originals. Provide a reading toggle and SRT export for generated captions and existing SRT files; leave other languages alone. Proper names, new words and phonetic spelling remain approximations.

Use a native whisper.cpp worker with an OpenVINO encoder. Try usable NPU hardware first, then GPU, then CPU. Report only the backend actually used. OpenVINO accelerates the audio encoder; the decoder may remain on CPU. Do not change system drivers. Preserve cancellation, generation checks, bounded chunks, selected audio and exact MPLS matching. Allow a longer initial compilation deadline and avoid repeatedly trying a failed accelerator in one session.

Kana-only substitution cannot read kanji; cloud recognition conflicts with local processing. Hardware detection alone is not proof of accelerated inference. Render managed captions in an input-transparent Qt overlay to ensure bundled font use and avoid accumulated VLC external subtitle tracks.

Validate readings, non-Japanese preservation, toggling, accelerator failure/timeouts/cancellation and honest backend reporting. Repeat physical menu/chapter/main-title/shutdown, AV1, real recognition, portable package and CI checks. Keep full PotPlayer parity, universal DRM and certified Dolby Vision output on the documented roadmap rather than claiming completion.
