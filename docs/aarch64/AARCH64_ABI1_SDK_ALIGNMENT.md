# AARCH64 ABI1 SDK Alignment — Phase 12


- **Outcome:** `SDK_DEPENDENCY_IDENTIFIED` (B)
- **Audit date:** 2026-11-01
- **Repository:** `D:\dev\guideXOSServer_AARCH64`
- **Branch and starting HEAD:** `AARCH64_SUPPORT`, `1d596d4eaeb194bddeaae53f032b14989deac5df`
- **Upstream at audit:** `origin/AARCH64_SUPPORT` at `2d80cf60a67b87f96de7f79a8a0357088f9f894a`; local branch was two commits ahead.
- **Starting worktree:** no tracked modifications; existing untracked qualification outputs were preserved.

## Original failure and root cause

`scripts/build-aarch64-phase12.ps1` passed the AARCH64 Server root to the
standalone Developer Studio build as `-ServerRoot`. The Developer Studio
`build.ps1` defaults `-SdkInclude` to `<ServerRoot>/sdk/include`; it then puts
that path before `-Isrc` for the ARM64 application and ABI test. Developer
Studio has no copied SDK headers of its own, so this is not a stale copied
header or a shadowing include-order defect. The wrapper paired a Studio source
revision written for the v0.5 SDK with the AARCH64 Server SDK.

At the pinned Developer Studio revision, `058c83fe1ae270a103eadc7caca961328e8abe2a`,
`src/developer_studio_app_activation.cpp` and
`tests/document_activation_abi_test.cpp` access
`gx_host_calls.get_document_activation_path`. The initial integration build
fails when compiling those files against the AARCH64 SDK:

```text
error: 'gx_host_calls' has no member named 'get_document_activation_path'
```

The failure was reproduced by running the external build from its own checkout
with `-SdkInclude D:\dev\guideXOSServer_AARCH64\sdk\include` and a scratch
`ServerRoot`. The App Model activation tests passed `145/145` checks and
`100/100` lifecycle cycles before the ABI compile failed. No source in the
Developer Studio repository was changed.

## Authoritative definitions and provenance

The target contract is the AARCH64 Server SDK at
`sdk/include/guidexos/abi.h`, from the requested starting revision. Its Git
blob is `97f49d2492e9c3c670b170f20358cb77f002c9d2` and its SHA-256 is
`10bf1243bf56bdf14170b24d90bc3b949f7a2ba75b6ce4b563d70e976a30f5d2`.
The existing native ABI test measures this definition as a 400-byte table.

The v0.5 Developer Studio SDK is from the `guideXOS-Server` repository at
`git@github.com:guideX/guideXOS-Server.git`, ref
`origin/v0.5_DEVELOPER_STUDIO`. The document activation callback was added by
revision `3b47738fbd4616990c05c11d6e1f88fe08d58b4c`; the requested 464-byte
contract, including the appended Build cancellation slot, is present at
revision `f63a7710a30b49f785aac134abf06a3a6c5b5aca` in
`sdk/include/guidexos/abi.h`. That file is Git blob
`4513bb24930cf7a4069f5f82e51897d07bd52f4a`, SHA-256
`7fa89a1d996defa69fdeb0f03ea83b02276ce303ef3288f55e34f253756a08d7`.

`scripts/phase12-sdk-alignment.json` locks the Developer Studio source
revision, both sets of ABI inputs, and their Git blob identities. The audit
script checks the checked-out SDK files against the pinned v0.5 source commit
before compiling. If that dependency is missing or differs, it emits a
`PHASE12_SDK_DEPENDENCY_*` diagnostic and stops instead of falling back to the
current Server headers.

## Contract comparison

Both tables use natural structure layout, no packing pragma, and 8-byte
function pointers for the supported 64-bit targets. The regression checks
assert pointer size and table alignment. `GX_CALL` selects the Microsoft x64
calling convention on x86-64 builds; it does not change the table's field
offsets. On AArch64 the compiler uses the target AAPCS64 calling convention.

| Contract | Selected field positions | Size | Result |
| --- | --- | ---: | --- |
| AARCH64 Server SDK | `guiVersion` 240; `bare_metal_build_project_start` 288; final current slot ends at 400 | 400 | Host table used by this Server's AARCH64 runtime |
| v0.5 Developer Studio SDK | `bare_metal_build_project_start` 240; `native_window_create` 352; `get_document_activation_path` 448; `bare_metal_build_project_cancel` 456 | 464 | Source contract expected by the pinned standalone Studio test |

The v0.5 table is a different field layout, not a safe append-only extension
of the AARCH64 Server table. At offset 240, the AARCH64 runtime exposes
`guiVersion`; the v0.5 header interprets that location as
`bare_metal_build_project_start`. Compiling the Studio package with the v0.5
header would make it read callback pointers from different slots when run by
the AARCH64 runtime. The callback at offset 448 does not make those earlier
slots compatible.

The historical AARCH64 400-byte contract is retained unchanged. Its existing
layout test now also asserts the 64-bit pointer assumption, 8-byte natural
alignment, and the size/version prefix offsets.

## Integration change and safeguards

The Phase-12 wrapper now resolves and passes the AARCH64 SDK include directory
explicitly. Before starting the kernel build or asking Developer Studio to
write a package, it verifies the pinned inputs and refuses the incompatible
pairing with `PHASE12_SDK_CONTRACT_BLOCKED`. The diagnostic explains the
callback and slot-offset conflict. This preserves the existing ABI and avoids
building or staging an ARM64 app with the wrong table definition.

The regression entry point is:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\test-aarch64-phase12-sdk-alignment.ps1 `
  -OutputDirectory .\out\phase12-sdk-alignment-run
```

It verifies the pinned Studio and SDK revisions, checks that the compiler
resolves `guidexos/abi.h` from the selected include root, runs the pinned v0.5
layout test, compiles both SDK size/offset/alignment probes for the host and
`aarch64-none-elf`, links and runs the genuine document activation test, runs
the AARCH64 400-byte ABI test, and confirms the v0.5 consumer fails to compile
against the incompatible AARCH64 header. The script refuses to reuse an
existing output directory.

## Build and test evidence

| Check | Result |
| --- | --- |
| SDK alignment regression script | PASS; v0.5 include resolved to `D:/dev/guideXOSServerV0.5_PHASE4R1_QAC_ADMISSION/sdk/include/guidexos/abi.h` |
| v0.5 SDK layout assertions | PASS; 464 bytes, activation offset 448, Build start offset 240 |
| v0.5 layout probe for `aarch64-none-elf` | PASS |
| AARCH64 SDK 400-byte layout for host and `aarch64-none-elf` | PASS; GUI version 240, Build start 288, final callback 392 |
| Genuine Developer Studio document activation ABI test | PASS; `developerStudioDocumentActivationAbiChecks=9/9`; compiled and linked against the pinned v0.5 SDK |
| AARCH64 400-byte native ABI layout | PASS |
| Incompatible SDK negative control | PASS; AARCH64 headers reject the Studio consumer at `get_document_activation_path` |
| Missing SDK negative control | PASS; emits `PHASE12_SDK_DEPENDENCY_MISSING` with the pinned source and revision |
| Phase-10 host application loading controls | PASS; ARM64 resolver selected `bin/arm64/proof.elf` (`EM_AARCH64`), AMD64 resolver selected `bin/amd64/proof.elf` (`EM_X86_64`), and wrong-machine, missing, traversal, malformed, oversized, duplicate, and legacy cases passed |
| Phase-12 freestanding foundation build | PASS via `build-aarch64-phase10.ps1 -Phase12Proof`; ARM64 UEFI loader, Phase-12 kernel, and ARM64/AMD64 proof ELFs built and verified |
| GICv3 hosted suite | PASS; **62 assertions**, unchanged |
| Phase-12 Developer Studio integration build | BLOCKED before package writes by `PHASE12_SDK_CONTRACT_BLOCKED`; direct isolated reproduction failed at the same missing callback |
| QEMU or physical hardware | Not run |

The regression run used `out/phase12-sdk-alignment-regression6`, the
freestanding Phase-12 foundation build used
`out/aarch64-phase12-alignment-build`, and the 62-assertion hosted GICv3 run
used `out/aarch64-gicv3-host-phase12-alignment`. The isolated Developer Studio
failure used `out/phase12-integration-repro2/server-root`. The missing SDK
negative control wrote no output.

The successful hosted and freestanding checks can be repeated with:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\test-aarch64-phase12-sdk-alignment.ps1 `
  -OutputDirectory .\out\phase12-sdk-alignment-run
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-aarch64-phase10.ps1 `
  -Phase12Proof -OutputDirectory .\out\aarch64-phase12-alignment-build
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\test-aarch64-gicv3-host.ps1 `
  -OutputDirectory .\out\aarch64-gicv3-host-phase12-alignment
```

The checked build artifacts are in the task's fresh `out/` directories. The
Phase-12 wrapper's preflight failure created no output directory and did not
remove or replace `Apps/DeveloperStudio/bin/arm64/developerstudio.elf`.

## Remaining limitation

The v0.5 SDK is locally available and its genuine activation ABI test passes,
but it cannot safely be selected for a package consumed by this AARCH64 Server
runtime. The 400-byte AARCH64 SDK also cannot compile the pinned Studio source
that requires the activation callback. Completing outcome A requires a
coordinated Developer Studio implementation that targets the AARCH64 Server
table (or a separately designed, coordinated Server ABI evolution with
runtime support and compatibility coverage). No such source or runtime change
was made here. No dummy callback, synthetic field, cast, or no-op was added.

No QEMU launch, QAC admission, or hardware test was attempted.

## Follow-up verification in the supplied checkout

For the follow-up run on 2026-11-01, the checkout was already at
`eecc865ea16bc358cab762ae0fd786bfb7f82964` on `AARCH64_SUPPORT`, three local
commits ahead of `origin/AARCH64_SUPPORT` at
`2d80cf60a67b87f96de7f79a8a0357088f9f894a`. This differs from the requested
starting revision `1d596d4eaeb194bddeaae53f032b14989deac5df`; the follow-up
did not reset or rewrite that history. Tracked files were clean at entry. All
pre-existing untracked `out/` artifacts were preserved.

The regression script was rerun after adding a compiler dependency-file check
for the AARCH64 SDK include path. It resolved the two genuine headers to their
separate pinned roots, passed the v0.5 layout test and the document activation
test (`9/9`), passed the AARCH64 400-byte layout test for both the host and
`aarch64-none-elf`, and rejected the v0.5 consumer when given the AARCH64 SDK.
Output: `out/phase12-sdk-alignment-verification2-20261101`.

The Phase-12 freestanding foundation rebuild passed. It compiled the ARM64
UEFI loader and kernel, linked the AARCH64 and AMD64 proof applications, and
ran the hosted Phase-10 loader controls. Those controls selected
`bin/arm64/proof.elf` as `EM_AARCH64`, selected `bin/amd64/proof.elf` as
`EM_X86_64`, and passed the missing, unknown, wrong-machine, traversal,
malformed, oversized, duplicate, and legacy cases. Output:
`out/aarch64-phase12-verification-20261101`.

The GICv3 hosted suite passed all 62 assertions in
`out/aarch64-gicv3-host-verification-20261101`. The Phase-12 integration
wrapper was invoked separately and stopped with
`PHASE12_SDK_CONTRACT_BLOCKED` before creating its requested output directory
or writing a package. QEMU, hardware, and QAC were not run.
