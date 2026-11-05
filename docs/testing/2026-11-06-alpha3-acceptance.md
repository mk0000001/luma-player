# alpha.3 검증 기록 / alpha.3 acceptance record

## 한국어

이 기록은 `codex/caption-layout-quality`의 2026-11-06 작업을 다룹니다. 소스 자동 검사, 실제 모델/장치, GUI 관찰, 배포 ZIP 검사를 구분합니다. 보호 디스크의 영상·음성·메뉴 이미지·생성 자막과 키는 공개하지 않습니다. 최종 배포의 정확한 커밋과 해시는 Release 검증 자산 및 `manifest.json`에 기록합니다.

현재 확인한 소스 회귀는 긴 자막 페이지/60초 선행·되감기 중복(코어 21개), 엄격한 외부 자막과 실제 FFmpeg 형식/인코딩 변환(28개), 이어보기 저장(11개), 번역 파서(73개), 번역 서비스의 취소/모델 교체와 설치기입니다. 기본 CTest에서 환경 변수가 없어 건너뛴 네이티브 항목을 장치 통과로 세지 않습니다.

소유 영상의 실제 파일 이어보기, 영상/재생바 창의 키 처리, 입력 컨트롤 보존, 실패한 가져오기/오래된 결과와 Escape 회귀는 7개 검사(초기화/정리 포함), 22.758초에 통과했습니다. 이후 오디오 변경 시 외부 자막 삭제와 기본 자막 선택의 가져오기 취소 누락을 별도 RED로 재현해 보강했습니다.

Windows GUI에서 소유 미리보기 영상의 F/Enter 전체화면, 74px 바, 5초 숨김 전후 영상 위치/크기 유지를 확인했습니다. 보호된 E: TUKI 디스크를 새 영상 창으로 열어 캡처하면 영상만 검정이고 본창·설정·재생바는 보이는 결과를 관측했습니다. 이는 Windows 캡처 제외의 관측이며 인증 DRM/HDCP나 모든 캡처 경로의 차단을 뜻하지 않습니다.

실제 모델 검사는 상용 자막 대신 소유 WAV·직접 작성한 일본어 문장으로 진행했습니다. Whisper Turbo Q5는 RTX CUDA에서 소유 영어 15초를 모델 로딩 포함 3.278초에 처리했습니다. Qwen과 비교한 Hy-MT2-7B 의미·속도·잔여 오류와 최종 서비스 경로는 [번역 검증](../translation-quality-validation.md)에 기록합니다. 한국어 번역 NPU 성공은 주장하지 않습니다.

### 최종 소스·실물 확인

| 검사 | 실제 결과와 해석 |
| --- | --- |
| 통합 CTest | 19/19 통과, 최종 보완 후 123.68초. 기본 opt-in 건너뜀을 실물 통과로 계산하지 않음 |
| 네이티브 UI/실물 묶음 | 8/8, 0 skip, 159.637초. 소유 파일 이어보기·외부 자막의 오디오 변경 유지·GPU 자막 준비/즉시 pause·오디오 선택 확인·TUKI 메뉴/탐색 포함 |
| 소유 파일 GPU 준비 | 최초 2,517ms, 따뜻한 탐색 294ms, 자동 재개 직후 pause 의도 유지 977ms. 40초 소유 파일 전체 구간까지 선행 확인 |
| TUKI First Play | 자연 시계가 20,020ms까지 진행 후 메뉴 도달 30,561ms. 자동자막 유지, 시작 안내에서 hold 없음 |
| TUKI 장거리 탐색 | 실제 708,461ms에서 GPU 자막 13,905ms 준비까지 49,942ms. 수동 pause 유지. 이후 선행 73,905ms 확보까지 총 75,776ms. 재개 8초 동안 추가 hold/재시도 없이 시계 진행 |
| 실제 앱 모델 설정 | GUI에서 Turbo Q5와 Hy-MT2-7B를 선택하고 번역 GPU를 CUDA0/RTX4080Laptop으로 지정. 소유 SRT 원문·한글 독음·한국어 해석 세 줄 표시, 번역 실패 없음 관측 |
| 마지막 화면 보완 | CUA에서 발견한 다음 페이지 첫머리 문장부호와 인식 언어 변경 시 외부 자막 삭제를 각각 RED로 재현한 뒤 수정. 코어는 22개 검사로 증가 |
| 설치·패키징 사전 검사 | 패키징 18/18. 모델 설치기 11개, 번역 CUDA 설치기 PS7/PS5.1 각각 11개, ASR CUDA 설치기 5개. 제한 PATH의 CUDA 실제 장치 검색과 자체 ASR 실행 별도 확인 |

첫 GitHub CI의 Windows 전체 빌드/런타임 검사는 통과했습니다. Linux는 컴파일과 18개 모듈이 통과했고, Windows 절대 경로를 가정한 테스트 fixture 하나가 실패했습니다. 해당 fixture를 임시 절대 경로로 수정하고 다시 검사합니다. 최종 커밋의 양쪽 CI와 새 ZIP의 추출·전체 해시·제한 PATH 검사는 공개 Release 검증 자산에 기록합니다. 이 문서만으로 최종 ZIP의 합격을 주장하지 않습니다.

일부 종료 시점 FFmpeg/H.264 버퍼 반환 로그가 남으며, 장거리 탐색 속도는 개선 완료로 표시하지 않습니다. 일반 재생에서 지연 프레임이 전혀 없다는 뜻도 아닙니다. [후속 이슈 #7](https://github.com/mk0000001/luma-player/issues/7)에서 광학 읽기와 재생 부하를 계속 구분합니다.

## English

This record covers the November 6 work on `codex/caption-layout-quality`, separating automated source checks, real models/devices, GUI observations and portable-package acceptance. Protected-disc media, menu assets, generated transcripts and keys are not published. The Release validation asset and `manifest.json` identify the exact distributed commit and hashes.

Verified regressions include timed pages/sixty-second coverage/cached-range reuse (21 core checks), strict subtitle import and actual FFmpeg formats/encodings (28), resume persistence (11), translation parsing (73), service cancellation/model changes and installers. Environment-gated native skips are not counted as hardware passes.

Seven owned-file UI checks including setup/cleanup passed in 22.758 seconds: resume, video/transport keys, input preservation, failed/stale imports and Escape. Audio-switch caption deletion and native-subtitle selection failing to cancel import were subsequently reproduced as separate RED regressions.

Native GUI observations confirmed F/Enter fullscreen, a 74px bar, and unchanged video geometry across five-second hiding. Opening the protected E: TUKI disc showed black video capture with visible main UI/settings/transport. This is a bounded Windows capture-affinity observation, not DRM/HDCP certification or universal capture prevention.

Actual model tests used owned WAV and authored Japanese, not commercial transcripts. Whisper Turbo Q5 processed fifteen seconds of owned English on RTX CUDA in 3.278 seconds including model loading. Hy-MT2-7B semantic/speed evidence and residual errors are in the [translation record](../translation-quality-validation.md). No Korean-translation NPU success is claimed.

### Final source and physical checks

| Check | Observation and interpretation |
| --- | --- |
| Integrated CTest | 19/19, 123.68s after follow-up fixes; opt-in skips are not hardware passes |
| Native UI/physical bundle | 8/8, zero skips, 159.637s, covering owned-file resume, retained external captions across audio switches, GPU readiness/pause intent, acknowledged audio selection and TUKI navigation/seeking |
| Owned-file GPU readiness | First 2,517ms, warm seek294ms, immediate manual pause after auto-resume977ms; prefetch reached the full forty-second owned fixture |
| TUKI First Play | Natural clock reached20,020ms and then menu at30,561ms; AI stayed enabled with no First Play hold |
| TUKI long seek | GPU caption coverage13,905ms at actual708,461ms after49,942ms. Manual pause preserved;73,905ms ahead after75,776ms total, followed by eight seconds of resumed clock progress without another hold/retry |
| Actual app settings | Selected TurboQ5/Hy-MT2-7B and CUDA0/RTX4080Laptop translation via GUI; observed original, Hangul reading and Korean translation together on owned SRT without translation failure |
| Final display fixes | Reproduced/fixed leading punctuation on a new page and external-caption loss on ASR language changes; core coverage increased to22 checks |
| Installer/package preflight | Packaging18/18, model installer11, translation CUDA11 each on PS7/PS5.1, ASR CUDA5; separate real CUDA inventory/ASR proofs with restricted PATH |

The first GitHub run passed the complete Windows build/runtime job. Linux compiled and passed eighteen modules, with one test fixture incorrectly assuming a Windows absolute path. The fixture now uses a temporary absolute path. Exact final-commit CI and extracted-ZIP/full-hash/restricted-PATH acceptance are recorded in the published Release validation asset. This document alone does not claim the final ZIP passed.

Some FFmpeg/H.264 buffer-return messages remain around shutdown. Long optical seeking is not reported as solved, nor is playback claimed to have zero late frames. [Issue#7](https://github.com/mk0000001/luma-player/issues/7) tracks optical reads separately from playback load.
