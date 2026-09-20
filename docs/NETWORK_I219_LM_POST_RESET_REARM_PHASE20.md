# Intel I219-LM Post-Reset TX Rearm — Phase 20

Status: implementation and host/QEMU evidence complete; AIDA_LPT physical result PENDING.

## Repository and artifact provenance

- Repository: D:\dev\guideXOSServer_NAVIGATOR_IMPROVEMENTS
- Branch: NAVIGATOR_GENERAL_IMPROVEMENTS
- Starting HEAD: e68feecce1e3c0d1ce5c4f2315b1ed0dfb52c3d4
- Clean start: yes
- Upstream: origin/NAVIGATOR_GENERAL_IMPROVEMENTS
- Ahead/behind at preflight: 0/0
- Implementation commits: 031ff74b0b78c13ad7142569aef1fb6b8d4184d4,
  047bf37e9b4b4b2268d27c4f56d0a51e57598d4
- Build-identity commit: ddf1e1e41edca08a4a2bf454d29b156f93979e52
- Artifact source commit: ddf1e1e41edca08a4a2bf454d29b156f93979e52
- Push status: not pushed
- Packaging start: clean

The final report itself is a subsequent documentation-only local commit. The
artifact manifest intentionally records the clean source commit used to build
and package the ISO.

## Answers to the Phase 20 questions

### nicinfo tx reset semantics

Before Phase 20, nicinfo tx reset was already a retained, cache-only Phase 19
boundary summary. It did not write MMIO, flush rings, assert CTRL.RST, or
reinitialize the device. Phase 20 preserves that compatibility and makes the
semantics explicit in help:

- nicinfo tx reset: read-only retained/latest reset audit.
- nicinfo tx reset brief: read-only 12-line summary; no hardware access.
- nicinfo tx reset run: explicit destructive I219 MAC reset followed by the
  bounded post-reset RX/TX rearm.
- nicinfo tx rearm: explicit post-reset rearm without another reset; it
  requires a retained completed reset boundary.
- nicinfo tx lifecycle: read-only 20-line lifecycle/register summary.

Thus a later raw-TX result cannot silently be attributed to a reset initiated by
the old diagnostic spelling: only reset run performs that reset.

### PCI configuration offset 0xE4 and flush semantics

GuideXOS now records PCI configuration offset 0xE4, named
PCICFG_DESC_RING_STATUS, and exposes its exact raw mask:

~~~
PCI_CONFIG_FLUSH_DESC_REQUIRED = 0x0100
~~~

This is bit 8 of PCI config 0xE4. flush-required=yes means the raw bit is
asserted, independently of ring length. The reset action predicate is separate:
flush-needed = flush-required && TDLEN != 0. That distinction matters for the
Phase 19 observation of zero rings.

The exact AIDA_LPT 0xE4 value and whether bit 8 is physically asserted are
still PENDING until nicinfo tx reset brief is photographed on the target.
The command now makes both the exact four-digit value and the raw bit
impossible to omit.

The related bus-master definitions are diagnostic only:

- STATUS.GIO_MASTER_ENABLE = 0x00080000 (bit 19), reported as
  gio-master=yes/no/unknown.
- CTRL.GIO_MASTER_DISABLE = 0x00000004 (bit 2), not written by Phase 20.

### Phase 19 evidence incorporated

The latest physical Phase 19 snapshot remains:

~~~
TDLEN=0; TDH/TDT=0/0
RDLEN=0; RDH/RDT=0/0
ring-owner=unknown/none
preflush-needed=no
preflush-attempted=no
preflush-complete=yes
FEXTNVM11-before=0xFB21C1C2
FEXTNVM11-after=0xFB21C1C2
reset-performed=yes
reset-completed=yes
failure=none
~~~

This strongly weakens stale-ring/reset-hang as the explanation, but did not
prove the 0xE4 flush bit because that field was not visible in the
photographs. Phase 20 retains this snapshot and adds explicit cfg-e4 and
flush-required reporting.

## Reference comparison

The reference used for the audit is the current upstream e1000e implementation
for the PCH SPT/I219 family:

- https://raw.githubusercontent.com/torvalds/linux/master/drivers/net/ethernet/intel/e1000e/netdev.c
- https://raw.githubusercontent.com/torvalds/linux/master/drivers/net/ethernet/intel/e1000e/ich8lan.c
- https://github.com/torvalds/linux/blob/master/drivers/net/ethernet/intel/e1000e/e1000.h
- https://cdrdv2-public.intel.com/612523/ethernet-connection-i219-datasheet.pdf

| Reference stage | guideXOS Phase 20 stage | Present/order | Reset-sensitive | Descriptor-fetch relevance |
|---|---|---|---|---|
| PCI Memory Space + Bus Master enable | PCI command is enabled during bind; rearm verifies both bits | Yes; before reset and before rearm rings | Yes | Required for DMA |
| STATUS.GIO_MASTER_ENABLE | Read into lifecycle snapshots; no write | Diagnostic only | Observable | Separates PCI command from MAC-side master state |
| CTRL.GIO_MASTER_DISABLE | Defined but not written | Deliberately not added | Potentially | Speculative for this failure |
| Interrupt mask/drain | Masked before reset and after reset; rearm does not enable causes | Yes | Yes | Prevents unrelated interrupt state |
| MAC reset | nicinfo tx reset run performs one bounded CTRL.RST; wait then poll completion | Yes; explicit only | Yes | Establishes the reset boundary |
| FEXTNVM11 | Preserve/read and apply upstream 0x2000 MULR-fix handling only in reset preparation | Yes | Reset workaround | Relevant only if flush status requires it |
| CTRL_EXT.DRV_LOAD | Phase 18 mechanism is reacquired after reset and again immediately before ring programming | Yes; before RX/TX | Yes | Ownership can gate descriptor fetch |
| MAC/RAR state | RAL0/RAH0 is read back after reset and compared to the known physical MAC; no speculative write | Yes; before rings | Reset-sensitive readback | Prevents publishing with MAC drift |
| MAC/PHY initialization | Existing boot path remains unchanged; rearm does not reset PHY or change PHY registers | Boot: yes; rearm: read-only MAC check | PHY intentionally unchanged | Not a new variable |
| Packet buffer/PBA | Read diagnostically (0x00000012 observed); no Phase 20 write | Diagnostic only | Possible | Unproven relationship; deferred |
| RX ring | Rebuilt at the existing static placement after reset | Yes; before TX | Yes | Restores complete device ownership without changing RX placement |
| TDBAL/TDBAH, TDLEN | Constrained-low Phase 16 ring is cleared and programmed | Yes; before publication | Yes | Direct descriptor-fetch inputs |
| TDH/TDT | Both written zero and read back before enable/publication | Yes | Yes | Defines empty ring and doorbell boundary |
| TXDCTL0/1 | Established Phase 15 policy retained and read back | Yes | Yes | Direct engine readiness |
| TIPG, TCTL | Established values retained; TCTL is disabled while rebuilding, then enabled | Yes | Yes | Required transmitter policy |
| TARC0/1, IOSFPC | Established SPT workaround reapplied; no new policy | Yes | Yes | Retained known prerequisite |
| Link refresh | Existing bounded physical link refresh remains available; no PHY mutation | Yes; separate read-only operation | Link state may change | Not a descriptor-fetch proof |
| Descriptor publication | No descriptor is published until all rearm readbacks pass | Yes | Yes | Controlled experiment boundary |
| TDT doorbell | Raw Phase 17 fixture publishes exactly one legacy descriptor | Yes | Yes | Final physical observation |

The upstream SPT board record establishes PBA 26 (0x1A). GuideXOS has observed
PBA 0x00000012 on AIDA_LPT. Phase 20 keeps PBA read-only because the audit did
not prove that changing the split is required for this descriptor
non-consumption failure. The same restraint applies to FIFO and flow-control
registers.

## Ownership and reset lifecycle

Phase 18 established the only ownership mechanism used here:

~~~
CTRL_EXT.DRV_LOAD = 0x10000000
~~~

The physical Phase 18 values were:

~~~
before reset:      0x014A1027
after reset:       0x014A1027
after DRV_LOAD:    0x114A1027
~~~

Phase 20 preserves unrelated CTRL_EXT bits, keeps FWSM read-only, never seizes
SWFLAG/SWSM ownership, and records:

1. the normal boot-time initial ownership;
2. the completed reset boundary;
3. the post-reset DRV_LOAD acquisition;
4. a second ownership acquisition immediately before post-reset ring setup;
5. final ownership verification after TX setup.

Whether the physical reset clears DRV_LOAD remains an explicit AIDA question:
the retained values say whether the target cleared it or preserved it, and the
rearm command proves whether guideXOS restores it before ring programming.

## Controlled post-reset rearm

nicinfo tx reset run is bounded and exact-I219-only. Its sequence is:

1. verify reset applicability and perform the existing pre-reset audit;
2. preserve the validated reset preparation/flush policy;
3. assert CTRL.RST, wait, poll completion, and capture the post-reset boundary;
4. capture and reassert the Phase 18 DRV_LOAD ownership;
5. verify PCI Memory Space + Bus Master;
6. verify the constrained-low Phase 16 DMA handoff;
7. read back and validate RAL0/RAH0 against the existing adapter MAC;
8. rebuild RX at its existing static placement; no RX placement or PHY policy
   changes are introduced;
9. disable TCTL while rebuilding TX;
10. clear all 64 legacy 16-byte TX descriptors;
11. program/read back TDBAL/H, TDLEN 0x400, and TDH/TDT zero;
12. apply/read back the established TXDCTL0/1 policy;
13. program/read back TIPG, retain TARC0/1 and IOSFPC;
14. enable TCTL with the established policy;
15. read every critical TX register and ownership state back;
16. only after those checks pass, permit the existing one-descriptor raw test.

The rearm helper rebuilds RX as a reset-recovery prerequisite, but uses the
same existing RX placement. The experimental variable remains post-reset TX
rearm; RX placement, PHY behavior, DHCP, VLAN, checksum, timestamping, and
descriptor ABI are unchanged.

Failures are retained precisely, including:
TX_PCI_MASTER_DISABLED, TX_HW_CONTROL_NOT_RESTORED,
TX_POST_RESET_DMA_UNAVAILABLE, TX_POST_RESET_REGISTER_DRIFT,
TX_RING_REARM_READBACK_FAILED, and TX_POST_RESET_REARM_FAILED.

## Raw fixture preservation

The Phase 17 fixture is unchanged:

- destination ff:ff:ff:ff:ff:ff;
- source physical MAC;
- EtherType 0x88B5;
- marker GXOS-I219-P17;
- 60 bytes excluding FCS;
- legacy 16-byte descriptor;
- command EOP|IFCS|RS = 0x0B;
- status initially zero;
- one descriptor only;
- bounded polling and safe one-attempt poisoning unchanged.

No advanced descriptor command, cache flush, RX placement change, PHY reset,
DHCP packet, VLAN, checksum, or interrupt workaround was added.

## Host and virtual validation

- scripts/run-network-phase20-tests.ps1: PASS.
- Phase 11–19 regressions invoked by that script: PASS.
- Reset brief source bound: 12 newline sites.
- Lifecycle source bound: 20 newline sites.
- AMD64 build: PASS with selectors I219Phase5Stage=8,
  I219Phase6Stage=0, I219Phase7Stage=4, and constrained TX DMA enabled.
- ISO packaging and structural verification: PASS using PyCdlib.
- Fresh QEMU release boot with -device e1000: PASS through firmware,
  bootloader, kernel, ramdisk, desktop, and main-loop readiness.

QEMU is regression evidence only; it does not prove Intel I219-LM behavior.

## Artifacts

- ISO:
  dist\guideXOS-Server-v0.1.0-phase20-aida-i219-post-reset-rearm-amd64.iso
- Size: 91,293,696 bytes
- SHA-256:
  1db406528cb21421c7f48407cba2acdc33ab8e4014ebd4249ee5099480b5bac6
- Sidecar:
  dist\guideXOS-Server-v0.1.0-phase20-aida-i219-post-reset-rearm-amd64.iso.sha256
- Manifest:
  dist\guideXOS-Server-v0.1.0-phase20-aida-i219-post-reset-rearm-amd64.manifest.json

## AIDA_LPT procedure

Use the final ISO and a separate fresh boot for each test. Do not run the two
variants sequentially on one poisoned ring.

### Test 1 — ordinary fresh-boot baseline

1. Boot AIDA_LPT with no manual reset command.
2. Run nicinfo tx reset brief (read-only), then nicinfo tx lifecycle.
3. Run nicinfo tx brief.
4. Run nicinfo tx raw exactly once.
5. Run nicinfo tx raw status, then nicinfo tx lifecycle.
6. Photograph the reset brief, lifecycle, raw status, and serial markers.
7. If the attempt is ambiguous or times out, do not retry on that boot.

### Test 2 — separate fresh boot with explicit reset and rearm

1. Power-cycle/reboot AIDA_LPT into the same final ISO.
2. Run nicinfo tx reset brief before the destructive command.
3. Run nicinfo tx reset run exactly once.
4. Run nicinfo tx lifecycle.
5. Run nicinfo tx raw exactly once.
6. Run nicinfo tx raw status, then nicinfo tx lifecycle.
7. Photograph the full reset brief, lifecycle, rearm result, and raw status.
8. Do not retry after ambiguous ownership or a poisoned ring.

Record in both tests: exact cfg-e4, raw flush-required, owner state, PCI
command, GIO master state, DRV_LOAD state, DMA mode, TDBA, TDLEN, TXDCTL,
TCTL, TDT, TDH, descriptor DD, timeout/poll count, poison, and failure.

## Physical outcome classification

- A: normal fresh raw fails, but reset + complete rearm moves TDH/DD:
  reset/post-reset ordering is the root-cause candidate.
- B: normal fresh raw succeeds: prior testing was lifecycle-perturbed;
  reproduce across fresh boots before DHCP.
- C: both variants remain TDT=1, TDH=0, DD=0: reset/lifecycle is rejected;
  move to a narrow descriptor-fetch/FIFO/format experiment.
- D: cfg-e4 shows flush-required despite zero ring lengths/heads/tails:
  investigate exact I219 flush-status semantics before further reset work.
- E: DRV_LOAD/ownership is lost and not restored: correct only ownership
  lifecycle and retest.
- F: PCI/GIO bus-master state is disabled or drifts: correct only DMA-master
  lifecycle and retest.
- G: TDH advances but DD does not: descriptor writeback/status semantics
  are the next boundary.
- H: DD is in memory but guideXOS misses it: fix completion observation or
  barrier logic only.

Current physical classification: PENDING. No AIDA_LPT result is claimed.

## Phase 21 recommendation

Choose Phase 21 only after comparing the two fresh-boot results above. If the
result is A, E, F, G, or H, keep the next change narrowly scoped to that
boundary. If the result is C, a single descriptor-fetch/FIFO/format experiment
may be justified. If the result is D, resolve the physical 0xE4 semantics
first. Do not switch to advanced descriptors or change PBA/FIFO policy before
that evidence exists.
