# S22R3 evidence

This directory records the v0.2.1 DeveloperStudio historical media review, the v0.3 compatibility boundary, and the release preflight hardening performed on 2026-10-19.

## Decision

**Outcome C — DeveloperStudio v0.2.1 is not qualified for Server v0.3.** The preserved v0.2.1 ISO contains the historical package, but its manifest requires Server 0.5.0 and the v0.3 bare-metal host lacks its workspace, build, Run, and debugger services. The new version gate rejects that minimum before release outputs can be accepted. No v0.3 ISO or QEMU guest was created.

## Evidence files

- [`historical-v021-iso.manifest.json`](historical-v021-iso.manifest.json): original sidecar copied from the preserved `dist/` release media.
- [`historical-media-inspection.json`](historical-media-inspection.json): read-only ISO and embedded FAT inspection, package hashes, and normalized source-tree comparisons.
- [`historical-v021-DeveloperStudio-app.json`](historical-v021-DeveloperStudio-app.json): exact on-media application manifest bytes.
- [`historical-v021-build-identity.txt`](historical-v021-build-identity.txt): exact on-media build identity.
- [`repository-preflight.json`](repository-preflight.json) and [`initial-path-status.porcelain.txt`](initial-path-status.porcelain.txt): starting branch, HEAD, tracked/staged status, and all 853 pre-existing untracked paths.
- [`current-newer-pair-preflight.log`](current-newer-pair-preflight.log): current-source rejection before build cleanup.
- [`preflight-results.txt`](preflight-results.txt): runtime-version rejection results and preservation hash comparison for the S22 ISO.
- [`release-integrity-tests.log`](release-integrity-tests.log): PowerShell 5.1 integrity harness result.
- [`evidence-manifest.sha256`](evidence-manifest.sha256): hashes for the curated evidence files.

The full analysis and the component decision are in [`SETTINGS_S22R3_DEVELOPERSTUDIO_V021_COMPATIBILITY.md`](../../SETTINGS_S22R3_DEVELOPERSTUDIO_V021_COMPATIBILITY.md). The prior S22/R/S22R2 records remain unchanged.

- [developerstudio-artifact-identities.json](developerstudio-artifact-identities.json): historical, Phase 25, current-source, and preserved ESP pair identities.
- [environment-preflight.json](environment-preflight.json): preserved candidate sizes, capacity, and QEMU ownership observations.
