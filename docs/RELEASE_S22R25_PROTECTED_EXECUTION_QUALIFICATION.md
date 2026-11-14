# Server v0.3 — S22R25 Protected Execution Qualification

**Outcome C — `PROTECTED_EXECUTION_ARCHITECTURE_BLOCKED`.** The ABI-correct voluntary context path passes its hosted checks and the AMD64 freestanding image builds and passes its symbol audit. A useful protected application task cannot yet be constructed: there is no general address-space/page owner, ring-3 GDT/TSS stack setup, full asynchronous return frame, active scheduler, or safe mediated Native ELF service boundary. The application survivability requirement remains unsatisfied.

## Repository and candidate identity

- Start: `D:\dev\guideXOSServer`, branch `main`, HEAD `6dc43f3ede0adfb36384f6534406c94d4a409560`, five commits ahead of `origin/main`, no tracked changes at preflight.
- `assets/Backgrounds/troncity.png` was already untracked at start and remains preserved; preflight SHA-256 `444ac64211907d80f0266e2283960cefaa932803d0aee7e0ccfd414f6fae2fda`.
- S22R23 accepted ISO: 90,245,120 bytes; SHA-256 `8d397659e0a6ec8f1be113ae4f5be14dbcaf1ef22ccda41a6e5ee5014919cf40`. Its accepted kernel: 2,812,208 bytes; SHA-256 `9d2f17c33f157a566b6ee513196b5b74831bf8532699be78538b5979886fc166`.
- Accepted persistent image: `E:\CodexArtifacts\S22R22\persistent-data\guidexos-user-data.img`; SHA-256 `3ee67553ab9bece4130e4aa84ec4ed0d341cb29d7fd78798884c5ae3b0b7b148`.
- S22R25 validation kernel, built separately with the public-preview compile profile: 2,812,296 bytes; SHA-256 `854712b5b2396a993829910998b60f682e5f1d067d839d1a187d120c3c1e8bb1`. This is a validation image, not a release candidate; no ISO was packaged and no earlier guest acceptance is inherited.
- The accepted candidate, persistent source image, existing `kernel/build` output, generated S22R23/S22R24 evidence, `dist`, and readiness/signing/publication state were preserved. A later separate `g++` process in `D:\dev\guideXOSServer_NAVIGATOR_IMPROVEMENTS` was observed and left undisturbed.

New hosted and freestanding evidence is isolated under `E:\CodexArtifacts\S22R25\tests` and `E:\CodexArtifacts\S22R25\freestanding`.

## Validation results

| Check | Result | Scope |
|---|---|---|
| AMD64 voluntary context ABI regression | **PASS** | Hosted x64 MinGW/NASM execution; context initialization, stack range/overflow, entry argument, entry stack alignment, shadow-space use, repeated switching, and eight nonvolatile GPRs. Does not exercise a guest or scheduler. |
| AMD64 freestanding public-preview build | **PASS** | Separate output directory; kernel linked as AMD64 ELF. |
| AMD64 symbol audit | **PASS** | 50 undefined-symbol rows; 22 distinct symbol-table entries; no relocations or imports. |
| Native ELF security policy | **PASS** | 30 cases, zero failures, four synthetic approved-entry calls. This tests policy, not privilege isolation or runtime recovery. |
| Native ABI layout / present-frame contract | **PASS** | Existing hosted ABI layout and frame validation regressions. |
| PacMan source registration | **PASS** | Pre-audio, audio-bearing, missing-implementation, and missing-definition cases. |
| Release build integrity | **PASS** | 37 assertions; no candidate packaged. |
| Settings native layout | **PASS** | 1,037/1,037, including 1,000 resize-geometry transitions. |
| Settings native runtime | **PASS** | 908/908; 100 lifecycle repetitions, 250 provider refresh cycles, 1,000 notification/state transitions. |
| Desktop/window policy | **PASS** | Hosted window UI regression only; no guest input-routing claim. |
| VFS mount-table integrity | **PASS** | 19/19. |
| Hosted VFS read | **PASS** | 4/4. |
| User-data-volume policy | **PASS** | 9 cases. |
| FAT/VFS persistence | **PASS** | 44 checks against `E:\CodexArtifacts\S22R25\tests\fat-persistence-copy-01\hosted-data-volume-copy.img`, a writable copy of the accepted source. Covered `/Documents`, `/AppData`, wallpaper/icon settings readback, and preservation of an existing Notepad document during writes. |
| Native filesystem contract | **PASS** | Hosted all-source build and synthetic Native filesystem contract execution; the contract test reported `Native filesystem contract test PASS`. This covers filesystem host-service behavior and does not establish CPL3 pointer isolation. |

These hosted regressions cover policy and existing utility contracts; they do not invoke a changed freestanding Native ELF execution route. The ABI-context test is actual host execution of the Microsoft x64 switch assembly, but is not a simulator-free AMD64 guest switch.

## Stack and memory safety

Compiler static stack estimates for the new context helpers are 8 bytes for `init_context`, 48 for `arch_thread_create`, 8 for `arch_thread_destroy`, 64 for `arch_switch_to`, and 8 for each timer hook. No large stack-local context or task-stack buffers were introduced. The assembly constructs its switch frame on the caller-owned task stack.

The AMD64 build also reports existing large frames outside the established guarded paths, including `NavigatorApp::submitFormForBlock` (8,928 bytes) and several `kernel_apps.cpp` smoke/restore paths above 4 KiB. The four S22R24 guarded paths retain their earlier result but are not proof of whole-kernel stack safety. A per-task stack budget still needs call-chain analysis and guard pages before running real work on new stacks. The boot-time 16 KiB stack contract is unchanged.

## Application, persistence, and security regression boundary

- The S22R18 exact PacMan manifest/ELF/resource/source checks remain unchanged and the hosted policy suite passes.
- Existing normal PacMan launch/input/close and clean-return evidence applies to the exact S22R23 ISO only. No new guest was run, so normal lifecycle was not re-qualified on the changed validation kernel.
- Settings, window policy, VFS, storage policy, and 44 persistence checks passed. The FAT test wrote only the separate copy named above; the accepted source data image was not attached for writing.
- Public-preview ABI v1 remains the only accepted Native ELF contract. No diagnostic executable was added to the allowlist, and no attempt was made to replace the existing ring-0 PacMan path with an incomplete protected path.
- There is no claim of safe behavior under arbitrary ring-0 app faults or memory corruption. Documents and settings are covered only by the prior accepted normal-operation evidence and the current hosted disposable-copy tests.

## Guest and QAC disposition

No QAC reservation or guest launch was attempted, and QAC coordinator ownership/state was not queried. The S22R25 path cannot yet start an independently scheduled protected task or reclaim it, so a non-cooperative guest loop would risk a hang and an external QEMU shutdown would not constitute recovery evidence. No diagnostic ISO was created.

The exact remaining qualification requirement is a separately identified diagnostic AMD64 guest running the completed CPL3/address-space/scheduler/trap/reaper path. It must show protected entry, kernel timer progress, independent kernel progress, bounded return or controlled exception, task teardown off its own stack, and desktop/input responsiveness. Only after a safe bounded non-cooperative scenario exists should a guest demonstrate that the task stops while kernel control continues. Candidate qualification remains separate.

## S22R26 minimum implementation scope

1. Build an owned physical-page/address-space subsystem. Map kernel pages supervisor-only; map validated ELF text read-only/executable, data/BSS/user stack writable/non-executable, enforce W^X, and provide guarded stacks plus bounded user copy-in/copy-out.
2. Add user GDT selectors, per-CPU TSS `RSP0`, safe exception/IST stacks, and a correct privilege transition/return frame. Remove placeholder syscall paths before use.
3. Add a real full-register interrupt context and run queue. First schedule a kernel idle/desktop context and protected user task. Switch only at audited boundaries; defer scheduling across kernel critical sections and expose bounded service calls.
4. Initialize and own per-task FPU/XSAVE state or prohibit user SIMD until it is validated.
5. Implement a separately versioned syscall-backed Native ELF ABI with exact pointer bounds, generation-tagged task-owned service handles, documented errors, and idempotent off-stack reaping. Keep ABI v1 and the PacMan-only release route intact until a compatible migration is implemented.
6. Add hosted page-permission, pointer-validation, task-state, queue, and cleanup tests; repeat the freestanding build/symbol/stack audits; then run the bounded diagnostic guest under QAC ownership and record serial/task-state evidence.

The recovery requirement remains open after this phase. Do not claim the kernel can reclaim execution from an unresponsive Native ELF until an independently controlled protected task has demonstrated that property in a guest.
