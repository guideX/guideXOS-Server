# AIDA_LPT Intel I219-LM Reset Boundary Isolation — Phase 6

Status: diagnostic isolation and candidate reset sequencing implemented; physical AIDA_LPT verification is still required. No I219 functionality claim is made by this document.

Repository: `D:\dev\guideXOSServer_NAVIGATOR_IMPROVEMENTS`
Branch: `NAVIGATOR_GENERAL_IMPROVEMENTS`
Phase 5 baseline: `735e13b723efe7678f1c46773ed00c0417bffe47`

## 1. Physical authority

AIDA_LPT contains:

- PCI vendor/device: `8086:156F`
- subsystem: `103C:8079`
- device family: Intel Ethernet Connection I219-LM, PCH SPT family

The Phase 5 Stage 2 image physically passed. It performed exact identity binding, safe BAR discovery/mapping, PCI memory and bus-master preparation, and one read-only `STATUS` MMIO probe. The desktop rendered, mouse and keyboard worked, Start and Terminal worked, shell commands ran, and `netdiag` completed. DHCP remained `INIT` and IPv4 remained unconfigured as expected.

The Phase 5 Stage 3 image physically failed. Firmware and boot completed, the graphical desktop and Welcome notification rendered, but mouse and keyboard input were dead. The failure did not require an Ethernet cable.

Therefore the following are not the physical failure boundary:

- exact `8086:156F` matching;
- BAR discovery and mapping;
- PCI memory/bus-master preparation;
- the minimal read-only `STATUS` access.

Stage 5 and Stage 7 must remain untested until this Phase 6 boundary is resolved.

## 2. Exact Phase 5 Stage 3 audit

The audited code is `kernel/core/nic.cpp`, `init_e1000()`, at the Phase 5 baseline. The Phase 5 I219 path enters this region after the Stage 2 `STATUS` read has completed.

### 2.1 Operation order

| Order | Operation | Register | Offset | Value / mask | Read semantics |
|---:|---|---|---:|---|---|
| 1 | disable receiver | `RCTL` | `0x0100` | write `0x00000000` | no readback |
| 2 | disable transmitter | `TCTL` | `0x0400` | write `0x00000000` | no readback |
| 3 | clear interrupt mask | `IMC` | `0x00D8` | write `0xFFFFFFFF` | clears all mask bits |
| 4 | drain pending causes | `ICR` | `0x00C0` | read and discard | read-to-clear |
| 5 | read control | `CTRL` | `0x0000` | read `ctrl` | rejects `0xFFFFFFFF` |
| 6 | request reset | `CTRL` | `0x0000` | write `ctrl | 0x04000000` | sets bit 26, preserves every bit returned by the read |
| 7 | wait for reset | `CTRL` | `0x0000` | up to `100000` reads | succeeds when bit 26 is clear; rejects `0xFFFFFFFF` |
| 8 | re-mask after reset | `IMC` | `0x00D8` | write `0xFFFFFFFF` | clears all mask bits |
| 9 | drain causes again | `ICR` | `0x00C0` | read and discard | read-to-clear |

There is no `IMS` write in this Stage 3 path. `IMS` is not touched by the audited reset sequence. The later normal path may enable interrupts, but the Phase 5 Stage 3 image stops before that path and the Phase 6 images never enable `IMS`.

The reset expression is exactly:

```text
ctrl = MMIO32[0x0000]
MMIO32[0x0000] = ctrl | 0x04000000
```

It is not a literal `0x04000000` write and it is not a masked reconstruction of documented writable bits. If the read returns `x`, the write is `x | 0x04000000`; all other returned bits are preserved. The only explicit mask is `E1000_CTRL_RST = (1u << 26) = 0x04000000`.

The Stage 3 diagnostic image also calls the common fail-closed `set_init_failure()` path after its intentional stop. That path repeats `IMC=0xFFFFFFFF` followed by an `ICR` read. This is cleanup after the reset boundary, not an additional reset requirement. Phase 6 diagnostic stops suppress that cleanup MMIO access so the observed boundary is not moved beyond the operation under test.

### 2.2 Stage 2 to Stage 3 delta

Stage 2 ends after:

```text
STATUS read at offset 0x0008
```

Stage 3 adds, in the source order above:

```text
RCTL <- 0x00000000
TCTL <- 0x00000000
IMC  <- 0xFFFFFFFF
ICR  <- read-to-clear
CTRL <- read
CTRL <- CTRL | 0x04000000
CTRL <- bounded polling reads until bit 26 clears
IMC  <- 0xFFFFFFFF
ICR  <- read-to-clear
```

The physical result proves that the Stage 2 `STATUS` probe is safe. It does not by itself distinguish the eight added operations, nor does it prove that the failure is a reset completion loop rather than a reset side effect.

## 3. Upstream e1000e evidence

The authoritative reference used here is the upstream Linux `e1000e` driver:

- PCI/device definitions: [`drivers/net/ethernet/intel/e1000e/hw.h`](https://github.com/torvalds/linux/blob/master/drivers/net/ethernet/intel/e1000e/hw.h)
- PCI table and board selection: [`drivers/net/ethernet/intel/e1000e/netdev.c`](https://github.com/torvalds/linux/blob/master/drivers/net/ethernet/intel/e1000e/netdev.c)
- PCH MAC/PHY implementation: [`drivers/net/ethernet/intel/e1000e/ich8lan.c`](https://github.com/torvalds/linux/blob/master/drivers/net/ethernet/intel/e1000e/ich8lan.c)
- Intel I219 datasheet: [Ethernet Connection I219 Datasheet](https://cdrdv2-public.intel.com/612523/ethernet-connection-i219-datasheet.pdf)

The relevant Linux source paths and functions are:

| Question | Upstream evidence |
|---|---|
| Which MAC family is `8086:156F`? | `E1000_DEV_ID_PCH_SPT_I219_LM 0x156F` in `hw.h`; the PCI table maps it to `board_pch_spt` in `netdev.c`. |
| Which MAC type is selected? | `board_pch_spt` selects `e1000_pch_spt` through `e1000_pch_spt_info` in `ich8lan.c`. |
| Which reset function? | `e1000_pch_spt_info.mac_ops` uses `ich8_mac_ops`; its `reset_hw` is `e1000_reset_hw_ich8lan`. |
| Is this the ordinary generic E1000 reset? | No. The family-specific `e1000_reset_hw_ich8lan` implementation is selected. It is a PCH reset flow that happens to issue `CTRL.RST` as one step. |
| Is `CTRL.RST` issued? | Yes. `e1000_reset_hw_ich8lan()` reads `CTRL` and writes `ctrl | E1000_CTRL_RST`. |
| What precedes it? | `e1000e_disable_pcie_master()`, interrupt mask, `RCTL=0`, `TCTL=E1000_TCTL_PSP`, a `STATUS` flush, and approximately 10 ms. |
| What follows it? | Linux deliberately does not flush immediately after the `CTRL.RST` write because the source comments that the flush hangs hardware; it waits about 20 ms, releases the software flag, performs configuration-done / post-PHY work when applicable, then masks and drains interrupts again. |
| Is PHY reset distinct? | Yes. The function checks `phy.ops.check_reset_block()`, and when reset is not blocked it adds the PCH `CTRL_PHY_RST` bit to the value written with `CTRL.RST`. The PHY has its own reset and initialization path. |
| Is ownership checked? | Yes. Linux checks the PCH `FWSM` management/reset-block state and acquires the ICH8/PCH software flag before the reset write. |
| Does the PHY have a fixed MDIC address? | The PCH PHY initialization sets `phy->addr = 1`; the I219 datasheet documents the MDIC fields used by the management path. |

### 3.1 Reset semantics conclusion

For `8086:156F`, issuing the legacy-looking `CTRL.RST` bit is not inherently the wrong register operation: Linux e1000e does issue it for the `e1000_pch_spt` MAC type. However, a bare `CTRL.RST` write surrounded only by generic E1000 operations is not equivalent to the Linux PCH reset. The PCH implementation treats reset as a coordinated MAC/PCH/PHY operation with:

1. PCIe-master quiescence before reset;
2. `TCTL.PSP` rather than a zero transmitter value;
3. a `STATUS` flush and a pre-reset delay;
4. management-firmware reset-block checks;
5. PCH software-flag ownership;
6. conditional PHY reset (`CTRL_PHY_RST`) and PHY post-reset handling;
7. a post-write delay and no immediate flush;
8. interrupt re-mask and cause drain after reset.

This is concrete evidence against treating I219 as an ordinary 82540/82574 reset with only a different PCI ID. It is not evidence that all of those mechanisms can safely be copied into guideXOS before their PCIe, FWSM, PHY semaphore, and timing abstractions exist. Phase 6 therefore adds only a diagnostic candidate for the documented MAC boundary and does not speculate about PCI configuration, ME ownership, PHY, SMBus/ LANPHYPC, ULP, NVM, or DMA behavior.

The upstream PCH code also shows why skipping reset cannot yet be called a correct permanent initialization strategy: Linux still has to establish a known MAC/PHY state and deal with firmware-managed ownership before using the PHY. A firmware-initialized/no-reset path may be a future bring-up option, but it would need an explicit inventory of inherited state and a physical validation plan.

## 4. Exception and fault-path audit

The NIC is initialized after the AMD64/x86 interrupt and exception infrastructure and PIT are initialized in `kernel/core/main.cpp`. The architecture exception stubs use the dedicated exception stack, and `exception_dispatch()` in `kernel/arch/amd64/syscall.cpp` logs the vector, error code, RIP, and RFLAGS before the fatal path.

Relevant behavior:

- page fault (`#PF`) and general protection (`#GP`) are logged and then halted;
- double fault is halted;
- other unhandled vectors below 32, including machine check (`#MC`), are logged and halted;
- normal hardware IRQs are not enabled for the I219 diagnostic images;
- `phase6_trace_begin()` emits a serial marker before every diagnostic MMIO access and `phase6_trace_complete()` emits the matching marker only after the access returns.

Consequently:

- a `begin` marker without `complete` identifies the MMIO access, or a fault during that access, as the last observed boundary;
- a visible exception record identifies a delivered CPU exception rather than an infinite polling loop;
- absence of both the completion marker and an exception record cannot prove a C++ infinite loop: an MMIO transaction, machine-check delivery, serial path, or platform-level hang may prevent the handler from producing output;
- no diagnostic path hides a CPU exception by catching it or converting it into a successful stage.

Phase 6 intentionally stops immediately after the operation under test. It does not perform a cleanup MMIO write that could hide a fault or shift the physical boundary.

## 5. Polling analysis

The existing bound is `I219_PHASE5_HW_WAIT_LIMIT = 100000`. It is an iteration bound, not a time bound, and it does not depend on PIT progress. That property is retained in Phase 6.

The bound can still look like a freeze if each PCIe MMIO read is slow or if the device holds the read transaction while reset is in progress. The source does not have a calibrated MMIO or CPU-frequency timing service, so no precise wall-clock duration is claimed. The diagnostic image logs one `reset-poll begin`, one `reset-poll complete`, and the final iteration count; it does not log every read.

The candidate reset adds the Linux-documented approximately 10 ms pre-reset and 20 ms post-write sequencing delays using the existing bounded bare-metal delay convention. These delays are diagnostic sequencing guards only. Reset completion remains determined by the finite `CTRL.RST` polling loop, not by a timer or an assumed number of delay iterations.

The physical serial trace is the authoritative way to distinguish:

- delay starvation before the reset write;
- a reset write that never returns;
- a reset that returns but never clears `CTRL.RST`;
- a reset that completes and leaves the platform unusable afterward.

## 6. Phase 6 micro-stages

The selector is `GXOS_AIDA_I219_PHASE6_STAGE`. It is compiled into the kernel and carried in build identity metadata. It affects only exact PCI device `8086:156F`; existing `8086:100E`, `10D3`, and `153A` paths retain their normal behavior.

All micro-stages start after the known-good Phase 5 Stage 2 status probe and stop before PHY, MAC-address, DMA-ring, DHCP, or normal NIC registration. The I219 interrupt mask remains closed; no stage writes `IMS`.

| Selector | Image identity | Operations, cumulative | Stop point |
|---:|---|---|---|
| 1 | `mask` | `IMC=0xFFFFFFFF`; `ICR` read-to-clear | after interrupt mask only |
| 2 | `rctl` | Stage 1 plus `RCTL=0x00000000` | after RCTL disable |
| 3 | `tctl` | Stage 2 plus `TCTL=0x00000000` | after TCTL disable |
| 4 | `ctrl-read` | Stage 3 plus `CTRL` read and all-ones check | after CTRL read |
| 5 | `ctrl-rst-write` | Stage 4 plus `CTRL <- ctrl | 0x04000000` | immediately after reset write, with no poll/readback |
| 6 | `reset` | Linux-informed candidate: IMC, `RCTL=0`, `TCTL=PSP`, `STATUS` flush, 10 ms, CTRL read, `CTRL.RST` write, 20 ms, bounded CTRL poll, post-reset IMC and ICR drain | after reset candidate completes or reports timeout/all-ones |

Stage 5 is the critical separation between the reset write itself and the first subsequent `CTRL` poll/read. Stage 6 is not claimed as a production reset implementation; it is the smallest candidate that tests the documented PCH MAC-reset ordering without adding PHY/MDIC or DMA work.

### 6.1 Trace markers

The micro-stage serial trace uses this form:

```text
[AIDA-I219-P6] op=<name> begin
[AIDA-I219-P6] op=<name> complete
[AIDA-I219-P6] op=<name> reg=0x<offset> value=0x<value>
```

The reset candidate additionally emits `reset-poll iterations=0x...`, or a timeout/failure line. A missing `complete` marker is meaningful and must be preserved in the physical test record.

## 7. Candidate repair decision

No permanent default reset-path replacement is made in Phase 6. The existing Phase 5 path remains the default when `GXOS_AIDA_I219_PHASE6_STAGE=0`, preserving the Phase 5 regression baseline for automated comparison. The Linux evidence justifies testing the Stage 6 candidate, but physical AIDA_LPT isolation has not yet established that it restores input or that all PCH ownership/PHY requirements are satisfied.

If physical testing shows Stage 6 completes and Phase 3 input checkpoints remain healthy, the next change should be a separate, narrowly reviewed production reset implementation that explicitly accounts for the still-unimplemented PCH mechanisms. If Stage 5 is the first failing image, the reset write itself is the boundary and simply adding a poll or delay is not a repair. If Stage 6 completes but the desktop is still non-responsive, the remaining cause is outside the reset-completion read loop and must be isolated before any permanent reset change.

## 8. ISO matrix

The generator is `scripts/create-phase6-i219-isos.ps1`. It builds each image from the canonical AMD64 path and invokes `scripts/create-release-iso.ps1` with `IsoBackend=PyCdlib`. It does not overwrite the Phase 5 output names.

Expected artifacts under `dist/`:

```text
guideXOS-Server-v0.1.0-phase6-aida-i219-mask-amd64.iso
guideXOS-Server-v0.1.0-phase6-aida-i219-rctl-amd64.iso
guideXOS-Server-v0.1.0-phase6-aida-i219-tctl-amd64.iso
guideXOS-Server-v0.1.0-phase6-aida-i219-ctrl-read-amd64.iso
guideXOS-Server-v0.1.0-phase6-aida-i219-ctrl-rst-write-amd64.iso
guideXOS-Server-v0.1.0-phase6-aida-i219-reset-amd64.iso
```

For each ISO, the release packaging step must produce the adjacent SHA-256 file and manifest. The manifest must record:

- exact ISO filename and byte size;
- SHA-256;
- source commit;
- `i219Phase5Stage=8`;
- the Phase 6 selector and human stage identity;
- canonical build arguments;
- ISO backend and tool versions.

The known Phase 5 Stage 2 image is a separate baseline and must not be overwritten.

## 9. Physical test instructions

Use AIDA_LPT in this exact order, rebooting between images:

1. Re-run or retain the known-good Phase 5 Stage 2 image as the baseline. Confirm desktop, mouse, keyboard, Start/Terminal, and `netdiag`.
2. Boot Phase 6 `mask` (selector 1). Capture serial output and test the same desktop/input checks.
3. If Stage 1 passes, boot `rctl` (selector 2) and repeat.
4. If Stage 2 passes, boot `tctl` (selector 3) and repeat.
5. If Stage 3 passes, boot `ctrl-read` (selector 4) and repeat.
6. If Stage 4 passes, boot `ctrl-rst-write` (selector 5) and repeat. Do not expect reset completion; the image intentionally stops immediately after the write.
7. Only if Stage 5 is safe, boot `reset` (selector 6), capture the complete serial trace and poll count, then repeat desktop/input checks.
8. Do not boot Phase 5 Stage 5 or Stage 7 as part of this sequence.
9. Do not enable an Ethernet cable as a variable until the reset boundary result is recorded; cable state is not required to reproduce the known failure.

At each stage record:

- whether the desktop renders;
- whether mouse and keyboard work;
- whether Start/Terminal and shell commands work;
- the last `[AIDA-I219-P6]` operation marker;
- any `[KERNEL-EXCEPTION]` line;
- the `CTRL` read/write values and reset-poll iteration count;
- whether `[AIDA-PHASE3] checkpoint=main-loop-ready` appears;
- whether `[AIDA-PHASE3] checkpoint=input-ready` appears.

## 10. Interpretation matrix

| Physical result | Interpretation |
|---|---|
| Stage 1 fails before `imc-write complete` | IMC write, its MMIO completion, or a delivered/undelivered CPU/platform fault is implicated. |
| Stage 1 passes; Stage 2 fails before `rctl-disable complete` | `RCTL=0` write is implicated. |
| Stage 2 passes; Stage 3 fails before `tctl-disable complete` | `TCTL=0` write is implicated. |
| Stage 3 passes; Stage 4 fails before `ctrl-read complete` | First `CTRL` read is implicated. |
| Stage 4 passes; Stage 5 fails before `ctrl-rst-write complete` | The reset write transaction is implicated. |
| Stage 5 fails after `ctrl-rst-write complete` or input dies later | Reset side effects, asynchronous platform/device state, or later startup behavior is implicated; this is not a polling-loop proof. |
| Stage 6 fails before a candidate marker completes | The named candidate operation or delay boundary is implicated. |
| Stage 6 reaches `reset-poll complete` with a finite count but input dies | Reset completed; investigate post-reset chipset/device state or unrelated startup interactions. |
| Stage 6 reports `reset poll timeout` | Reset was accepted but `CTRL.RST` did not clear within 100000 reads; do not increase the bound without evidence. |
| Every stage reaches the main-loop/input checkpoints and input works | The candidate is physically promising, but it is not yet a production I219 implementation until PHY/ownership/MAC/DMA work is separately validated. |

## 11. Automated validation status

The required validation set for the Phase 6 handoff is:

- focused network diagnostics;
- canonical AMD64 build;
- hosted Navigator smoke;
- kernel Navigator deterministic smoke;
- QEMU E1000 regression;
- virtio-rng regression where relevant;
- release ISO structural verification;
- `git diff --check`.

Automated results and the final ISO SHA-256 values belong in the Phase 6 handoff record after the matrix is built. Automated/QEMU success does not substitute for AIDA_LPT physical evidence.

## 12. Remaining hardware unknowns

The following remain intentionally unresolved:

- exact AIDA_LPT `CTRL` value before reset and the value after each reset stage;
- whether the Stage 3 failure is caused by an individual write, reset transaction, reset side effect, or later startup;
- whether AIDA_LPT firmware/ME blocks or owns PHY reset;
- whether PCIe-master quiescence is required on this platform before MAC reset;
- whether the inherited UEFI state is sufficient for a no-reset initial bring-up;
- whether the PCH PHY/SMBus/ULP/LANPHYPC path can be safely implemented in guideXOS;
- whether a completed MAC reset leaves RAR/NVM/PHY state suitable for later driver work;
- physical Ethernet link negotiation and DHCP behavior.

Until those questions are answered on hardware, guideXOS must not claim I219-LM functionality or proceed to the later PHY/DMA diagnostic stages.
