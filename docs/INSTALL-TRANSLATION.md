# 한국어 번역 모델 설치 / Korean translation model installation

## 한국어

포터블 코어에는 번역 실행기가 들어 있고, 선택 번역 모델은 별도로 설치한다. 모델 한 개의 다운로드 크기는 **2,497,280,256바이트, 약 2.50 GB(2.33 GiB)**이다. 설치할 폴더에 약 2.56 GB의 빈 공간과 쓰기 권한이 필요하다. 설치할 때 인터넷을 사용하며, 완료 후에는 오프라인으로 번역할 수 있다. 계정·API 키·Python·추가 SDK가 필요하지 않다.

압축을 푼 플레이어 폴더에서 실행한다.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ".\tools\install-translation.ps1"
```

다른 위치에서 실행하려면 플레이어 폴더를 지정한다.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "C:\Downloads\LumaPlayer\tools\install-translation.ps1" -PlayerDirectory "D:\Apps\LumaPlayer"
```

실행 정책 옵션은 이번 PowerShell 프로세스에만 적용된다. 기본 설치 위치는 스크립트가 있는 `tools` 폴더의 부모다. 개발 저장소의 `scripts`에서 실행하면 저장소 폴더가 기본 위치다.

스크립트는 플레이어 폴더 또는 `licenses` 폴더의 `translation-dependencies.lock.json`을 읽고, 고정한 공식 Hugging Face 모델 파일만 받는다. 파일 크기와 SHA-256을 모두 확인한 다음 `runtime/translation/models/Qwen3-4B-Q4_K_M.gguf`로 옮긴다. 기존 모델이 올바르면 다운로드를 생략한다. 전체 모델이 포함된 포터블 배포본에서도 그대로 재사용한다.

다운로드가 중단되거나 검증에 실패하면 기존 모델을 교체하지 않는다. 다시 실행하면 완료된 임시 파일을 검증해 재사용하거나, 불완전한 `.download` 파일을 버리고 다시 받는다. 공간이 부족하면 오류를 표시한다. 앱이 파일을 사용 중이어서 교체할 수 없으면 앱을 종료한 뒤 다시 실행한다.

정확한 모델 SHA-256은 `7485fe6f11af29433bc51cab58009521f205840f5b4ae3a32fa7f92e8534fdf5`이며 모델 라이선스는 Apache 2.0이다. 배포본의 라이선스 원문을 보존한다. 번역은 자동 생성 결과이며 원문과 함께 확인한다.

## English

The portable core includes the translation runtime; install its optional model separately. The one-model download is **2,497,280,256 bytes, about 2.50 GB/2.33 GiB**. Allow approximately 2.56 GB of free space and write access in the destination folder. Installation uses the internet; subsequent translation works offline. No account, API key, Python or additional SDK is needed.

Run from the extracted player folder:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ".\tools\install-translation.ps1"
```

Or specify the player folder when running elsewhere:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "C:\Downloads\LumaPlayer\tools\install-translation.ps1" -PlayerDirectory "D:\Apps\LumaPlayer"
```

The execution policy option applies only to that PowerShell process. The default destination is the parent of the script's `tools` folder, or the repository root when invoked from `scripts` in a development checkout.

The installer reads `translation-dependencies.lock.json` from the player folder or its `licenses` folder and downloads only the pinned official Hugging Face model. It verifies both size and SHA-256 before renaming to `runtime/translation/models/Qwen3-4B-Q4_K_M.gguf`. A valid existing model is reused without downloading, including a full portable package that already includes it.

Interrupted or failed verification never replaces the existing model. Rerunning verifies/reuses a complete temporary file or discards an incomplete `.download` file before downloading again. Insufficient disk space is reported. If the app is holding a file that cannot be replaced, close it and rerun.

The model SHA-256 is `7485fe6f11af29433bc51cab58009521f205840f5b4ae3a32fa7f92e8534fdf5`; the model is Apache 2.0. Preserve the bundled upstream licenses. Generated translations should be checked alongside the original.
