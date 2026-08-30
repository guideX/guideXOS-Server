# AIDA_LPT Intel I219-LM Bring-Up — Phase 7

Status: the exact I219 PCH reset repair and staged diagnostic paths are implemented and have passed source/build validation. Physical Phase 7 MAC, PHY, DMA, registration, and packet-traffic validation remains required. This document makes no functional Ethernet claim without AIDA_LPT packet evidence.

Repository: `D:\\dev\\guideXOSServer_NAVIGATOR_IMPROVEMENTS`
Branch: `NAVIGATOR_GENERAL_IMPROVEMENTS`
Starting Phase 7 HEAD: `46e1621db2b2e1f20f6f50d45e3b307d83184cec`

## 1. Hardware scope and physical authority

AIDA_LPT contains the exact Intel Ethernet device targeted by this phase:

- PCI identity: `8086:156F`
- subsystem: `103C:8079`
- revision: `21`
- family: Intel I219-LM, PCH SPT

The Intel Wi-Fi device `8086:24F3`, subsystem `8086:0010`, revision `3A` remains identity-only and unsupported. This phase adds no Wi-Fi support.

Phase 6 physical evidence is authoritative:

| Phase 6 stage | Cumulative operation | AIDA_LPT result |
|---:|---|---|
| 1 | IMC interrupt mask and ICR cause drain | PASS |
| 2 | Stage 1 plus `RCTL = 0` | PASS |
| 3 | Stage 2 plus `TCTL = 0` | PASS |
| 4 | Stage 3 plus `CTRL` read | PASS |
| 5 | Stage 4 plus exact `CTRL <- ctrl \| CTRL.RST` write | PASS |
| 6 | Linux-informed PCH reset candidate: mask/drain, `RCTL=0`, `TCTL=PSP`, `STATUS` flush, approximately 10 ms pre-delay, `CTRL.RST`, approximately 20 ms post-delay, bounded poll, final mask/drain | PASS; desktop remained interactive |

The Phase 6 image intentionally stopped before complete MAC/PHY/DMA/network initialization, so networking was unavailable. The old generic reset boundary remains defective, but the `CTRL.RST` write itself is not the physical freeze cause. The image also survived without an Ethernet cable.

## 2. Permanent reset repair

For exact PCI device `8086:156F`, `kernel/core/nic.cpp` now uses the physically proven PCH ordering:

1. Mask interrupt causes with `IMC`; drain pending causes by reading `ICR`.
2. Disable receive with `RCTL = 0`.
3. Set `TCTL = E1000_TCTL_PSP`, preserving the PCH-safe transmitter state used by Phase 6.
4. Read and flush `STATUS`; reject an all-ones read.
5. Apply a finite approximately 10 ms pre-reset delay.
6. Read `CTRL`, reject an all-ones read, and write the preserved value ORed with `E1000_CTRL_RST`.
7. Do not perform an immediate post-write flush/read. Wait a finite approximately 20 ms first.
8. Poll `CTRL.RST` with a finite iteration bound; reject all-ones reads and fail on timeout.
9. Mask and drain interrupts again.

The reset helper is selected only for exact I219 `8086:156F` when the Phase 7 selector is active. Existing QEMU/legacy E1000 devices retain their prior path. Historical Phase 5/6 selectors remain available for regression and boundary isolation.

The old generic path wrote `RCTL=0`, `TCTL=0`, masked/drained interrupts, read `CTRL`, immediately wrote `ctrl | CTRL.RST`, immediately began reset readback, and then continued with generic initialization. The repaired I219 path differs in `TCTL.PSP`, the pre-reset `STATUS` flush, bounded delays, the preserved-control reset write, the deliberate post-write no-read window, and the final bounded completion poll. It is fail-closed: reset failure is logged, interrupts remain masked, I219 bring-up stops, and boot continues.

The implementation deliberately does not add Linux's complete PCH ownership machinery speculatively. In particular, guideXOS does not yet implement `e1000e_disable_pcie_master()`, `FWSM` reset-block interpretation, ICH/PCH software-flag ownership, PHY semaphore ownership, conditional PCH PHY reset, ULP/LANPHYPC handling, or Linux's full NVM/configuration-done/EEE/PHY post-reset sequence. Those mechanisms need a concrete guideXOS abstraction and physical evidence before being added.

## 3. Linux e1000e reference

Behavioral references:

- [`hw.h`](https://github.com/torvalds/linux/blob/master/drivers/net/ethernet/intel/e1000e/hw.h): `E1000_DEV_ID_PCH_SPT_I219_LM` is `0x156F`.
- [`netdev.c`](https://github.com/torvalds/linux/blob/master/drivers/net/ethernet/intel/e1000e/netdev.c): the PCI table selects `board_pch_spt`.
- [`ich8lan.c`](https://github.com/torvalds/linux/blob/master/drivers/net/ethernet/intel/e1000e/ich8lan.c): `e1000_pch_spt_info` selects the `e1000_pch_spt` family and `e1000_reset_hw_ich8lan`.
- [Intel Ethernet Connection I219 Datasheet](https://cdrdv2-public.intel.com/612523/ethernet-connection-i219-datasheet.pdf): PCH I219 register behavior and MDIC context.

Linux confirms that PCH SPT reset still issues `CTRL.RST`, but only as part of a coordinated PCH flow. It uses `TCTL.PSP`, a `STATUS` flush, timing around the reset, ownership checks, and a deliberate delay because an immediate flush after reset can hang the hardware. Phase 7 adopts only the physically proven MAC-reset subset. SWFLAG/FWSM/PHY-semaphore machinery is not copied without evidence.

## 4. MAC stage

The Phase 7 MAC path reads `RAL0` and `RAH0` after the repaired reset. It validates that the six-byte station address is neither all zero, all ones, nor multicast, and logs the source and value:

```text
[AIDA-I219-P7] mac=XX:XX:XX:XX:XX:XX valid=yes source=RAL0/RAH0
```

An invalid address logs `mac-invalid`, stops the selected I219 stage, leaves interrupts masked, and continues boot. No EEPROM/NVM fallback is introduced in this stage. Stage 1 stops before PHY access.

## 5. PHY/MDIC stage

Stage 2 performs read-only, bounded MDIC transactions using the current PCH assumptions:

- PHY address: `1` (`0x01`)
- PHY ID registers: `2` and `3`
- status register: `26` (`0x1A`)

The code requires MDIC ready and no MDIC error, rejects all-ones responses, rejects zero PHY IDs, and abandons bring-up on timeout or invalid data. It logs one concise identity and status record:

```text
[AIDA-I219-P7] phy-id=0x....:0x.... address=0x01 valid=yes
[AIDA-I219-P7] phy-status=0x.... link=up|down
```

No PHY programming, PHY reset, semaphore acquisition, or speculative register interpretation is performed. A status value with link down is structurally valid and remains a cached link-down state; a failed transaction is a hard stage failure. No interrupt handler performs MDIC polling.

## 6. DMA audit and ring stage

Before descriptor programming, the implementation validates the existing bare-metal address model:

- virtual-to-physical conversion is `KernelPhysicalBase + (virtual - 0x100000)`, matching the loader's linked-image mapping;
- descriptor rings and buffers are statically allocated and 16-byte aligned;
- RX/TX descriptors are 16 bytes;
- RX and TX rings contain 128 descriptors;
- RX buffers are 2048 bytes and TX buffers are 1514 bytes;
- ring lengths and descriptor base addresses use the existing 64-bit register programming;
- translation rejects zero, alignment failure, arithmetic overflow, and address-range overflow;
- translated RX ring, TX ring, TX buffer, and every RX buffer are checked for overlap;
- static objects reside in the kernel image, which the loader maps as one visible image range.

The audit found no concrete address-translation defect requiring a broader memory change. The DMA stage remains physically unproven: descriptor ownership, cache coherency, firmware interaction, link-dependent receive traffic, and device visibility of the translated addresses remain hardware questions. Stage 3 programs the rings and required engines with interrupts still masked, logs `rx-ring=ready` and `tx-ring=ready`, then stops.

## 7. Registration stage and interrupt ordering

The compile-time selector is `GXOS_AIDA_I219_PHASE7_STAGE`:

| Selector | Name | Cumulative behavior | Stop/continue behavior |
|---:|---|---|---|
| 0 | reset-only | repaired PCH reset | stop after reset; production-safe default |
| 1 | mac | reset plus MAC read/validation | stop before PHY |
| 2 | phy | reset, MAC, bounded PHY reads | stop before DMA |
| 3 | dma | reset, MAC, PHY, ring programming | stop after rings; interrupts masked |
| 4 | register | all prior steps plus NIC registration state | continue main loop; no interrupt enable |

For exact I219, the interrupt mask remains closed throughout early bring-up. `IMS` is not enabled before `[AIDA-PHASE3] checkpoint=main-loop-ready`; the Phase 7 registration stage intentionally keeps interrupts masked even after registration. Deferred interrupt enable is a no-op for this diagnostic registration path. DHCP remains boot-deferred/manual.

Useful diagnostics include `netdiag`/`nicinfo` state, the Phase 7 markers, the cached PHY ID/status, and `NIC Registered: YES` with `I219 Phase 7 stage: register (registration; IRQ masked)` at Stage 4. A registration result is not packet functionality.

## 8. Diagnostic images

The generator is `scripts/create-phase7-i219-isos.ps1`. It invokes the canonical AMD64 build with `I219Phase5Stage=8`, `I219Phase6Stage=0`, and one Phase 7 selector per image, then packages a unique release ISO without overwriting Phase 5 or Phase 6 artifacts.

| Stage | Artifact |
|---:|---|
| 1 / MAC | `guideXOS-Server-v0.1.0-phase7-aida-i219-mac-amd64.iso` |
| 2 / PHY | `guideXOS-Server-v0.1.0-phase7-aida-i219-phy-amd64.iso` |
| 3 / DMA | `guideXOS-Server-v0.1.0-phase7-aida-i219-dma-amd64.iso` |
| 4 / registration | `guideXOS-Server-v0.1.0-phase7-aida-i219-register-amd64.iso` |

Each artifact has an adjacent `.sha256` and `.manifest.json` under `dist/`. The manifest records stage number/name, source commit, byte size, SHA-256, canonical build arguments, ISO backend, tool versions, and a unique build ID. Artifact-specific byte sizes and hashes are authoritative in those manifests and in the Phase 7 handoff report after packaging.

## 9. Exact AIDA_LPT test order

Reboot between images and capture serial output. Keep the cable disconnected for the reset/MAC/PHY/DMA/registration boundary tests unless a later link observation is explicitly being performed.

1. Re-run the known-good Phase 5 Stage 2 baseline. Verify desktop, mouse, keyboard, Start/Terminal, and `netdiag`.
2. Boot the Phase 7 MAC image. Verify the reset marker, MAC marker, desktop, and input. Expected stop: `mac=... valid=yes`; no PHY marker.
3. Boot the Phase 7 PHY image only if MAC passed. Verify the MAC marker, valid PHY ID, bounded PHY status read, desktop, and input. Expected stop: `phy-id=...`, `phy-status=...`; no ring marker.
4. Boot the Phase 7 DMA image only if PHY was structurally sound. Verify `rx-ring=ready`, `tx-ring=ready`, desktop, and input. Expected stop: DMA ready with interrupts masked.
5. Boot the Phase 7 registration image only if DMA passed. Verify `registered=yes interrupts=masked`, `[AIDA-PHASE3] checkpoint=main-loop-ready`, desktop, input, and `NIC Registered: YES`. DHCP must remain explicitly deferred.
6. Only after Stage 4 is stable, connect Ethernet and observe link state. Then manually run `dhcp /discover`, `ipconfig /all`, and `ping <gateway>` in that order.

At every stage record the last Phase 7 marker, any `mac-invalid`, `phy-timeout`, or `dma-address-invalid` line, any kernel exception, and whether the input/main-loop checkpoints appear. Do not interpret compilation or QEMU success as physical Ethernet success.

## 10. Fail-safe behavior

Reset, MAC, PHY, and DMA failures are finite and fail closed. The driver logs the failure, calls the common initialization-failure path as appropriate, keeps I219 interrupts masked, does not enter synchronous DHCP, and continues boot so the desktop and input remain available for diagnosis. MDIC loops are bounded and are never run from an IRQ handler. Per-iteration logging is avoided.

The permanent default is selector 0: exact I219 receives the proven reset repair and then stops. Stages 1–4 are opt-in diagnostic images until AIDA_LPT proves them safe. Existing legacy/QEMU E1000 behavior is unchanged except for shared descriptor alignment/address-validation correctness.

## 11. Automated validation

The Phase 7 handoff must report focused network diagnostics, canonical AMD64 builds, hosted Navigator smoke, kernel Navigator smoke when the host harness is usable, QEMU E1000, virtio-rng, release ISO structural/boot verification, and `git diff --check`. The previous kernel harness failure (`MSB6001` duplicate `Path/PATH` followed by FileTracker `E_ACCESSDENIED`) is treated as a host/tooling result if it recurs, not hidden as a product result.

The final handoff report records the exact result of each check and the ISO path, size, SHA-256, and source commit. No functional I219 Ethernet claim is made until AIDA_LPT demonstrates packet traffic.

## 12. Incidental lid observation

During Phase 6, AIDA_LPT survived physical lid closure and reopening: the display visibly turned off on closure, returned on reopening, and guideXOS remained alive. This is recorded only as a hardware observation. It is not evidence of ACPI suspend/resume functionality.

## 13. Remaining hardware unknowns

- Whether the inherited firmware/ME state requires PCIe-master quiescence or PCH ownership handling before later initialization.
- Whether the fixed PHY address and registers remain correct on all I219-LM board variants.
- Whether the returned PHY status semantics are sufficient for safe link state caching.
- Whether descriptor ownership, cache behavior, and address visibility are correct on AIDA_LPT under actual traffic.
- Whether RAL0/RAH0 is always provisioned correctly or needs a narrow NVM fallback.
- Whether the PCH PHY/ULP/LANPHYPC path requires a physically demonstrated guideXOS implementation.
- Whether registration, link negotiation, DHCP, IPv4, and packet transmit/receive work on AIDA_LPT.
