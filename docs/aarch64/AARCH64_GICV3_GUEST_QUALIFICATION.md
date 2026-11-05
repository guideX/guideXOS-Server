# AArch64 GICv3 Guest Qualification — GIC3-2

- **Outcome:** `GICV3_NATIVE_REPEATABILITY_VERIFIED`
- **Run date:** 2026-11-05
- **Repository / branch:** `D:\dev\guideXOSServer_AARCH64`, `AARCH64_SUPPORT`
- **Source base HEAD:** `37c739b3610721405be4a5bba23cdbe0d01a0c25`
- **Upstream:** `origin/AARCH64_SUPPORT` at the source base; no push was made.

## Result

The production GICv3 path completed one authorized diagnostic boot followed by three independent fresh QAC-authorized guest boots. Every run reached `AARCH64_PHASE4_PASS`, delivered the architectural physical timer through GICv3, recorded 10,000 completed scheduler preemptions, returned cleanly from exceptions, and reported zero unexpected IRQs or exceptions. Each run used a separate firmware-variable file and QAC reservation. The runner verified managed-child ownership, child exit, reservation release, and zero QEMU processes after cleanup.

A separately authorized GICv2 Phase 2 architecture/timer guest regression also passed. The preserved SCHEDR1 GICv2 3/3 evidence and candidate hashes remain in `AARCH64_SCHEDR1_REPEATABILITY.md` and were not modified.

## Source and candidate

The accepted PCI device-tree parser repair was present in `kernel/arch/arm64/phase2_platform.cpp`. The current parser passed the captured firmware DTB validation (52 assertions; `parsed=1`, diagnostic 0, stage 4, token 9, offset 7096; fixture SHA-256 `5aec32b925cb7259d651aca355106e58d77f7f1c553d80f0420c5b3e3036f311`). The source tree used for this qualification is the base HEAD above plus the local Phase 4 MMIO-range and QAC-runner changes described below. The candidate manifest records source input hashes and the dirty-tree state.

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| GICv3 `kernel.elf` | 210,840 | `78c6264eb26badc70db1ca409621099c4b2d73e045e637fe6822e801b927d947` |
| GICv3 `BOOTAA64.EFI` | 12,800 | `6465fbebf0427557623feff5e7d07b11f679dedb6ea72692b49db176ea2a9b2d` |
| Ramdisk | 67,108,864 | `74452c6109212bff33a73f2315e5aa029b44bff3cf8bb24257cd9850be5c22b9` |
| Candidate manifest | — | `c4515d5c037b920476abf0c3c182256b99ae938ede5f742eaad92791fe2bffc4` |

The manifest and staged image are under `out/aarch64-gicv3-native-qualification-verified-20261105/gicv3-image/`. The manifest records the QEMU executable hash, UEFI firmware hashes, LLVM/LLD versions and hashes, build profile, and tracked/untracked build-input hashes. The accepted QEMU profile was `-machine virt,gic-version=3,acpi=off -cpu cortex-a53`, 512 MiB RAM, UEFI, and the minimal Phase 4 workload.

## Hosted and build gates

| Gate | Result |
| --- | --- |
| Phase 2 parser/malformed-input host suite | PASS, 51 assertions |
| Captured firmware DTB using the repaired parser | PASS, 52 assertions |
| GICv3 hosted controller suite, including GICv2 selection and no-fallback controls | PASS, 62 assertions |
| Phase 3 fresh ARM64 build and scheduler host contract | PASS (`AARCH64_PHASE3_HOST_CONTRACT_PASS`) |
| Phase 4 host MMIO-resource regression and scheduler contract | PASS; covers the maximum eight GICR regions and preserves the GICv2 layout |
| Production freestanding Phase 4 ARM64 build/link | PASS |
| Fresh GICv2 Phase 2 build and host suite | PASS, 51 assertions |

## Native GICv3 evidence

The guest DTB reported Distributor base `0x08000000`, size `0x10000`, and one Redistributor region at `0x080a0000`, size `0x0f60000`; no fixed Galaxy Tab A8 addresses were assumed. The boot CPU affinity from the DTB was `0`. The discovered Redistributor frame was `0x080a0000`, its SGI/PPI frame was `0x080b0000`, and `MPIDR_EL1=0x80000000` normalized to affinity `0`. The guest observed an affinity match, an awake Redistributor, `WAKER=0`, and `GICR_CTLR=2`.

The guest read back `ICC_SRE_EL1=0x7`, `ICC_PMR_EL1=0xf8`, `ICC_BPR1_EL1=3`, `ICC_IGRPEN1_EL1=1`, and `ICC_CTLR_EL1=0x8c00` with combined EOI mode. The physical timer PPI was INTID 30, Group 1, enabled, level-triggered, priority 128. The architectural timer frequency was 62.5 MHz with a 100 µs interval. Guest logs record successful timer dispatch, exception return, scheduler/VFS integration, and the Phase 4 PASS marker.

The Phase 4 boot-resource validation initially exposed a real GICv3-specific defect: `phase4_main.cpp` unconditionally added a zero-sized GICC MMIO range even though GICv3 has no GICC. The fix now constructs common MMIO ranges by selected controller: GICv2 retains UART + GICD + GICC; GICv3 uses UART + GICD + all discovered GICR regions. `kMaxMmioRanges` is 10 to hold the UART, Distributor, and up to eight Redistributor regions. A hosted regression covers the maximum GICv3 layout and unchanged GICv2 layout. No GICv2 driver implementation was changed.

An intermediate authorized guest reached the Phase 4 PASS marker but the runner misclassified `completion-errors=0` as fatal because its error pattern matched a substring. The runner now requires a complete `FAIL`, `FATAL`, or `ERROR` token. That intermediate run was retained as diagnostic evidence and is not counted toward the final 3/3 result; the accepted result below came from a new diagnostic-plus-three-boot invocation using the fixed candidate and runner.

## QAC reservations and fresh boots

All runs used the genuine guideXOS QAC 2.0.0 CLI acceptance flow and its exclusive reservation mechanism. The runner used the bounded 1,800-second admission window and 300-second guest runtime limit. Before each admission there were no QEMU processes. QAC launched the exact `qemu-system-aarch64.exe` as its direct managed child with the requested machine and CPU arguments. Each session record verifies that the child PID's parent was the QAC CLI process, the child exited with code 0, the reservation was released, there was no ownership degradation, and the post-cleanup QEMU count was zero. The QAC snapshots after each run showed no remaining reservations or managed sessions.

The four GICv3 reservations were diagnostic `09c969964a1e41f5aabc5052df6c3b05`, then fresh boots `e24c054d94c34329a2e9317bed0cd361`, `97e17cfc26ae41d294b19922d46ca656`, and `df7187caa85846408240288448cfc741`. Each used a different vars file, initialized from template SHA-256 `b3b855c5a80310168051164986855692d1bdb06e67619856177965cd87c6774f`; each post-guest vars file had SHA-256 `ec6cd5a9b07d495686db24e25725d1edf484d535dd47b37a50100f77300b225e`.

| Run | QAC reservation | Timer ticks / ACK / EOI | Preemptions / switches | Task A / B / C progress | VFS reads / enumerations | Unexpected IRQs / exceptions |
| --- | --- | ---: | ---: | --- | ---: | ---: |
| Diagnostic | `09c969964a1e41f5aabc5052df6c3b05` | 10,000 / 10,000 / 10,000 | 10,000 / 20,006 | 23,928,540 / 24,013,268 / 24,280,174 | 212,070 / 13,254 | 0 / 0 |
| Fresh 1/3 | `e24c054d94c34329a2e9317bed0cd361` | 10,001 / 10,001 / 10,001 | 10,000 / 20,006 | 23,397,117 / 23,248,533 / 23,035,571 | 212,782 / 13,298 | 0 / 0 |
| Fresh 2/3 | `97e17cfc26ae41d294b19922d46ca656` | 10,001 / 10,001 / 10,001 | 10,000 / 20,006 | 25,799,949 / 25,576,784 / 25,046,523 | 224,905 / 14,056 | 0 / 0 |
| Fresh 3/3 | `df7187caa85846408240288448cfc741` | 10,001 / 10,001 / 10,001 | 10,000 / 20,006 | 25,248,347 / 25,202,609 / 24,878,652 | 224,605 / 14,037 | 0 / 0 |

All four GICv3 logs report invalid ACKs 0, completion errors 0, pending interrupts 0, successful Phase 4 VFS operations, and `AARCH64_PHASE4_PASS`. The three fresh runs each have their own serial log, QAC log, vars file, and session record under `out/aarch64-gicv3-native-qualification-verified-20261105/`.

## GICv2 compatibility

The separate QAC-authorized regression reservation was `ee4bd16332f84d52aaa7763dd9181ab2`, project `guideXOSServer-AARCH64-GIC3-2-GICv2`, with `virt,gic-version=2,acpi=off` and Cortex-A53. It reached `AARCH64_PHASE2_PASS`, selected GICv2, used GICD `0x08000000` and GICC `0x08010000`, and delivered the physical timer IRQ. Its `kernel.elf` SHA-256 is `3cd8f4ee645be52787d1a84a4b99f28bb86bb26436d3d34fecf9ceb7cd0bbb39`; its candidate manifest SHA-256 is `32b4649540ebcbfc37067e3d3b5648b0317d0eb3c8c7f5d090b5c751208eb93d`. QAC verified direct child ownership, exit, release, and zero post-cleanup QEMU processes for this run as well.

## Evidence locations and remaining scope

The final run record is `out/aarch64-gicv3-native-qualification-verified-20261105/logs-gicv3-20261105-150259-679/session.json`. It contains the candidate/manifest hashes and individual QAC attempt ownership, vars, exit, marker, and cleanup fields. Independent guest and QAC logs are in that directory and its `qac/` subdirectory. Build, host-test, and candidate-manifest outputs are preserved under the same `out/aarch64-gicv3-native-qualification-verified-20261105/` root. Earlier failed-at-boot and harness false-positive artifacts remain in their separate output directories.

This qualifies the production GICv3 path on the QEMU `virt` Cortex-A53 platform with the minimal Phase 4 workload. It does not qualify physical tablet or Raspberry Pi hardware, secondary-CPU Redistributor setup, SMP, or arbitrary platform-specific GIC layouts beyond the validated DTB and bounded MMIO/page-table capacities. The separate SCHEDR1 GICv2 3/3 native evidence remains intact.
