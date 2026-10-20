# S22R2 validation status

## Preflight

- Repository: D:\dev\guideXOSServer, branch main.
- Starting and source HEAD: bce4145843a8c93a4b253116e374341b51f11655.
- No S22R2 acceptance evidence existed at start.
- Initial status had no staged or tracked unstaged paths and 853 pre-existing untracked paths. The untracked DeveloperStudio compatibility note was preserved.
- Initial free space: C: 89.32 GB, D: 26.05 GB, E: 6,387.07 GB.
- Preserved output paths included out/s22-v03-candidate-20261018 (about 182.53 MB), out/v03-amd64-qemu-candidate (about 69.79 MB), out/v03-amd64-scifi-final-candidate (about 346.79 MB), and out/validation (about 10.09 GB). No S22R2 output location existed. The prior ISO is about 91.3 MB and its writable UEFI boot image is 91,226,112 bytes; at least about 200 MB plus build scratch would be needed for an isolated ISO and writable copy. No new output location was selected because Gate A stopped the package work.
- No QEMU process was present in the initial process snapshot. No QEMU guest was launched in S22R2.

## Preserved package and historical checks

- The original S22 ISO remains at out/s22-v03-candidate-20261018/guideXOS-Server-v0.3.0-s22.20261018-amd64.iso; current size is 91,293,696 bytes and SHA-256 is B303C4C1A1CD31E500353CBD2DD7E11A8FD438B81AC9306285A6CD20EA1B1A3F, matching the recorded identity.
- The existing S22R DeveloperStudio preflight result is exit code 1. It rejected current Apps/DeveloperStudio before replacing outputs. Protected historical ESP and S22 ISO identities matched the S22R record.
- Historical S22R release-integrity harness: 13 assertions passed. It covers the pin identity and fail-closed receipt/staging checks recorded in the prior report. No S22R2 harness changes were made.
- Historical native appearance/provider runtime suite: 1,426/1,426 passed after the empty-default repair. No source or packaging change in this attempt invalidated that result, and the suite was not rerun.
- Historical isolated AMD64 kernel build passed with 232 warning lines and zero compiler/linker error lines. The audit classified all 21 distinct undefined symbol names for that exact build: 19 strong names had final PE definitions; __dso_handle was a weak ABI guard reference; mainCRTStartup was absent from input-object undefined references and the custom linker selected _start. No new kernel was built, so no new symbol comparison was made.
- The S22R2 twelve-scenario preflight test matrix was not rerun. Gate A did not authorize a staging selection; the historical 13-assertion fixture harness remains the only recorded fresh integrity suite for this baseline.

## Work not performed at the policy boundary

The DeveloperStudio release contract was not established for Server v0.3, so the authorized path stopped before a package build. Consequently there is no S22R2 top-level build, stage summary, fresh manifest, bootloader/kernel/ramdisk identity, new ISO, ISO-content audit, QEMU boot, Settings guest smoke, wallpaper readback, legacy Control Panel smoke, or DeveloperStudio guest smoke. The S22 and S22R historical guest evidence is not represented as S22R2 evidence.

The release-integrity tests were not rerun because no code or test path changed. No DeveloperStudio source, package artifact, ESP file, build script, release pin, or .phase file was changed.
