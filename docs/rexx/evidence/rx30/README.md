# RX30 Qualification Evidence

RX30 preserves the RX29 untracked-path manifest as its starting set and the RX29 protected-artifact manifest as its hash baseline. `preflight.json` records the verified branch/HEAD, index, duplicate-scope audit, and initial path comparison. `protected-artifacts-end.json` and `protected-artifact-integrity.json` record the final 31-file size/SHA-256 comparison.

`hosted-regression-final.log` contains the RX1–RX29 inherited hosted run and RX30 storage/language tests. `sanitizers-final.log` and `sanitizers-summary.json` record 30/30 ASan and UBSan target matrices and the 196-case local expected-output corpus (`EXPECTED_EXIT`; Regina differential `NOT_RUN`). The native build evidence records the freestanding AMD64 link, its ten REXX objects, flags, and kernel SHA-256.

`native-guest/` contains the two separately recorded exclusive QAC acceptance attempts, readiness reviews, QMP Console screenshots, QAC parent/child exit codes, QMP shutdown acknowledgment, reservation-release status, and process census. The compact-output second run is the final visible guest evidence. QEMU media and kernel outputs remain in the isolated RX30 `build/` tree and are not part of the accepted commit.

The canonical post-commit untracked-path manifest is `untracked-end-ordinal.paths`: UTF-8 without BOM, LF line endings, ordinal case-sensitive sorting. It is generated after the final staging set and compared against the RX29 baseline as a set; the scorecard records its path, SHA-256, additions, and missing pre-existing paths.