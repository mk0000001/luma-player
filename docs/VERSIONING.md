# Version and publication policy

- `VERSION` is the source of truth for executable version, diagnostic reports, package version and release tag.
- SemVer prereleases are explicit: for example `0.1.0-alpha.1` / tag `v0.1.0-alpha.1`.
- Every distributed version has an annotated Git tag, a GitHub Release, source snapshot, portable archive, SHA-256 and release notes under `docs/releases/`.
- Never move an existing release tag or replace an existing published archive. Corrections require a new version.
- Development commits remain in GitHub history; an untagged development commit is not a claim of release acceptance.
- Windows and Linux CI run on development branches/PRs. Physical media and native rendering are opt-in local checks with recorded evidence.
- Release workflow verifies the tag equals `VERSION`, builds from that tag, checks dependencies against their pinned hashes, runs tests, scans the package for private playback data, and publishes without overwriting another release.
- A release documents incomplete capabilities. Passing CI cannot silently turn a physical-disc, DRM or HDR failure into a pass.
