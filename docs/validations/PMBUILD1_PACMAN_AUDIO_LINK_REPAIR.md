# PMBUILD1 — PacMan Native ELF Audio Link Repair

## Outcome

**Outcome A — accepted PacMan repair.** The standalone production package builds, the audio symbols resolve, package checks and PacMan tests pass, and the normal AMD64 build passes its PacMan stage. The top-level build later stops at an unrelated DeveloperStudio signed-off ELF check; that blocker is recorded separately below.

## Checkout and source selection

- Server checkout: `D:\dev\guideXOSServer`
- Branch: `main`
- Starting HEAD: `6e15a4f584d6e1e4cd3e2892f3f0e25fed8a1b2c`
- Ending HEAD: the resulting PMBUILD1 commit; its SHA is recorded in the completion summary.
- At start, tracked files were clean; `main` was one local commit ahead of `origin/main`. Numerous pre-existing untracked validation artifacts were present and preserved.
- `build.ps1` on this branch invokes `scripts/build-pacman-package.ps1`; that is the production path qualified here.
- PacMan source checkout: `D:\dev\pacman`, branch `master`, starting HEAD `8fc2aedd603ddc57e492bdc02c99442bb165109b`, clean. It was read and archived only; no PacMan source files were changed.
- FONT_SUPPORT stayed on its existing `FONT_SUPPORT` checkout and was not modified.

The main-branch PowerShell package builder had no revision pin. It defaulted to `D:\dev\pacman\guidexos` and compiled whatever that mutable sibling checkout had at `HEAD`. The observed source revision was `8fc2aedd603ddc57e492bdc02c99442bb165109b`. The audio implementation entered the PacMan history at `24a805112d538ac26c1c10633b0b71519339e72b`; the observed `8fc2` revision descends from it and also contains later gameplay changes. PMBUILD1 pins the exact previously selected `8fc2` source revision, preserving those changes.

The separate v0.3 release checkout `D:\dev\guideXOSServerV0.3_SCI_FI_THEME` has an intentional `196aaea97a6ed5a9d1f78643973d8c84a8e8ed50` pre-audio pin. That pin and checkout were left unchanged. The pre-audio source has no PacMan audio references or `pacman_audio.cpp`, so the conditional source registration does not impose an audio object requirement on it.

The PacMan CMake project already registered `pacman_audio.cpp` and staged the seven parent-repository WAV assets. The defect was in Server's PowerShell production build: it registered only `main`, `game`, `level`, `level_rules`, `renderer`, and `bitmap_loader`, and copied only the two graphics resources.

## Root cause and reproduction

At revision `8fc2aedd603ddc57e492bdc02c99442bb165109b`, `src/main.cpp` references:

- `pacman_audio_load_resources(gx_app_context*)`
- `pacman_audio_submit(void*, PacManSoundId)`

The production script compiled the six sources above but omitted `src/pacman_audio.cpp`. A fresh reproduction used the script's Native ELF flags and source list in an isolated E: artifact directory. All six compiles exited 0; the static link exited 1 with exactly those two unresolved C++ symbols from `main.o`. `llvm-nm -C` showed the matching undefined references in `main.o`. Compiling `pacman_audio.cpp` with the same freestanding target flags exited 0 and showed matching C++ definitions in `pacman_audio.o`; no declaration/definition, linkage, parameter, or ABI mismatch was found. Linking the same objects with `pacman_audio.o` exited 0.

The preserved reproduction log is `E:\bkup\artifacts\PMBUILD1-20261018\repro-before-fix\reproduction.log`.

## Repair

Changed the main-branch package builder to:

1. Read a full source SHA from `scripts/pacman-revision.txt` and export that committed revision from the PacMan Git repository into a fresh per-run artifact directory. The archive includes both `guidexos/src` and the seven tracked WAV assets at the repository root.
2. Register `pacman_audio.cpp` only when source files reference the audio functions. Before compiling, validate that the file exists and defines both required functions. Reject duplicate source names or object paths.
3. Copy and hash-check the seven audio WAVs under `resources/audio/` when audio is required. Historical pre-audio source remains valid without them.
4. Inspect the final ELF for AMD64 ELF64 executable format, `gx_main`, required audio definitions, and an empty undefined-symbol list; validate the manifest and audio resource hashes.
5. Write `build-evidence.json` for each run with the source revision/archive hash, compiler and flags, source/object hashes, compile arguments and exit codes, link arguments and exit status, symbols, resource hashes, and package hashes. Fresh run directories preserve earlier build evidence.

The selected full source pin is `8fc2aedd603ddc57e492bdc02c99442bb165109b`. The separate v0.3 pin at `196aaea97a6ed5a9d1f78643973d8c84a8e8ed50` was not changed.

## Standalone package and native checks

The repaired production script completed with exit 0. It compiled, in order: `main.cpp`, `game.cpp`, `level.cpp`, `level_rules.cpp`, `renderer.cpp`, `bitmap_loader.cpp`, and `pacman_audio.cpp`. Each used `--target=x86_64-unknown-elf -std=c++11 -ffreestanding -fno-exceptions -fno-rtti -fno-stack-protector -mno-red-zone` plus the Server SDK and PacMan source include paths. The `main.cpp` build retained the existing production diagnostic/test-hook `=0` definitions.

The link command used `ld.lld -m elf_x86_64 -static -e gx_main` with each object exactly once, including `pacman_audio.o`. The final ELF is 63,640 bytes, SHA-256 `EEB544BBB2E2574EEAED7BB7F0BAD61EA24F6945231B7757223A225B30B8E1CC`. The manifest is 956 bytes, SHA-256 `3F3592E6C76053E323F16D48073E98C67D7F5702B8E47F0B507D6BFB3D126449`.

`llvm-readobj` reports `elf64-x86-64`, little-endian, `EM_X86_64`, `Executable`, entry `0x202430`, and no needed libraries. `llvm-nm --undefined-only` reports no undefined symbols; `gx_main` and both demangled audio definitions are present. The audio object has no `std::`, allocator, RTTI, exception, or unwind runtime references. Audio uses the existing bounds-checked `gx_play_pcm` host ABI and the manifest's existing `audio.output` permission. The wrapper returns `GX_ERROR_NOT_IMPLEMENTED` when an older host lacks the appended call slot; PacMan's existing audio state disables further requests when audio is unavailable. No audio stubs or gameplay changes were added.

All seven original WAV resources are present in the package and hash-match their pinned source-archive bytes. The final per-run evidence JSON is at `E:\bkup\artifacts\PMBUILD1-20261018\final-production-package-build\run-20261018-152412-048\build-evidence.json`. The standalone package is at `E:\bkup\artifacts\PMBUILD1-20261018\final-standalone-package`.

## Tests

- Source-registration regression: passed pre-audio, audio-bearing, missing implementation, and missing-definition cases. It verifies six base objects for pre-audio input and exactly one `pacman_audio` object for audio-bearing input.
- PacMan game tests: **36 groups / 1,548 checks passed**.
- PacMan audio tests: **6 groups / 42 checks passed**.
- PacMan endurance tests: **5 groups / 85 checks passed**, including 360,000 fixed-step updates (60 simulated minutes).
- CMake built all three test executables in an E: artifact directory. The project registers no CTest cases, so the executables were run directly; all exited 0.
- Freestanding validation used the real LLVM clang++ and lld toolchain and the existing AMD64 Native ELF flags.

Logs are under `E:\bkup\artifacts\PMBUILD1-20261018\` (`pacman-test-runs.log`, `pacman-tests.log`, `final-standalone-build.log`).

## Top-level AMD64 build

Ran the normal command `build.ps1 -Arch amd64` from the authorized Server checkout. The PacMan stage passed with the same ELF and manifest hashes as the standalone build. The runtime-image packer included the PacMan manifest, ELF, graphics, and all seven WAVs. The UEFI bootloader and AMD64 kernel then built, and the top-level flow began staging the ESP.

The build stopped at the existing DeveloperStudio signed-off ELF gate:

- Expected: 1,015,064 bytes, SHA-256 `5343F5DF0EDFE423542348C0AF8C9F8690CE339D197DEB4737DD05A27348CAC7`
- Observed: 1,104,652 bytes, SHA-256 `F07270EACBD62F9AB67F1F38856A326981280E43D0A4D44F8ED1D17C777F01A6`

This is a later, unrelated package gate. The full top-level build did not complete, so no release image or full ESP acceptance is claimed. The partial top-level candidate and hash manifest are preserved under `E:\bkup\artifacts\PMBUILD1-20261018\top-level-candidate`; its 64 MiB runtime image has SHA-256 `31A83A3263846ABEFFD890AD34DB482145E88DCA2BAF96837B1425CB1C8263A0`. The complete build log is `E:\bkup\artifacts\PMBUILD1-20261018\top-level-amd64-build.log`.

No QEMU guest was launched. This report does not claim that FONT13R2's clean-install font acceptance gap is resolved.

## Files changed and handoff

The PMBUILD1 commit contains:

- `scripts/build-pacman-package.ps1`
- `scripts/pacman-revision.txt`
- `scripts/pacman-source-registration.psm1`
- `tests/pacman_source_registration_test.ps1`
- `docs/validations/PMBUILD1_PACMAN_AUDIO_LINK_REPAIR.md`
- `docs/validations/FONT13R2_PACMAN_BUILD_HANDOFF.md`

The handoff for the FONT13R2 owner is in [`FONT13R2_PACMAN_BUILD_HANDOFF.md`](FONT13R2_PACMAN_BUILD_HANDOFF.md). FONT_SUPPORT itself was not edited. Once the owner integration workflow makes the PMBUILD1 Server changes available to the FONT_SUPPORT integration baseline, rerun its normal `build.ps1 -Arch amd64` and record whether it passes the PacMan stage and reaches runtime-image/ESP staging. If the DeveloperStudio hash gate still fails, record it separately. PacMan build success does not close FONT13R2's live Settings or clean-install font acceptance.

## Requalification rerun — 2026-10-18

The existing repair was requalified in place. The Server checkout started at and, before this report update, ended at `655fe1f67c1d79d2b2c97aae01dd6267609ba84f` on `main`, four commits ahead of `origin/main`; its tracked worktree was clean at both points. The PMBUILD1 implementation is already committed as `192854434dfe8da4234db81af9f64d7841406bff`; the PowerShell compatibility follow-up is `eac07e130f439b91d328558f286fa51266b9c7a0`. No PacMan source or FONT_SUPPORT file was changed.

The Server `main` pin remains `8fc2aedd603ddc57e492bdc02c99442bb165109b`. The packaging script exports that commit with `git archive`; it does not compile uncommitted files from the sibling checkout. The separate `D:\dev\guideXOSServerV0.3_SCI_FI_THEME` checkout remains pinned to `196aaea97a6ed5a9d1f78643973d8c84a8e8ed50`. That pre-audio revision has no `pacman_audio.cpp` or audio references and registers only the six base objects; the conditional registration therefore preserves its build.

Fresh reproduction and production-build evidence is under `E:\bkup\artifacts\PMBUILD1-20261018-requalification-01`:

- Compiling and linking the six base objects reproduced exit 1 with exactly `pacman_audio_load_resources(gx_app_context*)` and `pacman_audio_submit(void*, PacManSoundId)` undefined. Their declarations, definitions, C++ linkage, and argument types match. Compiling `pacman_audio.cpp` with the same freestanding flags and linking it once as the seventh object resolved both symbols (exit 0).
- The production package script compiled `main`, `game`, `level`, `level_rules`, `renderer`, `bitmap_loader`, and `pacman_audio`, then linked with `ld.lld -m elf_x86_64 -static -e gx_main`. All compile and link exits were 0. The ELF is 63,640 bytes, SHA-256 `EEB544BBB2E2574EEAED7BB7F0BAD61EA24F6945231B7757223A225B30B8E1CC`; the manifest is SHA-256 `3F3592E6C76053E323F16D48073E98C67D7F5702B8E47F0B507D6BFB3D126449`.
- The source-registration regression exited 0 for pre-audio, audio-bearing, missing-implementation, and missing-definition cases. CMake-built PacMan tests passed: game 36 groups / 1,548 checks; audio 6 / 42; endurance 5 / 85, including 360,000 fixed-step updates.
- `llvm-readobj` reports little-endian AMD64 ELF64 and no needed libraries. `llvm-nm` reports `gx_main`, both audio definitions, and no unresolved final-ELF symbols. `pacman_audio.o`'s `memset` reference resolves to PacMan's own freestanding implementation in `main.o`; the audio source adds no STL, allocation, exception, or RTTI use. The manifest grants the existing `audio.output` permission. If the host lacks the appended `play_pcm` slot, `gx_play_pcm` returns `GX_ERROR_NOT_IMPLEMENTED` and PacMan reports unavailable audio while continuing gameplay.

The normal `build.ps1 -Arch amd64` invocation also passed the PacMan package stage, staged its 11-file package in the ESP and runtime-image package tree, and built the UEFI bootloader and AMD64 kernel. Only `GUIDEXOS_PACMAN_ARTIFACT_ROOT` was directed to E: for scratch output; `-Clean` was not used. The produced 64 MiB `ESP\ramdisk.img` has SHA-256 `CFC032BAB0C2FACC6D3FF642C4FAE569436494651B50A59E02F2FCBC783F2223`. The run then exited 1 at the unrelated DeveloperStudio signed-off ELF gate: expected 1,015,064 bytes / `5343F5DF0EDFE423542348C0AF8C9F8690CE339D197DEB4737DD05A27348CAC7`, observed 1,104,652 bytes / `F07270EACBD62F9AB67F1F38856A326981280E43D0A4D44F8ED1D17C777F01A6`. This top-level script did not create a release ISO. No QEMU guest was launched.

The fresh production evidence is in `production-build-runs\run-20261018-175204-040\build-evidence.json`; top-level PacMan evidence is in `top-level-pacman-runs\run-20261018-175526-587\build-evidence.json`; test logs are `pacman-cmake-tests.log` and `pacman-source-registration-test-confirmation.log`; the normal build log is `top-level-amd64-build.log`. The staged candidate and the pre-run output snapshot are retained under `top-level-candidate` and `pre-top-level-output-snapshot`. After capture, tracked generated outputs were restored to their starting contents; no build artifacts were added to the repository. This report update is the only tracked requalification change.

For FONT13R2, after the owner integration workflow makes PMBUILD1 available in the FONT_SUPPORT integration baseline, rerun `build.ps1 -Arch amd64`. Confirm the pinned revision, one `pacman_audio.o`, all seven WAVs, and the PacMan manifest/ELF in the runtime image and ESP. Record any later DeveloperStudio gate separately. This result does not resolve the clean-install font acceptance gap; see [`FONT13R2_PACMAN_BUILD_HANDOFF.md`](FONT13R2_PACMAN_BUILD_HANDOFF.md).
