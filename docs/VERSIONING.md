# 버전·공개 정책 / Version and publication policy

## 한국어

- `VERSION`을 실행 파일·진단·패키지·릴리스 버전의 기준으로 사용합니다. 현재 준비 대상은 `0.1.0-alpha.4`, 공개 예정 태그는 `v0.1.0-alpha.4`입니다.
- **배포한 버전**마다 SemVer의 annotated Git 태그, GitHub Release, 소스 스냅샷, Windows 포터블 ZIP, SHA-256, `docs/releases/`의 변경·검증 기록을 보관합니다. 모든 임시 컴파일을 릴리스로 만들지는 않습니다.
- 공개 태그를 이동하거나 이미 공개한 바이너리를 교체하지 않습니다. 수정은 새 버전으로 공개하고 이전 자료를 보존합니다.
- 릴리스 워크플로는 기존 annotated 태그를 지정해 수동 실행하며 초안만 만듭니다. 공개 배포는 실제 포터블 실행 검증 뒤 진행합니다. 태그를 푸는 것만으로 미검증 바이너리를 자동 공개하지 않습니다.
- 수동 워크플로의 `include_cuda` 입력은 기본 `true`입니다. 패키징 스크립트의 `-IncludeCuda`는 `windows-x64-cuda` ZIP에 음성 인식·번역 CUDA를 함께 넣습니다. 스크립트에서 이 옵션을 생략한 `windows-x64` 코어도 유지하며, 두 구성의 파일명·manifest·체크섬을 구분합니다. 번역 모델 가중치는 기본 ZIP에서 분리합니다.
- 작업 커밋과 PR은 개발 이력입니다. PR/CI 통과, 로컬 폴더 생성, 소스 버전 변경만으로 공개 배포 또는 실물 합격이 되지 않습니다. 초기 `alpha.1` 로컬 스냅샷과 공개 릴리스를 혼동하지 않습니다.
- 릴리스 빌드는 태그와 `VERSION`의 일치, 고정 의존성·해시, 자동 검사, 새로 푼 포터블 폴더의 실행, 라이선스·체크섬, 비공개 데이터 제외를 확인한 뒤 공개합니다. 선택 번역 모델을 분리한 코어 ZIP과 모델을 넣은 로컬 폴더의 범위를 구분합니다.
- Windows/Linux CI 결과는 해당 커밋의 컴파일·자동 검사 증거입니다. 실물 디스크·오디오·렌더링·NPU/GPU·캡처 검증은 실제 환경·빌드·관측값으로 따로 기록합니다.
- 릴리스 기록에는 통과, 실패, 미검증을 유지합니다. 새 모델 준비 방식이나 출력 옵션이 구현됐다는 이유만으로 지연·HDR·무손실·캡처 검사를 통과로 바꾸지 않습니다.
- 공개 문서는 한국어 섹션 다음 영어 섹션으로 작성합니다. 키·인증서·상용 영상·음성·메뉴 이미지·생성 자막·개인 경로를 업로드하지 않습니다.

[릴리스 목록](https://github.com/mk0000001/luma-player/releases) · [변경 기록](../CHANGELOG.md) · [alpha.4 검증 상태](releases/0.1.0-alpha.4.md)

## English

`VERSION` controls the executable, diagnostics, package and release version. The next target is `0.1.0-alpha.4`, with intended release tag `v0.1.0-alpha.4`.

Every **distributed version**, rather than every scratch compilation, receives an annotated SemVer tag, GitHub Release, source snapshot, Windows portable ZIP, SHA-256 and a release/validation record. Never move published tags or replace published binaries; corrections require a new version while preserving history.

The release workflow is manually dispatched against an existing annotated tag and creates a draft. Publish only after testing the extracted portable package. Pushing a tag alone does not automatically expose unverified binaries.

Its `include_cuda` input defaults to `true`. The packaging script's `-IncludeCuda` creates a `windows-x64-cuda` ZIP with both ASR and translation CUDA runtimes. Omitting that script switch preserves the `windows-x64` core. Keep flavor-specific filenames, manifests and checksums distinct. Default ZIPs omit optional translation weights.

Development commits and PRs are not release acceptance. CI success, a local package folder or a version edit alone does not prove publication or physical-disc success. The early local alpha.1 snapshot is distinct from a published immutable release.

Before publication, verify tag/version agreement, pinned hashes, tests, an independently extracted portable folder, license notices, checksums and exclusion of private playback data. Distinguish the core ZIP without the optional translation model from a full-model local installation.

Windows/Linux CI establishes the tested commit's compilation and automated behavior. Hardware playback, audio, rendering, NPU/GPU and capture claims require actual observations tied to the build/environment. Release records retain passed, failed and pending results; implementing an option cannot silently satisfy a hardware or latency gate.

Public prose is Korean first, then English. Do not upload keys, certificates, commercial media/menu images/transcripts or private paths. See the release links above for current publication and validation status.
