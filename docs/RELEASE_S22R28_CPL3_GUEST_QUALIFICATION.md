# S22R28 — CPL3 Guest Qualification

## Release outcome

**`CPL3_FOUNDATION_IMPLEMENTED_GUEST_PENDING` (Outcome B).** The protected-task implementation passes focused hosted checks and both diagnostic-enabled and default freestanding AMD64 builds. It has not been run in a guest. A real UEFI BootInfo memory map, CR3 activation, processor-reported CPL3 execution, controlled exit, cleanup, and continued desktop operation remain to be qualified.

## Artifact and repository identity

- Starting revision: `bc2acf5adba271b0e7f8d288c8a52498dc1bbb3b` on `main`; tracked worktree clean; branch one commit ahead of `origin/main`.
- The pre-existing untracked `assets/Backgrounds/troncity.png` was preserved with SHA-256 `444ac64211907d80f0266e2283960cefaa932803d0aee7e0ccfd414f6fae2fda`; it is not part of this commit.
- Accepted public-preview ISO: `E:\CodexArtifacts\S22R23\candidate-source\out\release-profile\4c9988cf-5d0d-414d-afe8-81f7351c2706\preview-artifacts\guideXOS-Server-v0.3.0-amd64-public-preview-pacman-only-candidate.iso`, SHA-256 `8d397659e0a6ec8f1be113ae4f5be14dbcaf1ef22ccda41a6e5ee5014919cf40`, 90,245,120 bytes. The accepted image and existing `dist` outputs were not changed.
- Development diagnostic kernel: external scratch output `C:\Users\guideX\.codex\visualizations\2026\11\21\01a1ff0e-fd30-7300-85f4-4729536db631\s22r28\diagnostic\kernel-build\bin\kernel.elf`, SHA-256 `264701d1d30e8a44702da6f375b6beabe37e655f26cda7d82e735eadecfbad64`, 2,840,424 bytes.
- Default diagnostic-off kernel: external scratch output `C:\Users\guideX\.codex\visualizations\2026\11\21\01a1ff0e-fd30-7300-85f4-4729536db631\s22r28\production\kernel-build\bin\kernel.elf`, SHA-256 `2b9c39a27d082e03f9b9c37eb1c091f6fe879c83275fd63c31fe5c5d002268e3`, 2,829,792 bytes.
- Both kernel outputs are ELF64 AMD64 ET_EXEC with entry `0x100000`. `readelf` found no relocations in the diagnostic image; `nm` confirmed the user-entry, exit-stub, dispatcher, and kernel-stack symbols.
- No diagnostic ISO was produced because the exclusive QAC lane was occupied. The accepted ISO was not repackaged or replaced.

## Hosted and freestanding evidence

| Check | Result |
| --- | --- |
| AMD64 protected-task selectors, TSS layout, IRET frame, exit request, lifecycle | PASS; executable SHA-256 `aca5a6d73b2efd4c8bc8e80d82760cc47aa66fa820dccb107fdc6d7f5f77ceb5` |
| S22R27 physical-frame owner and task address-space production code | PASS; bounded allocation, rollback, effective permissions, shared supervisor mappings, repeated teardown; executable SHA-256 `d12450e5fc02b3ed9fc6d6045e07180431c73c992fc454278c2eaa167a994852` |
| AMD64 voluntary Microsoft x64 context switch | PASS; alignment, shadow space, repeat resumes, eight nonvolatile GPRs; executable SHA-256 `f156f6e0c32a832b4f242d838c14286f2009992e73d7ef099e34c0ddf217fd5e` |
| Native ELF security policy | PASS; policy gates and PacMan-only resource identity checks |
| Native ABI layout | PASS |
| Native filesystem contract | PASS |
| Hosted VFS read and mount-table integrity | PASS; 4/4 read checks and 19/19 mount checks |
| FAT persistence on a disposable copy of `guidexos-user-data.img` | PASS; 44/44 checks; original data image was not modified |
| User-data-volume policy | PASS; all nine policy cases |
| Settings native layout/runtime/presentation | PASS; 1,037/1,037, 908/908, and 3,659/3,659 checks |
| PacMan source registration | PASS; pre-audio, audio-bearing, missing implementation, and missing definitions |
| Window UI and compositor pointer buttons | PASS |
| Freestanding AMD64 with `CPL3_DIAGNOSTIC=1` | PASS; Mbed TLS profile verification and link passed |
| Freestanding AMD64 with `CPL3_DIAGNOSTIC=0` | PASS; Mbed TLS profile verification and link passed; startup reports `[CPL3] status=disabled` |

GCC `-fstack-usage` records for the changed path showed 896 bytes for `kernel_main`, 96 for the diagnostic wrapper, 288 for the lifecycle-cycle routine, 80 for the exit dispatcher, 144 for cleanup, and no more than 240 bytes for the listed address-space routines. These frame records do not include a full call-chain or nested interrupt proof. The diagnostic lifecycle, RSP0, and exception stacks are separate 16 KiB regions; no whole-kernel stack-safety claim is made.

The S22R27 boot-time frame-owner checks are wired to real BootInfo v3 fields and memory-map descriptors, but no guest log is available to confirm that a real firmware map satisfied them. No loader or BootInfo source changed, so no separate loader build was needed.

## QAC state and guest gates

Read-only QAC CLI checks reported the coordinator online (v2.0), one owner-verified `guideXOSServer` acceptance reservation running with one managed QEMU process, zero foreign QEMU processes, zero queued reservations, and zero blocked requests. The run remained active on the final check and its reservation identity changed during this task, so it was not touched. The QAC computer-use UI endpoint timed out; the coordinator CLI supplied the health and reservation data. No QAC request or guest launch was made.

Because there was no exclusive guest reservation, none of the guest gates ran:

1. **Ordinary boot:** no actual BootInfo map, frame-owner readiness, desktop responsiveness, or guest exception log was observed.
2. **CPL3 task:** no actual CS `0x23`, user result `42`, `int 0x80` return, three-cycle frame accounting, or continued guest operation was observed.
3. **Fault containment:** intentionally not attempted. CPL3 exceptions are recorded and halt; return-to-kernel fault recovery and reaping remain unimplemented.

The smallest remaining qualification step is to obtain an exclusive QAC reservation, build and hash a separately identified diagnostic ISO from this source revision, then boot it and collect serial evidence for memory-owner readiness, the actual exit frame (`CS=0x23` and result `42`), all three teardown/accounting cycles, and normal operation afterward. Do not exercise the fault path in that run.

## Preserved production behavior and S22R29 scope

The normal build does not invoke the diagnostic or install the DPL3 exit gate. PacMan remains on the existing synchronous CPL0 Native ELF route with its current Native security policy. S22R28 does not claim arbitrary protected applications, user-fault recovery, desktop survivability after a guest task, preemption, or non-cooperative task termination.

S22R29 should qualify a timer-driven scheduler only after defining full GPR and hardware-frame capture, CR3 and FS/GS base ownership, per-task kernel stacks, ready/running/blocked/exit state transitions, safe interrupt boundaries, kernel-mediated resource cleanup, and desktop-latency measurements. It must account for per-task x87/SSE state (and XSAVE/FXSAVE policy) before running user SIMD code. A bounded CPL3 spin-loop containment test belongs in a separate milestone; voluntary exit is not evidence of such recovery.
