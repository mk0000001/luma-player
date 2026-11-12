# Android 외부 구성요소 / Android third-party notices

## 한국어

LumaPlayer Android 앱의 소스와 빌드 스크립트는 GPL-3.0-or-later입니다. 앱에 포함된 구성요소에는 각 원본 라이선스가 적용됩니다. APK의 `assets/licenses`에는 원문과 이 고지가 포함되며, 정확한 Gradle 의존성은 `app/gradle.lockfile`에 기록되어 있습니다. [LumaPlayer 소스](https://github.com/mk0000001/luma-player)

| 구성요소 | 버전과 라이선스 | 원본 |
|---|---|---|
| AndroidX Media3 | 1.11.1, Apache-2.0 | [고정 버전 소스](https://github.com/androidx/media/tree/1.11.1) |
| AndroidX 라이브러리 | Annotation, Arch Core, Collection, Concurrent Futures, Core, CustomView, ExifInterface, Lifecycle, Media, ProfileInstaller, RecyclerView, Startup, Tracing, VersionedParcelable, Apache-2.0 | [AndroidX 소스](https://android.googlesource.com/platform/frameworks/support/) |
| OkHttp / Okio | 4.12.0 / 3.6.0, Apache-2.0 | [OkHttp 소스](https://github.com/square/okhttp/tree/parent-4.12.0), [Okio 소스](https://github.com/square/okio/tree/parent-3.6.0) |
| Guava | 33.3.1-android 및 failureaccess 1.0.2, Apache-2.0 | [고정 버전 소스](https://github.com/google/guava/tree/v33.3.1) |
| Kotlin 표준 라이브러리 / JetBrains Annotations | 2.2.10 및 JDK 호환 모듈 1.9.10 / 13.0, Apache-2.0 | [Kotlin 소스](https://github.com/JetBrains/kotlin/tree/v2.2.10), [JetBrains Annotations 소스](https://github.com/JetBrains/intellij-community) |
| Public Suffix List 데이터 | OkHttp의 `publicsuffixes.gz`, MPL-2.0 | [OkHttp 원본 데이터와 고지](https://github.com/square/okhttp/tree/parent-4.12.0/okhttp/src/main/resources/okhttp3/internal/publicsuffix), [Public Suffix List 소스](https://publicsuffix.org/list/public_suffix_list.dat) |
| Noto Sans CJK KR | 변경하지 않은 Regular OTF, SIL OFL 1.1 | [고정 원본](https://github.com/notofonts/noto-cjk/tree/f8d157532fbfaeda587e826d4cd5b21a49186f7c/Sans/OTF/Korean), [OFL 원문](../LICENSES/NotoSansCJK-OFL.txt) |

APK의 `okhttp3/internal/publicsuffix/NOTICE`는 OkHttp 원문입니다. JUnit 4.13.2(EPL-1.0)와 JSON-java 20250517(Public Domain)은 JVM 단위 테스트에 사용됩니다. 빌드 도구와 테스트 의존성은 APK 런타임에 포함되지 않습니다. [JUnit 소스](https://github.com/junit-team/junit4/tree/r4.13.2), [JSON-java 소스](https://github.com/stleary/JSON-java/tree/20250517)

## English

The LumaPlayer Android source and build scripts are GPL-3.0-or-later. Bundled components retain their upstream licenses. The APK includes the license texts and this notice under `assets/licenses`. `app/gradle.lockfile` records the exact Gradle dependencies. [LumaPlayer source](https://github.com/mk0000001/luma-player)

| Component | Version and license | Upstream source |
|---|---|---|
| AndroidX Media3 | 1.11.1, Apache-2.0 | [Pinned source](https://github.com/androidx/media/tree/1.11.1) |
| AndroidX libraries | Annotation, Arch Core, Collection, Concurrent Futures, Core, CustomView, ExifInterface, Lifecycle, Media, ProfileInstaller, RecyclerView, Startup, Tracing, VersionedParcelable, Apache-2.0 | [AndroidX source](https://android.googlesource.com/platform/frameworks/support/) |
| OkHttp / Okio | 4.12.0 / 3.6.0, Apache-2.0 | [OkHttp source](https://github.com/square/okhttp/tree/parent-4.12.0), [Okio source](https://github.com/square/okio/tree/parent-3.6.0) |
| Guava | 33.3.1-android and failureaccess 1.0.2, Apache-2.0 | [Pinned source](https://github.com/google/guava/tree/v33.3.1) |
| Kotlin standard library / JetBrains Annotations | 2.2.10 with 1.9.10 JDK compatibility modules / 13.0, Apache-2.0 | [Kotlin source](https://github.com/JetBrains/kotlin/tree/v2.2.10), [JetBrains Annotations source](https://github.com/JetBrains/intellij-community) |
| Public Suffix List data | OkHttp `publicsuffixes.gz`, MPL-2.0 | [OkHttp data and notice](https://github.com/square/okhttp/tree/parent-4.12.0/okhttp/src/main/resources/okhttp3/internal/publicsuffix), [Public Suffix List source](https://publicsuffix.org/list/public_suffix_list.dat) |
| Noto Sans CJK KR | Unmodified Regular OTF, SIL OFL 1.1 | [Pinned source](https://github.com/notofonts/noto-cjk/tree/f8d157532fbfaeda587e826d4cd5b21a49186f7c/Sans/OTF/Korean), [OFL text](../LICENSES/NotoSansCJK-OFL.txt) |

The APK preserves OkHttp's original `okhttp3/internal/publicsuffix/NOTICE`. JVM unit tests use JUnit 4.13.2 (EPL-1.0) and JSON-java 20250517 (Public Domain). Build tools and test dependencies stay outside the APK runtime. [JUnit source](https://github.com/junit-team/junit4/tree/r4.13.2), [JSON-java source](https://github.com/stleary/JSON-java/tree/20250517)
