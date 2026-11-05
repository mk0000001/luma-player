# 번역 구성요소 라이선스 / Translation component licenses

## 한국어

공식 Qwen3-4B GGUF는 Apache 2.0, llama.cpp는 MIT, 포함된 LLVM OpenMP는 Apache 2.0 with LLVM Exceptions 조건이다. 상업 이용과 재배포를 허용하는 원문을 함께 보존한다. Qwen 모델은 변경·재양자화하지 않은 공식 파일이다. 이 폴더의 원문은 수정하지 않는다.

`llama-upstream`에는 고정 커밋의 JSON, cpp-httplib, 해시 및 포함된 UI 벤더 고지도 원래 경로대로 보존한다. 이들은 MIT/BSD 계열 조건이며 정확한 저작권·조건은 각 원문을 따른다. 앱이 웹 UI를 켜지 않더라도 공식 서버 바이너리에 포함된 고지를 함께 배포한다.

선택적인 OpenVINO 바이너리는 공식 llama.cpp 릴리스의 `openvino-licensing` 전체를 보존한다. OpenVINO 오픈 소스 라이선스와 Intel Distribution License, `redist.txt`, oneDNN/oneTBB 및 기타 제3자 고지가 포함된다. Intel 배포 라이선스 2.1(c)는 재배포 가능 객체 코드의 배포를 허용하며 해당 조건을 유지해야 한다. 각 바이너리는 해당 원문 조건을 계속 적용받는다.

고정 버전·원문 URL·해시는 `translation-dependencies.lock.json`에 있다. OpenVINO 고지 묶음의 출처는 그 파일에 고정된 OpenVINO ZIP이며, ZIP SHA-256이 묶음 전체의 출처를 검증한다.

## English

Official Qwen3-4B GGUF is Apache 2.0, llama.cpp is MIT, and bundled LLVM OpenMP is Apache 2.0 with LLVM Exceptions. Preserve the original terms permitting commercial use and redistribution. The official Qwen model is unmodified and not requantized. Do not edit upstream license texts in this folder.

`llama-upstream` preserves the pinned commit's JSON, cpp-httplib, hash and bundled UI vendor notices at their original paths. These use MIT/BSD-family terms; the exact copyright and conditions remain in each original. Preserve notices for code included in the official server binary even while the app disables its web UI.

The optional OpenVINO binaries retain the complete `openvino-licensing` folder from the official llama.cpp release. It contains the OpenVINO open-source license, Intel Distribution License, `redist.txt`, oneDNN/oneTBB and other third-party notices. Intel Distribution License section 2.1(c) permits redistribution of designated object-code redistributables with its conditions retained. Each binary remains subject to its applicable upstream terms.

Pinned versions, source URLs and hashes are in `translation-dependencies.lock.json`. The pinned OpenVINO ZIP is the source of its complete notices; the ZIP SHA-256 verifies the bundle's provenance.
