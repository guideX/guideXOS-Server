# Stellar Navigator SN10 — Native Application Bring-up

**Outcome: B — SN10 progress validated; guest qualification remains pending.**

The project phase remains `SN10`. SN9 remains accepted as the Windows-hosted functionality milestone recorded by commit `c7993bc6bc536a2c673839e20af8c8dd4eb93253`; this work does not reopen SN9.

## Phase reconciliation

The requested starting HEAD was `c7993bc6bc536a2c673839e20af8c8dd4eb93253` on branch `STELLAR_NAVIGATOR`. The working `.phase` had regressed to `SN9`, while the accepted commit itself records `.phase` as `SN10` and accepts SN9 on Windows with 323 passing tests and live Windows UI evidence. The marker was restored to `SN10`, and the SN9R3 report was reconciled to preserve its supplemental host evidence and keep guest work under SN10.

The reconciliation was committed locally as `397a7c8d` (`docs: preserve SN9R3 evidence under SN10 scope`). The 239 unrelated pre-existing worktree entries were preserved and excluded from that commit.

## Runtime architecture discovered

The server contains a bare-metal AMD64 Native ELF package loader in `kernel/core/native_elf_baremetal.cpp`. It discovers packages from mounted `/Apps`, validates `app.json` metadata and executable paths, and accepts static ELF64 `ET_EXEC` images with the `guidexos-c-abi-v1` ABI and `gx_main` entry point. The loader rejects dynamic interpreters and relocations. It supplies app-owned windows, text and rectangle drawing, event polling, logging, file reads, and cleanup.

Packages use `kind: NativeElf`, architecture `amd64`, an ABI entry for `gx_main`, and package-relative files. The existing Desktop/App Model resolver maps a validated package ID to `desktop::launch_app`, which dispatches to the bare-metal loader. The normal hosted runtime does not execute Native ELF packages; the implemented boundary is the guideXOS bare-metal guest.

The SDK contract is defined by `sdk/include/guidexos/abi.h`, `app.h`, and `ui.h`. The implementation uses the existing ABI and loader without changing the server ABI or Windows Forms application.

## Native application delivered

The first native package is in `sdk/samples/stellarnavigator/`. The app provides a read-only window for Earth (NAIF 399) at ET/TDB `0` seconds past J2000. It presents the target, epoch, J2000 SSB frame, kilometre units, position, and authenticated-data status. If the asset is missing, unreadable, corrupt, or outside coverage, it presents a bounded error state. Escape or window close triggers shutdown.

The freestanding C++ core uses fixed-size structures, caller-owned storage, 64 KiB reads, and a bounded segment index. It has no STL, heap allocation, exceptions, RTTI, or dynamic ELF dependencies. It authenticates the full asset before parsing and evaluation, validates index and center-chain bounds, evaluates the indexed Type-2 Chebyshev records, and sums Earth → Earth-Moon barycenter → solar-system barycenter states.

The package manifest and built candidate are `Apps/StellarNavigator/app.json` and `Apps/StellarNavigator/bin/amd64/stellarnavigator.elf`. `scripts/run-stellar-navigator-sn10-qemu.ps1` stages a private ESP and has an opt-in kernel launch hook compiled only with `GXOS_STELLAR_NAVIGATOR_SN10_GUEST_PROOF`. The hook calls the real Desktop/App Model dispatch after VFS mount and package discovery; ordinary kernel builds do not launch the app automatically.

## Data and numerical conventions

The application reads the existing `ephemeris_de440s_chebyshev.sn4bin` at package-relative path `Astronomy.Ephemeris/Data/ephemeris_de440s_chebyshev.sn4bin`. The expected size is 32,117,373 bytes and the trusted SHA-256 is `d52b91573c29e6a4e5b6dc19c4af4d9747f6da2122f5b3aff4503684a96d2c0a`. The native parser validates the SNEPH04B v1 index (13 segments in the bundled asset) and does not extrapolate outside segment coverage.

The state is geometric position in the J2000 frame with SSB origin, in km, at ET/TDB zero. The native test matches the existing managed SN4 provider output to 0.001 km, which is the provider's displayed three-decimal precision. The expected display is:

```text
(-27566740.483, 132361381.154, 57418653.286) km
```

The Windows Forms application and its host-only behavior were not changed.

## Validation evidence

- Native package build passed using LLVM `clang++` and `ld.lld`. The output is ELF64 AMD64 static `ET_EXEC`, has the `gx_main` entry point, and has no `PT_INTERP` or relocations. Candidate SHA-256: `6d5f1ca1b04a9ddbaf08e2eed8600ffaf776c06b6e45aab435f61fda9c2e66eb`.
- Focused portable native-core tests: **15 passed, 0 failed**. They cover authenticated open and index validation, the fixed-ET Earth state and managed parity, deterministic repetition, invalid and unsupported inputs, coverage bounds, short reads, hash and size failures, and reinitialization after teardown.
- The accepted SN9 regression baseline remains **323 passed, 0 failed, 0 skipped**, as documented by the accepted SN9 report. That suite was not rerun for this native-only slice.
- The repository’s pinned Mbed TLS/TF-PSA dependency bootstrap completed and `verify-mbedtls-profile.ps1` passed for the locked Mbed TLS 4.1.0 and TF-PSA-Crypto 1.1.0 revisions. This populated the ignored `third_party/mbedtls` dependency tree.
- A full proof-kernel rebuild was attempted with `GXOS_STELLAR_NAVIGATOR_SN10_GUEST_PROOF`. It stopped during kernel compilation at `app_model_limits.h:3` because `<cstddef>` is unavailable while the AMD64 Makefile passes `-nostdinc++`. The existing freestanding build configuration was not weakened to bypass this error.
- No guest run is claimed. The latest exclusive process census found QEMU PID `25192` running a boot acceptance from another checkout (`guideXOSServerV1.1_DOTNET_SUPPORT`). An earlier process from that run exited and another boot was launched; both process checks were read-only. There was no `qac` command or coordinator process. Neither foreign QEMU process was contacted or changed.

The QEMU runner is prepared to fail closed when another QEMU or QAC coordinator is present. It uses OVMF and the repository's UEFI bootloader path, an isolated writable FAT ESP, serial logs, and the App Model proof hook. It has not produced guest serial, screenshot, numerical, or lifecycle evidence because the lane was occupied and the proof kernel did not build.

## Remaining SN10 gates

1. Repair or supply the AMD64 freestanding C++ header configuration so the existing kernel target compiles with its `-nostdinc++` contract intact.
2. Build the proof kernel and run the isolated package through an available exclusive QEMU lane.
3. Confirm package discovery and App Model dispatch, authentic guest ephemeris evaluation and parity, successful first draw, Escape/close handling, and loader cleanup from guest evidence.
4. Run the required integration and regression validation before accepting SN10.

Keep `.phase` at `SN10` until these gates pass. Once SN10 is accepted, a suitable SN11 scope is adding user-selected targets and ET epochs to the native read-only viewer, with coverage-aware errors and parity checks against the managed provider.
