# Intel I219-LM LAN_INIT_DONE Diagnostics — Phase 26 Audit

**Phase:** PHASE26  
**Branch:** `NAVIGATOR_GENERAL_IMPROVEMENTS`  
**Starting HEAD:** `8a9c20fcb75816eb196e8daa0f07321099199573`  
**Implementation commit:** `abfeed747d94173b5f96f82061f0e8fce242734c`  
**Status:** implementation and ISO gates passed; physical qualification pending; phase remains open.

## Phase authority and starting evidence

At start, the repository, branch, HEAD, and clean working tree matched the
Phase 26 request. The Phase 25 audit records `last_completed=PHASE25` and
`next_expected=PHASE26`. Phase 25 diagnostic Outcome D is preserved: its GCR
TX no-snoop value already matched the SPT policy, so it did not make a TX
attempt. No Phase 26 audit or accepted physical result existed. This repository
tracks phase authority in audit documents; no `.phase` file was created or
required.

Phase 25 left the I219-LM TX failure physically unresolved. Its evidence did
not establish that missing LAN_INIT_DONE sequencing caused the TX-engine
failure. Existing physical observations included reset completion and link
up, which weigh against a broad PHY bring-up failure, but the LAN_INIT_DONE
bit had not been separately captured. PCIe-master/reset quiescence remained a
separate, higher-risk candidate.

## Linux reference and scope

Linux defines `E1000_STATUS_LAN_INIT_DONE` as `0x00000200` in
[`e1000e/defines.h`](https://github.com/torvalds/linux/blob/master/drivers/net/ethernet/intel/e1000e/defines.h).
Its ICH8LAN helper polls STATUS for this indication before continuing PHY
configuration, then reads STATUS and writes it back with LAN_INIT_DONE
cleared in
[`e1000e/ich8lan.c`](https://github.com/torvalds/linux/blob/master/drivers/net/ethernet/intel/e1000e/ich8lan.c).
The reference concerns completion of basic PHY configuration; it does not by
itself establish a direct dependency on TX descriptor fetch.

Phase 26 deliberately observes the same bit without implementing Linux's
wait or STATUS write. It therefore captures what the existing guideXOS path
sees at its reset-to-PHY boundary, but does not test Linux's full sequencing,
write-to-clear semantics, or a behavior-changing remedy. A captured
correlation cannot establish TX causation.

## Existing initialization traced

1. The UEFI loader scans PCI and recognizes Intel `8086:156F`, selects a
   usable memory BAR without BAR-sizing writes, and hands its mapped MMIO
   window to the kernel.
2. The kernel validates the BAR and MMIO window, reads the PCI command
   register, enables memory space and bus mastering only through the existing
   path when needed, and retains the read-back command value.
3. The selected I219 PCH path masks/drains interrupts, disables RCTL/TCTL,
   flushes STATUS, waits the existing 10 ms pre-reset delay, writes CTRL.RST,
   waits the existing 20 ms post-reset delay, then bounded-polls CTRL.RST
   completion.
4. **New checkpoint:** after reset completion is recorded and before the
   existing reset-boundary refresh, ownership handling, MAC/PHY setup, or MDIC
   PHY discovery, the diagnostic image reads STATUS and CTRL three times.
   The fixed maximum is three samples, with a nominal 1 ms delay between
   samples (at most two delays). There is no wait-for-bit loop. The first
   sample is immediate after the existing reset-completion poll.
5. Existing reset-boundary handling and hardware-control steps continue;
   guideXOS reads the MAC address, prepares PHY access, discovers/reads PHY
   state through MDIC, and establishes link using its existing bounded path.
6. Existing RX/TX rings and PCH TX settings are configured and the bounded
   link refresh runs. **New second observation:** one STATUS/CTRL read is
   retained after this existing initialization sequence and before NIC
   registration, along with already-cached reset, PHY-ready, PHY-status, and
   link state.
7. Normal registration and startup proceed. The diagnostics do not alter
   PCIe-master behavior, reset, PHY programming, TX/RX descriptor ownership,
   ring indices, or TX tail advancement. No TX packet attempt is added.

The diagnostic path reuses the driver's existing MMIO accessors, PCI identity
and command state, family selection, cached PHY/link state, and shell output.
It is compile-time opt-in. With diagnostics disabled, capture call sites are
compiled out and the helper invokes no MMIO reader or delay callback. With
diagnostics enabled, the two nominal 1 ms inter-sample delays add at most
2 ms before PHY setup; this bounded timing perturbation is part of interpreting
the physical observation. The captured interval may allow device initialization
to progress sooner than an uninstrumented run.

Each retained sample has a sequence number, raw STATUS and CTRL values, and
validity flags (`0xFFFFFFFF` is treated as an invalid read). Console output
decodes LAN_INIT_DONE, PHYRA, link-up, and CTRL.RST while preserving raw
values. The command also reports PCI ID/family and cached pre-reset PCI command
bits. `nicinfo laninit` only prints retained data; it does not access hardware.

## Changes and validation

The implementation commit adds the fixed-size capture record/helper, opt-in
build configuration, initialization checkpoints, `nicinfo laninit`, hosted
simulated-MMIO tests, and an ISO build script. The tests cover LAN_INIT_DONE
present/absent/changing, sequence progression, all-ones invalid reads, the
three-sample clamp and delay count, no STATUS writes, disabled-build zero
accesses, and structural guards for checkpoint order and TX/RX descriptor
operations.

| Gate | Result |
|---|---|
| Phase 11–25 NIC/network regression chain | PASS |
| Phase 26 enabled/disabled simulated-MMIO tests and source guards | PASS |
| AMD64 kernel build | PASS; compiler warnings included unused-parameter/function and signedness/narrowing warnings |
| UEFI bootloader build | PASS; MSVC option-override warnings |
| ISO structural verification | PASS; UEFI El Torito platform `0xEF`, bootable no-emulation media |
| QEMU ISO boot/readiness smoke | PASS; firmware, loader, kernel, ramdisk, desktop-ready, and kernel-main-loop markers observed |
| AIDA_LPT physical qualification | PENDING; no physical Phase 26 capture was available for this audit |

The QEMU run used QEMU's emulated generic `e1000` device. It is virtual boot
evidence only and is not an Intel I219-LM observation.

## Physical-test ISO

- **ISO:** `D:\dev\guideXOSServer_NAVIGATOR_IMPROVEMENTS\dist\guideXOS-Server-v0.1.0-navgen-phase26-laninit-diag-amd64.iso`
- **Size:** 91,293,696 bytes
- **SHA-256:** `66d8134f76ce9ebc5b5dc56991f5b2d2351228235132df59222432bcf6951aef`
- **Source commit:** `abfeed747d94173b5f96f82061f0e8fce242734c`
- **Configuration:** AMD64 UEFI; I219 Phase 5 stage 8; Phase 6 stage 0; Phase 7 stage 4 (`register`); LAN_INIT diagnostics enabled; TX DMA-placement experiment disabled; tracked PacMan NativeElf package reused.
- **Kernel SHA-256:** `d6431ae1e98fe8da365a72d2798a853af89840bb5a1dff9774de394a4e4163ba`
- **Physical output access:** after boot, use the built-in command `nicinfo laninit` to print the retained initialization samples.

The release manifest records the exact source commit and all diagnostic build
settings. It also records a dirty packaging-start worktree because the
canonical build generated the tracked `ESP/build-identity.txt` file; that file
contains the ISO's Phase 26 configuration and the repository copy is restored
to its pre-build contents after packaging. The kernel and bootloader sources
packaged in the ISO correspond to the implementation commit listed above.

## AIDA_LPT physical procedure

Use the existing known test setup. Do not change BIOS settings, cabling, or
reset behavior for this observation.

1. Boot the Phase 26 ISO on AIDA_LPT and wait for the normal shell.
2. Run `nicinfo laninit` and first verify it reports `PCI ID=8086:156F`
   and `family=I219/PCH`. If it reports another ID/family or says capture is
   disabled, record that and do not interpret the samples as I219 evidence.
3. Save or photograph the complete `nicinfo laninit` output. Record each
   `PRE` raw STATUS value, LAN_INIT_DONE (`LD`), PHYRA, LU, CTRL/RST, and
   sequence number; record the `POST` sample, PHY status, and link state too.
4. Run `nicinfo brief`, `nicinfo link`, and `nicinfo tx brief` to preserve the
   existing link and TX-engine context. These commands do not submit a TX
   packet. Do not run `nicinfo tx phase25`, `nicinfo tx raw`, rearm/reset
   commands, or any packet-send command during this capture.
5. Save or photograph those outputs with the ISO build identity, date, and
   note whether link was up. Then enter `shutdown` and wait for the machine to
   power off.

## Interpretation and next experiment

- **A — LAN_INIT_DONE set in the first post-reset sample:** a missing wait may
  be irrelevant at this observed boundary. This weakens the current hypothesis
  but does not eliminate every possible timing scenario.
- **B — LAN_INIT_DONE absent at the checkpoint:** this supports further
  sequencing/timing investigation, but does not prove that it caused TX
  failure.
- **C — LAN_INIT_DONE changes during the three bounded samples:** the raw
  sequence shows a transition window. Existing guideXOS PHY access begins only
  after the final sample, but the diagnostic delays themselves add up to
  2 ms; do not infer the uninstrumented counterfactual or TX causation from
  this alone.
- **D — LAN_INIT_DONE never appears in pre- or post-init observations:**
  preserve that result and investigate the sample boundary, hardware-family
  applicability, and other prerequisites before proposing a wait.

The separate PCIe-master/reset-quiesce candidate remains appropriate for a
later, separately gated phase only if physical evidence makes it the stronger
candidate. Phase 26 implements no behavioral repair. The phase remains open
until its physical qualification criteria are met; do not advance to Phase 27.

**Phase state:** `last_completed=PHASE25`; `next_expected=PHASE26`.
