# AArch64 GICv3 Guest Qualification — GIC3-2

- **Outcome:** `GICV3_GUEST_PENDING_QAC`
- **Run date:** 2026-11-01
- **Repository:** `D:\dev\guideXOSServer_AARCH64`
- **Branch/base:** `AARCH64_SUPPORT` at `ca51ea964053ef8b7223c4e2114c547d8c3b891d`
- **Upstream at preflight:** `origin/AARCH64_SUPPORT`, 1 ahead / 0 behind; worktree clean before this qualification work.

## Result

Hosted and build readiness passed, but the native guest did not start. QAC was online and protecting a separate active acceptance session. A new, project-specific acceptance request for this qualification entered QAC's exclusive queue at position 1. QAC did not grant it, so the request was cancelled before launch. No QEMU guest process, UEFI boot, or guest serial output belongs to this run.

This report deliberately makes no claim about guest DTB contents, GIC register values, timer delivery, interrupt completion, scheduler preemption, or exception return. Those measurements require a QAC-authorized AArch64 guest.

## Hosted and build evidence

| Area | Result | Evidence |
| --- | --- | --- |
| Hosted GICv3 suite | PASS, 62 assertions | `scripts/test-aarch64-gicv3-host.ps1`; Distributor/Redistributor models, DTB validation, affinity, CPU interface, PPI/SPI operations, timer integration, malformed-input rejection, and bounded waits. |
| Production ARM64 Phase 4 | PASS | `scripts/build-aarch64-gicv3.ps1`; production GICv3 driver and common IRQ path compiled and linked. |
| Final build artifacts | `out/aarch64-gicv3-gic3-2-final` | Staged from the final source after the diagnostic label correction. |
| Final `kernel.elf` | 210,376 bytes | SHA-256 `adc1f900412216f8b4ecd559c0a6eff73771bba9c68740e7c6b113514b85063d` |
| `BOOTAA64.EFI` | 9,216 bytes | SHA-256 `50ee56a3ee33788305d88a9e15cb52359c7bf96f16ce313fda84dbe0926aa7cb` |
| Staged `ramdisk.img` | 67,108,864 bytes | SHA-256 `74452c6109212bff33a73f2315e5aa029b44bff3cf8bb24257cd9850be5c22b9` |
| Preserved GICv2 Phase 2 build | PASS | `scripts/build-aarch64-phase2.ps1`; Phase 2 host validation passed. `kernel.elf` is 117,056 bytes, SHA-256 `b57bba83a5948aeed01ed613762c7a9ccad1050dff5ce4b2806a49e3f23ef447`. |
| QEMU model support | PASS | QEMU 11.0.0 reports `virt`, `gic-version`, and `cortex-a53`. This is configuration support only, not guest or generated-DTB evidence. |

The selected profile is `-machine virt,gic-version=3,acpi=off -cpu cortex-a53`, with 512 MiB RAM, the existing AArch64 UEFI code/vars firmware, and the staged FAT ESP. Each actual boot will receive a fresh UEFI vars copy. The script does not override the guest's discovered controller identity.

## QAC admission record

- Coordinator: guideXOS QAC 2.0.0, online.
- CLI: `D:\dev\guideXOS_QAC\src\GuideXOS.QAC.Cli\bin\Debug\net10.0-windows\GuideXOS.QAC.Cli.exe`.
- Before the request: one verified acceptance session active, zero foreign QEMU processes, zero queued/blocked reservations.
- Qualification project: `guideXOSServer-AARCH64-GIC3-2`, mode `acceptance`.
- Final persisted request: reservation `2a7cc4c8eb3a4444a869862c41bbbdd5`, queued at position 1, then cancelled before grant after the runner's 10-second admission window. Its grant time and child PID remained null. The request used the final kernel SHA-256 shown above.
- Three earlier runner bring-up requests (`ff2165d9b4064a0b97b4c5e8624ebfc1`, `6beef4cdd5a246adb032e1d500683ce6`, and `c669849b45ac4059921f3468f3a7b13e`) also queued at position 1 and were explicitly cancelled before launch. The queue-row indentation check was corrected before the final persisted attempt.
- After cancellation: the unrelated acceptance reservation remained the only managed session; this task's queue was empty.

All requests used QAC's real `run --project ... --mode acceptance -- <direct qemu-system-aarch64.exe>` flow. No grant file was created or used, no other project's reservation was reused, and no QEMU process was started for this task. The active QAC-owned guest was left untouched.

The persisted session and QAC CLI log are local build artifacts at:

```text
out/aarch64-gicv3-qualify-gic3-2/logs-gicv3-20261101-150000-403/session.json
out/aarch64-gicv3-qualify-gic3-2/logs-gicv3-20261101-150000-403/qac/diagnostic.log
```

## Guest evidence status

| Required evidence | Result |
| --- | --- |
| UEFI loader and kernel entry | Not observed; no guest launched. |
| Generated DTB selects GICv3 | Not observed. |
| DTB Distributor and Redistributor ranges | Not observed. |
| Boot-CPU Redistributor affinity and wake state | Not observed. |
| ICC SRE, priority mask, Group 1, and EOI mode | Not observed. |
| Physical timer PPI configuration and IRQs | Not observed. |
| Acknowledge / EOI / invalid-ack / completion counters | Not observed. |
| Scheduler task progress, preemptions, and exception return | Not observed. |
| Unexpected IRQs or synchronous exceptions | Not observed. |
| Three fresh GICv3 boots | 0 of 3. |
| GICv2 guest architecture/timer regression | Not run; pending separate QAC admission after GICv3 qualification. |

The GICv2 hosted coverage remains part of the 62-assertion suite. The separate GICv2 QEMU profile remains `virt,gic-version=2,acpi=off` with Cortex-A53 and was not changed by this result.

## Phase-12 ABI isolation

The Phase-12 wrapper's external Developer Studio check expects `gx_host_calls.get_document_activation_path`. The AARCH64 Server checkout supplies a 400-byte `gx_host_calls` with no such member; the intended v0.5 Developer Studio SDK defines the append-only callback at offset 448 in a 464-byte structure. The test fails against the AARCH64 SDK and compiles against the v0.5 SDK. This is a checkout/version expectation mismatch; the canonical contract for this AARCH64 SDK is not established as missing a field. No Developer Studio or SDK source was changed.

The known standalone Phase-3 timeout remains a separate historical issue. Phase-9 and Phase-12 source/runtime behavior was not modified. No Raspberry Pi GICv2 preparation, tablet boot chain, or physical device was changed or tested.

## Next step

When QAC releases the exclusive acceptance lane, rerun `scripts/test-aarch64-gicv3-qemu.ps1`. Do not advance the outcome until the diagnostic boot and three fresh boots produce guest-originated GICv3, physical-timer, EOI, scheduler, and exception-return evidence. Run the GICv2 guest regression afterward under its own valid QAC authorization.
