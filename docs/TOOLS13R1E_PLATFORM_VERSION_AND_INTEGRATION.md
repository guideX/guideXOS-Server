# TOOLS13R1E — Platform Version and Isolated Server Integration

**Outcome B — `INTEGRATION_READY_VERSION_APPROVAL_PENDING`**  
**Date:** 2026-11-08 (Pacific)

The accepted TOOLS13R1D window bridge is integrated and hosted-verified in a clean, isolated Server worktree. The minimum-platform-version meaning remains undocumented and needs an explicit Server platform-owner decision. Guest packaging and QAC admission were not started.

## Repository discovery and provenance

| Checkout | Branch / HEAD | State and use |
|---|---|---|
| Developer Studio Tools source, `D:\dev\guideXOS_DeveloperStudio_Tools_Source` | `DEVELOPER_STUDIO_TOOLS`, `48f2da543517bd7238378c5a18769be148b10863` | Clean; accepted TOOLS12 ELF and package inputs are present under `out`. |
| Server Tools integration, `D:\dev\guideXOS_DeveloperStudio_Tools` | `DEVELOPER_STUDIO_TOOLS`, `a9ea040e25160961999c3648d94024a0c866fdc8` | Clean, two commits ahead of its remote; used for this decision record and the compatibility runner. |
| Original Server candidate, `D:\dev\guideXOSServerV0.5_DEVELOPER_STUDIO` | `v0.5_DEVELOPER_STUDIO`, `801b4763aa09ddd6cab06bf726610063ca0389de` | Left untouched. Its 36 pre-existing untracked output trees remain in place. |
| TOOLS13R1D repair checkout, `D:\dev\guideXOSServerV0.5_TOOLS13_WINDOW_BRIDGE` | `TOOLS13_WINDOW_BRIDGE`, `37c15be49342ed833972ac819e88c88af144548d` | Clean; original repair commit preserved. |
| Isolated integration candidate, `D:\dev\guideXOSServerV0.5_TOOLS13R1E_INTEGRATION` | `codex/tools13r1e-server-integration`, `e712106509473247eba959a494b65595769372a6` | Clean dedicated worktree; the hosted test runner portability correction is committed here. |

The candidate lineage is `801b4763` (Server baseline) → `37c15be4` (window bridge) → `e7121065` (normalize Windows CRLF before passing the bridge harness to WSL Bash). The integration commit changes only the hosted test runner; it does not alter runtime code. The runner correction was needed because its Windows here-string had left carriage returns in Bash source and output paths. The first invocation exposed that issue; after the one-line correction the full runner passed. No branch in an existing checkout was switched, and no production package, push, merge, reset, stash, or cleanup of pre-existing outputs was performed. The development app-model regression writes a binary under a tracked `tmp` path; after the test, that test-created binary was restored to its clean candidate-HEAD contents.

## Accepted application and ABI identity

The tested AMD64 application is the existing TOOLS12 artifact, not a rebuild:

| Artifact | Bytes | SHA-256 |
|---|---:|---|
| `out\TOOLS12\dual-architecture-package\Apps\DeveloperStudio\bin\amd64\developerstudio.elf` | 1,262,228 | `9CBC32163417A553B38A1F01EF69CDBAF49B99C58B9D63D0F06E2E9A31C1AC16` |
| Matching `app.json` | — | `47A9C855AE32A372CC7674350A9E7255BB58927FF943798CC929F6EDFCBCF1BE` |
| Packaged ARM64 ELF | 1,448,964 | `9CEB966F4AD74EE70E138F7A2BA1797FCE9B50A007F3E57E74BD3C5EAD365AB7` |

The manifest names application version `0.1.0`, minimum guideXOS version `0.5.0`, ABI `guidexos-c-abi-v1`, API table version `0`, and the `native-elf` runtime. The accepted host-call table remains 464 bytes with 57 pointer slots. No application binary, manifest, kernel version, ABI version, or SDK layout was changed.

## Version-source findings

| Version concept | Evidence | Finding |
|---|---|---|
| Kernel version | `kernel/core/include/kernel/version.h` defines `KERNEL_VERSION_STRING` as `0.1.0` (introduced in Server commit `3489b7bc`). | Identifies the kernel version. No source or policy says it is the Server product/platform version. |
| Server platform/product version | No separate authoritative product-version field or version-policy document was found in the inspected Server and Tools sources. | Unknown for this candidate. The `v0.5_DEVELOPER_STUDIO` branch name is not version evidence. The only Server tag in the checkout is `v0.2`; it does not define a mapping for this branch. |
| Native ELF ABI | `sdk/include/guidexos/abi.h` declares `GX_ABI_NAME` as `guidexos-c-abi-v1` and `GX_API_VERSION` as `0`. | ABI identity and table version are separate from a platform release version. Compatibility also depends on table layout and required callback support. |
| Application version | The TOOLS12 `app.json` has `version: 0.1.0`. | Version of Developer Studio itself; it is independently chosen and need not equal a Server version. |
| Package manifest schema | `app.json` has `schemaVersion: 1`; Developer Studio's package audit requires schema 1. | Identifies manifest structure, not the platform version. |
| Minimum platform metadata | `minGuideXOSVersion: 0.5.0` is copied into `AppManifest` by `app_manifest_loader.cpp`. | The inspected validator and launch paths do not compare this field with a Server platform version. |

The Developer Studio source history shows `minGuideXOSVersion: 0.5.0` in the initial manifest commit (`d4787670522e4c242f9f012664dcf126944c46dc`), without an associated version-policy rationale. The Server validator checks the manifest schema and ordinary fields but not this minimum; the package audit checks schema and ELF/package identity but does not compare platform versions. The branch name cannot supply the missing rule.

**Owner clarification in this thread:** `v0.5_DEVELOPER_STUDIO` was a guess used as a branch name. It does not require Developer Studio itself to release as version `0.5.0`; the application's own version may be chosen independently. This does not change the separate manifest field `minGuideXOSVersion: 0.5.0`, which still lacks an authoritative Server platform-version comparison rule.

**Compatibility decision:** no authoritative mapping exists between the kernel version, Server platform version, and `minGuideXOSVersion`. Therefore this record cannot conclude that the candidate meets or fails the application's platform minimum. The field is not enforced by the inspected production loader. Keep kernel `0.1.0`, app minimum `0.5.0`, and every ABI value unchanged until the owners decide the contract. No metadata correction is currently justified, and the minimum must not be lowered.

## Proposed platform-version contract — for owner approval only

This proposal is not adopted policy and does not authorize a version change.

1. **What the version identifies:** a public guideXOS Server platform release, including the kernel and the supported Native ELF runtime surface. Keep kernel build version, application version, Native ELF ABI version, and manifest schema version independent.
2. **Which component defines it:** the Server platform/release owner maintains one canonical numeric `GUIDEXOS_PLATFORM_VERSION` value in Server-owned release metadata. The exact value is embedded in the built Server and exposed by its version-reporting path. A branch name or kernel build number is never used as a substitute.
3. **How minimum versions compare:** parse `minGuideXOSVersion` as `major.minor.patch` and compare it numerically with the canonical platform version. Malformed or unknown values fail closed. A package passes this gate only when the platform version is equal to or greater than its declared minimum.
4. **How ABI compatibility is checked:** independently require the manifest ABI identity, ABI/API version, table size/layout, required callback set, and declared runtime capabilities to match the Server. A platform-version match alone does not certify ABI compatibility, and ABI compatibility does not satisfy the platform-version gate.
5. **How version changes are approved and tested:** the Server platform owner approves the canonical value and release notes. CI tests equality, lower/higher boundaries, malformed values, unsupported ABI/API versions, required callbacks, and capability mismatches against representative manifests. A version change ships only with those checks and a package audit.
6. **How older manifests are handled:** continue parsing supported `schemaVersion: 1` manifests. A schema-1 manifest with no minimum is marked compatibility-unknown; after enforcement is introduced it cannot install or launch until its owner publishes a reviewed manifest update with an explicit minimum. Do not silently default it to `0.0.0`. Unsupported schema versions and malformed declared minimums fail closed. New packages must declare the minimum.

The Server platform owner must approve or replace this proposal and state the authoritative platform version for commit `e7121065`. The Developer Studio owner must confirm that the existing `0.5.0` field is intended to mean that platform version. No code or metadata change should precede those decisions.

## Isolated hosted validation

All runtime-facing checks below used the isolated candidate at `e712106509473247eba959a494b65595769372a` and the accepted TOOLS12 ELF. There were **9/9 passing hosted gates**. The bridge harness ran in both available configurations; the other legacy runners expose one optimized configuration.

| Gate | Result and exact scope |
|---|---|
| Native ABI layout | PASS. `native_abi_layout_test.cpp` compiled 191 `static_assert`s and ran 4 append-order invariants. Host table is 464 bytes / 57 callback slots. |
| Accepted production ELF validation | PASS. 10 explicit checks on the exact 1,262,228-byte AMD64 ELF: ELF policy, fixed base, entry `0x500D36F0`, three load segments, mapped bound, and 512 KiB aligned stack. Reported mapped extent: 16,093,184 bytes. |
| Window bridge — Debug | PASS, 42/42 checks. |
| Window bridge — Release | PASS, 42/42 checks. |
| Existing Native ELF validator regression | PASS, 13/13 checks. |
| Existing Native ELF runtime regression | PASS, 18/18 checks. |
| Existing Native ELF trampoline regression | PASS; one execution verified 7 return, status, register-preservation, and stack-range invariants. |
| Existing Native ELF development app-model regression | PASS, 13/13 checks. |
| TOOLS12 package audit | PASS. Exactly 3 package files: manifest, AMD64 ELF, and ARM64 ELF; both ELF architectures, hashes, and manifest entries matched. |

The native hosted checks contain 340 explicit compile-time/runtime assertions across the ABI (195), accepted ELF (10), bridge Debug/Release (84), and four legacy regressions (51), plus the passing package audit. The bridge tests exercise both `request_window` and `request_window_ex` callback paths, real compositor IDs and ownership, drawing/repaint, frame presentation, focus and key-down/mouse dispatch, close/teardown, failed creation, stale handles, and the bounded full-span app-memory guard. Both bridge configurations emitted the same unused-variable warning for `s_bareDebugStrings`; each completed and passed.

Commands used for reproduction:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\run-tools13r1c-runtime-compatibility-test.ps1 `
  -ServerRoot D:\dev\guideXOSServerV0.5_TOOLS13R1E_INTEGRATION `
  -AcceptedElf D:\dev\guideXOS_DeveloperStudio_Tools_Source\out\TOOLS12\amd64-acceptance-stage\Apps\DeveloperStudio\bin\amd64\developerstudio.elf

powershell.exe -NoProfile -ExecutionPolicy Bypass -File D:\dev\guideXOSServerV0.5_TOOLS13R1E_INTEGRATION\scripts\run-native-elf-host-test.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File D:\dev\guideXOSServerV0.5_TOOLS13R1E_INTEGRATION\scripts\run-native-elf-runtime-host-test.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File D:\dev\guideXOSServerV0.5_TOOLS13R1E_INTEGRATION\scripts\run-native-elf-trampoline-host-test.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File D:\dev\guideXOSServerV0.5_TOOLS13R1E_INTEGRATION\scripts\run-native-elf-development-app-model-host-test.ps1

powershell.exe -NoProfile -ExecutionPolicy Bypass -File D:\dev\guideXOS_DeveloperStudio_Tools_Source\tests\validate-developer-studio-package.ps1 `
  -ServerRoot D:\dev\guideXOSServerV0.5_TOOLS13R1E_INTEGRATION `
  -PackageRoot D:\dev\guideXOS_DeveloperStudio_Tools_Source\out\TOOLS12\dual-architecture-package\Apps\DeveloperStudio
```

These are hosted checks, not execution of `gx_main`. They do not exercise real guest input, VFS operations, or the application's Build/Run/Debug callbacks end to end. No guest image or QEMU process was started.

## Remaining input limitation

The Native ELF bridge has no keyboard key-up dispatch path. `KernelCompositor` exposes `handleKeyDown`; the bridge overrides `onKeyDown`; the generic `GX_KEY_ACTION_UP` enum does not create a producer callback. Consequently a guest app cannot reliably clear its own held Shift/Ctrl/Alt state from a release event. Modifier-dependent shortcuts or selections may retain stale held-key state until the app resets it (for example after focus changes). This phase does not add an input subsystem.

## Guest packaging plan

The image can be staged without modifying a production Server package: build from a second disposable worktree at the integrated candidate commit and put all generated files under that worktree's `ESP` directory. This procedure was not executed.

- **Server source:** `e712106509473247eba959a494b65595769372a` (repair `37c15be49342ed833972ac819e88c88af144548d` on baseline `801b4763aa09ddd6cab06bf726610063ca0389de`).
- **Application:** accepted AMD64 ELF and matching manifest from `D:\dev\guideXOS_DeveloperStudio_Tools_Source\out\TOOLS12\dual-architecture-package\Apps\DeveloperStudio`; keep the audited 3-file package unchanged.
- **ABI:** `guidexos-c-abi-v1`, API version `0`, 464-byte table, 57 slots.
- **Version disposition:** owner approval pending. Do not lower `minGuideXOSVersion`, relabel the kernel version, or bypass a future comparison.
- **Guest fixture source:** `D:\dev\guideXOS_DeveloperStudio_Tools_Source\out\TOOLS13R1\fixture\DeveloperStudioNavigationSmoke` (four source/readme files). Planned guest workspace: `<private ESP>\TOOLS13R1C\workspace`, guest path `/TOOLS13R1C/workspace`; keep it outside `Apps\DeveloperStudio` and do not enable Replace Apply.
- **Image build:** create a disposable worktree at the commit above, then run `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\build.ps1 -Arch amd64` from that worktree. Do not pass `-Clean` or `-RunQemu`. Expected staged boot inputs are `ESP\EFI\BOOT\BOOTX64.EFI`, `ESP\kernel.elf`, and `ESP\ramdisk.img`; current Server launch scripts use a directory-backed FAT ESP, not an ISO. Then copy the exact package contents to `ESP\Apps\DeveloperStudio` and the fixture to the workspace destination.
- **Image audit:** run `tests\validate-developer-studio-package.ps1` against the staged package. Produce a sorted SHA-256 inventory for `BOOTX64.EFI`, `kernel.elf`, `ramdisk.img`, every fixture file, and all package files. Verify the staged kernel equals `kernel\build\amd64\bin\kernel.elf`; verify the AMD64 ELF remains exactly 1,262,228 bytes and the accepted SHA-256. Reject unexpected package files.
- **QAC launch:** a TOOLS13R1-specific runner is required. The existing `Phase29YUseStagedDeveloperStudioPackage` path is pinned to a different staged ELF and is not valid for this artifact without an owned, reviewed update. After authorization, the runner must call the QAC CLI with the direct QEMU child (`qac run --project DeveloperStudio --mode acceptance -- <qemu-system-x86_64.exe> <arguments>`), use the existing Server boot arguments with loopback QMP, and never start QEMU while queued or without a grant.
- **QAC CLI:** the documented local apphost is `D:\dev\guideXOS_QAC\src\GuideXOS.QAC.Cli\bin\Debug\net10.0-windows\GuideXOS.QAC.Cli.exe`. The future runner must preserve the direct-child argument boundary and use QAC admission; this record did not invoke the CLI or request a reservation.
- **Evidence and cleanup:** keep QAC host trace, serial output, and a guest screenshot in one run-specific evidence directory; associate them with the staged image/package hashes and reservation ID. Require the bounded Tests A–E, visible first frame, keyboard/mouse interaction, and clean window close. Request QMP shutdown, verify QEMU exit and QAC `Released`, then confirm no owned session remains before removing only disposable build/image data. Preserve evidence.

## QAC baton and exact next action

The latest local QAC coordination closeout, `D:\dev\guideXOS_QAC\docs\QAC_SHARED_QEMU_COORDINATION_PHASE4R2.md`, says **.NET Support retains first QEMU priority** and requires a separately authorized handoff. Formal Phase30B remains 0/5 in that record. The last documented live census in the TOOLS13R1 preflight (2026-11-07) was idle, but an idle coordinator does not release the owner baton. No reservation was requested and no QEMU command was run here.

**Next action toward the first AMD64 guest acceptance:** the Server platform owner records the canonical platform-version contract and its decision for candidate `e7121065`; the Developer Studio owner confirms the meaning of its existing `0.5.0` minimum. Then obtain explicit .NET Support baton release or an independently authorized Tools lane. Only after both independent gates are cleared should a private guest-build worktree, TOOLS13R1 QAC runner, and managed reservation be prepared.

The future isolated build/staging sequence is:

```powershell
git -C D:\dev\guideXOSServerV0.5_TOOLS13R1E_INTEGRATION worktree add --detach `
  D:\dev\guideXOSServerV0.5_TOOLS13R1E_GUEST_BUILD e712106509473247eba959a494b65595769372a6
Set-Location D:\dev\guideXOSServerV0.5_TOOLS13R1E_GUEST_BUILD
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\build.ps1 -Arch amd64
New-Item -ItemType Directory -Force .\ESP\Apps\DeveloperStudio, .\ESP\TOOLS13R1C\workspace | Out-Null
Copy-Item 'D:\dev\guideXOS_DeveloperStudio_Tools_Source\out\TOOLS12\dual-architecture-package\Apps\DeveloperStudio\*' .\ESP\Apps\DeveloperStudio -Recurse -Force
Copy-Item 'D:\dev\guideXOS_DeveloperStudio_Tools_Source\out\TOOLS13R1\fixture\DeveloperStudioNavigationSmoke\*' .\ESP\TOOLS13R1C\workspace -Recurse -Force
```

The command block is a plan only; it was not run in this phase. Audit the staged package and full SHA-256 inventory before any later QAC-owned launch.

## Local commits

- Server isolated integration: `e712106509473247eba959a494b65595769372a` — Windows/WSL hosted-runner line-ending fix; parent is accepted bridge repair `37c15be49342ed833972ac819e88c88af144548d`.
- This decision record: committed locally in the Server Tools integration repository; no push.
