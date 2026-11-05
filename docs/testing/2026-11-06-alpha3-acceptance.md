# alpha.3 검증 기록 / alpha.3 acceptance record

## 한국어

이 기록은 `codex/caption-layout-quality`의 2026-11-06 작업을 다룹니다. 소스 자동 검사, 실제 모델/장치, GUI 관찰, 배포 ZIP 검사를 구분합니다. 보호 디스크의 영상·음성·메뉴 이미지·생성 자막과 키는 공개하지 않습니다. 최종 배포의 정확한 커밋과 해시는 Release 검증 자산 및 `manifest.json`에 기록합니다.

현재 확인한 소스 회귀는 긴 자막 페이지/60초 선행·되감기 중복(코어 21개), 엄격한 외부 자막과 실제 FFmpeg 형식/인코딩 변환(28개), 이어보기 저장(11개), 번역 파서(73개), 번역 서비스의 취소/모델 교체와 설치기입니다. 기본 CTest에서 환경 변수가 없어 건너뛴 네이티브 항목을 장치 통과로 세지 않습니다.

소유 영상의 실제 파일 이어보기, 영상/재생바 창의 키 처리, 입력 컨트롤 보존, 실패한 가져오기/오래된 결과와 Escape 회귀는 7개 검사(초기화/정리 포함), 22.758초에 통과했습니다. 이후 오디오 변경 시 외부 자막 삭제와 기본 자막 선택의 가져오기 취소 누락을 별도 RED로 재현해 보강했습니다.

Windows GUI에서 소유 미리보기 영상의 F/Enter 전체화면, 74px 바, 5초 숨김 전후 영상 위치/크기 유지를 확인했습니다. 보호된 E: TUKI 디스크를 새 영상 창으로 열어 캡처하면 영상만 검정이고 본창·설정·재생바는 보이는 결과를 관측했습니다. 이는 Windows 캡처 제외의 관측이며 인증 DRM/HDCP나 모든 캡처 경로의 차단을 뜻하지 않습니다.

실제 모델 검사는 상용 자막 대신 소유 WAV·직접 작성한 일본어 문장으로 진행했습니다. Whisper Turbo Q5는 RTX CUDA에서 소유 영어 15초를 모델 로딩 포함 3.278초에 처리했습니다. Qwen과 비교한 Hy-MT2-7B 의미·속도·잔여 오류와 최종 서비스 경로는 [번역 검증](../translation-quality-validation.md)에 기록합니다. 한국어 번역 NPU 성공은 주장하지 않습니다.

이 문서의 이후 확정 결과는 아래에 추가합니다. 이 중간 기록 자체가 최종 소스 CI나 ZIP 검증 완료를 뜻하지 않습니다.

## English

This record covers the November 6 work on `codex/caption-layout-quality`, separating automated source checks, real models/devices, GUI observations and portable-package acceptance. Protected-disc media, menu assets, generated transcripts and keys are not published. The Release validation asset and `manifest.json` identify the exact distributed commit and hashes.

Verified regressions include timed pages/sixty-second coverage/cached-range reuse (21 core checks), strict subtitle import and actual FFmpeg formats/encodings (28), resume persistence (11), translation parsing (73), service cancellation/model changes and installers. Environment-gated native skips are not counted as hardware passes.

Seven owned-file UI checks including setup/cleanup passed in 22.758 seconds: resume, video/transport keys, input preservation, failed/stale imports and Escape. Audio-switch caption deletion and native-subtitle selection failing to cancel import were subsequently reproduced as separate RED regressions.

Native GUI observations confirmed F/Enter fullscreen, a 74px bar, and unchanged video geometry across five-second hiding. Opening the protected E: TUKI disc showed black video capture with visible main UI/settings/transport. This is a bounded Windows capture-affinity observation, not DRM/HDCP certification or universal capture prevention.

Actual model tests used owned WAV and authored Japanese, not commercial transcripts. Whisper Turbo Q5 processed fifteen seconds of owned English on RTX CUDA in 3.278 seconds including model loading. Hy-MT2-7B semantic/speed evidence and residual errors are in the [translation record](../translation-quality-validation.md). No Korean-translation NPU success is claimed.

Final results will be appended below. This interim record alone does not establish final source CI or ZIP acceptance.
