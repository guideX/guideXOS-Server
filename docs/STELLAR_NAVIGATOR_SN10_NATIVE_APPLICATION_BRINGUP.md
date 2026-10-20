# Stellar Navigator SN10 — Native Application Bring-up

**Current result: Outcome B — SN10R1 compatibility, native build, and host regressions passed; guest qualification remains pending.**

The phase remains `SN10`. SN10R1 started on branch `STELLAR_NAVIGATOR` at the expected HEAD `b6996a4e5e24e928deec552e1a530a2f1f678add`, with `.phase` set to `SN10`. The 239 pre-existing unrelated worktree entries were preserved.

## SN10R1 qualification update (2026-10-20)

### Freestanding header root cause and repair

The proof-kernel include trace was `kernel/core/desktop.cpp` → `kernel/core/include/kernel/kernel_apps.h` → `guide_web_http_shared.h` → `app_model_limits.h`. The final header included `<cstddef>`, which is a C++ standard-library header unavailable under the AMD64 kernel's existing `-nostdinc++` flag. This was a hosted header dependency in a shared bounds header, not a need for C++ library functionality.

`app_model_limits.h` uses global `size_t` and namespace-scope `constexpr` bounds only. It contains no `std::` names, hosted facilities, structs, or ABI layout declarations. All seven direct references are from C++ headers or sources; the file is C++-only because it declares a namespace and `constexpr` values. The kernel's established `kernel/core/include/kernel/types.h` convention uses `<stddef.h>` for freestanding `size_t`. The compatibility fix therefore changes only the include from `<cstddef>` to `<stddef.h>`. Constant values, public names, layouts, and calling conventions are unchanged.

With the original freestanding restrictions, syntax compilation passed for `core/desktop.cpp`, `core/kernel_apps.cpp`, `core/main.cpp`, and `core/gxos_tls_foundation.cpp`. The real proof-kernel build also passed with the guest proof flag and no weakened flags:

```text
C:\mingw64\bin\mingw32-make.exe -C kernel ARCH=amd64 EXTRA_CFLAGS=-DGXOS_STELLAR_NAVIGATOR_SN10_GUEST_PROOF -B -j4
```

The build exited 0 and produced `kernel/build/amd64/bin/kernel.elf` (SHA-256 `3aea5f9f10734dc6f248c6bdc05379d3b69a8d0d4b2ad3ec689f4dc71cb9ea1a`), an ELF64 AMD64 `ET_EXEC` with entry `0x100000`. It emitted warnings in unchanged kernel sources, including unused parameters in `kernel_app.h`, an array-bounds warning in `shell.cpp`, and a type-limits warning in `tcp.cpp`; no warning points to the changed header. Full compiler and linker output is preserved at `E:\bkup\artifacts\SN10R1_20261020\proof-kernel-build.log`.

The hosted C++ AppModel consumers `app_manifest_loader.cpp` and `app_manifest_validator.cpp` also passed syntax compilation with `g++ -std=c++17`, the language standard set by `guideXOSServer.vcxproj`. This confirms the C-header include continues to compile in the hosted configuration.

### Native package and regressions

The native package build completed with the existing sample command, `powershell -ExecutionPolicy Bypass -File sdk/samples/stellarnavigator/build.ps1`. Candidate identity:

- Path: `Apps/StellarNavigator/bin/amd64/stellarnavigator.elf`
- SHA-256: `6d5f1ca1b04a9ddbaf08e2eed8600ffaf776c06b6e45aab435f61fda9c2e66eb`
- Format: ELF64 AMD64 static `ET_EXEC`, entry symbol `gx_main` at `0x201a40`
- Dependencies: no interpreter, dynamic section, or relocations
- Manifest: `com.guidexos.stellarnavigator`, `kind: NativeElf`, `runtime: native-elf`, AMD64 entry `gx_main`, ABI `guidexos-c-abi-v1`, visible in the Start Menu

The existing launch registration resolves the validated NativeElf package ID to the bare-metal native ELF launcher through the Desktop/AppModel path. This confirms the package wiring in source and manifest; it is not guest launch evidence.

The focused native ephemeris suite passed **15/15**. It includes the fixed ET/TDB 0 Earth state, authenticated 13-segment DE440s index, deterministic repeat, invalid input, coverage, short-read, hash, size, teardown, and reinitialization checks. The current candidate matches the prior SN10 package hash. ELF inspection is recorded in `E:\bkup\artifacts\SN10R1_20261020\native-elf-inspection.txt`; native build and test logs are beside it.

The accepted SN9 Release regression was rerun from an isolated validation copy so the pre-existing build-output worktree entries stayed intact. Results were **323 passed, 0 failed, 0 skipped**. The clean test compile emitted the known inherited xUnit2002 warning at `StellarNavigator.Tests/EphemerisTests.cs:51`; that unrelated assertion was not changed. The SN9 Release GUI build passed with **0 warnings and 0 errors**. Logs are `E:\bkup\artifacts\SN10R1_20261020\SN9-Release-tests.log` and `E:\bkup\artifacts\SN10R1_20261020\SN9-GUI-Release-build.log`.

### Numerical comparison method

The trusted hosted SN4 reference for Earth (NAIF 399) at ET/TDB `0` seconds past J2000, in the J2000 SSB frame and kilometres, is `(-27566740.483, 132361381.154, 57418653.286)`. The native core test compares each computed double component against these reference coordinates with an absolute tolerance of `0.0005 km`, corresponding to the documented three-decimal display precision (`0.001 km`). The native focused test passed. There is no guest observation or guest numerical delta because QEMU was not launched.

### QEMU census and guest status

A fresh system-wide process census at `2026-10-20T10:40:40Z` found QEMU PID `2956`, executable `C:\Program Files\qemu\qemu-system-x86_64.exe`, using firmware and guest files from `D:\dev\guideXOSServerV0.5_DEVELOPER_STUDIO`. No `qac` command or QAC process was available. The session was identified as another project's guest and was left untouched. The census is recorded in `E:\bkup\artifacts\SN10R1_20261020\qemu-census-blocked.json`.

No SN10R1 guest boot was attempted. Therefore there is no serial log, screenshot, guest AppModel launch result, guest ephemeris output, guest invalid-data behavior, close/reopen evidence, or post-exit desktop-stability evidence. The existing QEMU checks remain open until an exclusive lane is available. This is the reason for Outcome B; native build success is not being presented as native guest execution or interactive qualification.

### Outcome and remaining work

**Outcome B — SN10 partial.** The header compatibility failure is repaired; the proof kernel, native package, focused tests, SN9 regression, and SN9 GUI build pass. `.phase` remains `SN10`. Do not advance it until an exclusive guest run verifies normal desktop initialization, AppModel package discovery and launch, the visible responsive shell, the authenticated ET/TDB 0 result and units/frame, corrupt or missing-data handling, normal close and reopen, and desktop stability. Capture serial output, screenshots, candidate hashes, and measured numerical differences from that run.

After SN10 guest acceptance, suggested SN11 scope remains a read-only native viewer with user-selected target and ET epoch, coverage-aware errors, and parity checks against the managed provider.

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

## Prior SN10 report evidence (before SN10R1)

- Native package build passed using LLVM `clang++` and `ld.lld`. The output is ELF64 AMD64 static `ET_EXEC`, has the `gx_main` entry point, and has no `PT_INTERP` or relocations. Candidate SHA-256: `6d5f1ca1b04a9ddbaf08e2eed8600ffaf776c06b6e45aab435f61fda9c2e66eb`.
- Focused portable native-core tests: **15 passed, 0 failed**. They cover authenticated open and index validation, the fixed-ET Earth state and managed parity, deterministic repetition, invalid and unsupported inputs, coverage bounds, short reads, hash and size failures, and reinitialization after teardown.
- At the time of the earlier SN10 report, the accepted SN9 regression baseline was documented as **323 passed, 0 failed, 0 skipped** and had not been rerun for that native-only slice. SN10R1 reran it; see the current results above.
- The repository’s pinned Mbed TLS/TF-PSA dependency bootstrap completed and `verify-mbedtls-profile.ps1` passed for the locked Mbed TLS 4.1.0 and TF-PSA-Crypto 1.1.0 revisions. This populated the ignored `third_party/mbedtls` dependency tree.
- In the earlier SN10 attempt, the proof-kernel build stopped at `<cstddef>` under `-nostdinc++`; SN10R1 resolves this failure as recorded above. The historical guest census found another project's QEMU session and did not interact with it.

The QEMU runner uses OVMF and the repository's UEFI bootloader path, an isolated writable FAT ESP, serial logs, and the AppModel proof hook. The earlier report had no guest evidence because the proof kernel had not built and another project's QEMU occupied the lane. SN10R1 fixes the kernel build; the foreign QEMU session still prevents this task from launching a guest.

## Remaining SN10 gates

1. Run the already-built proof kernel and native package through an available exclusive QEMU lane.
2. Confirm package discovery and AppModel dispatch, authentic guest ephemeris evaluation and numerical parity, successful first draw, controlled invalid-data behavior, close/reopen, loader cleanup, and desktop stability from guest evidence.

Keep `.phase` at `SN10` until these gates pass. Once SN10 is accepted, a suitable SN11 scope is adding user-selected targets and ET epochs to the native read-only viewer, with coverage-aware errors and parity checks against the managed provider.
