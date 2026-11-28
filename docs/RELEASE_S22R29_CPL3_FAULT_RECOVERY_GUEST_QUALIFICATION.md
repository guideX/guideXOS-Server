# S22R29 CPL3 Fault Recovery Guest Qualification

**Classification:** diagnostic engineering evidence; not a public release.

## Result

**Outcome D — `QAC_UNAVAILABLE`.** No S22R29 guest fault experiment was started. At the final QAC check on 2026-11-28 00:45 UTC, another verified owner held the `guideXOSServer` acceptance reservation `41bdd9d6effd409893e43190f46b654c` with QEMU child PID 20428. S22R29 did not attach to, signal, cancel, terminate, or otherwise interfere with that guest. No unmanaged QEMU was launched.

The offline implementation, tests, stack/symbol audits, and diagnostic ISO are preserved. The processor-generated guest exception classes remain unqualified; no `CPL3_FAULT_RECOVERY_GUEST_ACCEPTED` claim is made.

## Offline gate evidence

- Repository began from `main` at `02422a86d44319e6e6473342af7e8b1e29c55ff9`; the S22R28 accepted artifacts are preserved and separately identified in the [design record](RELEASE_S22R29_CPL3_FAULT_RECOVERY_DESIGN.md).
- Hosted exception-frame/task-identity/CR3/continuation regressions passed.
- Hosted frame-owner repeated teardown, Native ELF policy, RAMDISK/VFS, and persistent FAT/AppData regressions passed.
- Default-off and opt-in diagnostic AMD64 freestanding kernels linked successfully and passed ELF/PE symbol audits.
- The 16 KiB stack guard passed, including a separate TSS privilege-entry stack model, a separate exception-dispatch stack, and off-stack lifecycle teardown.
- The implementation uses a bounded diagnostic with #PF, #UD, #GP, a repeated #PF, and a normal task following each fault. This is the expected sequence only; it is not evidence that the guest executed it.

The diagnostic image is `E:\CodexArtifacts\S22R29\candidate-diagnostic-ce4d4a987c64\artifacts\guideXOS-S22R29-CPL3-FAULT-RECOVERY-DIAGNOSTIC-NOT-PUBLIC-RELEASE.iso`, SHA-256 `329d2f59ed44eb1c8f98e0fc1226803ca0d9b518e014e771203fee7bce122a31`. Its embedded FAT32 ESP is SHA-256 `f430b888fd3171246a66a4bded3ba39e9bb9dcf61001c8cc70ef2564d4ab8cc4`; the ISO verifier confirmed a UEFI El Torito no-emulation boot image. The full hashes and flags are in `E:\CodexArtifacts\S22R29\candidate-diagnostic-ce4d4a987c64\artifact-manifest.json`.

## QAC reservation protocol

Before guest execution, recheck coordinator status and acquire an exclusive `guideXOSServer` acceptance reservation through the established QAC CLI. If another owner remains active or the coordinator cannot admit the run, preserve the offline artifacts and stop before injecting any exception. Every guest process must be a direct QAC-managed QEMU child.

Use disposable OVMF variables and a disposable writable copy of the accepted user-data image. Keep networking disabled. Do not attach accepted source images read-write. Do not force-clear reservations, restart the coordinator, or stop another owner's process.

## Required evidence for an admitted run

Record the diagnostic ISO SHA-256, source commit, pinned dependencies, flags, loader/BootInfo/RAMDISK identities, QAC reservation and owner status, QEMU child PID, command arguments, serial log, QMP evidence, exit code, and shutdown/release events.

The serial evidence must establish in order:

1. UEFI boot and valid BootInfo v3.
2. RAMDISK, `/`, and `/Apps` mount successfully.
3. Existing CPL3 baseline tasks exit through vector `0x80` with value 42 and clean frame accounting.
4. The first real CPL3 processor exception is captured with matching task ID, saved CPL3 CS, RIP/RSP, vector, error code, CR2 when it is #PF, task CR3, `Running -> Faulted`, kernel CR3 restoration, and off-stack reaping.
5. The kernel remains in the same boot and a new CPL3 task exits normally with value 42.
6. Each separately enabled later fault class returns through the same validated continuation; no subsequent class is attempted after an earlier failure.
7. Frame accounting returns to baseline after every task; repeated faults do not accumulate frames.
8. The PIT heartbeat and desktop event loop continue; real keyboard or pointer input is processed.
9. `/` and `/Apps` remain accessible, Settings opens/closes, and PacMan retains its approved CPL0 lifecycle where practical.
10. Expected `/Documents` and `/AppData` files are read through a disposable data-volume copy, which is preserved with before/after hashes.
11. The guest requests its own normal shutdown; QAC records the direct child exit and releases its reservation.

QMP acknowledgments alone do not prove guest input processing. A halt followed by a reboot or externally killed QEMU is a failed guest gate.

## Per-class qualification ledger

| Class | Intended CPU event | Required evidence | Status |
| --- | --- | --- | --- |
| Unmapped user read | #PF (14), error `0x4`, CR2 = unmapped user guard | Actual saved CPL3 frame, safe continuation, reaping, then result-42 task | Pending guest run |
| Invalid instruction | #UD (6), no processor error code | Actual saved CPL3 frame and zero normalized error word, safe continuation, then result-42 task | Pending guest run |
| Absent DPL0 software-interrupt gate | #GP (13), error `0x40A` | Actual processor frame, safe continuation, then result-42 task | Pending guest run |
| Repeated unmapped user read | #PF (14), error `0x4`, expected CR2 | Second independent task identity, no leaked task frames, then result-42 task | Pending guest run |

Until a QAC-managed guest supplies this evidence, no exception class is guest-qualified.

## Remaining qualification limits

Even a successful guest sequence qualifies only the named CPL3 diagnostic exception classes and this QEMU configuration. It does not qualify kernel-memory-corruption containment, arbitrary task state, nested-fault recovery, FPU/SIMD state, non-cooperative infinite loops, timer preemption, general scheduler fairness, production Native ELF CPL3 policy, or PacMan CPL3 migration.

The selected release model remains Public Technical Preview v0.3. QEMU remains the formal reference guest. No residual-risk acceptance, signing authorization, promotion, publication, or release-readiness decision is implied.
