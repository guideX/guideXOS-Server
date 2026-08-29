# Intel I219-LM Bring-Up — Phase 3

Date: 2026-08-28/29
Target: Intel I219-LM, PCI `8086:156F`, subsystem `103C:8079`, revision `21`
Outcome: **B — major bring-up implemented; physical verification remains**

## 1. Starting repository state

The repository was audited before any file was changed.

| Item | Starting value |
|---|---|
| Path | `D:\dev\guideXOSServer_NAVIGATOR_IMPROVEMENTS` |
| Branch | `NAVIGATOR_GENERAL_IMPROVEMENTS` |
| HEAD | `ce1b1479e9c39b086b16d5b2539ebeacfb64dc70` (`Add bare-metal network bring-up diagnostics`) |
| Upstream | `origin/NAVIGATOR_GENERAL_IMPROVEMENTS` |
| Ahead/behind | `0 / 0` |
| Worktree | clean |

No reset, discard, push, or unrelated rewrite was performed.

## 2. Hardware evidence

The real HP laptop reports the wired controller as:

- vendor/device: `8086:156F`
- subsystem: `103C:8079`
- class/subclass/prog-if/revision: `02/00/00/21`
- current pre-Phase-3 status: `unsupported (identity only)`
- wireless device `8086:24F3` is out of scope

The starting behavior with a cable attached was no NIC structure and `ipconfig /all` reporting no adapter.

## 3. Existing networking architecture discovered

The implementation reuses the existing path:

1. `guideXOSBootLoader/pci.cpp` enumerates PCI class `02`, records network identities, sizes the selected supported Ethernet register BAR, enables PCI memory space and bus mastering, and passes `NicInfo` through BootInfo.
2. `kernel/core/nic.cpp` owns the existing E1000-family NIC abstraction, MMIO access, static descriptor rings, bounded main-loop polling, and IRQ acknowledgement.
3. `kernel/core/ethernet.cpp` handles Ethernet framing and dispatch.
4. `kernel/core/arp.cpp`, `ipv4.cpp`, `udp.cpp`, and `dhcp.cpp` provide the existing ARP/IPv4/UDP/DHCP path.
5. `kernel/core/main.cpp` registers an active NIC, initializes IPv4 and the protocol stack, and invokes the normal DHCP client.
6. `kernel/core/shell.cpp` exposes `lspci`, `netdiag`/`nicinfo`, `ifconfig`, and `ipconfig /all`.

QEMU uses an Intel E1000 device. No exact I219 emulator is available in the repository test path. No parallel I219 network stack was added.

## 4. I219 implementation approach

The match is narrow: Intel vendor `8086` and device `156F`, with Ethernet subclass `00`. The subsystem is reported but is not used as a machine-specific matcher, so the same device implementation remains useful across I219-LM boards without claiming unrelated Intel hardware.

I219 is kept distinct from the older generic E1000 identity path for the hardware-sensitive operations:

- station address from `RAL0/RAH0`, requiring the RAR valid bit;
- PHY management through `MDIC` at PHY address `1`;
- PHY status from register `26`;
- link, duplex, and speed are reported as unknown when the PHY transaction is not trustworthy.

The register choices are based on the [Intel Ethernet Connection I219 datasheet](https://cdrdv2-public.intel.com/612523/ethernet-connection-i219-datasheet.pdf) and the [Intel PCIe GbE Controllers Open Source Software Developer’s Manual](https://www.intel.com/content/dam/www/public/us/en/documents/manuals/pcie-gbe-controllers-open-source-manual.pdf), not an imported GPL driver.

## 5. PCI binding

The bootloader and kernel support tables now include exactly:

```text
8086:156F  Intel I219-LM / PCH LAN
```

The existing supported identities remain unchanged:

```text
8086:100E  QEMU/82540EM E1000
8086:10D3  82574L E1000E
8086:153A  I217-LM
8086:156F  I219-LM
```

The driver name exposed by `lspci`, `netdiag`, and `ipconfig /all` is:

```text
intel-i219-lm (PCH)
```

Unsupported devices remain identity-only. The Intel wireless `8086:24F3` is enumerated as a network-class identity but is not claimed.

## 6. BAR/MMIO handling

The supported Ethernet path now examines all six conventional PCI BAR slots and chooses the first valid 32-bit or 64-bit memory BAR. This is required because a legacy I/O BAR can precede the register MMIO BAR; the QEMU E1000 proof exposed that case during validation.

BAR sizing is bounded and reversible:

- I/O and reserved BAR encodings are rejected;
- zero/all-ones addresses are rejected;
- PCI memory and I/O decoding are disabled during sizing;
- both halves of a 64-bit BAR are restored;
- 32-bit masks are inverted at 32-bit width;
- accepted sizes are power-of-two values from `0x1000` through `0x1000000`;
- no machine-specific address is used.

The bootloader enables PCI memory space and bus mastering, maps the selected BAR through its identity page-table path, and marks that exact range uncached with PCD/PWT flags. The kernel verifies the command bits again before touching the device.

`netdiag` reports the physical BAR address, size, mapped virtual address, PCI command word, CTRL, and STATUS. The bootloader prints the selected BAR number, address, size, and type.

## 7. Reset and initialization sequence

The finite initialization sequence is:

1. Disable RCTL/TCTL and mask interrupts.
2. Drain ICR.
3. Read CTRL and reject an all-ones MMIO response.
4. Set `CTRL.RST` and poll for the self-clearing bit with a 100,000-iteration bound.
5. Re-mask interrupts and drain ICR again.
6. Set link-up/auto-speed bits; do not force full duplex for I219.
7. Read and validate the station MAC.
8. Read I219 PHY IDs and status.
9. Clear the multicast table.
10. Initialize RX and TX rings.
11. Enable the existing interrupt causes only after rings are ready.

Every reset, EEPROM, and MDIC wait has a finite bound. Failures preserve the stage and a short reason, for example `reset timeout`, `invalid MAC in RAL0/RAH0`, or `PHY MDIC timeout or invalid response`.

## 8. MAC acquisition

For I219 the kernel reads `RAL0` and `RAH0` after reset and requires `RAH0.AV`. It rejects:

- all-zero addresses;
- all-FF addresses;
- multicast station addresses;
- all-ones register reads.

The bootloader passes a zeroed MAC field. No fallback or invented MAC is used. A valid value is surfaced through `netdiag` and `ipconfig /all` after successful registration.

## 9. PHY/link implementation

I219 MDIC reads use PHY address `1` and bounded polling of the MDIC ready bit. The initialization path reads PHY ID registers `2` and `3`, then I219 PHY status register `26`.

The diagnostic state is explicit:

- `PHY access: ok` plus `Link: UP` or `DOWN` when the read succeeds;
- `PHY access: failed` and `Link: UNKNOWN` for timeout/error/invalid responses;
- speed and duplex are shown only for a valid link-up status.

The driver does not infer link state from cable presence, CTRL bits, or DHCP activity.

## 10. DMA and descriptor rings

The existing ring design was retained:

- 32 RX descriptors and 8 TX descriptors;
- 16-byte descriptor layout;
- 16-byte ring alignment;
- 2048-byte RX buffers;
- static storage in the kernel image, never stack memory;
- RX/TX base addresses translated using the existing `KernelPhysicalBase` plus the linker’s `0x100000` kernel virtual layout;
- null, underflow/overflow, zero-physical-address, and alignment checks;
- compiler memory barriers around descriptor ownership transitions;
- bounded polling and packet-length validation in the existing RX/TX path.

RX and TX are initialized before the NIC is marked registered. The existing main-loop polling path remains the primary packet drain; the existing IRQ path acknowledges events and refreshes link state.

## 11. NIC integration

The `NICDevice` structure now records binding, MMIO, PCI command, CTRL/STATUS, MAC, PHY, ring, registration, link, and last-failure state. A successful initialization sets:

```text
Driver Bound: YES
RX ring: initialized
TX ring: initialized
NIC registration: YES
```

A failed I219 stage retains the bound identity and precise failure in `netdiag` instead of collapsing back to an absent device. `ipconfig /all` reports “Adapter detected, but the driver is not ready” in that state.

## 12. DHCP/network-stack integration

No I219-specific DHCP or ARP logic was added. Once NIC initialization succeeds, `main.cpp` enters the existing Ethernet → ARP → IPv4 → UDP/DHCP path. If initialization fails, DHCP is not started and no success is claimed.

The QEMU run reached active E1000 registration and the existing DHCP client, but that run did not obtain a DHCP offer. Its static QEMU configuration and Navigator smoke result are not evidence of I219 DHCP success.

## 13. Automated tests

The following checks were run during this phase:

| Check | Result |
|---|---|
| `tests/network_diagnostics_test.cpp` hosted compile/run | PASS; exit code 0 |
| Freestanding direct compile of `kernel/core/nic.cpp` | PASS; exit code 0 |
| `git diff --check` | PASS; only normal CRLF normalization warnings |
| MSBuild UEFI bootloader rebuild | PASS; only pre-existing `/sdl`, `/Oi` override warnings |
| `pwsh.exe -NoProfile -ExecutionPolicy Bypass -File .\build.ps1 -Arch amd64` | PASS; bootloader, kernel, ESP staging, and QEMU prerequisite checks completed |
| `cmd.exe /c run-qemu.bat` with `-device e1000,netdev=net0` | PASS for boot and existing E1000 registration; terminated after evidence capture |
| `pwsh.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\smoke-navigator-hosted.ps1` | FAIL in unrelated pre-existing Navigator fixture coverage; the log reports missing `table-phase8b.html`/`table-phase8c.html` and later Navigator phase fixtures, while the network-specific checks remained outside this smoke suite |

The hosted assertions cover the new exact I219 match, unsupported-device non-match, MAC validation, descriptor sizes, and power-of-two ring counts.

## 14. Emulated results

The first QEMU run exposed and corrected a real BAR-sizing defect. The final QEMU run showed:

```text
Found 1 PCI network controller(s); supported Ethernet drivers: 1
BAR0: Phys=0000000081040000 Size=20000 (32-bit)
Driver: intel-e1000 family (supported)
NIC MMIO mapped: Phys=0000000081040000 Virt=0000000081040000
[NIC] NIC info found in BootInfo, using mapped MMIO
[NIC] MAC: 52:54:00:12:34:56
[NIC] Link: UP
[NIC] E1000 initialization complete
[KERNEL] NIC active, registering IRQ0B
[NAVIGATOR-SMOKE] result=PASS
```

This proves the BAR handoff, uncached mapping path, existing E1000 initialization, ring setup, NIC registration, IRQ registration, and preservation of the existing Navigator/QEMU boot path. QEMU does not prove the I219-specific MDIC/RAR/PHY path.

The separate hosted Navigator smoke was also run. It was not a networking proof and failed on missing/unavailable Navigator fixture routes (including `table-phase8b.html`, `table-phase8c.html`, and later CSS phase fixtures). Those failures are unrelated to the I219 changes and were not staged.

## 15. Exact physical-test instructions

Use the Phase-3 image/artifacts built from this branch. Connect the Ethernet cable before boot. After the shell is ready, run only:

```text
lspci
netdiag
ipconfig /all
```

If `ipconfig /all` shows a DHCP gateway, run:

```text
ping <gateway shown by ipconfig /all>
```

`nicinfo` is an alias for `netdiag` if needed. Do not run the wireless device through this test.

The levels are:

| Level | Evidence required |
|---|---|
| 1 — PCI driver binding | `lspci` shows `8086:156F` and `intel-i219-lm (PCH) (supported)`; `netdiag` says `Driver Bound: YES`. |
| 2 — MMIO/device initialization | Nonzero selected BAR and size; `MMIO Mapped: YES`; PCI command has memory and bus-master bits; CTRL/STATUS are not all-ones; init reaches beyond reset with no failure. |
| 3 — MAC + PHY/link | Real nonzero unicast MAC; `PHY access: ok`; `Link: UP` or `DOWN`; speed/duplex only if reported valid. |
| 4 — NIC registration | `Driver Ready: YES`, RX/TX rings initialized, `NIC registration: YES`; `ipconfig /all` names an Ethernet adapter instead of reporting no adapter. |
| 5 — Ethernet TX/RX | TX completed and RX accepted counters increase after a gateway ping or other LAN traffic; no growing TX/RX error counters. |
| 6 — DHCP/IPv4 | `DHCP State: BOUND`/applied lease, IPv4 address, subnet mask, gateway, and DNS appear in `ipconfig /all`; gateway ping succeeds. |

## 16. Remaining hardware uncertainty

The exact I219-LM path has not yet run on the HP laptop after this code change. The following points remain hardware-dependent:

- actual I219 BAR address/size and PCI command state;
- whether the device accepts the reset sequence without a PCH-specific prerequisite;
- whether RAR0 is populated and its valid bit is set;
- MDIC readiness, PHY IDs, and PHY status register behavior;
- DMA reachability of the kernel-image-backed rings on the laptop’s physical memory map;
- link negotiation and the physical switch/cable;
- real TX/RX completion and DHCP response.

If the boot stops at a stage, preserve the complete `netdiag` output. `Last initialization failure` is intended to identify the next safe frontier without inventing MAC, link, or DHCP state.

## 17. Outcome classification

**Outcome B.** The implementation advances guideXOS from identity-only handling to a real, narrow I219 binding and a complete bounded initialization path covering PCI activation, BAR/MMIO mapping, reset, RAR MAC acquisition, MDIC PHY/link reads, descriptor-ring setup, NIC registration, and normal-stack integration. The existing QEMU E1000 path was revalidated and remains operational. Exact physical I219 proof, TX/RX evidence, and DHCP success remain pending on the HP laptop.
