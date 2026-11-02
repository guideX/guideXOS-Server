# AArch64 ABI3 Developer Studio Guest Qualification

- **Working status:** `STUDIO_GUEST_PARTIAL`
- **Run date:** 2026-11-02
- **Repository:** `D:\dev\guideXOSServer_AARCH64`
- **Branch/base:** `AARCH64_SUPPORT` at `fe4e3ebf909f834f991ebd61d96607a6190dc80e`
- **Upstream at preflight:** `origin/AARCH64_SUPPORT`; local branch was 3 commits ahead, 0 behind.
- **Pinned Developer Studio:** `058c83fe1ae270a103eadc7caca961328e8abe2a`
- **Pinned v0.5 SDK contract:** `f63a7710a30b49f785aac134abf06a3a6c5b5aca`

## Result

The hosted ABI, SDK-alignment, Studio build, and ARM64 package checks passed. One actual QAC-authorized AArch64 guest started. UEFI loaded the ARM64 kernel, which entered at EL1 and validated its stack and firmware handoff. The kernel then rejected the copied firmware DTB with `GXOS_AARCH64_DTB_MALFORMED` (diagnostic 2) and stopped before desktop initialization.

The guest did not discover or start Developer Studio. No Studio window, input, workspace operation, Build/Run workflow, or Studio cleanup was observed. A follow-up request for a diagnostic build queued behind another exclusive acceptance session and was cancelled after the 12-second admission window; it did not launch QEMU. The parser trace added for that candidate has therefore not been observed in a guest.

`STUDIO_GUEST_PARTIAL` records the real but early guest boot. The run did not reach Studio, so the stronger Studio-lifecycle condition for a completed Outcome C result is also unmet. This is an incomplete qualification state, not guest acceptance; the adapter has not been shown running in the guest. There are **0 of 3 qualifying Studio boots**.

## Inputs and production package

| Input | Revision or value |
| --- | --- |
| Server base commit | `fe4e3ebf909f834f991ebd61d96607a6190dc80e` |
| Developer Studio | `058c83fe1ae270a103eadc7caca961328e8abe2a` |
| v0.5 SDK | `f63a7710a30b49f785aac134abf06a3a6c5b5aca` |
| ARM64 package ABI | `guidexos-c-abi-v1-studio-v05-464` |
| Profile | `production-ui`; `GXOS_PHASE12_SMOKE` is disabled in Studio |
| Package kind / entry | NativeElf / `gx_main` / `EM_AARCH64` |
| Guest machine | `virt,gic-version=2,acpi=off`, Cortex-A53, 512 MiB, AArch64 UEFI |

The authentic pinned Studio source was copied into the output tree; its pinned checkout remained clean. The generated overlay changes only the copied Studio build inputs: it restores shared model source files omitted by selected test source lists, imports two namespaced Build diagnostic helpers, and aliases the same-signature `window_destroy` spelling to `native_window_destroy`. It does not change ABI signatures, layouts, callbacks, or service semantics. The exact overlay hashes are recorded in each candidate manifest.

The ARM64 package entry retains the exact v0.5 identity above. The original AARCH64 400-byte host table and the separate v0.5 464-byte table remain independently typed. The adapter is selected from the validated manifest identity before entry dispatch; no ABI guessing or table reinterpretation was added.

The production Studio ELF is 1,321,236 bytes, sectionless `ET_EXEC`, `EM_AARCH64`, entry `0x500E5DF8`, with three `PT_LOAD` segments and no dynamic relocation table. It loads at `0x50000000`; its writable segment reserves about 14.99 MiB, bringing the image span below 16 MiB. The Studio ELF SHA-256 is `a08fc9c4599475165874a983b7d11366dcac2f35f5dc377ad6e875a960f2e483`. The loader's initial stack is 16 pages (64 KiB). These are build and static ELF checks, not proof of guest memory safety.

The packaged `Apps/DeveloperStudio/app.json` SHA-256 is `62af7dce96baa5605092bb561e6c4bfe5428367bff8bbb229d4f01d41691ad4c`.

Two fresh candidates were preserved:

| Candidate | Purpose | Kernel SHA-256 | EFI SHA-256 | RAM disk SHA-256 | Guest result |
| --- | --- | --- | --- | --- | --- |
| `out/abi3-studio-guest-final2-20261102` | Production UI, first diagnostic boot | `a993016d17ee4741d0d36570b5a3a0919e3ada368a9d292dd3cf2e33a54ac48a` | `c6799231c67c594eb752183d611deaad67cd5ce9ef81c0c69e850e9cda7ba4ca` | `b2252465b2b4d72d64cbdb9b3e74e9ae485922ef30c0fe0fca5181ebf6ef0c8e` | Authorized; failed at DTB parsing before desktop |
| `out/abi3-studio-guest-diag-20261102` | Same production UI plus bounded DTB parser trace | `f39ebf064794ee9e89ffe7ec9481b37bfcbf325f3a26526d2beaafd0936a5882` | `c6799231c67c594eb752183d611deaad67cd5ce9ef81c0c69e850e9cda7ba4ca` | `19a1edf66e05e2ed410730259032867fed89d5539cf7e91538adde24b6fccaa9` | Admission queued then cancelled; no QEMU launch |

The Studio ELF and package manifest hashes are the same in both candidates. The diagnostic candidate manifest includes SHA-256 fingerprints for the dirty server worktree files. No accepted GICv2/GICv3 candidate or physical-device asset was overwritten.

## Hosted, build, and package evidence

| Area | Result | Evidence |
| --- | --- | --- |
| ABI2 adapter suite | PASS, 104/104 | `scripts/test-aarch64-studio-v05-adapter.ps1`; typed forwarding, validation, lifetime, ownership, and confined VFS checks. |
| ABI1 SDK negative control and layout checks | PASS | `scripts/test-aarch64-phase12-sdk-alignment.ps1`; incompatible 400-byte SDK rejects the pinned activation consumer; 400-byte and 464-byte layouts remain distinct. |
| GICv3 hosted regression suite | PASS, 62 assertions | `scripts/test-aarch64-gicv3-host.ps1`, output `out/abi3-gicv3-host-diag-20261102`; no QEMU launched. |
| Phase 10 resolver/ELF controls | PASS | Build output `aarch64-phase10-host-tests.exe`; ARM64/AMD64 selection and negative controls passed. |
| Pinned Studio hosted/model suite | PASS | Production build ran Studio model, startup, activation, Run-controller, debugger, and other source tests. |
| ARM64 kernel and UEFI build | PASS | Candidate manifests and `llvm-readobj` outputs; the kernel is `ET_EXEC`, `EM_AARCH64`. |
| Native Studio compile/link/package | PASS | Authentic pinned source compiled with the v0.5 SDK, linked as AArch64 NativeElf, staged with exact ABI identity. |
| Guest VFS, Studio Build/Run, teardown safety | NOT OBSERVED | The first guest stopped before desktop initialization. |
| Physical hardware | NOT TESTED | No Raspberry Pi, tablet, or removable media was used. |

The earlier ABI2 source audit identifies document activation, debugger, cancellation, and the separate v0.5 NativeWindow create/set/run callbacks as unavailable in this adapter. The pinned Studio checks those as optional for its normal startup path. This is source/hosted evidence only; their runtime behavior in the guest was not exercised.

## QAC and guest record

The first run used the real QAC CLI and exclusive acceptance reservation. The QAC log records direct-child QEMU PID `25324`, launch at `2026-11-02T16:48:28Z`, and exit at `2026-11-02T16:48:38Z`:

```text
Project: guideXOSServer-AARCH64-ABI3-Studio
Mode: acceptance
Reservation: 51973721203c43b580806eefa6ccbcb7
QEMU: C:\Program Files\qemu\qemu-system-aarch64.exe
Machine: virt,gic-version=2,acpi=off
CPU: cortex-a53
RAM: 512 MiB
Firmware: edk2-aarch64-code.fd plus a fresh copy of edk2-arm-vars.fd
Devices: ramfb, virtio-keyboard-device, virtio-tablet-device
```

QAC reported the child launch and released that same reservation after QEMU exited. The preserved serial record is:

```text
out/abi3-studio-guest-final2-20261102/abi3-guest-20261102-084826-666/guest-final.log
out/abi3-studio-guest-final2-20261102/abi3-guest-20261102-084826-666/qac.log
out/abi3-studio-guest-final2-20261102/abi3-guest-20261102-084826-666/session.json
```

The guest sequence observed UEFI entry, kernel ELF load, GOP and DTB handoff, ARM64 kernel entry at EL1, stack validation, `ExitBootServices`, and firmware-handoff validation. The next marker was `DTB diagnostic=2`, followed by `DTB: FAIL` and `AARCH64_PHASE12_ERROR`. The log shows no desktop, package discovery, manifest identity validation, adapter selection, NativeElf Studio entry, window creation, or initial render. The QMP screenshot was not captured because the initial-render marker never appeared, and no QMP input was sent.

The follow-up QAC reservation was `ea21fabb55934a1faa49a909ec5168d1`, queued at position 1 and cancelled by the runner after 12 seconds. Its session record explicitly says `qemu_launched: false`; after cancellation, QAC remained online with one unrelated managed guest, zero queued reservations, and zero foreign QEMU processes. The request log and session are preserved at `out/abi3-studio-guest-diag-20261102/abi3-guest-20261102-085543-478/`. No other reservation was reused and no further queue request was made.

| Requested guest evidence | Result |
| --- | --- |
| UEFI loader, kernel entry, stack, and firmware handoff | Observed in one authorized boot. |
| DTB parser result | Failed with diagnostic 2 (`MALFORMED`); diagnostic trace candidate not admitted. |
| Desktop / package discovery / adapter selection / Studio entry | Not reached. |
| Window content, focus, pointer, click, keyboard, redraw, close | Not observed. |
| Disposable Studio workspace, VFS reads/writes/boundaries | Not attempted in guest. |
| Actual Studio Build, compiler-produced ELF, ordinary Run | Not attempted in guest. |
| Studio repeated open/close, callback/image lifetime, cleanup | Not observed. |
| Unexpected IRQs, synchronous exceptions, scheduler stalls | Not measured; kernel stopped before these counters. |
| Qualifying fresh Studio boots | 0 of 3. One diagnostic boot failed before Studio. |
| GICv3 guest qualification | Separate milestone; remains pending. |

## Remaining acceptance work

The next authorized run needs to inspect the DTB failure using the preserved diagnostic candidate, repair the specific parser/handoff issue if confirmed, and reach desktop startup before attempting Studio operations. Then exercise compositor rendering and guest-observed input, create and validate a disposable guest workspace, perform Studio's actual Build and ordinary Run, and collect teardown evidence. Three fresh authorized boots must pass Studio startup, rendering, basic input, close, and cleanup; at least one must also pass the VFS and Build/Run workflow. Do not count the current diagnostic boot toward those three.

## BOOTDTB1 follow-up (2026-11-02)

A later authorized no-Studio Phase 4 guest isolated the early failure with bounded loader and parser diagnostics. It confirmed EFI_DTB_TABLE_GUID discovery, accepted FDT header (0xd00dfeed, totalsize 0x101000, version 17), copied the DTB to LoaderData, and validated the physical handoff range and LoaderData memory-map coverage. It then stopped at parser stage STRUCTURE, token FDT_PROP, error PROPERTY_VALUE_FORMAT, structure offset 5572. The original serial line said reserved-after-EBS=1, but that field was hard-coded and did not prove allocator reservation; the failed guest stopped before allocator initialization. The follow-up did not capture the DTB bytes or identify the property/predicate, so the root cause remains unresolved and no parser acceptance behavior was changed.

The minimal guest did not reach GIC selection, timer delivery, scheduler progress, desktop, or Studio. The status remains STUDIO_GUEST_PARTIAL, with **0 of 3 qualifying Studio boots**. See [AARCH64_BOOTDTB1_HANDOFF_DIAGNOSIS.md](AARCH64_BOOTDTB1_HANDOFF_DIAGNOSIS.md) for the QAC record, candidate hashes, provenance analysis, tests, and next acceptance requirement.
