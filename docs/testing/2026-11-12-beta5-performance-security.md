# 베타 5 성능 측정과 보안 점검 / Beta 5 performance and security review

## 한국어

### 테스트 PC와 측정 방법

Windows 11 Home x64 빌드 26300, Intel Core Ultra 9 185H(14코어·20스레드), RAM 32 GB(Windows 인식 31.4 GiB), RTX 4080 Laptop GPU(12,282 MiB, 드라이버 617.42), SOLIDIGM NVMe SSD와 PIONEER BD-ROM BDCTD03RS USB에서 측정했습니다. Windows Qt 6.11.3 베타 5 개발 빌드로 1080p H.264 파일과 외부 SRT(20초), 츠키 무도관 Blu-ray 본편과 준비한 자막(27초), 일본어 음성 테스트 파일과 실시간 AI(65초)를 각각 실행했습니다. 실시간 시험은 full-v3 Q5 음성 인식과 Hy-MT2-7B Q4_K_M 한국어 번역에 CUDA0을 지정했습니다. 다른 두 시험에서는 실시간 인식·번역을 껐습니다.

플레이어와 관찰된 자식 프로세스의 CPU 누적 시간·전용 할당량(private bytes)을 약 1.4초 간격으로 채집했습니다. GPU 값은 `nvidia-smi`의 장치 전체 지표이며, 전용 VRAM 증분은 각 실행 전 네 번 잰 평균을 실행 중 최고치에서 뺀 값입니다. CPU·GPU p95는 실행 후 첫 2초를 제외한 샘플입니다.

| 시험 | 샘플 | CPU p95 / 최대 (논리 코어 환산) | 전용 RAM 최고 | GPU p95 / 최대 | 디코더 최대 | VRAM 증가 최고 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 1080p 파일 + SRT | 15 | 0.70 / 0.70 | 502 MiB | 44 / 44% | 14% | 191 MiB |
| Blu-ray + 미리 만든 자막 | 20 | 0.70 / 1.06 | 378 MiB | 39 / 43% | 14% | 168 MiB |
| 1080p 파일 + 실시간 인식·번역 | 45 | 2.00 / 2.56 | 10,694 MiB | 99 / 99% | 6% | 8,761 MiB |

파일과 디스크에서 D3D11VA 하드웨어 디코딩을 확인했습니다. 실시간 시험에서는 GPU 음성 인식, CUDA0 번역, 재생 시작 후 최대 약 47초 원문 버퍼를 관찰했습니다. 실행 종료까지 약 59초 분량을 재생했고, 수집한 진단값에는 영상 프레임 손실이나 광학 디스크 버퍼링이 보고되지 않았습니다. 측정 구간은 광학 디스크의 전체 공연, 반복 탐색 또는 4K/HDR 성능을 대표하지 않습니다.

별도 네이티브 테스트에서 실물 Blu-ray의 20초 시작 안내를 거쳐 약 30초에 디스크 메뉴에 도달했습니다. 본편 약 11분 48초로 일시정지 상태에서 탐색한 뒤, 기본 인식 설정에서 GPU로 원문 10초 이상을 준비하는 데 약 44초, 60초 이상까지 약 63초가 걸렸습니다. 번역은 이 탐색 시험에서 껐습니다. 탐색·읽기·인식까지 포함한 한 번의 시간입니다.

### 최소·권장 사양 계산

- **일반 재생과 준비한 자막**은 CPU p95가 논리 코어 0.70개에 해당했고, 앱 메모리 최고치가 약 0.5 GiB였습니다. Windows 및 백그라운드 작업 여유를 두고 4코어·RAM 8 GB·H.264 하드웨어 디코딩을 추정 최소로, RAM 16 GB를 권장으로 잡았습니다. 기본 설치와 설치 파일, 임시 파일을 위해 SSD 여유 8 GB를 잡았습니다.
- **실시간 원문 인식과 한국어 번역**은 앱 프로세스 전용 할당량 최고 10.44 GiB, 추가 전용 VRAM 8.56 GiB였습니다. OS에 약 4 GiB를 가정하고 여유를 더해 RAM 16 GB를 추정 최소로, 여러 앱을 함께 쓸 때 32 GB를 권장합니다. GPU는 측정 시 유휴 점유 약 1.22 GiB까지 합해 약 9.77 GiB가 사용되어 10 GB를 메모리 추정 하한으로 잡았습니다. 증가분에 25% 여유를 더하면 `1.22 + 8.56 × 1.25 ≈ 11.92 GiB`여서 12 GB 이상을 권장합니다. CPU는 p95 2.00과 최고 2.56 논리 코어에 Windows와 추출·탐색 여유를 더해 6코어를 추정 최소로, 측정한 185H급을 권장합니다. 선택 모델 약 5.71 GB와 설치 임시 공간을 포함해 SSD 여유 16 GB를 권장합니다.
- 실시간 시험의 GPU 부하가 99%에 도달했습니다. 같은 기능 조합에는 테스트한 RTX 4080 Laptop급 이상의 처리 성능을 권장합니다. VRAM이 10 GB인 장치의 실시간 속도는 따로 검증해야 합니다. 이용률에서 장치별 TFLOPS나 NPU TOPS를 구할 수 없으므로 해당 숫자를 최소 사양으로 쓰지 않았습니다. 권장 사양 미만에서는 전체 자막을 먼저 준비해 재생하세요.

### 보안 점검 범위

- Windows 번들 Qt를 6.11.3으로 올리고 공식 아카이브를 SHA-256으로 고정했습니다. 미디어 이름·상태에 들어오는 문자는 일반 텍스트로 표시합니다. [Qt 릴리스](https://www.qt.io/blog/qt-6.11.3-released) · [HTTP/2 보안 공지](https://www.qt.io/blog/security-advisory-cve-2026-95620)
- 시작 단계에서 Windows DLL 검색 범위를 제한하고, libmpv의 사용자 스크립트 자동 로드와 안전하지 않은 재생목록 항목을 비활성화했습니다. 번역은 프록시와 리디렉션 없이 로컬 루프백에만 연결하며 HTTP/2를 끄고 상태 확인과 추론 응답을 제한합니다. [mpv 옵션 문서](https://mpv.io/manual/master/)
- 자막 가져오기의 로컬 파일 크기·UTF-8·변환 프로토콜 제한, 설치기·CUDA 구성 요소·모델의 고정 해시 및 경로 검사, 보호 미디어의 패키지 제외 규칙을 검토했습니다. VLC 3.0.24와 FFmpeg 9.0.2는 상류 보안 공지와 대조했습니다. [VideoLAN 공지](https://images.videolan.org/security/sb-vlc3024.html) · [FFmpeg 보안](https://www.ffmpeg.org/security.html)
- Qt 6.11.3 이후 공지된 HTTP 응답 자원 소모 문제는 이 공식 바이너리에도 관련됩니다. 앱의 Qt HTTP 경로는 로컬 번역기로 한정되며, 설치기 모델 다운로드는 별도입니다. 네이티브 미디어 파서의 전체 취약점이나 새로운 공격의 부재를 보장할 수는 없습니다. [Qt 보안 공지](https://www.qt.io/blog/security-issue-cve-2026-116337)

## English

### Test hardware and sampling

The test PC runs Windows 11 Home x64 build 26300 on a Core Ultra 9 185H (14 cores, 20 threads), 32 GB RAM (31.4 GiB visible), RTX 4080 Laptop GPU (12,282 MiB, driver 617.42), SOLIDIGM NVMe SSD, and PIONEER BD-ROM BDCTD03RS USB drive. The Qt 6.11.3 beta 5 development build ran a 1080p H.264 file with an SRT (20 seconds), the TUKI NIPPON BUDOKAN Blu-ray main feature with prepared captions (27 seconds), and an owned Japanese speech fixture with live captions (65 seconds). Live recognition used full-v3 Q5 and translation used Hy-MT2-7B Q4_K_M, both with CUDA0 selected. Live recognition and translation were disabled in the first two scenarios.

Samples of accumulated CPU time and private bytes covered the player and observed child processes about every 1.4 seconds. GPU figures are device-wide `nvidia-smi` readings. Peak incremental VRAM subtracts the mean of four prelaunch samples from peak usage. CPU and GPU p95 exclude the first two seconds.

| Workload | Samples | CPU p95 / peak (logical core equivalents) | Peak private bytes | GPU p95 / peak | Peak decoder | Peak incremental VRAM |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1080p file + SRT | 15 | 0.70 / 0.70 | 502 MiB | 44 / 44% | 14% | 191 MiB |
| Blu-ray + prepared captions | 20 | 0.70 / 1.06 | 378 MiB | 39 / 43% | 14% | 168 MiB |
| 1080p file + live recognition and translation | 45 | 2.00 / 2.56 | 10,694 MiB | 99 / 99% | 6% | 8,761 MiB |

Both file and disc used D3D11VA decoding. Live testing showed GPU recognition, CUDA0 translation, and up to about 47 seconds of recognized-caption coverage. About 59 seconds of media played during that run. The sampled diagnostics reported no dropped video frames or optical-disc buffering. The intervals do not cover a full concert, repeated seeks, or 4K/HDR.

A separate native test reached the physical disc menu in about 30 seconds, including the 20-second First Play clip. After a paused seek to about 11:48 in the main feature, the default recognition configuration on GPU took about 44 seconds to prepare at least 10 seconds of original captions and about 63 seconds to reach at least 60 seconds. Translation was disabled for this seek test. The single observation includes seeking, disc reads, and recognition.

### Deriving minimums and recommendations

- **Playback and prepared captions** reached 0.70 logical cores at p95 and about 0.5 GiB in app private bytes. Allowing for Windows and background tasks gives an estimated four-core CPU, 8 GB RAM, and hardware H.264 decode. We recommend 16 GB RAM and 8 GB free SSD space for the core installation, installer, and temporary files.
- **Live recognition plus Korean translation** used up to 10.44 GiB in app private bytes and 8.56 GiB in incremental VRAM. Allowing about 4 GiB for the OS plus headroom gives an estimated 16 GB RAM minimum and a 32 GB recommendation for multitasking. Peak GPU usage including the 1.22 GiB prelaunch baseline was about 9.77 GiB. This gives a 10 GB capacity estimate and a 12 GB recommendation with 25% headroom on the increment (`1.22 + 8.56 × 1.25 ≈ 11.92 GiB`). The 2.00 logical-core p95 and 2.56 peak give an estimated six physical cores after allowing for the OS, extraction, and seeks. We recommend the tested 185H class, and 16 GB free SSD space including the approximately 5.71 GB in optional models and temporary space.
- Live GPU utilization reached 99%. We recommend the tested RTX 4080 Laptop performance class or faster for this workload. Real-time performance on a 10 GB GPU needs separate testing. Utilization cannot give a portable GPU TFLOPS or NPU TOPS requirement. Prepare subtitles before playback if below the recommended configuration.

### Security review scope

- The Windows bundle uses Qt 6.11.3 with a SHA-256 pin on the official archive. Media and status strings are plain text in labels. [Qt release](https://www.qt.io/blog/qt-6.11.3-released) · [HTTP/2 advisory](https://www.qt.io/blog/security-advisory-cve-2026-95620)
- Startup constrains Windows DLL search. libmpv disables automatic user-script loading and unsafe playlist entries. The translation client uses loopback without a proxy or redirects, disables HTTP/2, and limits health and inference replies. [mpv option reference](https://mpv.io/manual/master/)
- We reviewed local subtitle file size, UTF-8 and conversion protocol limits, installer, CUDA and model hash/path checks, and protected-media exclusions. VLC 3.0.24 and FFmpeg 9.0.2 were checked against upstream advisories. [VideoLAN bulletin](https://images.videolan.org/security/sb-vlc3024.html) · [FFmpeg security](https://www.ffmpeg.org/security.html)
- The upstream HTTP response resource-consumption advisory published after Qt 6.11.3 still applies to the official binary. The app limits its Qt HTTP path to the local translation worker, and model downloads run separately in the installer. This review cannot establish the absence of vulnerabilities in native media parsers or unknown attacks. [Qt advisory](https://www.qt.io/blog/security-issue-cve-2026-116337)
