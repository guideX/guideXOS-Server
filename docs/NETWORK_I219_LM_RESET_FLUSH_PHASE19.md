# AIDA_LPT I219-LM reset / descriptor-ring hang audit — Phase 19

## Outcome

Phase 19 keeps the Phase 18 `CTRL_EXT.DRV_LOAD` ownership correction and the
Phase 16–17 constrained-low, legacy-descriptor raw-TX experiment unchanged.
It adds a bounded I219/SPT reset-boundary audit and a fail-closed,
ownership-gated upstream-equivalent flush helper for any future reset that
occurs after guideXOS owns live rings.

The answer to the primary question is qualified:

> The current permanent guideXOS I219 reset occurs before guideXOS programs
> its RX or TX rings. Therefore this boot path does not currently perform a
> reset with live guideXOS rings. It can still enter with stale firmware or
> previous-OS ring state; Phase 19 captures `0xE4`, TDLEN/RDLEN, bases, and
> ownership at that boundary. If `FLUSH_DESC_REQUIRED` is set with a nonzero
> TDLEN and ownership is not provably guideXOS, the image does not DMA through
> the stale address and does not issue the reset.

This does not yet explain the physical Phase 17/18 `TDT=1`, `TDH=0`, `DD=0`
result. Physical Phase 19 validation remains pending.

## Repository and artifact

- Repository: `D:\dev\guideXOSServer_NAVIGATOR_IMPROVEMENTS`
- Branch: `NAVIGATOR_GENERAL_IMPROVEMENTS`
- Build: full AMD64, Phase 5 selector `8`, Phase 6 selector `0`, Phase 7
  selector `4`, constrained-low TX DMA experiment enabled
- ISO: [`dist\guideXOS-Server-v0.1.0-phase19-aida-i219-reset-flush-amd64.iso`](../dist/guideXOS-Server-v0.1.0-phase19-aida-i219-reset-flush-amd64.iso)
- Size: `91,293,696` bytes
- SHA-256: `f99b7cdcb7600fab0b5fbc53bc00ca906aa7763e3d5dfb8c0aebb4aacf6ba019`
- Checksum: `dist\guideXOS-Server-v0.1.0-phase19-aida-i219-reset-flush-amd64.iso.sha256`
- Manifest: [`dist\guideXOS-Server-v0.1.0-phase19-aida-i219-reset-flush-amd64.manifest.json`](../dist/guideXOS-Server-v0.1.0-phase19-aida-i219-reset-flush-amd64.manifest.json)

The manifest records `bootable=true`, the PyCdlib backend, source commit
`3a4ad4e5136472c0b3f3f17dced75b5dddc79f9c`, branch
`NAVIGATOR_GENERAL_IMPROVEMENTS`, and the exact Phase 5/6/7 and DMA-placement
selectors used for this artifact.

## Phase 18 physical evidence carried forward

Phase 18 was physical Case B on AIDA_LPT. It showed:

```text
DriverReady=YES
Link=UP
AMT=yes
hw-control=CTRL_EXT.DRV_LOAD
CTRL_EXT=0x114A1027
DRV_LOAD=yes
ownership-requested=yes
ownership-readback=yes
before=0x014A1027
after=0x114A1027
after-reset=0x014A1027
FWSM=0xE001C25C
stage=final
failure=none
```

This proves that the Phase 18 `CTRL_EXT.DRV_LOAD` correction works on the
physical I219 and is retained in the Phase 19 image. It is not evidence that
the TX descriptor-fetch problem is solved.

The Phase 17/18 raw fixture also remains unchanged:

```text
destination = FF:FF:FF:FF:FF:FF
source      = physical NIC MAC
EtherType   = 0x88B5
payload     = GXOS-I219-P17
length      = 60 bytes excluding FCS
descriptor  = legacy 16-byte format
command     = EOP | IFCS | RS = 0x0B
```

The physical descriptor was approximately:

```text
qword0 = 0x00000000AAB03000
qword1 = 0x000000000B00003C
```

The CPU-visible descriptor stayed unchanged before TDT, after TDT, and at
the final observation. TDT publication succeeded (`0 -> 1`), TDH stayed at
zero, DD stayed absent, and the bounded million-poll attempt ended with:

```text
timeout=yes
poison=yes
failure=TX_DESCRIPTOR_NOT_CONSUMED
```

The DHCP attempt made afterward occurred after poisoning and is not
independent TX evidence. Phase 19 keeps DHCP out of the decisive test.

## Current upstream finding

The current Linux `e1000e` source explicitly documents the I219 reset hazard:
descriptor rings must be emptied before hardware reset or D3 entry; otherwise
the controller can enter a unit-hang state released only by PCI reset.

The authoritative source is the current upstream
[`e1000_flush_desc_rings`, `e1000_flush_tx_ring`, and `e1000_flush_rx_ring` implementation](https://github.com/torvalds/linux/blob/master/drivers/net/ethernet/intel/e1000e/netdev.c#L3579-L3672).
For PCH SPT and later, [`e1000e_reset`](https://github.com/torvalds/linux/blob/master/drivers/net/ethernet/intel/e1000e/netdev.c#L3877-L3880)
flushes descriptor rings before calling the MAC `reset_hw` operation. The
non-reset down path also flushes rings for this generation
([`e1000e_down`](https://github.com/torvalds/linux/blob/master/drivers/net/ethernet/intel/e1000e/netdev.c#L4071-L4075)).

The exact status definitions are current upstream
[`e1000.h`](https://github.com/torvalds/linux/blob/master/drivers/net/ethernet/intel/e1000e/e1000.h#L72-L73):

```text
PCICFG_DESC_RING_STATUS = PCI config offset 0xE4
FLUSH_DESC_REQUIRED     = 0x0100
```

The I219 flush path reads `FEXTNVM11` at `0x5BBC` and applies only
`E1000_FEXTNVM11_DISABLE_MULR_FIX = 0x2000`, preserving unrelated bits. The
register and workaround definitions are in current upstream
[`regs.h`](https://github.com/torvalds/linux/blob/master/drivers/net/ethernet/intel/e1000e/regs.h#L24-L25)
and [`ich8lan.h`](https://github.com/torvalds/linux/blob/master/drivers/net/ethernet/intel/e1000e/ich8lan.h#L81-L84).

The current upstream PCH SPT board record is
[`e1000_pch_spt_info`](https://github.com/torvalds/linux/blob/master/drivers/net/ethernet/intel/e1000e/ich8lan.c#L5736-L5754).
It carries the SPT PHY-statistics and EEE flags, but not `FLAG2_DMA_BURST`.
The exact I219-LM identity is listed in current upstream
[`hw.h`](https://github.com/torvalds/linux/blob/master/drivers/net/ethernet/intel/e1000e/hw.h#L70-L73).

## Exact reset ordering in guideXOS

The permanent physical path is selected by Phase 5 `8`, Phase 6 `0`, and
Phase 7 `4`. Its lifecycle is:

| Order | guideXOS operation | Ring state and ownership |
|---:|---|---|
| 1 | Bootloader identifies PCI `8086:156F`, maps BAR0, and passes BootInfo. | No guideXOS rings exist. |
| 2 | `init_from_bootinfo` clears software state, selects the loader-owned constrained-low TX region, and calls `init_e1000`. | No guideXOS rings are initialized. |
| 3 | Phase 18 captures initial `CTRL_EXT`/FWSM. | No guideXOS rings. |
| 4 | `i219_pch_reset` masks interrupts, disables RCTL/TCTL, drains STATUS, delays, reads CTRL, runs the Phase 19 pre-reset audit, optionally performs only a safe known-owner flush, writes `CTRL.RST`, delays, and polls the self-clearing reset bit. | `txRingInitialized=false`, `rxRingInitialized=false`; any nonzero hardware ring state is stale/unknown, not guideXOS-owned. |
| 5 | Phase 18 captures post-reset ownership state and sets `CTRL_EXT.DRV_LOAD`; MAC and PHY state is read/configured. | Still no guideXOS descriptor rings. |
| 6 | MTA is cleared; `init_rx` fills RX descriptors and programs `RDBAL/RDBAH/RDLEN/RDH/RDT`, then enables RCTL. | RX ring belongs to guideXOS. |
| 7 | `init_tx` fills 64 legacy descriptors and programs `TDBAL/TDBAH`, `TDLEN=0x400`, `TDH=0`, `TDT=0`, TXDCTL, TIPG, TCTL, TARC, and IOSFPC. | TX ring belongs to guideXOS. |
| 8 | Link refresh, final ownership check, NIC registration, and `driverReady` publication occur. | Both rings are live and guideXOS-owned. |
| 9 | `nicinfo tx raw` submits the unchanged 60-byte legacy fixture. No reset is attempted after raw TX; timeout poisons the ring and stops the experiment. | A failed TX does not trigger a reset or retry. |
| 10 | Shutdown/reboot uses ACPI/port/SBI platform paths. | No NIC reset or ring flush is currently performed there. |

The other I219 Phase 5/6 isolation paths also receive a diagnostic capture
immediately before each `CTRL.RST` write. They do not force a runtime flush;
the stages remain diagnostic stops. The PCI-scan-only legacy path never has a
safe mapped MMIO handoff and fails closed before hardware bring-up.

## Ring ownership and safe criteria

Phase 19 retains separate TX and RX ownership classifications in
`I219ResetDiagnostics`:

```text
none     = all relevant ring base/length/head/tail fields are zero
guidexos = programmed base and legacy geometry match the live guideXOS state
unknown  = a ring is programmed but its address/geometry is not provably the
           current guideXOS allocation
```

The compact `nicinfo tx reset` command reports the aggregate
`ring-owner=none|guidexos|unknown`; the cached structure retains TX and RX
classifications separately. A required flush is safe only when the active
ring is `guidexos`. If `0xE4 & 0x0100` is set with nonzero TDLEN and TX is not
provably guideXOS-owned, the image records
`I219_RESET_RING_OWNERSHIP_UNKNOWN`, sets
`strongerRecoveryRequired`, and performs no DMA through that address. No
reading of the bit alone is classified as failure when TDLEN is zero or the
bit is clear.

`nicinfo tx reset` is cache-only and is capped at 19 logical lines in the
normal branch:

```text
NIC TX reset audit
family=I219-SPT
reset-count=...
cfg-e4=0x....
cfg-e4-after=0x....
flush-required=yes/no
TDLEN=... TDH/TDT=...
RDLEN=... RDH/RDT=...
ring-owner=guidexos/none/unknown
preflush-needed=yes/no
preflush-attempted=yes/no
preflush-complete=yes/no
FEXTNVM11-before=...
FEXTNVM11-after=...
reset-performed=yes/no
reset-completed=yes/no
failure=...
```

## Controlled correction

The implementation is exact-I219/SPT gated and does not affect QEMU’s
ordinary E1000 path. The production helper follows the current upstream
decision boundary:

1. Capture FEXTNVM11, all ring registers, TCTL/RCTL, CTRL/CTRL_EXT, and PCI
   config `0xE4` immediately before reset.
2. Preserve FEXTNVM11 and OR only `0x2000`, as in upstream’s I219 flush path.
3. Treat flush as needed only when `FLUSH_DESC_REQUIRED` is set and TDLEN is
   nonzero.
4. Require a proven guideXOS TX owner before any TX flush. Require a proven
   guideXOS RX owner before any conditional RX flush.
5. Flush TX through the legacy ring with the current descriptor’s ring base,
   length `512`, command `IFCS`, cleared status/auxiliary fields, a store
   fence, and a bounded TDT publication/completion poll.
6. Re-read PCI config `0xE4`. If the required state remains, disable RX,
   program RXDCTL thresholds (`0x1F`, bit 8, and descriptor granularity
   `0x01000000`), briefly enable/flush RX, disable it again, and re-read the
   status.
7. Only a clear/no-longer-required status permits `CTRL.RST` to proceed.

If a flush times out, status persists, or a required register cannot be read,
the reset is refused and the failure is retained as one of:

```text
I219_RESET_FLUSH_REQUIRED
I219_RESET_RING_OWNERSHIP_UNKNOWN
I219_TX_FLUSH_TIMEOUT
I219_RX_FLUSH_TIMEOUT
I219_RESET_HANG_STATE_PERSISTS
I219_RESET_REGISTER_READ_FAILED
```

The current boot has no live guideXOS ring at the reset boundary, so normal
Phase 19 boots take the diagnostic/no-flush branch. If firmware or a previous
OS leaves a nonzero unknown ring with the required bit set, the boot fails
closed and a stronger recovery operation, potentially PCI function-level
reset, must be investigated separately. Phase 19 does not invent that PCI
reset.

## TXDCTL nuance

The upstream `e1000_pch_spt_info` record does not contain
`FLAG2_DMA_BURST`; current upstream `e1000_configure_tx` therefore does not
apply `E1000_TXDCTL_DMA_BURST_ENABLE` solely because the device is SPT.
GuideXOS Phase 15’s `TXDCTL=0x0141001F` is an experimental policy, not an
exact current-upstream SPT requirement. Phase 19 deliberately leaves it,
along with TCTL, TIPG, TARC0, IOSFPC, bus mastering, fences, TDT polling, and
the safe timeout poison, unchanged. A later A/B cleanup may isolate TXDCTL;
combining that cleanup with this reset experiment would make the result
ambiguous.

## Cold/warm provenance

The reset audit is captured on every I219 reset, so a later physical operator
can compare a boot after another OS/reboot with a fresh power-on boot without
requiring both comparisons before this ISO is produced. Compare at least:

```text
cfg-e4 / flush-required
TDLEN and RDLEN
TDBAL/RDBAL and TDH/TDT/RDH/RDT
ring-owner
preflush-complete
FEXTNVM11-before/after
reset-completed / failure
```

The current repository artifact does not claim that a QEMU or hosted result
is physical evidence.

## Tests, AMD64 build, and QEMU

The Phase 19 test script invokes the complete Phase 11–18 regression chain,
then compiles and runs the new hosted reset-flush policy test. It checks the
exact PCI offset/mask, I219 gate, no-flush predicates, ownership refusal,
bounded flush constants, reset ordering, shell bound, raw fixture, and
constrained DMA invariants.

The image is built with the canonical AMD64 build and packaged by the Phase
19 release script. The release packager performs its structural ISO checks
and emits the release manifest and SHA-256. Multiple fresh QEMU boots use an
ordinary `e1000` device; they verify only normal boot/desktop readiness and
that the I219-only path does not disturb QEMU. QEMU cannot prove the physical
I219 reset-hang theory.

## Physical validation procedure

Boot the Phase 19 ISO on AIDA_LPT with Ethernet connected. On every fresh
boot, before any TX command, run:

```text
nicinfo brief
nicinfo tx owner
nicinfo tx reset
nicinfo tx brief
```

Photograph the complete `nicinfo tx reset` output. Record `cfg-e4`,
`flush-required`, TDLEN/RDLEN, TDH/TDT/RDH/RDT, `ring-owner`,
`preflush-needed`, `preflush-attempted`, `preflush-complete`, FEXTNVM11, reset
state, and failure. Then run:

```text
nicinfo tx raw
nicinfo tx raw status
nicinfo tx brief
```

The first physical TX remains `nicinfo tx raw`, not DHCP. If it times out,
stop immediately. Do not run DHCP afterward because the ring is poisoned and
the later attempt cannot be independent evidence.

## Outcome cases A–G

| Case | Observation | Recommendation |
|---|---|---|
| A | `FLUSH_DESC_REQUIRED` was present, a safe I219 flush cleared it, reset completed, and raw descriptor consumption now works. | Strongly implicates the documented I219 reset-hang erratum. Repeat across fresh boots before broadening networking. |
| B | The bit was present and safely cleared, but raw TX still has TDT=1 / TDH=0 / DD absent. | Keep the lifecycle fix and move to descriptor/fetch-mode investigation. |
| C | `FLUSH_DESC_REQUIRED` was never present at relevant resets. | Reject the reset-hang theory for this physical boot; Phase 20 may investigate descriptor interpretation or an advanced-data-descriptor A/B. |
| D | The bit is set but active ring ownership is unknown/stale. | Do not DMA through it. Investigate safe PCI/function recovery or reset ordering only. |
| E | A flush is attempted but the status persists. | Investigate exact I219 flush semantics and the required stronger recovery path. |
| F | TDH advances but DD remains absent. | Descriptor fetch works; investigate completion/writeback semantics. |
| G | DD appears but software misses it. | Fix completion observation only. |

The recommended next phase depends on the case, but no case justifies
changing descriptor format before the reset evidence is captured.
