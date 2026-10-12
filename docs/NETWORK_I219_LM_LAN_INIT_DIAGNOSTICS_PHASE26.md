# Intel I219-LM LAN_INIT_DONE Diagnostics — Phase 26 Audit

| Field | Value |
|---|---|
| Phase | PHASE26 |
| Branch | `NAVIGATOR_GENERAL_IMPROVEMENTS` |
| Starting HEAD | `8a9c20fcb75816eb196e8daa0f07321099199573` |
| Implementation commit | `abfeed747d94173b5f96f82061f0e8fce242734c` |
**Status:** Phase 26 implementation and ISO gates passed. The supplied AIDA_LPT
capture records LAN_INIT_DONE set in visible PRE samples 2 and 3, but the Phase
26 image had TX DMA placement disabled and failed closed before TX ring setup.
Phase 26R prepares the gated provisioning path for a fresh physical check; the
phase remains open.

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
| Phase 11–26 hosted NIC/network regression chain, including new Phase 26R provisioning checks | PASS |
| Phase 26 enabled/disabled simulated-MMIO tests and source guards | PASS |
| AMD64 kernel build (Phase 26R) | PASS; existing unused-parameter/function and signedness/narrowing warnings |
| UEFI bootloader build (Phase 26R) | PASS; existing MSVC option-override warnings |
| Phase 26R ISO structural verification | PASS; UEFI El Torito platform `0xEF`, bootable no-emulation media |
| Phase 26R QEMU ISO boot/readiness smoke | PASS; firmware, loader, kernel, ramdisk, desktop-ready, and kernel-main-loop markers observed |
| AIDA_LPT physical qualification | PARTIAL; bounded PRE evidence supplied, but TX DMA/ring/registration acceptance failed at the disabled configuration gate |

The Phase 26R hosted additions exercise valid and empty handoffs, failed
translation and identity checks, invalid range/alignment/ownership/contiguity,
TX ring programming order, readiness gating, and the diagnostics-disabled
production build. Source guards also verify that allocation failure clears the
handoff and that the Phase 26R ISO enables the existing experiment in both the
kernel and loader build settings.

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
candidate. The Phase 26 evidence and Phase 26R source trace are reconciled
below. No LAN_INIT_DONE or PCIe-master behavior was changed. The phase remains
open until its physical qualification criteria are met; do not advance to
Phase 27.

## Phase 26R physical evidence and failure trace

### Supplied AIDA_LPT observation

The user-provided visible PRE samples 2 and 3 both report `STATUS=0x00080683`,
`LAN_INIT_DONE=1`, `PHYRA=1`, `LU=1`, `CTRL=0x00100240`, and `RST=0`. The
diagnostic reports three bounded PRE samples, no STATUS writes, and no
POST-INIT capture. The first PRE sample value was not supplied and is not
reconstructed here.

The same capture reports physical link UP and valid PHY/MAC link observations,
but TX ring NOT READY, NIC Registered NO, Driver Ready NO, initialization
stage TX ring, hardware initialization incomplete, and
`TX_DMA_REGION_UNAVAILABLE`. Therefore the visible samples weaken the missing
LAN_INIT_DONE hypothesis at samples 2 and 3, but do not prove that timing never
matters. They are not enough to assign the audit's “first sample set” or
“changed during samples” interpretations because PRE sample 1 is unavailable.

### Exact failure and why POST-INIT was absent

This is a Phase 26 build-configuration failure, not evidence of a failed UEFI
allocation or failed address translation:

1. The Phase 26 ISO build script did not pass `-I219TxDmaPlacementExperiment`.
   Its manifest confirms `i219TxDmaPlacementExperiment=false`.
2. With that compile-time flag off, the UEFI loader's guarded
   `AllocatePages(AllocateMaxAddress, EfiLoaderData, ...)` block is skipped.
   It leaves `BootInfo.TxDmaRegion` zero; no allocation is attempted.
3. `kernel/core/main.cpp` passes that empty descriptor to
   `nic::set_tx_dma_region()`. Its zero-base/size/flags check returns while
   retaining `TxFailureReason::DmaRegionUnavailable`.
4. With the placement flag off, `select_tx_dma_storage()` keeps kernel-image
   TX storage selected. `init_tx()` explicitly rejects that mode for I219
   before `validate_tx_dma_layout()`, `dma_address_range()`, address
   translation, or TDBAL/TDBAH programming. This is the precise early return
   that leaves the recorded failure as `TX_DMA_REGION_UNAVAILABLE`.
5. `init_e1000()` returns from its TX-ring failure branch. The POST-INIT
   capture is later in the success path, after TX setup and link refresh, so
   that checkpoint is unreachable. `init_from_bootinfo()` then returns before
   the hardware-completion gate and NIC registration.

Thus the observed failure is **configuration/gating**. No allocation failed;
no runtime translation, DMA address, physical placement, alignment, or
contiguity check was reached. The I219 fallback is intentionally fail-closed;
the kernel-image storage is not substituted as an unqualified DMA region.
No kernel behavior, LAN_INIT_DONE handling, STATUS write, or PCIe-master
sequence was changed.

The latest prior documented AIDA_LPT ring-ready evidence is the Phase 24
physical record carried in the Phase 25 audit: reset/rearm completed and the
TX snapshot showed `TDLEN=0x400`, `TXDCTL=0x0141001F`, and `TDH=TDT=0`. The
Phase 24 build script explicitly enabled the same placement experiment. That
comparison is evidence that the Phase 26 setting differs; it does not establish
successful TX descriptor consumption or transmission.

### Phase 26R image and gates

The new Phase 26R builder enables both the existing compile-time-gated DMA
placement experiment and the read-only LAN_INIT diagnostics for the exact
I219 physical test. The UEFI allocator remains firmware-selected and bounded
below 4 GiB; all ownership, identity mapping, contiguity, cacheability,
alignment, range, and non-overlap checks remain in force. RX, PHY, PCI, and
link initialization are unchanged. No packet transmission is added.

- Builder: `scripts/create-phase26r-aida-i219-tx-dma-provision-iso.ps1`
- ISO: `dist/guideXOS-Server-v0.1.0-navgen-phase26r-tx-dma-provision-amd64.iso`
- Size: `91,293,696` bytes
- SHA-256: `6b0fbbf226aee1377ee75fd05b677c31bd2f746e79d8677aca24d3e7a6ada4d2`
- Source commit used for kernel/bootloader: `607f3e3e3dcb379dea79d393eb3a615c99156bf0`
- Manifest build identity: `GXOS-P7-4-607f3e3e3dcb-991b20a0-75f1-47cc-bce5-d3bcb4a03db9`
- ESP identity: `GXOS-P7-4-64f60a6efce14397b9a409232fe109c4`
- Configuration: AMD64 UEFI; Phase 5 stage 8, Phase 6 stage 0, Phase 7 stage 4; TX DMA placement experiment enabled; LAN_INIT diagnostics enabled; tracked PacMan package reused.
- Kernel SHA-256: `cabdbb7e1cb2cf8dece90054cf1b30f3570eb4540f953bcb186380a331327dbc`
- UEFI bootloader SHA-256: `069381362193e77e2249b78611ef022386ed23c19e88566b828a125fcac8fa86`
- QEMU: firmware boot, loader, kernel, ramdisk, desktop-ready, and kernel-main-loop markers passed. Serial log: `out/release-iso/qemu-test-a35d2f4e6fdb4bbc9d9df85da3c89ea4/serial.log`.

The manifest records that packaging began with the test/script changes and
generated `ESP/build-identity.txt` present in the worktree. The packaged
kernel and bootloader hashes above identify the tested image; QEMU used a
generic emulated `e1000` and is not I219 qualification.

For this provisioning investigation, classify the observed boundary as:

- **Outcome A — allocation failure:** the experiment is enabled but UEFI does
  not return a valid region. Not observed in the supplied Phase 26 capture,
  because the allocator was not called.
- **Outcome B — handoff/validation failure:** the loader allocates a region but
  the kernel rejects ownership, mapping, range, or layout. Not observed in the
  supplied capture; Phase 26 never reached those checks.
- **Outcome C — configuration gate skipped provisioning:** the experiment is
  disabled, no allocation is attempted, and I219 TX initialization fails
  closed with `TX_DMA_REGION_UNAVAILABLE`. This is the supplied observation.

### Next AIDA_LPT check

Boot the Phase 26R ISO and capture the complete `nicinfo laninit`,
`nicinfo brief`, `nicinfo link`, and `nicinfo tx brief` output. Verify PCI
`8086:156F` and `family=I219/PCH`; record the retained PRE samples, whether
POST-INIT is now present, the TX DMA mode/provisioning fields, TX ring READY,
NIC registration, Driver Ready, and link state. Photograph the displayed
build identity and output.

These commands are read-only. Do not run reset/rearm, `nicinfo tx phase25`,
`nicinfo tx raw`, DHCP, or any packet-send command during this provisioning
check. If allocation or validation fails, record the precise reported state
and stop. If the handoff is valid, the TX ring is READY, registration succeeds,
and POST-INIT is captured, preserve that evidence first; it will establish a
safe basis for selecting a separate one-shot TX diagnostic. Ring readiness
alone does not justify a transmission in this run.

**Phase 26R investigation outcome C:** the visible Phase 26 TX failure is
explained by a disabled build configuration, and a reproducible enabled-gate
physical check is prepared. **Physical acceptance: pending**; the supplied
boot did not initialize/register the TX ring, and no successful descriptor
consumption or transmission is claimed.

**Phase state:** `last_completed=PHASE25`; `next_expected=PHASE26`.
