# S22R29 CPL3 Fault Recovery Implementation

**Classification:** diagnostic engineering evidence; not a public release.

## Repository and dependency preflight

The work began from `main` at `02422a86d44319e6e6473342af7e8b1e29c55ff9`, equal to the then-current `origin/main`. Existing accepted S22R28R3R3 CPL3-off and CPL3-on ISOs were verified from their artifact manifests and preserved. `assets/Backgrounds/NEW`, `dist`, signing data, and release-readiness markers were not modified.

The pinned dependency profile passed during both freestanding builds: Mbed TLS 4.1.0 commit `0fe989b6b514192783c469039edd325fd0989806`, TF-PSA-Crypto 1.1.0 submodule `29160dd877d29658279fd683b2ae57b320ddcf09`, and three guideXOS patch inputs. Its verified local patch worktree remains unchanged.

## Code changes

- Added a shared normalized AMD64 exception-frame and recovery contract in `kernel/core/include/kernel/amd64_exception_contract.h`. It fixes the GPR/vector/error/CPU-frame offsets and bounds recovery to exact task, address-space, CR3, selector, RFLAGS, RIP, RSP, CR2, and lifecycle-continuation identities.
- Updated all AMD64 exception stubs in `kernel/core/interrupts.cpp`: hardware-error and no-error frames normalize independently; all GPRs are saved; both normalized words are removed for a normal `IRETQ`; nested faults preserve their raw identity and halt; and a validated protected-task result branches to the separate kernel continuation.
- Updated `kernel/arch/amd64/protected_task.asm` to export the continuation label and lifecycle-stack bounds. Kernel data selectors are installed before restoring trusted FS/GS bases on both the normal exit and fault continuation paths.
- Updated `kernel/core/native_elf_baremetal.cpp` to classify by saved CS. CPL0 exceptions during the protected diagnostic stay kernel-fatal. A CPL3 fault is eligible only through the strict protected-task validator; rejected CPL3 contexts halt rather than returning through the stale user frame.
- Extended the existing `ProtectedTaskDescriptor` lifecycle in `kernel/core/protected_task.cpp`. A valid fault is copied into a full `FaultRecord`, marked terminal, removed from the active-task pointer, and moved to kernel CR3 before assembly abandons the exception frame. The lifecycle supervisor reaps it off-stack using the existing frame owner/address-space system, checks accounting, then runs another normal task.
- Added opt-in `CPL3_FAULT_RECOVERY_DIAGNOSTIC=1`, which requires `CPL3_DIAGNOSTIC=1`; the default kernel remains diagnostic-off. The bounded diagnostic covers #PF, #UD, #GP, a repeated #PF, and a normal result-42 task after each exception.
- Extended `tests/boot_stack_budget_guard_test.ps1` to account for the CPL3 processor-entry frame, exception-dispatch stack, lifecycle continuation, and recursive private page-table cleanup.

No production Native ELF allowlist, `/Apps` trust policy, writable-data executable policy, RAMDISK/VFS path, PacMan lifecycle, signing, or publication configuration changed. No preemptive scheduling or infinite-loop recovery is claimed.

## Offline verification

Hosted regressions passed:

| Check | Result |
| --- | --- |
| `amd64_exception_recovery_contract_test` | PASS; error/no-error normalization, #UD/#GP/#PF contracts, CPL classification, task/CR3/continuation checks, full context preservation, rejection cases |
| `amd64_protected_task_contract_test` | PASS; state transition and protected-exit contract |
| `amd64_memory_foundation_test` | PASS; owner accounting, rollback, permissions, shared mappings, repeated teardown |
| `amd64_context_switch_test` | PASS; Microsoft x64 alignment, shadow space, and eight nonvolatile registers |
| `native_elf_security_policy_test` | PASS; approved PacMan package gate and writable-data/identity/ABI rejection cases |
| `release_ramdisk_mount_test` | PASS; accepted RAMDISK read-only owner, root VFS, `/Apps`, approved PacMan package |
| `fs_fat_persistence_test` | PASS; 44 checks, 0 failures; writable copy of the persistent user-data volume only |
| `ramdisk_integrity_diagnostic_test` | PASS; bounded RAMDISK alias and mapping checks |
| App Model default-handler persistence | PASS; 54/54 checks and 100 reload cycles |
| App Model URI/protocol activation | PASS; 41/41 checks and 100 persistence/activation cycles |

The production-data source image was read from the accepted S22R22 persistent-data artifact. The FAT/VFS test script copied it to the S22R29 artifact directory before mutation; the source image and S22R28 guest data images were preserved.

The opt-in GCC `.su` stack audit passed with the established 16 KiB stacks and 4 KiB headroom requirement. Modeled usage was 176 bytes on the TSS privilege-entry stack, 504 bytes on the 16 KiB exception-dispatch stack, and 2,744 bytes worst-case on the lifecycle stack, including the bounded three-level private page-table walk and conservative reserves. This is a static C++/assembly budget, not runtime stack-guard proof.

AMD64 PE/ELF symbol audits passed for the default-off and fault-recovery builds: no PE imports, ELF relocations, dynamic symbols, or unexplained undefined dependencies. Toolchain: MinGW-W64 GCC 15.2.0 and Binutils 2.46.0.20260210. Mbed TLS profile verification passed.

The source commit for these images is `ce4d4a987c649dd87abaf9f1d61d809e9b05d534` (tree `bef5c1010f5e3e7e812a60efa715a0922122d062`). The default-off kernel ELF SHA-256 is `9be967ef2521b585991eb05095362a815a59c614d882da6b2b966333051c14f6`; the opt-in fault-recovery kernel ELF is `354963d1688a1c8b392820709a020c1b52f2e5d8aebcdd2d43b09ea463670677`. The diagnostic PE is `9b0e6d73cebd03c96365444c9b4bf6c5df8032a5e89fea5c34c5f1d6dbf5bb31`. The opt-in build flags are `-DGXOS_PUBLIC_PREVIEW_SECURITY_PROFILE -DGXOS_ENABLE_CPL3_DIAGNOSTIC -DGXOS_ENABLE_CPL3_FAULT_RECOVERY_DIAGNOSTIC`.

The separately identified diagnostic ISO was constructed under `E:\CodexArtifacts\S22R29\candidate-diagnostic-ce4d4a987c64\` from a copy of the accepted CPL3-on ESP. Its EFI loader SHA-256 remains `ac55ce9ed59f09ab4489b944fab31176b97691f1702c5da4150655fc1455772d`; the unchanged RAMDISK SHA-256 is `ead5105aa131dee575e0021fac1650a28330df422bd211ca6baccdd267179f05`. The rebuilt embedded ESP is 90,177,536 bytes with SHA-256 `f430b888fd3171246a66a4bded3ba39e9bb9dcf61001c8cc70ef2564d4ab8cc4`; the 90,245,120-byte ISO SHA-256 is `329d2f59ed44eb1c8f98e0fc1226803ca0d9b518e014e771203fee7bce122a31`. The FAT32 ESP verifier and independent raw ISO/El Torito verifier passed. The ISO identifies itself as **DIAGNOSTIC — NOT PUBLIC RELEASE** and remains outside `dist`.

The root artifact manifest is `E:\CodexArtifacts\S22R29\candidate-diagnostic-ce4d4a987c64\artifact-manifest.json` (SHA-256 `780214f2b849a9e155b072959f4b4e00d780f35028f5978af53640e4c7e97f01`). It records both kernel identities, pinned dependency revisions, flags, loader/RAMDISK/ESP/ISO hashes, the expected diagnostic sequence, and the QAC unavailability evidence. The captured QAC status and reservation snapshots are `artifacts/qac-status-final.txt` (SHA-256 `48a00af0b564520afabfa37a082b881fc248105c1052cd86a2f5484f55e10c88`) and `artifacts/qac-list-final.txt` (SHA-256 `e1af65be6c7ccbe9a6c5709f8a0c6d6acc6512e93c118c8124224d8a5dd4fef4`). The accepted S22R28 CPL3-off/on ISO hashes and accepted S22R22 user-data source hash were rechecked unchanged after S22R29 packaging and hosted tests.

## Guest qualification status

The QAC coordinator was checked after artifact construction and again at final review (2026-11-28 00:45 UTC). It remains online but reports another verified owner's managed `guideXOSServer` acceptance session (`41bdd9d6effd409893e43190f46b654c`, child PID 20428). No QEMU was launched, queued, canceled, or signaled by S22R29. This phase ends with **Outcome D — `QAC_UNAVAILABLE`**; the offline diagnostic ISO is prepared only in the isolated `E:\CodexArtifacts\S22R29` tree and remains unqualified.

Until then, actual processor delivery of vectors 6, 13, and 14, post-fault guest execution, and desktop/file-system survivability remain unproven. Hosted tests cannot substitute for that gate. See [the guest qualification record](RELEASE_S22R29_CPL3_FAULT_RECOVERY_GUEST_QUALIFICATION.md).

## Next milestone

S22R30 should begin with timer delivery from CPL3, complete interrupted context ownership, FPU/SIMD state policy, runnable selection on an independent kernel context, safe cancellation, bounded CPU-time accounting, and desktop responsiveness. It must separately test a bounded infinite-loop task. This milestone does not authorize production PacMan CPL3 migration.
