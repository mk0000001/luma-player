# HeartTranscriptor 가창 인식 / HeartTranscriptor singing recognition

## 한국어

HeartTranscriptor-oss는 HeartMuLa 팀이 Whisper Medium을 가창 데이터에 맞춰 학습한 음성 인식 모델입니다. 루마플레이어는 공식 가중치를 기존 로컬 인식 엔진이 읽는 Whisper GGML로 변환하고 Q5_0으로 양자화했습니다. 파일 크기는 539,212,484바이트(약 514MiB)입니다. [공식 모델 카드](https://huggingface.co/HeartMuLa/HeartTranscriptor-oss/tree/ee842de9aeec454236badef2205dea47a1fd8410) · [Apache 2.0 라이선스 원문](../LICENSES/HeartTranscriptor-Apache-2.0.txt)

### 설치

Windows 0.2.19 이상 설치본은 HeartTranscriptor Q5를 함께 설치합니다. 첫 실행의 기본 인식 모델이며, **AI 자막 → 음성 인식 모델**에서 저장한 선택이 우선합니다. 아래는 모델 파일을 별도로 추가할 때 사용하는 방법입니다.

1. **AI 자막 → 폴더 열기**를 누르고 내려받은 `ggml-hearttranscriptor-oss-q5_0.bin`을 그 폴더에 넣으세요. 모델을 다른 곳에 보관하려면 **모델 폴더 변경**을 누르세요. [모델 릴리스](https://github.com/mk0000001/luma-player/releases/tag/hearttranscriptor-ggml-v1) · [파일 크기와 SHA-256](../dependencies.lock.json)
2. **음성 인식 모델** 목록에서 **HeartTranscriptor · 가창 · Q5**를 고르세요. 일본어 노래에는 인식 언어를 **일본어**로 지정하세요. 한국어 자막은 로컬 번역 설정에서 켤 수 있습니다.

이 변경이 포함된 Windows 패키지는 플레이어 폴더에서 `./tools/install-asr-model.ps1 -Profile singing`을 실행하면 모델을 검증하고 설치합니다. 소스 체크아웃의 스크립트는 `./scripts/install-asr-model.ps1 -Profile singing`입니다. 모델 선택은 플레이어에서 저장합니다.

### 변환과 검증

- 원본은 HeartMuLa의 `ee842de9aeec454236badef2205dea47a1fd8410` 리비전의 `model.safetensors`입니다. 원본과 배포본의 SHA-256은 잠금 파일에 기록했습니다.
- [whisper.cpp b5454 변환기](https://github.com/ggml-org/whisper.cpp/blob/b5454/models/convert-h5-to-ggml.py)를 Python 3.10.17, PyTorch 2.7.1 CPU, Transformers 4.51.3, NumPy 2.2.6, Safetensors 0.5.3으로 실행해 FP16 GGML을 만들었습니다. OpenAI Whisper의 `31243bad24cc746f07d4c8bfdd2d974872cb1803` 리비전에서 가져온 80×201 멜 필터가 HeartTranscriptor 설정의 필터와 동일함을 확인했습니다. 이어 b5454의 `whisper-quantize`로 `q5_0`을 적용했습니다. 변환 도구는 개발 시에 사용합니다. 플레이어는 C++ 인식 엔진에서 완성된 모델 파일을 읽습니다.
- Windows에서 PJS의 16초 일본어 가창 샘플 `pjs056_song.wav`를 사용해 실시간 자막 경로와 전체 자막 준비 경로를 확인했습니다. 전체 자막 준비는 일본어 원문과 Hy-MT2 한국어 번역을 각각 SRT로 저장했습니다. [PJS 공개 자료와 CC BY-SA 4.0 조건](https://sites.google.com/site/shinnosuketakamichi/research-topics/pjs_corpus)
- 같은 16초 샘플로 모델을 한 번 실행한 뒤 측정한 인식 시간은 RTX 4080 Laptop GPU의 CUDA에서 0.41초, Intel Arc의 Vulkan에서 1.68초, CPU 4스레드에서 10.27초였습니다. 각 수치는 인식 엔진의 처리 시간입니다.

## English

HeartTranscriptor-oss is HeartMuLa's Whisper Medium model fine-tuned on singing. LumaPlayer converted the official weights to Whisper GGML for its local recognition engine and quantized them to Q5_0. The model file is 539,212,484 bytes, or approximately 514 MiB. [Official model card](https://huggingface.co/HeartMuLa/HeartTranscriptor-oss/tree/ee842de9aeec454236badef2205dea47a1fd8410) · [Apache 2.0 license text](../LICENSES/HeartTranscriptor-Apache-2.0.txt)

### Install

Windows 0.2.19 and later installers include HeartTranscriptor Q5. It is the first-launch default, and choices saved under **AI captions → Speech recognition model** take precedence. Use the following steps to add the model manually.

1. Use **Open folder** under **AI captions** and place the downloaded `ggml-hearttranscriptor-oss-q5_0.bin` in that folder. Use **Change model folder** to store models elsewhere. [Model release](https://github.com/mk0000001/luma-player/releases/tag/hearttranscriptor-ggml-v1) · [File size and SHA-256](../dependencies.lock.json)
2. Choose **HeartTranscriptor · 가창 · Q5** from the **Speech recognition model** list. Set the recognition language to **Japanese** for Japanese lyrics. Enable Korean captions in the local translation settings.

For Windows packages containing this change, run `./tools/install-asr-model.ps1 -Profile singing` from the player directory to verify and install the model. In a source checkout, use `./scripts/install-asr-model.ps1 -Profile singing`. The player saves the selected model.

### Conversion and verification

- The source is HeartMuLa's `model.safetensors` at revision `ee842de9aeec454236badef2205dea47a1fd8410`. The lock file records the source and distribution SHA-256 values.
- We ran the [whisper.cpp b5454 converter](https://github.com/ggml-org/whisper.cpp/blob/b5454/models/convert-h5-to-ggml.py) with Python 3.10.17, CPU PyTorch 2.7.1, Transformers 4.51.3, NumPy 2.2.6, and Safetensors 0.5.3 to produce FP16 GGML. The 80×201 mel filter bank from OpenAI Whisper revision `31243bad24cc746f07d4c8bfdd2d974872cb1803` matched the filters in HeartTranscriptor's configuration. The b5454 `whisper-quantize` tool then applied `q5_0`. These conversion tools run during development. The player reads the finished file using its C++ recognition engine.
- On Windows, we checked the live caption path and whole-file preparation with the 16-second Japanese singing sample `pjs056_song.wav` from PJS. Whole-file preparation exported both the Japanese transcription and the Hy-MT2 Korean translation as SRT files. [PJS corpus and CC BY-SA 4.0 terms](https://sites.google.com/site/shinnosuketakamichi/research-topics/pjs_corpus)
- On the same 16-second sample after a warm-up run, the recognition engine took 0.41 seconds with CUDA on the RTX 4080 Laptop GPU, 1.68 seconds with Vulkan on Intel Arc, and 10.27 seconds on four CPU threads.
