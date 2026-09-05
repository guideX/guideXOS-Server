# guideXOS Server Navigator — I219-LM TX Completion Phase 13

Status: implementation complete; physical AIDA_LPT validation pending.

## Outcome

No physical A/B/C result is claimed yet. Phase 13 produces an instrumented
and bounded TX-completion image, applies the smallest evidence-backed I219
TX correction selected from the upstream e1000e PCH erratum path, and keeps
the physical acceptance decision pending until AIDA_LPT is booted. The image
is prepared to distinguish descriptor publication, TDT observation, TDH
movement, DD writeback, DMA/register faults, and unsafe retry state.

The old physical frontier remains the deepest proven TX boundary:

```text
DHCP DISCOVER built -> frame construction -> send_frame() entered
                     -> physical failure reported as TX completion timeout
```

The old run did not retain enough register/descriptor evidence to say whether
the I219 consumed the descriptor. Phase 13 fixes that observability gap and
does not treat QEMU completion as physical proof.

## Repository state and inherited physical evidence

Phase 13 started in:

- Repository: `D:\dev\guideXOSServer_NAVIGATOR_IMPROVEMENTS`
- Branch: `NAVIGATOR_GENERAL_IMPROVEMENTS`
- HEAD: `b029288e39a3abd3df486f6bfa5a0ecb24a4e110` — `Record final Phase 12 QEMU logs`
- Upstream: `origin/NAVIGATOR_GENERAL_IMPROVEMENTS`
- Ahead/behind: `4/0`
- Worktree: clean

Phase 12 implementation commit: `d07135f` — `Implement Phase 12 I219 link
refresh`. Phase 12 baseline artifact: `guideXOS-Server-v0.1.0-phase12-aida-i219-link-refresh-amd64.iso`, SHA-256
`325b4ca06784165af72d884809120cdc75d398c63fb87ad6903f8825a512b59d`.

The pre-existing unrelated worktree was clean and was preserved. The build
temporarily rewrote the generated tracked `ESP/build-identity.txt`; its
content was restored to the starting blob before the Phase 13 changes were
committed. No reset, clean, stash, rebase, discard, overwrite of unrelated
work, or push was performed.

The exact Phase 12 physical link evidence incorporated here is:

```text
Driver Ready: YES
Link: UP
cache=UP
last=UP
src=combined
refresh=UP
polls=12/50
timeout=no
```

The known AIDA_LPT device is Intel I219-LM/PCH `8086:156F`, subsystem
`103C:8079`, revision `21`, with previously observed MAC
`EC-8E-B5-9F-36-38`. PCI discovery, MMIO, reset, MDIC PHY access, PHY
status, MAC acquisition, RX/TX ring READY, registration, active state, IRQ
registration, hardware PASS, Driver Ready, link refresh, and truthful DHCP
configuration state remain inherited and were not redesigned.

The exact Phase 12 DHCP/TX failure incorporated here is:

```text
Requesting IP address from DHCP server...
DHCP configuration failed.
Error: DHCP transmission did not complete; inspect 'dhcp status'.

built=1 attempts=4 submitted=0 complete=0 offer=0 request=0 ack=0 nak=0
timeout=0 fail=4

Failure:
TX completion
NIC TX descriptor completion timed out

DHCP Wait:
offer=not-started
ACK=not-started

IPv4 mode=dhcp
status=not configured
lease=none address=none mask=none gateway=none DNS=none
```

`submitted=0` was not treated as proof that no descriptor write occurred.
Before Phase 13, the high-level counter was projected only after
`nic::send_frame()` returned success, so a lower-level descriptor/TDT action
followed by a completion timeout could still appear as zero submissions.

## Counter-semantics audit

The Phase 13 meanings are now explicit:

- `built`: one valid DHCP DISCOVER packet was constructed.
- `attempts`: one invocation of the DHCP send path after link preflight.
- `published`: the descriptor fields and status were written by the NIC
  driver.
- `submitted`: the TDT doorbell write was accepted by the MMIO path and
  read back as the expected tail; this is not hardware completion.
- `complete`: the NIC wrote legacy descriptor `DD` and the driver observed it.
- `discoversSent`: `send_frame()` returned success, followed by the existing
  OFFER wait path.

The old `attempts=4` represented four DHCP retry-loop passes. The old
`submitted=0` and `complete=0` were success-only projections and therefore
did not expose lower-level publication/TDT evidence. Phase 13 projects
publication and accepted-doorbell evidence before checking the return code.
If a descriptor is accepted but completion times out, the shared TX buffer
and ring are marked poisoned and DHCP stops rather than overwriting a
descriptor that hardware may still own. The corresponding safe state is one
attempt, one accepted submission, zero completions, and one timeout.

## Complete old TX path and Phase 13 boundary

The runtime path is:

1. `dhcp /discover` performs the existing bounded link preflight.
2. DHCP builds the existing Ethernet/IP/UDP/DHCP DISCOVER in the shared
   packet buffer.
3. `dhcp_send()` calls `nic::send_frame()` through the existing device-family
   path.
4. The NIC selects `s_txCur`, checks the legacy descriptor's `DD` availability,
   copies the frame into the static TX buffer, translates its kernel address,
   and writes the descriptor.
5. The driver publishes `bufferAddr`, `length`, `cmd`, and cleared `status`,
   advances TDT, and polls descriptor `DD` without requiring an interrupt.
6. A successful DD increments completion/frame counters; a bounded timeout
   records the descriptor/register snapshot, poisons the ring, and returns a
   failure to DHCP.
7. DHCP records publication, doorbell, submission, completion, timeout, and
   failure category separately. No OFFER is required to prove TX completion.

The legacy descriptor, ring registers, TCTL/TIPG, PCI command state, DMA
translation, barriers, and DD polling are shared E1000/E1000E-compatible
mechanisms. The I219/PCH-specific behavior is the narrow SPT TX DMA erratum
workaround: preserve `IOSFPC.RDMTS_HEX` and program `TARC0` to reduce the
outstanding request setting from the three-request encoding to the
two-request encoding. No PHY or link operation is part of this change.

## Descriptor format and ABI audit

The configured mode is the legacy 16-byte descriptor format, not advanced
descriptors. The exact packed layout is:

| Offset | Size | Field | Phase 13 use |
|---:|---:|---|---|
| `0x00` | 8 | `bufferAddr` | 64-bit DMA address |
| `0x08` | 2 | `length` | Ethernet frame length |
| `0x0A` | 1 | `cso` | zero; no checksum offload |
| `0x0B` | 1 | `cmd` | `EOP \| IFCS \| RS = 0x0B` |
| `0x0C` | 1 | `status` | initialized `DD`, cleared to zero before ownership, then polled for `DD` |
| `0x0D` | 1 | `css` | zero; no checksum offload |
| `0x0E` | 2 | `special` | zero; no VLAN insertion |

Compile-time and hosted tests enforce size 16 and offsets 0, 8, 11, and 12.
The ring is eight descriptors, so `TDLEN=8*16=128` bytes: a 128-byte
multiple and a descriptor-count multiple of eight. The ring and each DMA
object are required to be at least 16-byte aligned. `IDE` is not used.
The NIC owns a descriptor after the tail doorbell; `DD` is the authoritative
writeback/completion signal. TDH is captured as corroborating hardware
evidence only and is not used alone to reclaim a descriptor.

The descriptor contract and register semantics were checked against the
[Intel PCIe GbE Controllers Open Source Software Developer’s Manual](https://www.intel.com/content/dam/www/public/us/en/documents/manuals/pcie-gbe-controllers-open-source-manual.pdf).
The I219/PCH workaround shape was compared with the
[upstream Linux e1000e TX configuration](https://raw.githubusercontent.com/torvalds/linux/master/drivers/net/ethernet/intel/e1000e/netdev.c),
including its SPT/KBL TX-hang comment and `IOSFPC`/`TARC0` writes.

## Ring allocation and DMA/physical-address audit

The existing static-storage model is retained:

- `s_txDescs[NUM_TX_DESC]` is the descriptor ring.
- `s_txBuffer[ETH_FRAME_MAX]` is the packet buffer.
- RX descriptors and RX buffers remain static objects in the loaded kernel
  image and are validated together with TX objects.
- Objects remain mapped and alive for the entire synchronous send.

The driver does not pass virtual addresses to hardware. The loader-linked
kernel image uses virtual link base `0x100000`; `BootInfo` supplies the
physical base. The translation is:

```text
physical = kernelPhysicalBase + (virtual - 0x100000)
```

The helper rejects null output, virtual addresses below the link base, zero
physical base, arithmetic overflow, and zero physical results. The full DMA
layout check additionally enforces 16-byte alignment, range overflow safety,
and non-overlap between RX descriptors, TX descriptors, TX buffer, and every
RX buffer. The descriptor ring physical address is recorded and programmed
with low/high 32-bit writes; the packet buffer physical address is recorded
in the legacy descriptor as a 64-bit value.

No bounce buffer or new allocator was introduced because the loader maps the
kernel's linked virtual region to the physical backing region described by
`KernelPhysicalBase`, and the static objects stay inside that mapped image.
The helper and layout checks prove the address transformation used by this driver, while
the physical diagnostic exposes the exact ring, descriptor, and buffer
addresses given to the NIC. AMD64 PCIe DMA address width is not truncated by
the driver. Cache coherence is handled by the existing x86 coherent memory
model plus an explicit `sfence` before TDT and `lfence` around DD observation;
MMIO accesses remain volatile with compiler ordering barriers.

## PCI bus mastering and register audit

PCI configuration offset `0x04` is read at TX diagnostic snapshot time. The
existing initialization enables only Memory Space Enable and Bus Master
Enable when absent, then verifies both bits. Phase 13 does not toggle
unrelated PCI command bits and no later TX path clears bus mastering. The
compact diagnostic prints the TX-time PCI command value; `0x0007` in the
existing QEMU serial path confirms I/O, memory, and bus-master bits present
there. The physical value must be recorded by `nicinfo tx` on AIDA_LPT.

For each TX attempt the driver records:

- after-ring-init: `TDBAL`, `TDBAH`, `TDLEN`, `TDH`, `TDT`, `TCTL`, `TIPG`,
  `TXDCTL`, `TARC0`, `IOSFPC`, and PCI command;
- before descriptor selection;
- after descriptor publication and before TDT;
- immediately after TDT write/readback;
- at completion or timeout.

The command exposes one descriptor and these decisive register values only;
it does not scan or dump the whole ring.

## TCTL, TIPG, TXDCTL, and I219/PCH sequencing audit

TX ring base, length, and empty `TDH/TDT=0` are programmed before enabling
the transmit engine. `TIPG` is written before `TCTL`. TCTL uses `EN | PSP`,
collision threshold `0x0F`, and collision distance `0x03F`, matching the
documented full-duplex legacy values and the upstream configuration shape.
`TXDCTL` is read and reported but not blindly rewritten: DD polling does not
depend on TX interrupts or a software reclaim threshold, and the current
minimal ring does not have evidence requiring a larger policy change.

For I219/PCH only, Phase 13 applies the upstream SPT workaround after the
ring/TCTL setup and before the ring is published ready:

```text
IOSFPC <- IOSFPC | RDMTS_HEX
TARC0  <- (TARC0 & ~CB_MULTIQ_3_REQ) | CB_MULTIQ_2_REQ
```

The TDT readback is used only to prove that the MMIO doorbell value was
observed by the register interface. It is not used to claim NIC consumption.
There is no added unbounded settle delay and no interrupt dependency.

## Descriptor submission, completion, and retry audit

The selected descriptor must have `DD` set before use. The driver then writes
the payload and descriptor, explicitly publishes stores, records the pre-TDT
register state, advances `s_txCur` modulo the ring, writes TDT, and captures
the immediate readback. A successful readback increments the low-level
submission counter; only descriptor `DD` increments completion.

Completion polling is bounded at `1,000,000` iterations and remains entirely
polling-based. It does not depend on IRQ registration, interrupt masking, or
the later main-loop interrupt enable path. On timeout the driver records final
status and registers, classifies the evidence, sets `ringPoisoned`, and
refuses to write another descriptor. This is intentional: the buffer is
shared static storage and a timed-out descriptor may still be hardware-owned.
Recovery is therefore a full NIC/ring reinitialization boundary, not an
unsafe descriptor reuse. The DHCP retry loop now stops after an accepted
descriptor timeout instead of producing four ambiguous submissions.

Failure categories currently projected are `TX_NOT_READY`,
`TX_RING_INVALID`, `TX_DESCRIPTOR_INVALID`, `TX_DOORBELL_NOT_OBSERVED`,
`TX_DESCRIPTOR_NOT_CONSUMED`, `TX_COMPLETION_TIMEOUT`,
`TX_DMA_ADDRESS_INVALID`, `TX_ENGINE_DISABLED`, and `TX_STATUS_READ_ERROR`.
`TDH` unchanged plus no `DD` produces `TX_DESCRIPTOR_NOT_CONSUMED`; changed
TDH without DD produces `TX_COMPLETION_TIMEOUT`. DD remains authoritative.

## Root cause and deepest proven TX issue

The exact physical root cause is not yet proven because no Phase 13 AIDA_LPT
run has been performed. What is proven is that the link-ready I219 reached
the real DHCP frame-construction/TX path and the old synchronous driver
returned a descriptor-completion timeout before DHCP entered OFFER wait.

The old implementation could not distinguish a virtual address, invalid
ring base, ignored TDT, unconsumed descriptor, disabled engine, or missed DD;
its `submitted=0` projection obscured that distinction. The strongest
evidence-backed correction is the I219/PCH SPT TX-hang workaround, combined
with exact DMA/register/DD instrumentation and safe timeout handling. The
next physical run must determine whether the result is actual DD completion
or a more specific pre-completion failure. No claim that the workaround
alone fixed AIDA_LPT is made until that run.

## Changes made

- Added legacy TX descriptor ABI assertions and DMA translation/layout
  validation.
- Added I219/PCH `IOSFPC`/`TARC0` TX-hang workaround, documented TCTL/TIPG
  setup, and changed collision distance to the documented `0x03F` value.
- Added explicit register snapshots around initialization, descriptor
  publication, TDT write, and completion/timeout.
- Added descriptor physical address, buffer physical address, command,
  status-before/status-final, poll limit/count, TDT-written, and ring-poison
  evidence.
- Added `nicinfo tx`, plus compact TX evidence in `dhcp status`.
- Corrected DHCP submission/completion projection to expose accepted
  descriptor/TDT evidence even when DD completion fails.
- Stopped unsafe DHCP retries after accepted timeout/ambiguous ownership.
- Added deterministic descriptor, DMA helper, failure classification,
  completion/timeout, poisoning, retry, and DHCP counter tests.
- Added `run-network-phase13-tests.ps1` and the unique Phase 13 ISO generator.

No PHY reset, PHY write, SWFLAG/semaphore operation, autonegotiation change,
NVM write, or speculative link behavior was added. Phase 10–12 PCI, MMIO,
MDIC, MAC, RX, registration, Driver Ready, link refresh, and DHCP link-gate
behavior remain in place.

## Verification

Focused gate:

```text
scripts/run-network-phase13-tests.ps1
Phase 11 network tests PASS.
Phase 12 network/link tests PASS.
Phase 13 TX tests PASS.
```

This includes the existing Phase 11/12 DHCP, IPv4, diagnostics, and link
tests; descriptor ABI/layout checks; DMA helper checks; command/register
constants; accepted-doorbell versus DD-completion semantics; timeout
classification; poisoned retry non-corruption; and no-false-submitted/
complete DHCP projection.

Full AMD64 freestanding build:

```text
build.ps1 -Arch amd64 -I219Phase5Stage 8 -I219Phase6Stage 0 -I219Phase7Stage 4
```

Result: PASS. The UEFI bootloader, kernel, ramdisk, and ESP image were
produced. Existing unrelated compiler warnings remain outside this phase.

Two fresh QEMU E1000 boots of the Phase 13 ISO passed the existing release
smoke verifier. Both reached firmware, bootloader, kernel, ramdisk, desktop,
and kernel-main-loop readiness markers; both initialized the emulated E1000
with link UP. Representative serial evidence included PCI command
`00000007`, RX ring setup, TX ring setup, E1000 initialization, and the
network-ready/main-loop-ready checkpoints. Logs:

- `out/release-iso/qemu-test-ce01f34ce3c3487fb55b186bf7c08641/serial.log`
- `out/release-iso/qemu-test-c56360d28653476793fd16912efa75f9/serial.log`

The QEMU smoke verifier did not inject a shell TX command, so these boots
prove boot and E1000 regression safety only; they do not prove physical I219
descriptor completion. The release structural verifier also passed the UEFI
no-emulation boot record, boot catalog, FAT32 boot image, and input-file
manifest checks.

## Phase 13 physical artifact

- ISO path: `D:\dev\guideXOSServer_NAVIGATOR_IMPROVEMENTS\dist\guideXOS-Server-v0.1.0-phase13-aida-i219-tx-completion-amd64.iso`
- Filename: `guideXOS-Server-v0.1.0-phase13-aida-i219-tx-completion-amd64.iso`
- Size: `91,293,696` bytes
- SHA-256: `61230eb4d9e40c99c86a53afe2539ec168b1351ea9d8622a302082209fec608f`
- SHA-256 sidecar: `D:\dev\guideXOSServer_NAVIGATOR_IMPROVEMENTS\dist\guideXOS-Server-v0.1.0-phase13-aida-i219-tx-completion-amd64.iso.sha256`
- Manifest: `D:\dev\guideXOSServer_NAVIGATOR_IMPROVEMENTS\dist\guideXOS-Server-v0.1.0-phase13-aida-i219-tx-completion-amd64.manifest.json`
- Image configuration: AMD64 UEFI, Phase 5 stage 8, Phase 6 stage 0,
  Phase 7 stage 4 (`register`), PyCdlib backend.

The manifest records the same ISO size/hash and structurally verified UEFI
boot image. It was generated from the Phase 13 working tree before the local
commit; the artifact itself contains the tested Phase 13 build. No physical
media was written.

## AIDA_LPT physical validation

Status: pending. The machine and known-good cable must be available; no
physical completion result is inferred from QEMU.

Use the Ethernet cable connected before power-on. After desktop/input are
confirmed, run and photograph the decisive output from:

```text
nicinfo brief
nicinfo tx
dhcp /discover
dhcp status
nicinfo tx
netdiag
ipconfig /all
```

The first `nicinfo tx` is the post-init baseline. The second is immediately
after `dhcp status`. Record `idx`, `len`, `cmd`, status before/final,
`ringPA`, `descPA`, `bufPA`, publication and doorbell flags, initial and
final TDBAL/H/TDLEN/TDH/TDT, TDT written, TCTL, TIPG, TXDCTL, TARC0, IOSFPC,
PCI command, poll count/limit, timeout, poison, and failure category.

Interpretation and next phase:

- TX completion plus DHCP lease: proceed to physical IPv4, ARP, gateway,
  ping, and DNS validation.
- TX completion but no OFFER: stop changing TX; validate frame bytes, wire
  visibility, RX filtering, and DHCP server response.
- TDT advances but TDH/DD do not: use DMA addresses, ring base/length,
  TCTL/TXDCTL, PCI bus master, I219 workaround registers, and descriptor
  bytes to isolate descriptor consumption/DMA/TX-engine setup.
- TDH advances but DD is absent: inspect descriptor format, command/status
  semantics, and hardware writeback observation.
- DD appears but software times out: fix status visibility/polling ordering
  or interpretation; do not change DHCP protocol semantics.
- TX fails before meaningful descriptor ownership: use the explicit failure
  category (`TX_RING_INVALID`, `TX_DMA_ADDRESS_INVALID`,
  `TX_ENGINE_DISABLED`, `TX_DOORBELL_NOT_OBSERVED`, or related state) and
  correct only that boundary.

The physical result of this procedure determines whether the next phase is
DHCP receive/wire validation, full IPv4 validation, or a narrower I219 DMA/
descriptor-engine correction.
