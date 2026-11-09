# TOOLS13R1F — Experimental Guest Acceptance Preparation

**Outcome D — `EXPERIMENTAL_ENVIRONMENT_BLOCKED`**

**Date:** 2026-11-08 (Pacific)

The accepted TOOLS12 application and the integrated Server window bridge were verified. A private AMD64 guest-build worktree was created, but the Server kernel build stopped because the isolated checkout has no `../third_party/mbedtls` make target. The failed build was preserved. No complete guest image was produced, so the app and fixture were not staged and no guest test was run. This is a build-environment block; it does not establish a runtime incompatibility or platform-version failure.

## Repository and artifact baseline

| Role | Checkout / branch | HEAD | State and use |
|---|---|---|---|
| Server Tools evidence | `D:\dev\guideXOS_DeveloperStudio_Tools`, `DEVELOPER_STUDIO_TOOLS` | `692cdfb240695901f6242f604b7d91c4661f5c13` at start | Clean; two local commits ahead of its remote. This report is the only task-owned tracked change. |
| Developer Studio Tools source | `D:\dev\guideXOS_DeveloperStudio_Tools_Source`, `DEVELOPER_STUDIO_TOOLS` | `48f2da543517bd7238378c5a18769be148b10863` | Clean. Existing TOOLS12 application and TOOLS13R1 fixture were read only. |
| Integrated Server candidate | `D:\dev\guideXOSServerV0.5_TOOLS13R1E_INTEGRATION`, `codex/tools13r1e-server-integration` | `e712106509473247eba959a494b65595769372a6` | Clean at inspection; exact requested candidate. |
| Private guest-build staging worktree | `D:\dev\guideXOS_DeveloperStudio_Tools_Source\out\TOOLS13R1F-guest-image` | detached at `e712106509473247eba959a494b65595769372a6` | New isolated worktree. Partial build products and the failure record are confined here. |

The integrated Server lineage is baseline `801b4763aa09ddd6cab06bf726610063ca0389de`, window bridge repair `37c15be49342ed833972ac819e88c88af144548d`, and integration commit `e712106509473247eba959a494b65595769372a6`. The integration commit changes only the hosted runner's Windows/WSL line-ending handling; it does not change Server runtime code. Existing branches and worktrees were not switched, reset, stashed, cleaned, or removed. The original integration candidate and source worktrees remained clean after this task.

The accepted AMD64 application was verified in the existing dual-architecture TOOLS12 package. It was not rebuilt or substituted:

| Artifact | Size | SHA-256 |
|---|---:|---|
| `developerstudio.elf` | 1,262,228 bytes | `9CBC32163417A553B38A1F01EF69CDBAF49B99C58B9D63D0F06E2E9A31C1AC16` |
| Matching `app.json` | — | `47A9C855AE32A372CC7674350A9E7255BB58927FF943798CC929F6EDFCBCF1BE` |

Source package: `D:\dev\guideXOS_DeveloperStudio_Tools_Source\out\TOOLS12\dual-architecture-package\Apps\DeveloperStudio`. The manifest is schema 1, application `com.guidexos.developerstudio`, application version `0.1.0`, AMD64 entry point `gx_main`, ABI `guidexos-c-abi-v1`, ABI/API table version `0`, and runtime `native-elf`. It declares `minGuideXOSVersion: 0.5.0`. TOOLS13R1E's hosted package audit recorded the exact audited package contents as the manifest and the AMD64 and ARM64 ELF files. No production package was modified.

## Compatibility classification

| Concept | Observed value | Classification |
|---|---|---|
| Developer Studio application version | `0.1.0` | Application release value, independent of Server releases. |
| Kernel version | `0.1.0` | Kernel version header value; not established as the Server platform compatibility version. |
| Platform compatibility version | Unknown | No authoritative platform-version field or mapping was established for this candidate. |
| Minimum platform metadata | `minGuideXOSVersion: 0.5.0` | Unresolved metadata. The inspected production loader parses/stores it but does not compare it to a Server version. Its satisfaction is not claimed, and this value alone is not evidence of incompatibility. |
| Native ELF ABI | `guidexos-c-abi-v1`, API table version `0` | Separate from application, kernel, and platform versions. The host-call table is 464 bytes with 57 pointer slots. |
| Manifest schema | `1` | Package structure version, not an OS version. |

The project owner clarified that `v0.5_DEVELOPER_STUDIO` was a development branch naming choice, not a required Developer Studio release. No version value was changed, the minimum was not lowered, and no actual loader enforcement was bypassed.

Runtime capabilities needed for meaningful guest evidence include AMD64 Native ELF loading and entry invocation; creating a real compositor window; drawing and presenting frames; keyboard and mouse delivery; workspace VFS operations; Build, Run, and Debug callbacks; and clean application shutdown. Existing hosted checks verify the candidate's ELF and ABI structure and exercise the repaired window bridge. They do not execute the accepted `gx_main` in a guest or qualify those end-to-end capabilities. The bridge still has no keyboard key-up callback; modifier-dependent operations can retain stale held-key state, so future guest input must account for that limitation.

## Integrated Server hosted evidence

The TOOLS13R1E record identifies **9/9 hosted gates passed** against the integrated candidate and accepted TOOLS12 artifact:

- Native ABI layout: 191 compile-time assertions and 4 append-order invariants passed.
- Exact accepted AMD64 ELF: 10 validation checks passed; entry `0x500D36F0`, three load segments, mapped size 16,093,184 bytes.
- Native ELF window bridge: Debug **42/42** and Release **42/42**, including both window request callbacks, compositor ownership, draw/repaint, presentation, focus, key-down/mouse dispatch, close/teardown, failed creation, stale handles, and app-memory bounds.
- Existing regressions: ELF validator **13/13**, runtime **18/18**, trampoline return/status/register/stack invariants, and development app model **13/13**.
- TOOLS12 package audit passed for the manifest and two architecture ELFs.

These are hosted checks and are not guest acceptance. The detailed test record and commands are in `docs/TOOLS13R1E_PLATFORM_VERSION_AND_INTEGRATION.md` in the evidence repository.

## Private image build and preserved failure

The build ran only in the new detached staging worktree with:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\build.ps1 -Arch amd64
```

The UEFI bootloader build completed. The AMD64 kernel build then failed:

```text
mingw32-make: *** No rule to make target '../third_party/mbedtls', needed by 'build/amd64/obj/core/gxos_tls_foundation.o'. Stop.
ERROR: Kernel build failed with process exit code 2
ERROR: Kernel build failed
```

The PowerShell build process exited with code 1. This exact failure is also preserved in `D:\dev\guideXOS_DeveloperStudio_Tools_Source\out\TOOLS13R1F-guest-image\build-failure.txt` (SHA-256 `D2C11F34D5E142426A396AC5EE26D7204D47B94731C265917567AF17F0A08B2D`). No retry, dependency initialization, or Server source modification followed.

The attempted kernel compile also emitted GCC `-Wstringop-overflow` at `core/desktop.cpp:2589` and `-Warray-bounds` at `core/shell.cpp:2096`. These warnings were not investigated after the missing make target stopped the build.

The private worktree's ESP is partial. `ESP\ramdisk.img` is 67,108,864 bytes with SHA-256 `7E93E34732526DBFFEE097912F776467B0DD03C989CBDA06F4AA24B0945A969B`; other listed base files include `desktop.cfg`, `startup.nsh`, and existing P28M/P28O/P28Q fixtures. The required boot inputs `ESP\EFI\BOOT\BOOTX64.EFI` and `ESP\kernel.elf` are absent. `ESP\Apps\DeveloperStudio\app.json` and `ESP\TOOLS13R1C\workspace` are also absent: the accepted package and TOOLS13R1 fixture were not copied after the build failure. Therefore this is not a guest image, and a complete image hash is **not applicable**. No image/package audit or guest fixture path verification was claimed.

The existing TOOLS13R1 fixture source remains at `D:\dev\guideXOS_DeveloperStudio_Tools_Source\out\TOOLS13R1\fixture\DeveloperStudioNavigationSmoke` with `main.cpp`, `helpers.cpp`, `helpers.h`, and `README.md`. It was not copied into the partial ESP.

## QAC ownership and admission

QAC was queried only through its read-only `status`, `list`, and `queue` commands. On the first check, QAC 2.0.0 was online with a managed `guideXOSServer` acceptance reservation (`2cfb19b75c5c4556859566c88df6d7ed`, QEMU PID 26568, owner PID 27916 verified). The queue was empty and no foreign QEMU was reported. That reservation was not touched.

After the private build attempt, a second read-only check showed QAC online with **zero QEMU processes, zero reservations, zero managed sessions, zero queued requests, and zero blocked requests**. This idle state does not release the ownership baton. The latest inspected shared-coordination closeout says .NET Support retains first QEMU priority and requires explicit handoff; no explicit release or independent Tools-lane authorization was found. No TOOLS13R1F reservation was requested and no QEMU was launched.

## Guest test results and safety

No guest boot occurred. Server boot and desktop initialization, Developer Studio startup, compositor window creation, rendering/input, Tools menu, Go to File, Go to Line, Task List, Back/Forward navigation, and clean application shutdown are all **not run**. No serial, visual, or QAC run evidence exists for a TOOLS13R1F guest.

Replace Apply was not enabled. No production package or existing development worktree was modified. The new staging worktree and partial outputs are retained as failure evidence; QAC-managed shutdown or cleanup was not applicable because this task created no QAC reservation or QEMU process. No push, merge, reset, or phase transition was performed.

## Determination and remaining gates

**`EXPERIMENTAL_ENVIRONMENT_BLOCKED`** applies because the isolated Server kernel build could not complete with the available `third_party/mbedtls` make target. It is not a guest failure, a version-compatibility verdict, or release certification. The `0.5.0` minimum remains unresolved metadata.

Meaningful guest testing requires an authorized isolated build environment that supplies the missing kernel build prerequisite and a new private AMD64 image that passes the package/ESP/hash checks. Guest launch additionally requires explicit QAC baton release or independent Tools-lane authorization, followed by one QAC-managed run and retained serial, visual, and QAC lifecycle evidence. Those gates were not met here.
