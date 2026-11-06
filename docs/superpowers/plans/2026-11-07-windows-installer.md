# Windows 설치 계획 / Windows installer plan

> Execute inline using superpowers:executing-plans in the existing isolated worktree.

## 한국어

검증된 alpha.10 CUDA 배포본을 사용자별 설치·시작 메뉴·제거 등록으로 감싼다. 기존 바이너리·릴리스는 보존하고 설치 빌드를 별도로 버전 관리한다. 기본 Turbo 포함, full-v3·Hy-MT2 선택 자동 다운로드. 고정 잠금 파일의 URL·크기·SHA-256으로 설치 엔진에서 무결성을 확인한다. 설정·생성 자막은 포함하거나 제거하지 않는다.

- [ ] 모델 잠금 변조 거부 테스트 실패 확인 후 생성기 구현.
- [ ] Inno Setup 템플릿과 매니페스트 검증 빌드 도구 구현.
- [ ] 테스트·실제 컴파일·격리 설치/제거·모델 다운로드 확인.
- [ ] 한국어/영어 문서·소스·설치 파일을 별도 릴리스로 공개.

## English

Wrap the immutable alpha.10 CUDA package with per-user install, Start Menu and uninstall registration. Bundle Turbo, offer pinned full-v3/Hy-MT2 downloads with SHA-256 enforcement, and preserve user data. Test lock rejection, verify the payload manifest, compile, exercise isolated installation and downloads, then publish separately versioned installer assets.
