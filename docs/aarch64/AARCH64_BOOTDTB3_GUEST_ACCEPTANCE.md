# AARCH64 BOOTDTB3 Repaired DTB Guest Acceptance

**Outcome: `BOOTDTB3_GUEST_PASS_WITH_REPRODUCIBLE_REPEATABILITY_STALL`**

**Qualification checkout HEAD:** `032757b5837327d6611361a402e082d5ccccb0c7`

**Branch:** `AARCH64_SUPPORT` tracking `origin/AARCH64_SUPPORT`

The repaired parser and preserved minimal GICv2 candidate pass the hosted gates. One genuine QAC-authorized guest boot reached the Phase 4 pass marker with timer IRQ and scheduler evidence. Two additional fresh authorized boots stalled at the same preemptive-scheduler stage and hit the runner's 120-second runtime bound. The candidate was not rebuilt or modified.

## Repository and candidate

At preflight, the repository was at the expected HEAD on `AARCH64_SUPPORT`, with a clean tracked and untracked worktree and zero commits ahead of or behind `origin/AARCH64_SUPPORT`. The BOOTDTB1 diagnosis and BOOTDTB2 property attribution records were reviewed. Existing candidates and evidence were retained.

The preserved minimal no-Studio candidate is `out/aarch64-bootdtb2-repaired-20261102`. Its requested artifact hashes match exactly:

| Artifact | SHA-256 |
| --- | --- |
| `BOOTAA64.EFI` | `6465fbebf0427557623feff5e7d07b11f679dedb6ea72692b49db176ea2a9b2d` |
| `kernel.elf` | `fb0908e8511369284b440c054a946560d76b149903d57adb5c2590b75f006f3a` |
| `esp/ramdisk.img` | `74452c6109212bff33a73f2315e5aa029b44bff3cf8bb24257cd9850be5c22b9` |

The staged EFI and kernel copies have the same hashes as the candidate root files. A fresh Phase 4 build from the acceptance source HEAD under `out/bootdtb3-gate-phase4-20261104` reproduced all three hashes, including the ramdisk. The manifest's repaired parser, Phase 4 kernel, and parser-test source fingerprints match the current files. The Phase 4 build includes the architectural timer, common IRQ registry, scheduler, and bounded preemptive worker checks; no Studio guest was staged.

The preserved firmware-source fixture is `out/aarch64-bootdtb2-minimal-20261102/bootdtb2-guest-20261102-153806-117/firmware-source.dtb`: 1,052,672 bytes, SHA-256 `5aec32b925cb7259d651aca355106e58d77f7f1c553d80f0420c5b3e3036f311`. It is the actual firmware blob captured from the earlier authorized diagnostic guest, not a generated host fixture. Its loader-copy counterpart was not captured in that run.

## Hosted regressions and builds

- The Phase 2 production parser suite passed **51 assertions**. It includes malformed FDT header/structure controls, rejects a malformed `#address-cells` value length, accepts the synthetic PCI host's legitimate `#address-cells = <3>`, and preserves GICv2/timer discovery.
- The same current parser suite with the captured firmware DTB passed **52 assertions**. The external blob result was `parsed=1`, diagnostic `0`, parser stage `4` (`COMPLETE`), token `9` (`FDT_END`), structure offset `7096`.
- A negative control compiled the pre-repair parser from `45add80b6ac02238feee8e955690828d478d228c`. Against the captured firmware DTB it rejected the original property with diagnostic `2`, stage `2` (`STRUCTURE`), token `3` (`FDT_PROP`), and structure offset `5572`. The pre-repair source retains the `cells > 2` property predicate. The output is preserved at `out/bootdtb3-old-parser-control-20261104/negative-control.log`.
- A focused unsupported-resource-width control passed by rejecting the synthetic three-cell `reg` decode at `REG_RANGE` (diagnostic `2`, stage `2`, error `9`). Its source and executable are under `out/bootdtb3-resource-width-control-20261104/`. This checks resource decoding separately from generic FDT structure parsing; it does not broaden resource acceptance.
- Phase 4 allocator/VFS hosted controls passed.
- The GICv3 hosted suite passed **62 assertions**. This is hosted coverage only, not GICv3 guest qualification.
- The ABI2 adapter suite passed **104/104 checks**. This does not qualify Developer Studio guest behavior.
- Freestanding Phase 2 and Phase 4 AArch64 loader/kernel builds linked successfully. The Phase 4 rebuild's hashes match the preserved candidate as listed above.

Build and test outputs are preserved under `out/bootdtb3-gate-phase2-20261104`, `out/bootdtb3-gate-phase4-20261104`, `out/bootdtb3-gate-gicv3-20261104`, and `out/bootdtb3-gate-abi2-20261104`.

## Guest configuration and QAC admission

The existing runner was reviewed and used with its established guest profile:

| Setting | Value |
| --- | --- |
| QEMU | QEMU 11; executable SHA-256 `8d2183e745155a303ffb9fd861c5da31c5e6c19fcbfd082081cc3e4bcdb0e748` |
| Machine | `virt,gic-version=2,acpi=off` |
| CPU / RAM | Cortex-A53 / 512 MiB |
| Firmware | EDK2 AArch64 UEFI; code SHA-256 `47765fe344818cbc464b1c14ae658fb4b854f5c2ceffa982411731eb4865594d` |
| Variables | Fresh copy of `edk2-arm-vars.fd` template per run; template SHA-256 `b3b855c5a80310168051164986855692d1bdb06e67619856177965cd87c6774f` |
| Image | Preserved repaired Phase 4 candidate above; no Studio payload |

The existing BOOTDTB2 runner accepts `QacProject` and `AdmissionTimeoutSeconds` parameters. The prior reservation `039272c5e27a42dbb61bf56fb4e9bb7f` used a 12-second **runner** admission timeout. QAC logged that it accepted the request and queued it at position 1. The runner then cancelled it; the QAC log explicitly says `Cancelled by client`, so this was not QAC's own reservation expiration. QAC's coordinator has a separate wait deadline (default 1,800 seconds, configurable from 1 to 86,400 seconds).

Queue position 1 means first in the pending queue; it is not an exclusive grant. The QAC CLI contract says `qac run` waits until the reservation state is `Granted` and starts its direct QEMU child only afterward. At the prior attempt's post-cancel snapshot, QAC was online with one managed session and one managed QEMU process. That establishes concurrent managed activity but the saved snapshot did not preserve its project or reservation mode. QAC scheduling permits either an active acceptance reservation or active development reservation(s) to delay an acceptance request, so the evidence does not identify the blocker as specifically an acceptance session. A provider or coordination failure is not indicated: QAC was online, accepted the request, queued it, and later reported client cancellation rather than an IPC or coordinator error.

The prior attempt record and QAC log are preserved at:

- `out/aarch64-bootdtb2-repaired-20261102/bootdtb2-guest-20261104-151308-246/session.json`
- `out/aarch64-bootdtb2-repaired-20261102/bootdtb2-guest-20261104-151308-246/qac.log`

The runner already had a configurable admission timeout, so all three authorized runs used `-AdmissionTimeoutSeconds 1800` and `-RuntimeSeconds 120`. Each run used a fresh UEFI variables copy. The three QAC reservation IDs were `ba493fbf08ff4b119b0de04c1aeb0dcd`, `a1aef4d739ca4eb99f49a346ad567bb6`, and `a806585f44f047629f67bd3d939d9090`. QAC granted each reservation, launched QEMU as its managed direct child, verified each child exit, and released each reservation.

Each boot has its own serial log, QAC log, and `session.json` under `out/aarch64-bootdtb2-repaired-20261102/bootdtb2-guest-20261104-*`:

| Boot | QAC reservation | Result | Guest evidence |
| --- | --- | --- | --- |
| 1 | `ba493fbf08ff4b119b0de04c1aeb0dcd` | `ARM64_DTB_GUEST_PROGRESS_PHASE4_PASS`; QAC child exit 0 and release verified | `bootdtb2-guest-20261104-194441-502` |
| 2 | `a1aef4d739ca4eb99f49a346ad567bb6` | Runtime timeout after 120 seconds; QAC child exit 0 after harness QMP shutdown and release verified | `bootdtb2-guest-20261104-194744-664` |
| 3 | `a806585f44f047629f67bd3d939d9090` | Same runtime timeout and stage as boot 2; QAC child exit 0 after harness QMP shutdown and release verified | `bootdtb2-guest-20261104-195049-201` |

## Guest observations and remaining gates

Boot 1 produced real guest evidence:

- UEFI discovered its firmware DTB and printed a valid 1,052,672-byte header; the kernel printed `DTB header: status=0` and `DTB: OK`. In the repaired parser, the only successful return is from the `FDT_END` case after depth, GIC, timer, RAM, UART, and platform checks; that real guest success marker therefore proves parsing reached `FDT_END`. The runner's detailed parser trace is emitted only on parser failure, so there is no success token line in the serial log.
- The parser accepted a known platform (`platform_kind != UNKNOWN`), RAM ranges, PL011, GIC, and timer. Guest initialization then reported `GIC: OK version=2` and a physical timer at 62,500,000 Hz, interval 100 microseconds, PPI 30.
- The guest's `memory/VFS durability: PASS` line reported 10,000 timer ticks, 10,000 preemptions, 20,006 context switches, `unexpected-irq=0`, and `exceptions=0`. In the exercised GICv2 path, each IRQ reads GICC_IAR, dispatches the registered timer handler, then writes GICC_EOIR; the 10,000 dispatched timer ticks therefore correspond to 10,000 acknowledgements and 10,000 EOIs. The GICv2 candidate does not expose separate acknowledgement/EOI counters, so those counts are derived from the guest tick count and the verified handler path.
- The guest printed `exception return: PASS scheduler continuation resumed` and `AARCH64_PHASE4_PASS`. The session record independently confirms timer enabled, 10,000 ticks, 10,000 preemptions, 20,006 context switches, zero unexpected IRQs/exceptions, QAC-launched child, verified reservation release, and QAC exit code 0.

Boots 2 and 3 both printed the same final guest markers: `DTB: OK`, `GIC: OK version=2`, timer programming at 62.5 MHz/PPI 30, scheduler initialized, first kernel task entered, cooperative scheduling passed with 10,004 switches, and `physical timer: enabled`. Neither emitted `exception return: PASS scheduler continuation resumed`, `scheduler/VFS integration: PASS`, a durability metric, or `AARCH64_PHASE4_PASS` before the 120-second runtime deadline. Their session records show `runtime_timeout=true`, `guest_pass_marker=false`, and no final tick/scheduler metric; the zero metric fields mean the final guest line was absent, not a separately observed IRQ counter.

The earliest reproducible outstanding failure is the preemptive scheduler continuation after timer enable: execution starts `phase3_timer_start()`, enables IRQs, then waits in `kernel::scheduler::start()`. The guest log stops after the subsequent `/phase4/hello.txt` open and before the scheduler reports continuation resumed. Boots 2 and 3 reproduce that stall; boot 1 passed through it. The records do not localize a deeper kernel cause, and no speculative kernel change was made.

GICv3 guest qualification remains pending. Developer Studio ABI3 remains at zero qualifying Studio boots; Studio was not launched. No Samsung tablet or Raspberry Pi hardware was accessed.
