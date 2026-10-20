# S22R3 release-contract decision

## Owner choice

The owner selected the DeveloperStudio package included on guideXOS Server v0.2.1 media as the preferred Server v0.3 candidate. This authorized historical investigation and safe compatibility work. It did not authorize a manifest-version change, a v0.5 service backport, a substitute package, or Server publication.

## Historical identity

The preserved ISO `dist/guideXOS-Server-v0.2.1-amd64.iso` has SHA-256 `985b524a4c281ed3d00060df97adda38c99965c8d03a5d11f69db793e8174c86`. Its sidecar names Server source commit `0e6039c0e3f1fd218424ba7367c9e83bd8d73223`; the source tree contains the release-preparation history at `3daba9c3261e1c04058626d6f4a5ee0d43dc4` and regenerated identity at `9a11b3251004bfeb935cc85b7a1d2f8943387951`. No `v0.2.1` tag exists.

The media package is the 1,175-byte manifest SHA-256 `4DEDC783EFD160A6B13D5AF9A9F87ABB6608004BB10CA8F42ABE6B306EE33701` plus the 1,015,064-byte AMD64 ELF SHA-256 `5343F5DF0EDFE423542348C0AF8C9F8690CE339D197DEB4737DD05A27348CAC7`. The release sidecar and independent read-only FAT inspection agree. The manifest itself declares `minGuideXOSVersion: 0.5.0`.

The ELF bytes equal the Phase 25 signed-off ELF, but the manifests differ: the historical v0.2.1 media manifest is 1,175 bytes and hashes to `4DED…3701`; the Phase 25 v0.5 manifest is 1,112 bytes and hashes to `D59C…F53B`. The exact v0.2.1 package is identified from its media and is not replaced by the Phase 25 pair. Hash identity does not establish v0.3 runtime compatibility.

## Runtime decision

The v0.3 host supports Native ELF identity, basic window/render/event calls, and package-relative reads. It does not populate the workspace file operations, build, Run, or debugger services required by this DeveloperStudio implementation. The `0.5.0` minimum also exceeds Server v0.3. These are core product dependencies; implementing them would be a separate runtime project.

## Package choice

No package is accepted for v0.3 in this phase. The current newer pair remains rejected by exact identity. The historical media manifest is not accepted by the Phase 25 pin and is rejected by the runtime minimum check. The exact Phase 25 pair is rejected for a v0.3 target despite matching its identity pin. The release inventory is not altered and no fallback is staged.

Recommended follow-up: owner decision to defer DeveloperStudio through a separately qualified package-inventory change, or a separate compatibility project that resolves the minimum and missing services. Do not publish Server v0.3 until its full release gates pass.
