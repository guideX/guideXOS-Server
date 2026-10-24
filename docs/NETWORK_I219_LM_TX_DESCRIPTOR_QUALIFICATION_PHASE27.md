# Intel I219-LM TX Descriptor-Consumption Qualification — Phase 27

**Status:** bounded probe implementation and hosted qualification are complete.
A Phase 27 command sequence was run on AIDA_LPT, but no packet transmission
was recorded. The Phase 27 ISO and QEMU readiness qualification are recorded
in the final artifact addendum below. Physical descriptor consumption and wire
emission remain unverified.

**Current PHASE27R2 disposition:** Outcome C — AIDA_LPT DMA translation
remains unverifiable from the supplied evidence. No transmission is authorized;
the phase remains open at `last_completed=PHASE26`, `next_expected=PHASE27`.

## Repository gate and phase authority

- Required branch: `NAVIGATOR_GENERAL_IMPROVEMENTS`.
- Starting HEAD: `2b1d0fd14d323d200aee5b217e4caf952e61ea10`.
- Starting worktree: clean; the branch was one local commit ahead of its remote.
- Phase 23, 24, 25 and accepted Phase 26/26Q records were reviewed.
- The accepted Phase 26Q closeout records `last_completed=PHASE26` and
  `next_expected=PHASE27`.
- No prior Phase 27 implementation or report existed. No `.phase` file was
  introduced; phase authority remains audit-document based.

## Accepted Phase 26 baseline

Phase 26R established the working physical initialization path on AIDA_LPT:
Intel `8086:156F` I219-LM, constrained-low DMA placement enabled, TX ring READY,
NIC registered and active, Driver Ready YES, hardware initialization PASS,
PHY/MAC link UP, PCI command `0x0006`, valid TXDCTL1 `0x0141001F`, and
LAN_INIT_DONE set in the visible PRE and POST samples. No Phase 26 STATUS write
or physical packet transmission qualification occurred. The source of the old
`TX_DMA_REGION_UNAVAILABLE` state was a disabled configuration gate, not a
demonstrated allocation failure.

This baseline establishes initialization and ring provisioning only. It does
not prove that the I219 fetched a descriptor, set DD, emitted a frame, or
delivered a packet to a peer.

## Hypothesis and result vocabulary

The experiment asks whether one valid descriptor published to the initialized
I219 TX ring is consumed and completed. It does not change DMA placement, PHY
handling, STATUS, or reset sequencing.

- **A — no demonstrated consumption:** TDH remains unchanged, DD is clear after
  the bounded observation, and no independent evidence establishes a fetch.
  This does not prove the hardware never fetched the descriptor.
- **B — progress without completion:** TDH advances or descriptor memory shows
  device-side progress, but DD is not established.
- **C — completion-observation discrepancy:** DD is visible in the final
  descriptor read but software did not safely retire the descriptor.
- **D — descriptor completion successful:** software observed a valid DD and
  safely retired the descriptor. This does not prove wire emission.
- **PREREQUISITE_NOT_MET:** the command refused before submission and names the
  failed safety gate.
- **INCONCLUSIVE:** required observations are missing or invalid.

The independent claims remain separate: descriptor submitted, descriptor
consumed, descriptor marked complete, frame seen on an external tap, and packet
received by a peer.

## Existing `nicinfo tx iommu` path and safety changes

The prior command was explicitly user-invoked, built the established 60-byte
Phase 17 fixture, called the normal TX boundary once, wrote TDT at most once,
used the existing one-million-iteration poll limit, and did not retry or reset
after a timeout. It required the constrained-low TX DMA mode and retained the
ring poison state after an ambiguous timeout.

Before Phase 27 it did not require current driver readiness, live link-up,
current TDH/TDT ownership, an available slot at the current software index, or
zero previous TX activity. It also allowed transmission when active VT-d
translation had an unread I219 context, and it had no one-attempt-per-boot
latch. Those were safety and evidence gaps in the demonstrated path.

The existing command is now split into two modes:

- `nicinfo tx iommu` performs read-only preflight and never publishes a packet.
- `nicinfo tx iommu isolated` performs the same gates and may submit one
  descriptor only after the operator confirms an isolated test segment with no
  competing sender.

The diagnostic-only latch is consumed immediately before entering the existing
normal raw-TX boundary. It prevents a second Phase 27 probe during that boot,
including after a late pre-publication failure. Other TX/RX implementation
paths were not changed.

## Pre-transmission gates

The probe refuses unless all inspectable conditions pass:

- exact PCI identity `8086:156F` and `I219/PCH` family selection;
- `is_driver_ready`, completed hardware initialization, completed reset/rearm,
  no reset timeout, and no stronger-recovery requirement;
- cached link UP and live MAC STATUS link-up bit;
- Phase 16 constrained-low DMA handoff with valid/owned/contiguous/identity/
  below-4-GiB/cacheable flags, `EfiLoaderData` ownership, expected two-page
  geometry, and no overlapping DMA objects;
- fresh ring and selected-slot DMA translations, exact ring/descriptor/buffer
  addresses, ring alignment and length, and valid packet-buffer range;
- a valid read-only TX-register snapshot, PCI memory and bus-master enablement,
  TDBA/TDLEN match, enabled TX engine, valid TXDCTL0/TXDCTL1 policy, and TIPG;
- TDH and TDT both equal the software ring index and the selected descriptor has
  DD set before reuse;
- no prior software TX attempt/publication/completion during this boot, no
  poisoned ring, and no prior Phase 27 attempt;
- proven non-remapped DMA behavior: a readable matching DRHD with translation
  disabled. An absent or malformed DMAR table, unknown DRHD coverage, an
  unreadable/invalid register snapshot, or active translation refuses
  transmission because this kernel cannot inspect the I219 context entry or
  prove its IOVA mapping. DMAR absence is diagnostic evidence, not proof that
  translation is disabled or that DMA addresses are identity mapped.

The typed `isolated` token records the operator confirmation for the external
network condition. Software counters can establish that guideXOS has not
previously submitted TX on this boot; they cannot detect another machine on the
cable. The user must use an isolated test segment and stop other local senders.
No failed prerequisite is repaired automatically.

## Baseline, one-shot attempt, and timeout behavior

The read-only baseline is captured before the probe changes the shared payload
buffer or descriptor. Output includes TDBAL/H, TDLEN, TDH, TDT, TCTL, TXDCTL0/1,
PCI command, STATUS, link state, current descriptor bytes and status, ring and
buffer CPU/DMA addresses, preflight validation results, DMA handoff provenance,
EFI memory attributes, and existing TX counters.

After passing the gates, the established descriptor publisher prepares the one
60-byte fixture (`ff:ff:ff:ff:ff:ff`, EtherType `0x88B5`, marker
`GXOS-I219-P17`), publishes one descriptor, performs one TDT write, and polls
DD up to `1,000,000` iterations. The poll limit is finite but is not a measured
wall-clock duration. The existing `sfence` publication barrier and completion
barrier remain in use. The path adds no cache flush, reset, PHY setup, STATUS
write, or PCI command change.

The command captures pre-TDT, immediate TDT readback, post-doorbell registers,
final TDH/TDT, final descriptor bytes/DD, existing TX counters, and available
timeout error indicators. ICR is only read by the existing timeout-boundary
capture, not by the pre-transmission snapshot. A timeout does not retry, force
DD, free or reuse potentially device-owned DMA memory, or reset the NIC. The
existing poison state preserves ambiguous ownership and requires a cold boot
before another TX investigation.

## Hosted qualification

`scripts/run-network-phase27-tests.ps1` runs the existing Phase 11–26R network
regression chain, compiles and runs `tests/network_tx_phase27_test.cpp`, and
checks the Phase 27 source gates. Hosted cases cover A/B/C/D and inconclusive
classification; unsupported identity; family/driver/init/link failures; DMA
region, ring, buffer and IOMMU gates; ring ownership; unavailable descriptor;
prior TX, recovery and missing isolation confirmation; and the one-shot
preflight parser. Static checks enforce one call to the existing TX boundary,
the existing finite poll bound, no retry, no reset/rearm, no STATUS write, no
resource release, pre-publication DMA validation, and an unchanged production
`send_frame` wrapper.

**Result:** Phase 11–26R regression chain PASS; Phase 27 hosted decision tests
PASS. These are hosted logic and source-contract results, not physical I219
evidence.

The AMD64 kernel target compiled successfully with TX DMA placement and
LAN_INIT diagnostics enabled. The UEFI loader compiled successfully with
Visual Studio. Existing unrelated compiler warnings remain in other kernel
areas; the Phase 27 code introduced no reported warnings. No STL, `std::`,
exceptions, RTTI, dynamic allocation, unbounded polling, or unbounded diagnostic
storage was added.

## ISO qualification

The Phase 27 diagnostic image is built by
`scripts/create-phase27-aida-i219-tx-consumption-iso.ps1`. It keeps the accepted
Phase 26R build configuration enabled: AMD64 UEFI, Phase 5 stage 8, Phase 6
stage 0, Phase 7 stage 4, `-I219TxDmaPlacementExperiment`,
`-I219LanInitDiagnostics`, and the existing PacMan package reused. The ISO
manifest identifies the implementation source commit. Structural verification
and the isolated QEMU boot/readiness result are recorded in the artifact
addendum.

QEMU's generic e1000 boot is a readiness and packaging check only. It is not
evidence of physical I219 descriptor consumption.

## Original AIDA_LPT handoff (superseded by PHASE27R)

The read-only preflight and isolated command were executed on AIDA_LPT as
recorded below. Do not repeat the isolated command using the original Phase 27
ISO. Its no-DMAR handling is superseded by the PHASE27R safety correction.
After producing an image with the corrected source, the next useful physical
check is read-only `nicinfo tx iommu` output to confirm the unknown-mapping
gate. No physical transmission is authorized by this audit update.

The historical Phase 27 procedure was to boot the ISO named in the artifact
addendum on AIDA_LPT with the Ethernet link up, use a passive tap or isolated
managed-switch mirror if available, and capture the broadcast fixture with
EtherType `0x88B5` and marker `GXOS-I219-P17`.

Run these read-only commands first and photograph the complete output:

```text
nicinfo laninit
nicinfo brief
nicinfo link
nicinfo tx brief
nicinfo dma brief
nicinfo tx reset brief
nicinfo tx iommu
```

Abort if any safety gate is not met, if another TX attempt has occurred, if the
ring is poisoned/not idle, if the link is down, or if active/unknown IOMMU
translation is reported. Do not run a reset, rearm, raw TX, DHCP, or another
network sender to repair a failed gate. The historical procedure then allowed
a single isolated probe after preflight passed. That step is suspended pending
qualification of the corrected unknown-IOMMU gate and an independently
justified DMA-translation safety model:

```text
nicinfo tx iommu isolated
```

Photograph the complete command output, including `outcome`, before/after
TDH/TDT, descriptor status, poll count, DMA addresses, gate/counter lines and
VT-d state. On timeout, preserve the output and shut down; do not retry, free
DMA memory, or reset on that boot. After either completion or timeout, stop
network activity and enter `shutdown` in the shell (or use the OS shutdown
control). Use a cold boot before further TX investigation. Report wire
transmission as unverified unless a passive external capture sees the exact
fixture; descriptor completion alone does not prove DHCP/TCP/HTTPS
functionality.

## PHASE27R operator evidence and command-path reconciliation

On 2026-10-20 the operator confirmed running the complete Phase 27 command
sequence, including both `nicinfo tx iommu` and
`nicinfo tx iommu isolated`. The supplied observation summary reports
`descriptor-final-DD=not-submitted`, no TX timeout/error snapshot, and the
IOMMU reason `ACPI DMAR table is not present`. No independent frame emission
was demonstrated.

The two command paths are distinct. `nicinfo tx iommu` dispatches to the
read-only Phase 27 preflight; `nicinfo tx iommu isolated` dispatches to the
same function with the isolated-network confirmation flag set. The shell
parser distinguishes the exact third token `isolated`. Both reports call the
same `vtd::discover` audit used by `nicinfo dma brief`, so the ACPI/DMAR
classification and failure reason can look similar, but the TX IOMMU command
has its own output and safety gate path.

When the validated root has no DMAR entry, the audit leaves DRHD count at zero
and matching-DRHD false; those are consequences of the missing table. In
`nicinfo dma brief`, the audit reason is printed as `failure=ACPI DMAR table is
not present`. In the Phase 27 report, that reason is printed as
`IOMMU evidence-unavailable`; its `failure=` field reports a probe gate or TX
helper result. This label difference distinguishes overlapping audit data
from an accidentally shared command handler.

The UEFI loader selects an RSDP pointer from the ACPI 2.0/1.0 EFI configuration
table and passes it in BootInfo. The kernel validates the RSDP and selected
RSDT/XSDT checksums and bounds, then scans the root entries. The exact
`ACPI DMAR table is not present` result is emitted only after that traversal
completes without finding a DMAR signature. This supports “no DMAR in the
firmware-supplied root table,” rather than an RSDP/root discovery or validation
failure; it does not establish the hardware translation state.

In the original Phase 27 source, a validated ACPI root with no DMAR entry was
marked as a known DMA-mapping state. That did not prove translation was
disabled or that the active device context was identity mapped. The gate now
requires a readable matching DRHD and a valid snapshot showing translation
disabled. Missing DMAR remains a diagnostic limitation and now fails the
unknown-mapping safety gate; a separate, independently justified no-remapping
proof could support a future safe path.

`descriptor-final-DD=not-submitted` means the Phase 27 software path did not
record its publication fence and descriptor-publication counter together, so
it did not reach its TDT publication point. This establishes no Phase 27
descriptor publication; it does not identify the earlier gate or report
whether the one-shot latch was consumed. The summary supplied here omits the
isolated output's `failure=`, `probe-consumed=`, `preflight=`, and `attempt=`
values. In the original implementation the latch was set after preflight and
operator confirmation, immediately before entering the TX helper, so a late
pre-publication refusal could consume the latch while still reporting
`not-submitted`. An early safety-gate refusal leaves it unconsumed.

The phrase `ACPI DMAR table is not present` is the VT-d audit reason, not the
Phase 27 safety-gate string. If earlier gates pass, the tightened IOMMU gate
reports `active or unknown IOMMU mapping cannot be proven safe`; the exact
gate reported by the already-run isolated command cannot be recovered from the
summary alone. Keep the phase open and do not repeat a physical probe to
recover the omitted output.

The PHASE27R hosted Phase 11–27 regression chain passed. The AMD64 freestanding
kernel and UEFI loader built successfully with the Phase 27 I219 diagnostic
configuration enabled. A second AMD64 kernel/UEFI build also passed with TX
DMA placement and LAN_INIT diagnostics disabled. Existing unrelated compiler
warnings remain. No replacement physical ISO was packaged; the artifact in the
addendum contains the original Phase 27 behavior and must not be used for a
repeat probe.

## Physical status and phase tracking

The Phase 27 command sequence was run on AIDA_LPT, but the isolated probe
reported no descriptor submission. No outcome A, B, C or D is claimed because
the probe did not publish a descriptor. Descriptor consumption, external frame
emission and peer receipt are all unverified. Do not advance this state until
the safety path is qualified and physical evidence satisfies the Phase 27
acceptance criteria:

```text
last_completed=PHASE26
next_expected=PHASE27
```

Do not begin Phase 28 automatically.

## Artifact addendum

The Phase 27 implementation source commit is
`0d2dfb15be27eb8a31179dbe927787f3e289bd5d` on
`NAVIGATOR_GENERAL_IMPROVEMENTS`.

- ISO: `D:\dev\guideXOSServer_NAVIGATOR_IMPROVEMENTS\dist\guideXOS-Server-v0.1.0-navgen-phase27-i219-tx-consumption-amd64.iso`
- Size: `91,293,696` bytes.
- SHA-256: `c8efccec1fd471a1a4f799bdd728f04a8c1720d28c74ff60ed4bc1b08e860a87`.
- Checksum sidecar: `dist/guideXOS-Server-v0.1.0-navgen-phase27-i219-tx-consumption-amd64.iso.sha256`; its digest matches the ISO.
- Manifest: `dist/guideXOS-Server-v0.1.0-navgen-phase27-i219-tx-consumption-amd64.manifest.json`; `sourceGitCommit` is the implementation commit above.
- Build: AMD64 UEFI, Phase 5 stage 8, Phase 6 stage 0, Phase 7 stage 4 (`register`), constrained-low I219 TX DMA placement enabled, LAN_INIT diagnostics enabled, PacMan package rebuild skipped/reused, PyCdlib backend.
- Packaged kernel SHA-256: `d99e73466c081ad474b17dbc826791ff0d6fe8b0010ff849273b4afaaf3e1d48`.
- Packaged `BOOTX64.EFI` SHA-256: `5104d62c2a7616545729553f9a62e5f56767aafddd2877348b2e49953094a07d`.

The release verifier passed. It recorded a bootable UEFI El Torito image with
platform ID `0xEF`, no-emulation media, a `91,226,112`-byte EFI boot image, and
the expected ISO size. The isolated QEMU readiness test booted this exact ISO
using QEMU's generic `e1000` device. Its serial log contained the firmware DVD
boot entry, guideXOS UEFI bootloader, kernel load, 64 MiB ramdisk load, desktop
event loop ready marker, and kernel main-loop marker. QEMU was stopped after
the required bounded readiness grace period. This is boot/package evidence;
the generic virtual NIC does not emulate or qualify the physical I219-LM.

At packaging start, the release manifest reported a dirty worktree containing
only `M ESP/build-identity.txt`. The canonical build had regenerated this
tracked file with the Phase 27 build flags, unique build ID, timestamp, and
UEFI/kernel hashes; the file was restored to its original committed content
after packaging. The manifest records the exact generated identity file hash
inside the ISO and the clean implementation source commit. No implementation
source was uncommitted at packaging time.

The Phase 27 command was run on AIDA_LPT, but no descriptor publication,
consumption, DD completion, external frame capture, or peer receipt was
demonstrated. The audit's phase tracker remains `last_completed=PHASE26` and
`next_expected=PHASE27` until the physical procedure above is run and evidence
is supplied.

## PHASE27R2 DMA translation attestation audit

**Disposition: Outcome C — translation state is unverifiable.** The physical
evidence supplied for this review confirms successful I219-LM initialization,
TX ring READY, link UP, and PCI command `0x0006`. ACPI RSDP and root-table
traversal succeeded, but no DMAR signature was found in the firmware-supplied
root and no matching DRHD was available. No VT-d unit status was read, so the
hardware translation state remains unknown. The probe did not record descriptor
publication, and physical TX completion remains unverified.

The operator confirms that both read-only preflight and the explicit
`nicinfo tx iommu isolated` command were run. The old-ISO lines for
`preflight=`, `probe-consumed=`, `attempt=`, and `failure=` were not supplied.
No value for those fields, failed gate, latch state, descriptor preparation,
or TDT publication is inferred in this audit. The repository search found no
additional physical transcript in its Phase 27 documents or logs. This is not
a new physical attempt and does not authorize one.

### Starting state and duplicate-work check

- Starting branch: `NAVIGATOR_GENERAL_IMPROVEMENTS`.
- Starting HEAD: `b6de1226e08f54658b01d61900ad4ee1f1a2c0e8`.
- Starting worktree: clean.
- `.phase`: absent; this audit remains the phase-state record.
- Before this update, no PHASE27R2 audit or completed DMA-attestation
  implementation was present. Existing PHASE27R / fail-closed work was
  reviewed; it is incomplete for physical qualification and already preserves
  the no-DMAR state as unknown.

The Phase 27 state remains:

```text
last_completed=PHASE26
next_expected=PHASE27
```

### What the boot chain currently establishes

The UEFI loader obtains an ACPI RSDP from the firmware configuration table,
retains it in the boot handoff, and builds CPU page tables. For the I219
diagnostic build it allocates a two-page, below-4-GiB `EfiLoaderData` region,
checks that it is contiguous and does not overlap known boot objects, and
includes the region in the CPU identity-mapped ranges. The kernel uses the
resulting physical addresses for the TX ring and packet buffer.

Those steps establish CPU virtual-to-physical translation, reservation,
contiguity, and placement. They do not call a PCI DMA mapper, create an IOVA,
install a device context entry, or prove the NIC's DMA address translates to
that physical memory. PCI bus mastering and a ready ring likewise do not prove
device mapping.

The kernel's `vtd::discover` path validates the supplied RSDP and root table,
searches that root for DMAR, and only reads VT-d registers after parsing a
matching DRHD. It does not guess a VT-d MMIO base, write GCMD/RTADDR/FSTS, walk
the active root/context tables, or establish a mapping. Thus “no DMAR in the
validated firmware root” is evidence about the exposed ACPI table set; it is
not evidence that VT-d hardware is absent or disabled.

When DMAR is present, the parser matches a requester by PCI segment and either
an include-all DRHD or an explicit device scope. The running NIC's exact PCI
segment/BDF must be captured; `8086:156F` alone identifies the device model, not
its requester ID. The current explicit-scope helper accepts a one-element
device path only, so it can miss a valid multi-hop bridge path and fail closed.
That parser limitation should be addressed before treating explicit-scope
matching as complete. A safe future audit must validate all applicable
remapping-unit coverage and the actual requester path rather than infer it
from the vendor/device ID.

The existing register decoder reads `GSTS.TES` from the DMAR-described unit.
Intel defines TES as read-only status indicating whether DMA remapping is
enabled. Without a DMAR-described unit and a verified requester match, there
is no safe register base from which this kernel can obtain that status. The
current physical evidence therefore contains no positive or negative VT-d
status reading.

### Safety models

| Model | Evidence required before I219 TX | Assessment on AIDA_LPT / in guideXOS |
|---|---|---|
| **A — verified translation disabled** | Discover the applicable remapping unit(s) for the actual PCI segment/BDF; validate DRHD scope coverage; read a valid unit status showing `GSTS.TES=0` for each applicable unit. Do not infer this from absent DMAR, a BIOS label, bus mastering, or CPU identity maps. | **Not established.** No DMAR and no matching DRHD were exposed, so no status read is available. The kernel already refuses this state. It is feasible in principle if firmware exposes complete, trustworthy unit descriptions and readable status. |
| **B — verified DMA mapping** | Create a device-specific mapping for the full descriptor ring and packet buffer; use the returned device/IOVA addresses in TDBA and the TX descriptor; grant the NIC only the required read/write access; verify coverage, lifetime, ownership, and cache-coherency rules through TX completion. Preserve the mapping across kernel handoff or have the kernel own the IOMMU domain. | **Not implemented or established.** The loader does not use `EFI_PCI_IO_PROTOCOL.Map` or an IOMMU protocol, and the kernel has no VT-d mapping/page-table management path. UEFI defines PCI controller DMA mapping services, while TianoCore also defines an EDK II IOMMU protocol extension; neither is currently queried here. A boot-services mapping cannot be assumed to remain valid for the kernel after `ExitBootServices`; the handoff would need an explicit, verified lifetime contract or kernel-owned mappings. Feasible as a future OS-owned IOMMU feature, with substantial implementation and physical validation. |
| **C — unverifiable translation state** | Required hardware status or an explicit device mapping cannot be obtained and verified. Keep the TX submission gate closed. | **Current result.** This is the only evidence-supported classification. It is safe and matches the current fail-closed driver behavior. |

The UEFI PCI I/O protocol's `Map()` returns a controller-specific device
address and a mapping token, and the specification's DMA flow pairs mapping
with an `Unmap()` after use. UEFI boot services terminate at
`ExitBootServices()`, when the loader assumes platform resource management.
Those interfaces are useful design inputs, but their presence on AIDA_LPT and
the persistence of any firmware-managed mapping through this kernel handoff
have not been established. EDK II's `EDKII_IOMMU_PROTOCOL` is an implementation
extension, not a capability that can be presumed on arbitrary firmware.

### Most promising supported path

Keep the existing unknown-state refusal. The most supportable implementation
path is a future kernel-owned VT-d domain: parse and validate complete DMAR
coverage for the actual requester; allocate and initialize the required root,
context, and second-level tables; map the ring as device read/write and the TX
packet buffer as device read; install mappings with the required invalidation
and ownership barriers; then use the resulting IOVAs in the NIC registers and
descriptor. That work must account for all applicable units, reserved-memory
regions, requester aliases, cache policy, mapping teardown, reset behavior,
bounded polling, and no retry after ambiguous ownership. It needs hosted tests
and a separately qualified physical procedure before any TX experiment.

A narrower UEFI-assisted path could be considered only if AIDA_LPT exposes a
supported PCI/IOMMU mapping protocol for the I219 and the loader can prove that
the mapping remains installed and owned by the kernel after handoff. The
current source contains no such protocol discovery or handoff contract, so this
is a question for read-only firmware inspection, not an available safety
model.

### Old isolated-probe evidence and required output fields

The original Phase 27 source used for the old ISO treated a fully traversed
ACPI root with no DMAR table as `iommuMappingKnown=true`. That implementation
was unsafe and was corrected by the current fail-closed source at HEAD. This
source fact does not identify the old physical run's blocking gate: the probe
evaluates several ordered gates, and any earlier gate could have refused
before the no-DMAR condition mattered.

In the old source, the bare `nicinfo tx iommu` command ran the read-only
preflight. The isolated command ran the same inspectable gates, then checked
operator confirmation. Only after preflight and confirmation did it set the
one-shot latch and enter `send_raw_diagnostic_frame`. Within that helper,
additional PCI, ring, descriptor-availability, and DMA-address checks could
return before descriptor publication. Therefore `attempt=one` means the helper
was entered; it does not prove descriptor preparation or a TDT write.

For an existing transcript, these fields have distinct meanings:

- `preflight=` plus the exact `failure=` line identifies the first software
  gate reported as refusing the command. `preflight=PASS` alone does not
  establish isolated-network confirmation or submission.
- `probe-consumed=` reports the one-shot latch. In source, it is set after
  preflight and isolation confirmation, immediately before the helper call.
- `attempt=` reports whether the helper was entered. `attempt=none` means it
  stopped before the helper; `attempt=one` can still stop before descriptor
  preparation.
- `descriptor-pre-TDT=raw16`, decoded descriptor bytes, and
  `buffer-pre-TDT-first32`, together with descriptor/buffer DMA addresses,
  show the state captured after software descriptor preparation. The printer
  has no independent `descriptor-prepared=` flag, so missing or default raw
  values do not by themselves prove that the descriptor was untouched.
- `descriptor-submitted=`, `TDT-write=`, `publication-fence=`, immediate TDT
  readback, and before/final TDH/TDT are needed to establish whether the probe
  reached its doorbell boundary and whether the write was observed. A
  publication-fence flag alone is set immediately before the MMIO write; a
  matching immediate readback corroborates the completed doorbell.
- `descriptor-final-DD=not-submitted` means the software did not report a
  published descriptor and did not read a post-submission DD result. It is not
  a hardware DD-clear observation. If the transcript explicitly says
  `TDT-write=not-reached` and has no immediate readback, the probe did not hand
  that TX descriptor to the NIC for a new TX DMA operation.

The supplied physical summary gives `descriptor-final-DD=not-submitted` and
states that descriptor publication was not recorded. It does not provide the
other fields above. The historical physical attempt therefore remains
**unresolved** as to preflight failure, latch consumption, helper entry, and
descriptor preparation. No gate, latch state, descriptor ownership transition,
or failure string is retroactively assigned.

### BIOS inspection and image decision

A read-only BIOS/UEFI inspection is worthwhile because it may explain why the
firmware root omitted DMAR and indicate whether the platform claims to have
VT-d/DMA-remapping enabled. Record the platform model, BIOS/UEFI version, the
current setting label and value (for example, Intel VT-d, Directed I/O, IOMMU,
or DMA Remapping), and a photograph of the page. Exit Setup without saving any
changes. Do not toggle VT-d, load defaults, or change any other firmware
setting under this phase.

That setting is contextual evidence only. Even “disabled” in Setup would not
replace a hardware status read for the actual requester; “enabled” with no
DMAR would reinforce the firmware-description gap. No platform model or BIOS
setting value is present in the supplied evidence, so the BIOS result remains
pending.

No new diagnostic image is justified now. The current read-only path already
reports the missing DMAR/unknown-mapping refusal, the old physical output is
incomplete, and no safe transmission model has been implemented or tested.
Do not rerun the isolated probe or request a repeat transmission. If firmware
inspection provides new evidence, use it to plan a separate read-only audit;
any future transmission image requires an implemented and tested Model A or B
and separate physical qualification.

### PHASE27R2 changes and validation

This update changes only this audit document. No kernel or bootloader source,
probe behavior, ISO, firmware setting, or physical device state was changed.
No hosted tests or target builds were run because no implementation changed;
the existing Phase 11–27R test/build results remain as recorded above. The
documentation diff was checked for whitespace errors.

Primary references reviewed:

- [UEFI Specification 2.10, PCI Bus Support and DMA mapping](https://uefi.org/specs/UEFI/2.10/14_Protocols_PCI_Bus_Support.html)
- [UEFI Specification 2.10, Boot Services and `ExitBootServices`](https://uefi.org/specs/UEFI/2.10/07_Services_Boot_Services.html)
- [TianoCore EDK II IOMMU protocol definition](https://github.com/tianocore/edk2/blob/master/MdeModulePkg/Include/Protocol/IoMmu.h)
- [Intel VT-d Architecture Specification](https://cdrdv2-public.intel.com/831418/vt-directed-io-spec.pdf)

Final phase state remains:

```text
last_completed=PHASE26
next_expected=PHASE27
```
