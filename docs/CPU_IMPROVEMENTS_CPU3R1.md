# CPU3R1 · AMD64 guest qualification

November 22, 2026 (Pacific)

**Outcome B — Partial guest qualification.** The stock baseline and inventory candidates both returned to the UEFI shell after the loader failed to map the kernel. Private loader controls passed that stage and supplied live CPU discovery, fallback, PIC/PIT, desktop, input, and Native ELF evidence. Stock guest acceptance remains blocked on the loader mapping failure.

## Source and scope

The checkout `D:\dev\guideXOSServer_CPU_IMPROVEMENTS` started clean on `CPU_IMPROVEMENTS` at `34be9d34a99503932e7204e4b3a810a8c3f768bb`, Git tree `38df7d5b98c96e8c45f188dd666f1ab35be683b1`. The branch was two commits ahead of the local remote-tracking ref. The qualified source includes CPU1 `f85566375c483fca04230408b64878c8327eac65` and CPU2 `1892297ed3bc87e8520c6671e483928b6a4a910d`. [CPU1](CPU_IMPROVEMENTS_CPU1.md) · [CPU2](CPU_IMPROVEMENTS_CPU2.md) · [CPU3](CPU_IMPROVEMENTS_CPU3.md)

| Source inspection | Finding |
| --- | --- |
| Firmware and BootInfo | CPU2 reads the ACPI root and MADT while UEFI mappings are active, from ranges checked against the firmware memory map. BootInfo v3 keeps its 572-byte base and checksum. Bit 4 advertises the separately checksummed 2,640-byte extension at offset 576 of the same mapped page. The kernel checks the parent header, extension, and record bound before consuming it. [Loader](../guideXOSBootLoader/main.cpp#L634) · [Handoff](../guideXOSBootLoader/bootinfo_acpi.h) |
| Kernel mappings and protected tasks | CPU2 removes the speculative post-handoff RSDP mapping. Firmware table addresses in the extension are provenance. CPU1 reuses the CPUID NX query in the existing memory and gated MMIO paths. The task arena stays at bit 3, and both candidates leave the CPL3 experiment disabled. [Memory foundation](../kernel/core/amd64_memory_foundation.cpp) · [MMIO](../kernel/core/mmio.cpp) · [Boot gates](../kernel/Makefile#L99) |
| CPU identity and execution | CPU1 captures CPUID on the executing CPU. The guest inventory reporter consumes the topology/APIC identity from that snapshot. CPU3 installs the BSP at logical index 0 and admits only that slot to `Online`. Firmware entries mapped to other slots start `Offline`, including firmware-enabled entries. Missing or rejected inventory leaves a synthetic BSP at index 0. [CPUID](../kernel/arch/amd64/cpu_capabilities.cpp) · [Reporter](../kernel/arch/amd64/boot_processor_inventory.cpp) · [Roster](../kernel/arch/amd64/per_cpu.cpp) |
| Interrupts, desktop and Native ELF | The CPU commits leave the PIC/PIT, PS/2 and desktop implementations and the synchronous Native ELF entry/return path intact. AMD64 startup initializes the CPU roster before the existing IDT/PIC path, with PIT on IRQ0, keyboard on IRQ1 and pointer on IRQ12. CPU inventory adds no AP startup, IPI, APIC mode transition, interrupt routing, or preemption path. [Entry](../kernel/core/main.cpp#L777) · [PIT](../kernel/core/pit.cpp) · [Native ELF](../kernel/core/native_elf_baremetal.cpp) |

## Hosted verification

| Check rerun on `34be9d34` | Result |
| --- | --- |
| `test-amd64-cpu-capabilities.ps1` | Eight CPUID fixture groups plus the hosted native CPUID adapter and NX query passed. |
| `test-amd64-processor-inventory.ps1` | Six groups passed, including UEFI readable ranges, MADT data, BSP matching, and missing or invalid handoffs. |
| `test-amd64-per-cpu.ps1` | Seven groups passed, including firmware/online state separation and synthetic BSP fallback. |
| `test-amd64-protected-task-contract.ps1`, `test-amd64-memory-foundation.ps1`, `test-amd64-context-switch.ps1` | Passed. Covered selector/TSS/IRET and exit contracts, frame ownership and mapping/teardown, and voluntary Microsoft x64 register and stack behavior. |
| `test-native-elf-security-policy.ps1` | 29 cases passed, with `failures=0 nativeEntryCalls=4`. |
| `run-ps2keyboard-modifier-test.ps1`, `run-compositor-pointer-buttons-test.ps1` | PS/2 39/39 and pointer-button contract passed. The existing unsigned comparison warning was emitted. |
| `smoke-mmio-mapping.ps1` | Passed its source checks. |
| QAC ISO-only profile and launcher fixtures | ISO-only 7/7. The broader launcher test stopped at assertion 134 on a regex with a literal `}\n` boundary against a CRLF checkout. The same suite passed 166/166 against a scratch copy with only the runner line endings normalized. The checked-in runner and tests have no CPU-branch changes. |

## Builds and artifact identity

The two candidates use `create-release-iso.ps1 -Version 0.3.0 -ReleaseProfile ServerV03PacManOnly -Clean` in private checkouts of the starting commit. Both use AMD64, the pinned Mbed TLS 4.1.0 and TF-PSA-Crypto 1.1.0 profile, an x64 Release UEFI loader (MSBuild 18.11.1, v143), and a MinGW GCC 15.2.0 freestanding kernel. `CPL3_DIAGNOSTIC=0` in both. `CPU_INVENTORY_DIAGNOSTIC=0` selects the baseline and `1` selects the inventory candidate. Each build generated PacMan, the boot RAM disk, and a FAT32 El Torito UEFI ISO in its own staging tree. The baseline and inventory loader `.text` sections have identical SHA-256 (`7ca7db418f91a320c90448a9b1497b0f7d7a9083a4b9362cd5067a49068c8e8a`).

The missing-inventory control pairs the **exact inventory kernel and RAM disk** with an x64 Release BootInfo v3 loader built from `f8556637`, before CPU2 added inventory export. A stock version of that control was staged offline. After the stock mapping failures, the three lab boots used private loaders with `MAX_PT_PAGES` raised from 512 to 1,024, one from `34be9d34` and one from `f8556637`. The repository's FAT and ISO tools created and verified the controls separately. Their scratch ESP receipts identify the loader source and capacity change. The report commit carries evidence only.

| Candidate | Artifact | Bytes | SHA-256 |
| --- | --- | ---: | --- |
| Baseline | `kernel.elf` | 2,840,016 | `3fe2d71420ba18ad83f7388457d34d8f301fba54d590b1a810166fa518d64806` |
| Baseline | `BOOTX64.EFI` | 61,952 | `303a88c4af55ddebaa803bccfafaf37eecd1494d2d499cc6d8145abd819f84df` |
| Baseline | ISO | 90,245,120 | `3bebb490d1b7a74cad3f1615d3940d3d42269ca5e54e6d67a72a4bf422e8c485` |
| Inventory and control | `kernel.elf` | 2,848,600 | `7b2e8fcb5b1e4667fe9a71ea97a443e6cdb52520286519dcab49fd20d3fa413b` |
| Inventory | `BOOTX64.EFI` | 61,952 | `142cd2acd26cccc99632f3072a3412484d04a88b2f19f1c66a10ccb0b1f8b9be` |
| Inventory | ISO | 90,245,120 | `2d8ad5c7666ea623eab00e6df72bde87f0edf761285575f834bb9d86e2b2c4e7` |
| Stock pre-CPU2 control, staged offline | `BOOTX64.EFI` | 57,856 | `a957f802b9a0ca3de5f1b4a6ebee2003c0e848466c772632536e16d67878a94e` |
| Stock pre-CPU2 control, staged offline | ISO | 90,245,120 | `8ea18bee475ccecd744f3f5068762cb1508588d6b208befd658639316a9995c3` |
| Capacity controls, baseline and inventory | `BOOTX64.EFI` | 61,952 | `8850c39d3e82909010ef1bcc242261d0269f8fe7fb2f704525716c4bb04b8665` |
| Capacity control, baseline | ISO | 90,245,120 | `cdc5e3eec7462cde5109199f3d5613eb93a8e98b86882ff6f78723f8f06034c8` |
| Capacity control, inventory | ISO | 90,245,120 | `baf709553a90f1f65bc11bf467b27ede16e54c574060b3d149708c98f6cd4ae8` |
| Capacity control, pre-CPU2 | `BOOTX64.EFI` | 57,856 | `e70c8bdc9f082fb486e3306cf62699b09cc0e790b62b7cc74f68017f2fb8e06d` |
| Capacity control, pre-CPU2 | ISO | 90,245,120 | `bdf153a0cf436203014bee68e3481d82f7c68e4539a21686cd2d71a214971614` |
| All runs | `ramdisk.img` | 67,108,864 | `ead5105aa131dee575e0021fac1650a28330df422bd211ca6baccdd267179f05` |

Both kernel binaries are AMD64 ELF64 executables with entry `0x100000` and no relocations or dynamic loader dependency. The exact PE/ELF build audit classified 52 undefined rows across 22 names as non-loadable symbol-table entries, with no unexplained required unresolved dependency. Each loader is an AMD64 PE32+ EFI application. Startup object references confirm CPU3 initialization, PIC setup and the PIT handler in both builds. Only the inventory `per_cpu.o` references the CPU2 reporter. Neither startup object references the CPL3 diagnostic. Disassembly of the inventory CPUID and CPU3 objects showed CPUID reads and no MSR/XCR writes, descriptor-table loads, TLB invalidation, or interrupt-flag instructions.

## QAC reservation and guest evidence

QAC 2.0.0 reported Online with zero QEMU processes, reservations and queued or blocked requests at preflight. An owner-verified `guideXOSServer` acceptance reservation (`386eaec3fd814fc19941d0c8660329d8`) took the lane during the builds. CPU3R1 used the documented queue and checked only the coordinator's reservation metadata for that workstream. Its own request queued at position 1 at 10:22:43 Pacific and received the exclusive grant at 10:25:20 Pacific. [QAC operating procedure](QAC-DEVELOPMENT.md) · [UEFI runner and ownership checks](QAC3C3-UEFI-QUALIFICATION.md)

The selected existing ISO-only acceptance profile uses QEMU 11.0.0, `pc,accel=whpx`, two virtual logical processors (`-smp 2`), 2 GiB RAM, standard VGA, and disabled networking. It attaches the candidate ISO and QEMU's `edk2-x86_64-code.fd` read-only. Firmware SHA-256 is `33090cc07675baa5190d9f1e84bf5176b33bcbfa9bacac522961150cdb6dbb2a`. The profile uses no separate firmware-variable store or writable guest disk. Its private loopback QMP endpoint and serial and debug paths are per run. [ISO-only profile](../scripts/prepare-qac-s22r18-public-preview.ps1)

| Run in execution order | Exclusive QAC reservation | QEMU PID | Guest result |
| --- | --- | ---: | --- |
| Stock baseline | `493f4011feb74426aa9c304be286bdba` | 6692 | UEFI mapping failure, then shell |
| Stock inventory | `a5d9e0be42284c3caca4059227e5f764` | 15812 | Same UEFI mapping failure |
| Capacity control, inventory | `af0d510a366e4b9fa41c2bcb7d958759` | 12308 | Two discovered, one online, desktop and Native ELF interactive pass |
| Capacity control, pre-CPU2 | `dd1a99154ecd4d74b92637ea16d07c3c` | 28596 | Optional handoff absent, one online, desktop and Native ELF interactive pass |
| Capacity control, baseline | `8aee2c4882e84a338066c7c0dcb7b984` | 26540 | Reporter off, desktop and Native ELF interactive pass |

QAC verified the direct child, loopback QMP identity, exit code 0 and `Released` state for all five runs. The stock failures were closed through owned QMP, and the three controls shut down from the guest desktop before QMP quit. At 10:35 Pacific the coordinator reported zero QEMU processes, reservations, queued requests and blockers. All five private QMP listeners were closed. The shared launch profile was used as-is. [Reservation records and raw log hashes](evidence/cpu3r1-20261122/qac-reservations.txt)

### Stock boot blocker

Both stock ISOs loaded through OVMF, and the loader opened the AMD64 ELF and the 64 MiB RAM disk. Both reached `Building identity page tables with 12 ranges...`, then printed `Failed to map kernel virtual range`. OVMF reported `Out of Resources` and entered the UEFI shell. Neither reached `ExitBootServices`, kernel entry, PIT or PS/2 initialization, or the guideXOS desktop. The first boot's early keyboard and pointer commands reached firmware only. [Serial excerpts](evidence/cpu3r1-20261122/serial-excerpts.txt) · [Stock baseline screenshot](evidence/cpu3r1-20261122/stock-baseline-uefi-shell.png)

The stock loader tracks at most 512 page-table pages and uses 4 KiB mappings for both the identity alias and the virtual kernel range. For the baseline's observed kernel physical span starting at `0x594ce000` and virtual span starting at `0x100000`, the two disjoint spans cover 278 distinct 2 MiB windows each. Those mappings alone need 556 leaf page tables, before counting control tables and the other regions. The cap and mapping code precede CPU1–CPU3. The matching-loader control used the same QAC profile, kernel, and RAM disk, with the loader's tracking capacity raised from 512 to 1,024 in scratch. It advanced past this failure. The control demonstrates the capacity dependency for these binaries and addresses. [Page-table allocator and mapping loops](../guideXOSBootLoader/paging.cpp) · [Loader mapping order](../guideXOSBootLoader/main.cpp#L1239)

### Guest evidence with the scratch loader

The inventory control ran the unmodified inventory kernel with the capacity-adjusted matching loader. CPU1's topology output used CPUID leaf 1, with initial APIC ID `0`. The checked CPU2 handoff reported an XSDT and revision-3 MADT, two stored Type 0 processor records, and two firmware-enabled records. The MADT records were APIC IDs and ACPI UIDs `0` and `1`. Only ID `0` was marked the executing boot processor. CPU3 reported `roster=mapped`, two descriptors, one online, and one offline, consistent with two QEMU vCPUs and BSP-only guideXOS execution. Other CPUID feature fields were exercised by the hosted fixtures.

The guest reached the 1280×800 desktop and its main input loop. Serial confirmed IDT/PIC initialization, successive IRQ0 PIT heartbeats and PS/2 setup. Local APIC and I/O APIC reported `N/A` in the feature listing. The Super key opened Start. Relative pointer events moved the cursor and clicked the shutdown UI. The F12 shortcut reached the normal Native ELF dispatcher and displayed Nexgen PacMan. Directional input and Escape were followed by `result=0x00000000 requested=1 fault=none`, 14 presented frames, 11 input events, and a resource-cleanup `PASS`. The desktop rendered after return and accepted further input and a normal shutdown. The runner's public-preview PacMan verdict was `INCOMPLETE` because the ordinary PacMan-only build does not emit the preview-only `[PACMAN-SMOKE]` marker. The interaction, return and cleanup evidence above is from the actual guest.

The capacity-adjusted pre-CPU2 loader reached the same kernel with BootInfo v3 and no optional inventory extension. The guest reported `inventory=handoff-unavailable`, `boot-match=inventory-unavailable`, and CPUID APIC ID `0`. CPU3 produced `roster=unavailable`, one online synthetic BSP and zero offline descriptors even though QEMU provided two vCPUs. PIT, the desktop and both input paths continued to work. PacMan returned successfully after nine frames and nine input events, completed cleanup, and the desktop shut down normally.

The capacity-adjusted matching loader also booted the diagnostic-off baseline kernel to the desktop. The reporter stayed silent. The guest delivered 35 serial PIT heartbeats and responded to keyboard and pointer input. PacMan returned successfully after seven frames and nine input events and the desktop shut down normally. The QEMU console warned `Ignoring request for interrupt vector 0` during firmware startup in all five runs. The controls completed without an observed kernel fault, hang, or loss of interactivity. [Serial excerpts](evidence/cpu3r1-20261122/serial-excerpts.txt) · [Inventory control running Native ELF](evidence/cpu3r1-20261122/inventory-control-native-elf.png)

All three controls reported `[MEMORY-OWNERSHIP] status=overlapping-reservation user-mode=disabled`. The kernel uses that message for both explicit range overlap and a page-table-tree check that also stops above 512 table pages. The observed layout exceeds the latter bound on its own. The log does not identify which check rejected first. Protected-task frame-owner readiness remains unqualified, and the CPL3 diagnostic was disabled for each boot. [Kernel page-table walk](../kernel/core/amd64_memory_foundation.cpp#L99) · [Serial excerpts](evidence/cpu3r1-20261122/serial-excerpts.txt)

The full serial and QEMU debug logs, QMP transcripts, screenshots, build receipts, and image hashes are retained under `E:\CodexArtifacts\CPU3R1-18902cf5`. The before/after capture matched 22 of 22 original media hashes, covering the accepted S22R23 ISO, the checkout's ESP files, both firmware images and the two test-disk images. The accepted ISO retained SHA-256 `8d397659e0a6ec8f1be113ae4f5be14dbcaf1ef22ccda41a6e5ee5014919cf40`.

## Integration decision and CPU4 scope

Resolve the shared BootInfo ABI before combining these workstreams. CPU uses v3 bits 3 and 4 for the task arena and CPU inventory. Modern Input's v4 uses those bits for DMA-remapping inventory, and Disk Manager uses them for storage-profile data. Agree on a single flag registry, version and layout, extension bounds, and matching loader/kernel compatibility tests. The timer-driven Native ELF pilot on Security Settings also requires a separate decision about IRQ return, runtime ownership and execution-state saves. [Recorded cross-branch comparison](CPU_IMPROVEMENTS_CPU3.md#cross-branch-integration-risks)

Physical AMD64 firmware coverage remains open. QEMU supplies one firmware and CPUID environment. ACPI table placement and memory types, x2APIC and wider IDs, disabled and hotplug entries, and PCID/NX behavior warrant separate validation. Real hardware routing, per-CPU entry state, allocator synchronization, remote TLB invalidation, and extended-state ownership are prerequisites for any later activation work.

CPU4 should first resolve the loader mapping capacity and the kernel's page-table validation budget together. Account for the actual spans, preserve the trampoline and stack mappings, and distinguish a budget failure from a reservation overlap in diagnostics. Qualify fresh stock baseline and inventory candidates with the matching loader. Once stock boot passes, restrict CPU4 processor work to read-only MADT I/O APIC and interrupt-source override inventory with bounded parsing fixtures. Keep execution on the BSP throughout that qualification, with PIC/PIT on IRQ0 and PS/2 on IRQ1/12.
