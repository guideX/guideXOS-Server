# AIDA_LPT I219-LM Freeze Isolation — Phase 5

Status: diagnostic isolation implementation; physical AIDA_LPT validation is still required.

## Scope and physical evidence

AIDA_LPT completed UEFI boot and rendered the guideXOS desktop in Phase 4. The wallpaper and `Welcome to guideXOS` notification appeared, but mouse and keyboard input stopped responding. The behavior reproduced with Ethernet connected and with Ethernet disconnected before boot. Phase 3 remained usable because `8086:156F` was intentionally excluded from the I219 path.

The Phase 3/Phase 4 regression boundary is therefore the exact Intel Ethernet match `8086:156F` entering I219-LM/E1000e-compatible initialization. DHCP, carrier, negotiation, and external traffic are not required to reproduce the reported symptom. No physical I219 success is claimed by this document.

Phase 3 startup protections remain present, including the ordering and checkpoints:

```text
[AIDA-PHASE3] checkpoint=network-ready
[AIDA-PHASE3] checkpoint=input-ready
[AIDA-PHASE3] checkpoint=main-loop-ready
```

Boot-time DHCP remains deferred; this work does not restore synchronous DHCP.

## Phase 4 sequence reconstructed from source

The following is the sequence executed by the Phase 4 baseline (`a7b9c44505df74599823f845e45e25591ab913f2`) on the UEFI path:

1. The bootloader scans PCI buses 0–7 and functions, matches network class/subclass, and accepts exact Intel Ethernet IDs including `8086:156F`.
2. For a supported Ethernet match it scans conventional BAR slots read-only, skips I/O and reserved encodings, accepts the first valid memory BAR, and records a bounded `0x6000` register window. No BAR sizing write is used.
3. The bootloader enables PCI memory space and bus mastering, identity-maps the selected BAR uncached, and places the identity, physical BAR, virtual BAR, size, and IRQ line in BootInfo.
4. The kernel validates the handoff, repeats PCI memory-space/bus-master setup, and then writes `RCTL=0`, `TCTL=0`, and `IMC=0xffffffff`; it reads `ICR` to acknowledge stale causes.
5. The kernel reads `CTRL`, sets `CTRL.RST`, and polls `CTRL.RST` until clear.
6. It masks and acknowledges interrupt causes again, reads `CTRL`, writes `CTRL.SLU|CTRL.ASDE`, and reads `CTRL` and `STATUS` for cached state.
7. For I219, it reads `RAL0`/`RAH0`, requires `RAH0.AV`, and validates the resulting station address.
8. It performs three MDIC reads at PHY address `1`, registers `2`, `3`, and `26`; each transaction polls MDIC completion.
9. It clears all 128 MTA entries.
10. It initializes static RX and TX descriptor arrays and packet buffers, translates their kernel virtual addresses to physical addresses, and programs `RDBAL/RDBAH/RDLEN/RDH/RDT` and `TDBAL/TDBAH/TDLEN/TDH/TDT`.
11. It marks the NIC active/registered, publishes cached link state, registers the IRQ handler, and enables `RXT0|RXDMT0|LSC` in IMS before input initialization.
12. PS/2 mouse and keyboard initialization follows, then the input-ready and main-loop-ready checkpoints.

That final interrupt ordering was unsafe for a device whose reset, link-change cause, or interrupt delivery behavior was not yet established. The Phase 5 path keeps the existing QEMU E1000 sequence intact but moves I219 hardware interrupt enablement behind the main-loop checkpoint.

## Wait and fault audit

| Operation | MMIO | Bound | Timer dependency | Failure behavior |
|---|---:|---:|---|---|
| I219 reset completion | `CTRL` read after `CTRL.RST` write | 100,000 reads | None; counter only | Log reset timeout, mask causes, abandon NIC |
| I219 MDIC read, each transaction | `MDIC` read after command write | 100,000 reads | None; counter only | Log PHY/register timeout or error, mask causes, abandon NIC |
| Legacy E1000 EEPROM read | `EERD` | 100,000 reads | None; counter only | Existing bounded fallback behavior |
| Runtime TX completion | descriptor status | 1,000,000 iterations | None; counter only | TX error returned; not part of boot bring-up |

The Phase 4 reset and MDIC loops were logically finite and did not depend on PIT or timer interrupts. The timer was initialized before network setup, so the timer could advance, but the correctness of these waits did not depend on that progress. The important remaining risk was that a single MMIO read/write can itself fault or fail to return normally; a loop bound cannot protect against a synchronous MMIO fault. The staged path therefore stops before risky operations and preserves diagnostic failure state.

The I219 MDIC assumptions are now explicit: PHY address `1` and status register `26` are probed only in Stage 5, with three finite transactions and validation of MDIC ready/error/data plus nonzero/non-`ffff` PHY IDs. No speculative PHY programming was added. Their physical correctness still requires AIDA_LPT evidence.

## Interrupt audit

PIC/IDT initialization and PIT setup occur before the Phase 3 network-ready checkpoint. The Phase 4 path registered the NIC handler and enabled the NIC IMS mask before PS/2 input initialization. Reset re-masking and `ICR` acknowledgement existed, but the ordering still left a window for a real I219 to assert INTx/MSI state before input was ready.

Phase 5 behavior:

- Stages 0–7 keep the NIC interrupt mask disabled.
- `set_irq_registered(true)` records the handler boundary but does not enable I219 IMS for stages 0–7.
- Stage 8 records handler registration and defers IMS until after `[AIDA-PHASE3] checkpoint=main-loop-ready`.
- Stage 8 uses the existing minimum mask (`RXT0|RXDMT0|LSC`) only after that checkpoint.
- `IMC=0xffffffff` followed by `ICR` acknowledgement is used when entering/resetting the hardware path and on failure.
- The I219 IRQ handler no longer performs a multi-read MDIC poll. On a link-change cause it reads cached `STATUS` only, preventing an interrupt storm from repeatedly consuming the MDIC wait budget and starving input dispatch.

The driver continues to use the existing PCI interrupt-line/PIC registration model. No MSI/MSI-X capability programming was introduced. Whether AIDA_LPT routes this device through legacy INTx or another firmware-configured mode remains a physical/platform question.

## DMA audit

The RX and TX descriptors are packed 16-byte structures. Descriptor arrays are statically allocated and 16-byte aligned. RX storage is 32 × 2048 bytes and the TX staging buffer is 1518 bytes. Ring lengths are 512 bytes for RX and 128 bytes for TX.

The linker places kernel storage in the image beginning at virtual `0x100000`. The bootloader supplies `KernelPhysicalBase`; `dma_address()` translates a kernel virtual address by subtracting `0x100000` and adding that physical base. Descriptors and buffers are static image storage, not stack storage. No concrete overlap or address-width defect was found in this audit, and no speculative DMA rewrite was made.

The remaining bare-metal assumption is that the supplied physical image mapping is DMA-visible to I219 and that the static sections remain within the mapped/usable physical image range. Stage 6 is intentionally the physical test boundary for those assumptions. The driver does not claim I219 DMA functionality until that stage is demonstrated on AIDA_LPT.

## Staged-gate design

The selector is the small compile-time constant `GXOS_AIDA_I219_PHASE5_STAGE`, passed to both bootloader and kernel builds by `build.ps1`/`build-uefi.ps1`. It accepts `0..8`, defaults to `8`, and applies only to exact device `8086:156F`. Existing QEMU E1000 and previously supported NICs retain their normal path.

| Stage | Operations allowed | Stop boundary |
|---:|---|---|
| 0 | Exact identity bind and intended-driver report. No BAR read, mapping, PCI command write, reset, PHY, or DMA. | After bind |
| 1 | Read-only BAR discovery, BootInfo mapping/address validation, and bounded PCI memory-space/bus-master setup. | After BAR/PCI |
| 2 | One minimal read-only `STATUS` MMIO probe and all-ones validation. | After MMIO probe |
| 3 | Existing reset sequence with strict 100,000-iteration bound and interrupt masking. | After reset |
| 4 | `RAL0`/`RAH0` MAC acquisition and station-address validation. | After MAC |
| 5 | Existing `CTRL.SLU|ASDE` setup and PHY 1 MDIC reads of registers 2, 3, and 26, each bounded. | After PHY/MDIC |
| 6 | MTA clear, static RX/TX ring setup, DMA address programming. NIC causes stay masked. | After rings |
| 7 | NIC active/registration and cached status publication. Hardware causes remain masked. | After registration |
| 8 | Handler registration is recorded before input; actual minimum IMS enablement is deferred until main-loop-ready. | After deferred enablement |

If any attempted operation fails, the exact stage/operation is logged, the NIC is marked inactive and unregistered, hardware causes are masked whenever a Stage 3+ MMIO path was reached, and boot continues. `netdiag`/`nicinfo` retains the identity, stage, ring state, interrupt-mask state, and last failure.

## Diagnostics

The expected serial shape for a successful frontier is:

```text
[AIDA-I219-P5] loader-stage=2
[AIDA-I219-P5] stage=0 enter
[AIDA-I219-P5] stage=0 complete
[AIDA-I219-P5] stage=1 enter
[AIDA-I219-P5] bar=...
[AIDA-I219-P5] pci-command=...
[AIDA-I219-P5] stage=1 complete
[AIDA-I219-P5] stage=2 enter
[AIDA-I219-P5] mmio-status=...
[AIDA-I219-P5] stage=2 complete
[AIDA-I219-P5] stage=2 complete; bring-up intentionally stopped
```

Reset and MDIC emit one begin/complete or timeout/error line per operation boundary; polling iterations are not logged. The shell diagnostic reports `I219 Phase 5 stage`, `NIC mask`, ring state, and `Last init failure`.

## Test results

The following repository checks were run against the Phase 5 implementation. The ISO rows below are tied to the exact source commit recorded in each manifest.

| Check | Result |
|---|---|
| Focused network diagnostics | PASS — `tests/network_diagnostics_test.cpp` hosted build and run |
| Canonical AMD64 build | PASS — `build.ps1 -Arch amd64 -I219Phase5Stage 2`; default Stage 8 kernel build also passed during kernel smoke restoration |
| Hosted Navigator smoke | PASS |
| Kernel Navigator smoke | PASS — deterministic group, all scenarios |
| Release ISO verification | PASS — synthetic FAT pack/verify plus all four Phase 5 ISO packages |
| QEMU E1000 regression | PASS — Stage 2 and Stage 7 release ISOs reached desktop and main loop with QEMU E1000 |
| virtio-rng regression | PASS — transitional virtio-rng initialized and secure entropy selected in kernel smoke/QEMU boot |
| `git diff --check` | PASS |

The final report must distinguish automated results from physical results. A QEMU pass does not establish I219 physical success.

## Generated ISO matrix

The reproducible generator is `scripts/create-phase5-i219-isos.ps1`. It builds and packages unique PyCdlib-backed AMD64 UEFI images for the requested stage list. The final matrix is recorded here after the images are generated:

| Stage | ISO path | Size | SHA-256 | Source commit |
|---:|---|---:|---|---|
| 2 | `D:\dev\guideXOSServer_NAVIGATOR_IMPROVEMENTS\dist\guideXOS-Server-v0.1.0-phase5-aida-i219-stage2-amd64.iso` | 90,245,120 | `e523271b61b2725433da2dfd5af954fc787e78492ad32c63bbc314539e282045` | `f3ebc5b358dcba085dd5162e6af08113f7473195` |
| 3 | `D:\dev\guideXOSServer_NAVIGATOR_IMPROVEMENTS\dist\guideXOS-Server-v0.1.0-phase5-aida-i219-stage3-amd64.iso` | 90,245,120 | `bcea06fa672390a1bd92f6bc1162e143b33dd89bf6e93c986c7c7e4ef8990c4c` | `f3ebc5b358dcba085dd5162e6af08113f7473195` |
| 5 | `D:\dev\guideXOSServer_NAVIGATOR_IMPROVEMENTS\dist\guideXOS-Server-v0.1.0-phase5-aida-i219-stage5-amd64.iso` | 90,245,120 | `0e7fa021c88d93297cfa5f7ba8f6c4bd7a1b1db587d3bdce71a7c84819901014` | `f3ebc5b358dcba085dd5162e6af08113f7473195` |
| 7 | `D:\dev\guideXOSServer_NAVIGATOR_IMPROVEMENTS\dist\guideXOS-Server-v0.1.0-phase5-aida-i219-stage7-amd64.iso` | 90,245,120 | `2f0a109f0d08d885c3fe8aa39290685d43858c5d0f281001c81824bda631ae97` | `f3ebc5b358dcba085dd5162e6af08113f7473195` |

Every ISO also has a same-base `.sha256` file and `.manifest.json` containing the stage selector and source commit.

## Manual AIDA_LPT procedure

1. Test Stage 2 with Ethernet connected, record serial output through `stage=2 complete`, and verify UEFI, desktop, mouse, keyboard, Start menu, and Shell.
2. Repeat Stage 2 with Ethernet disconnected. A difference indicates a platform/firmware interaction outside the expected no-link dependency.
3. Test Stage 3. If Stage 2 is usable and Stage 3 loses input, the reset boundary is implicated. If Stage 3 is usable, continue to Stage 5.
4. Test Stage 5. If Stage 3 is usable and Stage 5 fails, the MAC/PHY/MDIC region is implicated; record the last successful MDIC line and any timeout/error.
5. Test Stage 7. If Stage 5 is usable and Stage 7 fails, inspect MTA/ring/DMA setup; the NIC remains interrupt-masked, so an interrupt storm is less likely.
6. Only after Stage 7 is usable, test Stage 8. If Stage 7 is usable and Stage 8 fails after the main-loop checkpoint, the interrupt enablement/routing or interrupt handler boundary is implicated.
7. At each usable desktop, run `netdiag`/`nicinfo` and record the reported stage, mask state, cached link, ring state, and last failure. Do not interpret a stopped stage as Ethernet functionality.

## Interpretation table

| Physical result | Interpretation |
|---|---|
| Stage 2 freezes | Identity/BAR mapping/status read or a pre-kernel firmware/device interaction; not reset, PHY, DMA, or NIC IRQ enablement. |
| Stage 2 works; Stage 3 freezes | Reset sequence or reset completion/readback boundary. |
| Stage 3 works; Stage 5 freezes | MAC/PHY/MDIC boundary; inspect exact MDIC operation, PHY address 1, and register 26 assumptions. |
| Stage 5 works; Stage 7 freezes | MTA/ring/DMA programming or device state after ring setup; interrupts are still masked. |
| Stage 7 works; Stage 8 freezes before main-loop-ready | Handler registration/PIC routing or another pre-checkpoint ordering issue. |
| Stage 8 freezes after IMS enablement | Hardware interrupt assertion, interrupt routing, acknowledgement, mask choice, or handler behavior. |
| A stage times out but desktop remains usable | Desired fail-safe behavior; the logged operation is the identified boundary. |
| All stages reach desktop and input remains usable | No freeze reproduced in this test set; networking/DMA/link functionality remains unproven. |

## Remaining unknowns

The implementation cannot distinguish a hardware MMIO bus fault that never returns from a software exception without a platform serial fault report or debugger evidence. AIDA_LPT testing is also required to determine whether the device uses legacy INTx, MSI, or MSI-X behavior in this firmware configuration, whether PHY address 1/register 26 are valid on this exact I219 stepping, and whether the kernel image physical placement is DMA-visible. The most likely Phase 4 freeze mechanism from source evidence is early I219 interrupt activity combined with the pre-input ordering, potentially amplified by MDIC work in the IRQ handler; Stage 7 versus Stage 8 is designed to test that hypothesis.
