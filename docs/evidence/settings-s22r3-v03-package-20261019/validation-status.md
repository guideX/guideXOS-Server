# S22R3 validation status

## Repository

- Repository: `D:\dev\guideXOSServer`.
- Starting branch/HEAD: `main` / `79e446781b219188831682708cb79d0ceb37a585` (the expected S22R2 evidence commit).
- Starting tracked files: 5,292; staged: 0; tracked dirty: 0; pre-existing untracked: 853. Exact initial status is preserved in `initial-path-status.porcelain.txt`.
- S22R3-equivalent work was not already present. No branch switch, reset, clean, stash, fetch, pull, push, or worktree operation occurred.
- The newer DeveloperStudio checkout remained read-only and untouched.

## Historical media

- Preserved Server v0.2.1 ISO: 90,245,120 bytes, SHA-256 `985b524a4c281ed3d00060df97adda38c99965c8d03a5d11f69db793e8174c86`.
- Its release sidecar source commit is `0e6039c0e3f1fd218424ba7367c9e83bd8d73223`; no `v0.2.1` tag exists.
- The UEFI FAT image hash and DeveloperStudio package hashes independently match the sidecar. Reads were performed in place; historical media was not modified.
- The on-media manifest minimum is Server `0.5.0`. No v0.2.1 DeveloperStudio launch/workflow smoke log was found. Separate Phase 25 evidence is for the paired v0.5 hosted runtime only.

## Preflight and checks

- Added `Assert-DeveloperStudioMinimumServerVersion` in the Server release-integrity module and called it before cleanup in `build.ps1`; final release identity validation applies the same check.
- `scripts/test-release-build-integrity.ps1` passed 18 assertions under Windows PowerShell 5.1.
- `build.ps1 -Arch amd64` rejected the current newer pair with exit code 1 before cleanup. `current-newer-pair-preflight.log` preserves output.
- The v0.2.1 media manifest and Phase 25 manifest each fail the v0.3 version check because both declare 0.5.0. A complete Phase 25 receipt is rejected for target 0.3 and remains valid for its declared 0.5 target in the test harness.
- The original S22 ISO still hashes to `B303C4C1A1CD31E500353CBD2DD7E11A8FD438B81AC9306285A6CD20EA1B1A3F` after the preflight attempt.
- No fresh production build, kernel build, package, ISO, package-content audit, or new QEMU guest was produced. No kernel sources changed; the prior 21-symbol S22R classification was not rerun.

## Preserved outputs and resources

- `out/s22-v03-candidate-20261018`: 174.07 MiB; original S22 ISO preserved.
- `out/v03-amd64-qemu-candidate`: 66.55 MiB.
- `out/v03-amd64-scifi-final-candidate`: 330.72 MiB.
- At the later storage check, free space was about 19.20 GiB on D: and 6,387.07 GiB on E:. No artifact-heavy build was attempted after the compatibility stop.

## Guest ownership and Settings

- QEMU PID `25192` belonged to user `LEON2_RDT\guideX` and command lines identified the separate checkout `D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT`. It was not attached to or stopped. Earlier PID `12596` was gone before its owner could be queried.
- No task-owned QEMU guest was started.
- Native Settings' existing source repair and prior 1,426/1,426 runtime suite remain recorded by S22R. The exact-package four-page smoke, Blue Flower-to-Dinos Apply/readback/reopen test, and legacy Control Panel guest test were not run because no fresh compatible ISO was built.
- Native Settings remains unqualified for Server v0.3. DeveloperStudio v0.2.1 also remains unqualified. Server v0.3 was not published.

## Final repository state

The evidence and fail-closed release check are committed locally on `main`; the exact resulting commit is the final `HEAD`. All 853 pre-existing untracked paths were preserved. No push occurred.
