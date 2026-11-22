# CPU4 — AMD64 boot page-table capacity

November 22, 2026 (Pacific)

**Outcome A — Stock AMD64 boot recovered.** The standard baseline and inventory ISOs booted through UEFI with the production loader, reached an interactive desktop, and preserved BSP-only execution. The separate protected-task diagnostic qualified its three-cycle CPL3 exit contract. Physical firmware and the live missing-inventory fallback remain unqualified.

## Source and root cause

The clean `CPU_IMPROVEMENTS` checkout started at `6a8263d2` (tree `4844a1941f976987f27064575065c5d4ea5eead8`). The production binaries use `39776d8bf37b370da601e24ba11be4aece787722` (tree `ea88b36bbcf8e4841eba03424136309eb8f8dfa4`). [CPU3R1 report](CPU_IMPROVEMENTS_CPU3R1.md) · [CPU3R1 serial evidence](evidence/cpu3r1-20261122/serial-excerpts.txt)

The stock CPU3R1 loader returned `EFI_OUT_OF_RESOURCES` while installing the kernel virtual alias, before `ExitBootServices`. Its 512-entry array counted every allocated page-table page. The ELF file was about 2.7 MiB, but its loaded span was about 555 MiB, dominated by the zero-initialized `.bss`. For a 4 KiB mapping, each distinct 2 MiB virtual window needs a separate leaf table. The CPU3R1 baseline span of `0x22a8ea60` bytes covered 278 windows at virtual `0x100000` and another 278 at its physical identity address `0x594ce000`. These disjoint aliases required **556 leaf tables** before intermediate tables and other boot ranges. The inventory build had the same window counts. The limit was a genuine shortage for that layout. [Loader ELF and paging code](../guideXOSBootLoader/elf.cpp) · [AMD64 paging reference](https://docs.amd.com/v/u/en-US/24593_3.44_APM_Vol2)

| Range | Purpose and table cost |
| --- | --- |
| Kernel ELF virtual and physical aliases | Both cover the complete load span, including BSS. The virtual mapping serves the linked kernel addresses. The physical identity alias preserves the existing handoff and physical access contract. |
| RAM disk and task-frame arena | 64 MiB and 16 MiB. These cover 33 and nine 2 MiB windows at the CPU3R1 placement. The RAM disk shares an edge window with the physical kernel span. |
| Legacy low memory and allocator windows | The existing spans cover the first 3 MiB and the 64 MiB window starting at 64 MiB. Their overlap with the kernel virtual alias reuses its leaf tables and retains the kernel translation. |
| Stack, BootInfo, memory map, loader image and trampoline | Small, firmware-backed ranges needed for the transition or subsequent kernel use. The stack and trampoline require safe identity mappings when CR3 changes. |
| GOP framebuffer and optional NIC or HDA BARs | Address ranges provided by firmware or discovery. The builder maps their requested 4 KiB coverage. CPU3R1 and CPU4 QAC used standard VGA and no NIC. |
| Boot tree | Root, branches and leaf pages are allocated on demand. They have identity mappings for the kernel’s post-handoff walk. The active tree occupies pages outside the task-frame arena. |

The physical and virtual aliases have different offsets within their 2 MiB windows. A 2 MiB page therefore cannot implement the observed virtual-to-physical translation. Large pages could cover aligned interiors of some identity ranges, but that would add mixed-size mapping and conflict handling to the loader and affect consumers that depend on 4 KiB walks or remaps. This correction preserves the existing page size, supervisor permissions, and MMIO behavior. [Loader handoff mappings](../guideXOSBootLoader/main.cpp) · [Task address spaces](../kernel/core/task_address_space.cpp)

## Correction and handoff

- The loader and kernel share a 1,024-page ceiling for the entire four-level boot tree. The ceiling allows 4 MiB in table pages plus an 8 KiB loader tracking array. Pages are allocated individually as `EfiLoaderData` below the lower canonical identity limit. The table-page list grows while self-mapping and the same ceiling bounds that work. The successful inventory boot used 605 pages, or 2,420 KiB, leaving 419 pages of headroom.
- The builder validates physical bounds, canonical virtual bounds and matching page offsets before writing a range. It preserves the existing present, writable, executable, supervisor-only mappings. Required identity ranges, including table pages, reject a conflicting alias. The pre-existing legacy low-memory and allocator windows keep the kernel mapping where the ranges intersect. The firmware-allocated handoff stack is now placed outside the virtual kernel alias because the old default at 2 MiB was covered by it. The loader uses the actual loaded-image size and maps the executable trampoline explicitly.
- On a build failure the loader releases tracked pages in reverse order and publishes no root. A failed free retains ownership for a retry. If `ExitBootServices` returns an error, the loader can release the tree through UEFI memory allocation services. On successful exit, the kernel receives the root through CR3. Every table remains identity mapped and live, and the protected-task allocator only owns the separately checked arena. The firmware map is captured after table allocation. ACPI inventory capture uses firmware mappings before exit, and BootInfo contains the copied inventory for the kernel. [Boot table builder](../guideXOSBootLoader/paging.cpp) · [UEFI allocation and exit semantics](https://uefi.org/specs/UEFI/2.10/07_Services_Boot_Services.html)
- The kernel distinguishes a reserved-range overlap, a page-table arena overlap, a malformed tree and a tree over budget. Before making task frames available it checks the active boot tree, verifies supervisor-writable identity mappings for the firmware map and arena, and applies the existing firmware descriptor and frame ownership rules. Private task mappings continue to use the existing U/S, W and NX policy and teardown paths. [Kernel boot walk](../kernel/core/include/kernel/amd64_boot_page_tables.h) · [Frame-owner initialization](../kernel/core/amd64_memory_foundation.cpp)

BootInfo v3 retains its 572-byte base, base checksum, bit 3 task arena and bit 4 CPU inventory. The checksummed 2,640-byte inventory extension remains at offset 576 in the same 4 KiB BootInfo page. A kernel with the old 512-page validator can still take its ordinary boot path with the new loader, but can reject task memory for this tree. An old loader still fails when it reaches its own capacity, independent of which kernel is paired with it. The matched build is the qualification unit. Modern Input v4 and Disk Manager assign other meanings to bits 3 and 4. Agree on a shared version, flag registry, extension bounds, and paired-loader fixtures before integrating those branches. [BootInfo](../guideXOSBootLoader/guidexOSBootInfo.h) · [CPU inventory extension](../guideXOSBootLoader/bootinfo_acpi.h) · [Cross-branch comparison](CPU_IMPROVEMENTS_CPU3.md#cross-branch-integration-risks)

## Offline verification

| Check | Result |
| --- | --- |
| AMD64 boot page tables | Exact 511, 512, 513 and 1,024-page loader/kernel fixtures passed. Both reject exceeding 1,024 pages. The CPU3R1 range fixture used 605 pages and passed alias, permission and self-map checks. Allocation failures at five points, failed-free recovery, repeated build/release, malformed ranges, address overflow, page offsets, and conflicting required translations passed. |
| AMD64 CPU1, CPU2 and CPU3 | CPUID 8 fixture groups plus hosted native CPUID and NX. Processor inventory 6 groups. Per-CPU roster 7/7. |
| Protected-task contract, memory foundation and voluntary context switch | All passed. Covered selector, TSS and return-frame contracts, bounded frame ownership and rollback, effective permissions, shared supervisor mappings, repeated teardown, and Microsoft x64 context ABI. |
| Native ELF, ABI and filesystem contracts | 29 policy cases passed, `failures=0 nativeEntryCalls=4`. Native ABI layout and hosted Native filesystem contract passed. |
| PS/2, compositor and MMIO | Keyboard modifiers 39/39, compositor pointer-button contract and MMIO mapping source checks passed. The keyboard build emitted its existing unsigned-comparison warning. |
| Release integrity, packaging and authentication | Integrity 37 assertions. Synthetic FAT packaging and read-only verification passed. Synthetic SSH signature fixture 10/10. |
| QAC ISO-only and artifact isolation | 7/7 and 36/36. |
| QAC launcher contract | CRLF checkout stopped at the known line-ending-sensitive assertion 134. A scratch copy with only the runner normalized to LF passed 166/166. The checked-in runner and fixtures were unchanged. |

The three clean, isolated builds used `scripts/create-release-iso.ps1 -Version 0.3.0 -ReleaseProfile ServerV03PacManOnly -Clean -RequireCleanWorktree` in separate source checkouts. The x64 Release UEFI loader was built by MSBuild with the v143 toolset. The AMD64 kernel used MinGW GCC 15.2.0, Mbed TLS 4.1.0 and TF-PSA-Crypto 1.1.0. PacMan source was exported at the pinned `8fc2aedd603ddc57e492bdc02c99442bb165109b` revision, with `GUIDEXOS_PACMAN_SOURCE_ROOT` set to `D:\dev\pacman\guidexos`. The ordinary baseline used `CPU_INVENTORY_DIAGNOSTIC=0 CPL3_DIAGNOSTIC=0`, inventory used `1,0`, and the independent S22R28 diagnostic used `1,1`.

All three loaders are PE32+ AMD64 EFI applications with identical `.text` SHA-256 `7d05e737021f099b702120605b4e1ff6c2051937523db7f128d9e1d2f5e9d1fd`. All three kernels are AMD64 ELF64 executables at entry `0x100000` with no relocations or imports. The symbol audit classified 52 undefined rows in each stock kernel and 54 in the CPL3 diagnostic as non-loadable symbol-table entries, across 22 names per build. Startup object references confirmed the PIC/PIT and CPU3 code paths, with the inventory reporter and CPL3 entry references present in their selected configurations. The canonical build verified the FAT ESP and its embedding in each El Torito ISO.

| Candidate | ISO SHA-256 | Kernel SHA-256 | Loader SHA-256 |
| --- | --- | --- | --- |
| Baseline | `b0a7301895fa8a4369151a93831358b6de47b2b5bd98c5f5ebdf70b16e539f2f` | `fe8cdc717e9253c9763b152ccda2e0432887ea323bbb19b8e61135cd6a28bf52` | `eab80fa21ba6c1b5fe188eabe79570da4ac3e9222887fd5fe0486f31c96f5de2` |
| Inventory | `b9dfee8abe23f9be5fa77ecbe6e9d4eec8310865895442382e2ee58fafd6e0d0` | `b4257470b5bcd6bf86a31d95088206a7fd499fbc7472d77be9f4d0bc0dbf858f` | `1728e6af18ce2e87dfc5ef55b466f39e2b1da355d87435098ac4481f9053ed72` |
| S22R28 diagnostic | `33b7f89383bf922eafc8c25553e515d3dfab5ed04d1bd087fee5866822d8a47e` | `31d689d1fab07a2319cf9a6747d0114014ab9f3d032265586f14cc42ad110336` | `15700ea3f91591528725546badd69e508349724aad675031008e27bfadf074cd` |

Each ISO is 90,245,120 bytes and contains the same 64 MiB RAM disk, SHA-256 `ead5105aa131dee575e0021fac1650a28330df422bd211ca6baccdd267179f05`.

## Guest qualification

QAC 2.0.0 granted exclusive `guideXOSServer` acceptance reservations. The standard runs used QEMU 11.0.0, `pc,accel=whpx`, two vCPUs, 2 GiB RAM, standard VGA, and disabled networking. The verified OVMF code image (`33090cc07675baa5190d9f1e84bf5176b33bcbfa9bacac522961150cdb6dbb2a`) and each ISO were read-only. The guests used private output and loopback QMP with no writable disks, filesystem sharing or persistent EFI variables. [QAC procedures](QAC-DEVELOPMENT.md) · [ISO-only profile](../scripts/prepare-qac-s22r18-public-preview.ps1)

| Candidate | Exclusive reservation / PID | Live result |
| --- | --- | --- |
| Stock inventory | `19256733007c47f396f6209ec9573f47` / 8544 | Loader 605 tables, kernel walk 605 tables, 4,096 free arena frames. UEFI, desktop, CPU inventory, input and PacMan passed. |
| Stock baseline | `8ba2cee9aeac483a91a59f8b1ccb94db` / 12476 | Loader 605 tables, kernel walk 605 tables, 4,096 free arena frames. UEFI, desktop, input and PacMan passed with the inventory reporter off. |
| S22R28 diagnostic | `4f799798cfd54a9580d1c6a677e1b2b2` / 28440 | 605 boot tables, frame owner ready, three hardware CPL3 entry, exit and cleanup cycles passed. |
| ACPI-disabled inventory control | `0e2d2c8be8414dd1b94ec70d24bc3bc5` / 18360 | Firmware/display initialization did not complete by the 150-second guest deadline. No loader or kernel output was recorded. |

Both stock runs reached `Post-ExitBootServices`, kernel entry and the desktop main loop. Serial showed IDT/PIC initialization, PIT IRQ0 registration and advancing heartbeats, PS/2 keyboard and pointer initialization, and `N/A` for the local and I/O APIC features. The observed inventory path used CPU1’s CPUID leaf 1 initial APIC ID `0`. CPU2 handed over an XSDT and a revision-3 MADT with firmware-enabled processor IDs `0` and `1`. CPU3 marked ID `0` as the BSP and reported two descriptors, one online and one offline.

The Super key opened Start. Relative pointer motion and clicks operated the desktop shutdown dialogs. F12 launched Nexgen PacMan, qcode direction input reached the guest, and Escape returned to the desktop with `result=0`, `requested=1`, `fault=none`, and lifecycle cleanup `PASS`. Inventory showed 11 frames and 11 input events. Baseline showed 10 frames and 11 input events. Both shut down from the desktop, with QMP quit and QAC-verified child exit code 0 and release. The runner’s separate public-preview PacMan classification was `INCOMPLETE` because the ordinary PacMan-only ISO lacks the preview-specific `[PACMAN-SMOKE]` marker. The observed input, clean exit, cleanup, and screenshots supply the CPU4 interactive evidence.

The separately built diagnostic reported three hardware traps from CPL3 at vector `0x80`. Each trap checked the kernel stack and restored kernel CR3. Each exit reported actual `CS=0x23`, result `42`, and verified cleanup with the task reaped. The final marker was `[CPL3] status=passed cycles=3 nonfaulting=1 preemption=0`. The guest continued through PIT heartbeats, the desktop main loop, keyboard and pointer input, PacMan launch and clean return, and normal desktop shutdown. QAC verified exit code 0 and release. [S22R28 qualification criteria](RELEASE_S22R28_CPL3_GUEST_QUALIFICATION.md)

The missing-inventory attempt used the exact stock inventory ISO and a private QAC profile with only `acpi=off` added to the machine setting and its validation. QMP entered the running state, the screenshot displayed QEMU’s uninitialized-display placeholder, and serial remained empty through the deadline. QAC quit the owned child through QMP and verified exit code 0 and release. The hosted CPU2 and CPU3 fixtures passed the absent and invalid handoff cases. The live control did not reach the code that implements that fallback.

### Evidence locations

Build receipts, compiler and verifier output, exact candidate files, hosted logs, QAC manifests, full serial and QMP traces, and screenshots are retained under `E:\CodexArtifacts\CPU4-6a8263d2`. The three successful runs produced 63, 50, and 49 PIT heartbeat lines for inventory, baseline, and CPL3 respectively. Raw serial files are in each QAC run’s `output/serial.log`.

| QAC run directory under `qac` | Raw serial SHA-256 |
| --- | --- |
| `cpu4-stock-inventory-ede855a7a5bf4f288db04ed739e3fcd3` | `82a72f7f857a4521b1002d5d6f083447f5279f1b6cc9ea5a6c76bed20bac9395` |
| `cpu4-stock-baseline-38631d1e3f3644d5aa67f042c3610739` | `2bea7f5da65fa4158ef052098827cc7dec68dbad069ebf755a0caea09b48495d` |
| `cpu4-cpl3-diagnostic-187c8cff125a4cf4acfdb3a01622b965` | `62a26a99e3b9b29def72ded095fd362dced5a8b4893b233aa26b24cd779a26db` |
| `cpu4-inventory-acpi-off-4139f6d402b14dcca869bcdd6f594801` | `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` (empty) |

At 12:29 Pacific, QAC reported zero QEMU processes, reservations, queue entries or blockers, and all four private QMP ports were closed. The before/after preservation check matched 22/22 original artifact hashes, including the accepted S22R23 ISO, tracked ESP inputs, firmware images and test disks. The accepted ISO retained SHA-256 `8d397659e0a6ec8f1be113ae4f5be14dbcaf1ef22ccda41a6e5ee5014919cf40`.

## Limits and CPU5 scope

This qualification covers the two-vCPU QEMU/OVMF configuration. Physical AMD64 firmware, different memory sizes and placements, and failure behavior under real firmware memory pressure need separate coverage. Layouts that exceed the shared ceiling or conflict with required identity ranges fail before handoff. A larger heap or BSS will consume both aliases’ table budgets. The existing diagnostic demonstrates voluntary protected-task exit and reaping, and leaves CPL3 fault recovery and arbitrary user workloads unqualified.

For CPU5, keep execution on the BSP and qualify read-only MADT I/O APIC and interrupt-source override inventory with bounded parsing, absent and malformed data fixtures, and stock guest boots. Resolve the cross-branch BootInfo contract before exchanging additional inventory. Add firmware and memory-placement coverage before revisiting alias reduction, table lifetime or SMP prerequisites. Keep PIC/PIT and PS/2 routing in use throughout that work.
