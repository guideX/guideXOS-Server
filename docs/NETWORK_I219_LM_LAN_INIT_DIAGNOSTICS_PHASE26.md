# Intel I219-LM LAN_INIT_DONE Diagnostics — Phase 26 Audit

| Field | Value |
|---|---|
| Phase | PHASE26 |
| Branch | `NAVIGATOR_GENERAL_IMPROVEMENTS` |
| Starting HEAD | `8a9c20fcb75816eb196e8daa0f07321099199573` |
| Implementation commit | `abfeed747d94173b5f96f82061f0e8fce242734c` |

**PHASE26Q starting HEAD:** `f2561fa9bc1c1a37126b7ecc14fbda4cebe50007`

**Current status (PHASE26Q):** Phase 26 is accepted for its bounded,
read-only LAN_INIT_DONE diagnostic scope and successful physical initialization
path. The Phase 26R AIDA_LPT follow-up now has a captured POST-INIT sample and a
ready, registered driver. Physical TX functionality remains unresolved; no
TX probe or packet transmission occurred during this qualification. The
Phase 26Q closeout at the end of this audit supersedes the provisional pending
status retained in the historical Phase 26R record below.

## Phase authority and starting evidence

At the start of the original Phase 26 implementation audit, the repository,
branch, HEAD, and clean working tree matched that request. The Phase 25 audit
records `last_completed=PHASE25` and `next_expected=PHASE26`. Phase 25
diagnostic Outcome D is preserved: its GCR TX no-snoop value already matched
the SPT policy, so it did not make a TX attempt. No Phase 26 audit or accepted
physical result existed. This repository tracks phase authority in audit
documents; no `.phase` file was created or required.

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

At the time of the original Phase 26/Phase 26R assessment, physical
qualification was still pending. That provisional state is superseded by the
PHASE26Q closeout below. The separate PCIe-master/reset-quiesce candidate
remains a later, separately gated investigation only if TX evidence makes it a
stronger candidate. No LAN_INIT_DONE or PCIe-master behavior was changed.

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

### Phase 26R follow-up procedure (historical; completed in PHASE26Q)

The following was the pending capture procedure at the Phase 26R handoff. The
user has since supplied the physical observations recorded in the PHASE26Q
closeout below.

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

**Phase 26R provisioning investigation outcome C (historical):** the earlier
Phase 26 image's TX initialization failure was caused by the disabled build
configuration; that boot did not attempt allocation. At the Phase 26R handoff,
the enabled-gate physical check was prepared and acceptance was pending. This
provisional status is superseded by PHASE26Q below.

## PHASE26Q physical qualification closeout

### Supplied AIDA_LPT evidence

The following values are from the user's transcription of photographs of the
Phase 26R boot. The first two PRE values from this boot were not supplied and
are intentionally left unknown.

| Capture | STATUS | LAN_INIT_DONE | PHYRA | Link Up | CTRL | Reset bit |
|---|---:|---:|---:|---:|---:|---:|
| PRE sample 3 | `0x00080683` | 1 | 1 | 1 | `0x00100240` | 0 |
| POST-INIT / PRE-REGISTRATION sample 4 | `0x00080683` | 1 | 1 | 1 | `0x00100240` | 0 |

The diagnostic reports `STATUS-writes=none`. At POST-INIT the retained state
also reports reset done, PHY ready, PHY status `0x12C7`, and link UP. These
observations confirm LAN_INIT_DONE at both supplied checkpoints and confirm
that the diagnostic remained read-only with respect to STATUS.

The same boot reports TX ring READY, NIC Registered YES, Active YES,
initialization stage `ready`, hardware PASS, failure none, Driver Ready YES,
and link UP. It reports `TXDCTL1=0x0141001F` as valid, PCI command `0x0006`,
and no TX diagnostic failure. IRQ is registered and masked; DHCP is INIT with
no lease.

No physical TX experiment was performed. The evidence contains no submitted
packet descriptor, observed descriptor DD completion, proven device fetch, or
independent on-wire packet capture. TX ring readiness and successful driver
registration establish initialization/readback progress only; they do not
establish descriptor consumption or successful packet transmission.

### Phase 26 acceptance decision

Phase 26's defined goal was bounded, read-only physical observation of
LAN_INIT_DONE with a working I219 initialization path. That goal is met:

- a PRE observation and a POST-INIT / PRE-REGISTRATION observation both
  preserve raw STATUS/CTRL and decode LAN_INIT_DONE as set;
- the diagnostic reports no STATUS writes;
- initialization advances through TX ring setup to ready, hardware PASS,
  registration, active driver readiness, and link UP; and
- the earlier disabled-DMA gate no longer prevents the Phase 26R path from
  reaching POST-INIT and registration.

Missing PRE samples 1 and 2 do not block acceptance under the original
diagnostic criteria. Their absence prevents determining whether LAN_INIT_DONE
was already set at sample 1 or changed during samples 1–3; no value is inferred
from sample 3, sample 4, or any earlier boot. That timing distinction is not
required to accept the bounded observation and working initialization path.
No repeat boot is needed for Phase 26 acceptance.

**PHASE26Q outcome:** accepted for diagnostic scope. Physical TX remains
unresolved and is explicitly carried into Phase 27.

### Disabled-DMA configuration reconciliation

The earlier Phase 26 ISO builder,
[`create-phase26-aida-i219-laninit-diag-iso.ps1`](../scripts/create-phase26-aida-i219-laninit-diag-iso.ps1),
enabled LAN_INIT diagnostics but omitted `-I219TxDmaPlacementExperiment`.
Its manifest recorded `i219TxDmaPlacementExperiment=false`. The loader's
gated allocation path therefore did not run, the TX DMA handoff remained
empty, and the I219 path failed closed with `TX_DMA_REGION_UNAVAILABLE`
before ring setup. This was a build-configuration gate, not an allocation,
address-translation, or physical DMA failure.

The Phase 26R builder,
[`create-phase26r-aida-i219-tx-dma-provision-iso.ps1`](../scripts/create-phase26r-aida-i219-tx-dma-provision-iso.ps1),
passes both `-I219TxDmaPlacementExperiment` and
`-I219LanInitDiagnostics` to the build and ISO manifest generation. The new
physical READY/registered result shows that initialization passed its
software-side DMA handoff and ring-programming gates. The supplied
transcription does not contain the exact allocated address, memory-map
attributes, TDBA, or descriptor bytes, so those values are not asserted here.

Under the earlier Phase 26R provisioning taxonomy, Outcome C described the
prior disabled-gate boot. The Phase 26R boot qualified in PHASE26Q is outside
failure Outcomes A (allocation failure), B (handoff/validation failure), and C
(placement gate skipped); it completes provisioning and driver initialization
without testing TX consumption.

### Historical TX failure frontier

The Phase 23–25 audits and earlier I219 investigations were reviewed against
the Phase 26R setting. The old Phase 26 boot's `TX_DMA_REGION_UNAVAILABLE`
result is not evidence about physical descriptor fetch because placement was
disabled and no TX ring was set up. By contrast, the Phase 17/22/24 failure
observations made with the constrained-low placement experiment enabled remain
relevant historical evidence. Phase 26R has not superseded them because it
performed no packet submission.

| Evidence | What it establishes | What it does not establish |
|---|---|---|
| Phase 17: TDT advanced `0 -> 1`, TDH stayed at zero, DD stayed clear. | The tail doorbell was published and read back in that historical run. | TDT advancement alone does not prove descriptor fetch, completion, or wire emission. |
| Phase 22: link up, PCI command `0x0006`, PCH TARC0/TARC1/RFCTL readbacks, and TXDCTL policy were observed; the raw TX timed out with no descriptor completion. | PCI memory/bus-master enablement and the selected PCH setup were present in that run. | Those readbacks do not prove device DMA access; the timeout did not identify which side of fetch/completion failed. |
| Phase 24: constrained-low ring rearm completed; `TDLEN=0x400`, `TXDCTL=0x0141001F`, and pre-attempt `TDH=TDT=0`; one attempt ended without DD. DMAR/DRHD was absent. | A TX attempt reached the existing descriptor publication path with the DMA-placement experiment enabled; VT-d was not applicable on that boot. | The incomplete photographs do not prove exact TDBA equality, final TDH/TDT, descriptor fetch, or a frame on wire. |
| Phase 25: GCR TX no-snoop controls were already clear. | The no-snoop change was not applicable; its command correctly skipped the TX attempt. | No new same-boot descriptor-consumption result was produced. |
| Phase 26R (new): ring READY, registration/driver readiness PASS, TXDCTL1 valid, PCI command `0x0006`. | The software initialization, DMA handoff validation, ring programming/readback, and activation path completed. | No descriptor was submitted, so device consumption and packet emission remain unknown. |

These historical observations leave several discrete boundaries open:

- **Descriptor base and placement:** Phase 16's owned two-page low DMA handoff
  and Phase 23/24 address capture remain relevant. A valid allocation and
  below-4-GiB address do not prove that the device can access it. Compare the
  physical ring address with TDBAL/TDBAH and the descriptor buffer address on
  the same probe.
- **Head, tail, and DD:** TDT readback proves doorbell publication only.
  TDH movement is positive evidence of descriptor progress; DD is the
  driver's completion signal. Stationary TDH with clear DD is no observed
  progress, not standalone proof that the descriptor was never fetched.
- **Device DMA/IOMMU:** Phase 24 found no DMAR/DRHD on its physical boot, but
  that is boot-specific. A fresh Phase 27 read-only VT-d snapshot is needed;
  an absent fault is not proof of a usable device context, and an active
  translation with an unread context leaves the mapping unknown.
- **TX engine and PCH controls:** Phase 22's TARC0/TARC1/IOSFPC/RFCTL and
  TXDCTL policy readbacks remain useful historical context. Phase 26R adds
  valid TXDCTL1 and PCI-CMD `0x0006`, but no new per-packet engine state.
- **PCIe-master/reset quiescence:** the Phase 25 audit's Linux/guideXOS
  difference remains a plausible, unproven separate candidate. The new boot
  confirms successful reset, link, and initialization, but supplies no
  pre-reset GIO-master/outstanding-transaction evidence and no TX result.

The Linux references used by the existing audits were checked against the
upstream e1000e sources: device ID `0x156F` maps to the SPT I219-LM in
[`hw.h`](https://github.com/torvalds/linux/blob/master/drivers/net/ethernet/intel/e1000e/hw.h),
while LAN initialization/reset and TX setup are described in
[`ich8lan.c`](https://github.com/torvalds/linux/blob/master/drivers/net/ethernet/intel/e1000e/ich8lan.c)
and
[`netdev.c`](https://github.com/torvalds/linux/blob/master/drivers/net/ethernet/intel/e1000e/netdev.c).
Those sources inform the historical comparison; they do not prove guideXOS
physical DMA or TX behavior.

### Phase 27 single-probe recommendation

After Phase 26 acceptance, the smallest useful physical TX experiment is one
explicit invocation of the existing bounded `nicinfo tx iommu` diagnostic on a
fresh Phase 26R-or-later image. It already captures the ring/descriptor/buffer
addresses, DMA provenance, pre/post TX registers, raw descriptor bytes, TDT
readback, TDH/DD outcome, and available before/after VT-d fault state. Do not
use `nicinfo tx phase25` for this probe: the recorded GCR policy already
matches, so that command appropriately skips its TX attempt when no change is
applicable. This is a recommendation only; no implementation or TX attempt is
part of PHASE26Q.

**DMA and ring invariants required before the probe:**

- exact target `8086:156F` / `I219/PCH`; driver active and ready, link UP,
  initialization failure none, reset complete, and fresh resetless rearm
  reported PASS;
- firmware-owned two-page `EfiLoaderData` region with valid handoff flags,
  8 KiB size, 4 KiB alignment, physical contiguity, identity CPU mapping,
  cacheable mapping contract, no overlap, and full range below 4 GiB;
- ring at region base with 64 legacy 16-byte descriptors (`TDLEN=0x400`),
  required ring alignment, descriptor slot 0 located at ring PA, and its
  buffer-address field equal to the validated packet-buffer PA at the second
  page offset;
- same-boot TDBAL/TDBAH reconstruct exactly to ring PA; TDLEN, TDH, TDT,
  TCTL enable, TXDCTL0/1 policy, TIPG, TARC0/1, IOSFPC, and PCI command read
  back valid immediately before submission (`PCI-CMD` memory and bus-master
  bits set); and
- unpoisoned ring, no concurrent network traffic, no DHCP discovery or other
  sender running, stable link, and a confirmed full-duplex link for a single
  non-colliding physical emission.

Capture `nicinfo laninit`, `nicinfo brief`, `nicinfo link`, `nicinfo tx brief`,
`nicinfo dma brief`, and `nicinfo tx reset brief` first. Run the existing
`nicinfo tx rearm` once only if needed to satisfy the diagnostic's fresh-rearm
gate; it is resetless and does not publish a packet. Abort on any failed
handoff, invalid or unreadable register, mismatch, poison state, reset/rearm
failure, link loss, competing traffic, or incomplete safety snapshot. If a
matching VT-d unit is present with translation active and the I219 context's
ring/buffer mapping cannot be independently established, do not publish TDT;
`context-entry=unknown` and “no new fault” are not mapping proof. No PCI
command, VT-d, STATUS, TARC, TCTL, TXDCTL, or reset-sequence experiment is
bundled into this probe.

Once all gates pass, an operator would explicitly run `nicinfo tx iommu` once.
The existing submission path publishes at most one descriptor and one
60-byte, protocol-independent Ethernet-II fixture (`ff:ff:ff:ff:ff:ff`
destination, experimental EtherType `0x88B5`, payload marker
`GXOS-I219-P17`; hardware appends FCS). It must perform one TDT write only,
then poll DD up to the existing `1,000,000`-iteration completion limit.
That limit is an iteration bound, not a measured wall-clock duration. No retry,
second descriptor, DHCP, ARP, or protocol request is allowed on that boot.

Preserve exact pre-TDT and post-attempt `ring-PA`, descriptor PA and bytes,
buffer PA and bytes, TDBAL/H, TDLEN, TDH, TDT (including immediate readback),
TXDCTL0/1, TCTL, TARC0/1, IOSFPC, PCI-CMD, STATUS/CTRL, IMS, descriptor DD,
poll count, poison/failure state, and any available VT-d before/after
translation/fault records. ICR is read-to-clear: do not read it in the
pre-snapshot; on timeout, use the existing single capture at the timeout
boundary. If an attempted probe fails or times out, preserve the poison state,
do not manually reset head/tail or clear the descriptor, and do not retry; save
the complete output and shut down. Reboot before any subsequent TX
investigation. If the probe succeeds, preserve output and stop all further TX
for that boot as well.

Do not attempt in-place restoration after descriptor publication because
hardware ownership may be ambiguous. The cleanup action is to preserve the
output, shut down, and cold boot before another TX investigation; the next
initialization re-establishes the baseline ring and device state.

Where available, verify physical emission using a passive Ethernet tap or
isolated managed-switch mirror and record the exact source MAC, broadcast
destination, EtherType, payload marker, frame count, and timestamp. Because the
fixture is broadcast, use an isolated test segment with no production bridge.
A device DD/software success proves completion as reported by the device; only
an independent capture proves the frame appeared on the wire.

| Observation | Phase 27 interpretation |
|---|---|
| TDT readback advances, TDH remains fixed, DD clear, descriptor unchanged, no captured frame. | No descriptor progress was observed (A-consistent); this does **not** prove the device never fetched it. A silent fetch/stall and a never-fetched descriptor cannot be separated by these software snapshots alone. A PCIe/DMA analyzer or independent device-side fetch trace would be needed for definitive A-versus-B separation. |
| TDH advances or descriptor memory shows device-side change, but DD stays clear and no frame is captured. | Positive evidence of descriptor progress/fetch with no observed completion (B-side boundary); investigate completion/writeback and link-layer transmit state. |
| The diagnostic reports timeout/failure, but a passive tap sees the exact fixture or independent device completion status is set. | Completion occurred outside the software's observed completion path (C); focus on DD visibility, polling, memory coherency, and status handling. |
| Driver observes DD and returns success, and the passive tap captures the exact fixture once. | TX completion and physical emission are confirmed (D); DHCP/no-lease then belongs to a higher networking layer. |

If descriptor/head/DD remain unchanged with no external frame, report the result
as “no progress observed” and leave A versus an unobservable B unresolved.
Likewise, DD without an independent capture is descriptor completion, not
proof of wire emission. These distinctions prevent a READY ring or a TDT
readback from being promoted into a TX-success claim.

### PCIe-master/reset-quiescence follow-up

The PCIe-master/reset-quiesce difference merits a separate, read-only
pre-reset capture if the Phase 27 probe shows valid DMA/ring/engine state but
no observed descriptor progress and no explanatory VT-d fault. The existing
Linux SPT path's `CTRL.GIO_MASTER_DISABLE` request and bounded
`STATUS.GIO_MASTER_ENABLE` poll are not represented by guideXOS's current
pre-reset sequence. A follow-up should first capture those exact states and
then, only in a separately reviewed fresh diagnostic boot, test documented
quiesce semantics with a bounded poll and explicit failure stop. Do not change
reset sequencing in Phase 26Q: current evidence neither proves outstanding
transactions nor connects quiescence to TX fetch, and reset/DMA quiescence has
greater operational impact than the proposed one-descriptor observation.

**Final audit-based phase state:**

```text
last_completed=PHASE26
next_expected=PHASE27
```

Physical TX functionality remains unresolved.

### Future freestanding-kernel implementation constraints

If Phase 27 later requires kernel/runtime code, preserve the freestanding
guardrails: add no `std::` usage, STL containers, exceptions, RTTI, unsupported
standard-library dependencies, or unsupported dynamic allocation. Prefer
bounded kernel-native structures, explicit ownership, and deterministic
failure handling. Any such implementation requires kernel-target compilation
and focused regression tests. These constraints do not automatically apply to
hosted development tools or tests.
