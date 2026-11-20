# Server v0.3 — S22R27 Physical Frame Ownership

**Disposition: Outcome A — `FRAME_OWNERSHIP_AND_ADDRESS_SPACE_FOUNDATION_VERIFIED`.** S22R27 adds an authoritative, bounded owner for task frames and page tables. It does not enable CPL3, scheduling, or application termination.

## Starting point

Work started on `main` at `50c6cff0993227de7c582b00eb1daabed62cf816`, following S22R26 Outcome C, `USER_MODE_EXECUTION_PREREQUISITE`. The UEFI boot handoff already supplied an EFI memory map, but the kernel had no physical-frame allocator. The kernel heap is a BSS allocation and does not establish ownership of its backing frames. The MMIO pool is specialized storage, and existing identity mappings do not make arbitrary physical memory safe to reclaim.

The UEFI path uses bootloader-built identity mappings for selected kernel, stack, BootInfo, memory-map, ramdisk, framebuffer, and device ranges. This change does not introduce a general direct map or reclaim memory outside the new arena. Multiboot boot and existing ring-0 application execution remain separate.

## Boot handoff and bounded arena

`BootInfo` is now version 3. In addition to the existing EFI map, it reports the loaded kernel physical span, the boot stack range, and an optional task-frame arena. The loader asks UEFI for 4,096 `EfiLoaderData` pages (16 MiB), rejects and frees a result outside the supported identity range, maps an accepted arena supervisor-writable, and sets the arena-valid flag. Allocation failure is non-fatal: the normal ring-0 boot path continues with protected-task memory disabled.

The kernel validates the BootInfo version and checksum, EFI descriptor count and stride, all ranges and arithmetic, arena alignment and limit, framebuffer bounds, identity-writable mappings for the map and arena, and that the arena does not overlap the kernel image, ramdisk, BootInfo, boot stack, framebuffer, or any active paging-structure page. It accepts the arena only when the EFI map identifies the entire range as loader data. Invalid or unavailable handoff data disables the memory foundation without activating user mode.

## Frame owner contract

The production API is in `kernel/core/include/kernel/physical_frame_owner.h`; its implementation is `kernel/core/physical_frame_owner.cpp`.

| State | Meaning |
| --- | --- |
| `Unavailable` | Outside the arena or not safely backed by the accepted firmware range |
| `Reserved` | Explicitly held from task allocation |
| `KernelOwned` | Kernel storage; never task-releasable |
| `SharedKernelMapping` | Shared kernel translation; not task-owned |
| `Free` | Available within the dedicated arena only |
| `TaskPrivateData`, `TaskPrivateCode`, `TaskPrivateStack` | Task-owned leaf frames |
| `PageTableStorage` | Task-owned private page-table root or child table |

Initialization has fixed bounds: at most 512 EFI descriptors and 4,096 4 KiB frames. It validates descriptor stride, range arithmetic, page counts, physical-address limits, and rejects overlapping firmware descriptors before marking any frame free. Unknown memory, firmware memory outside the arena, device ranges, and all other physical addresses remain unavailable. The fixed owner records live in kernel BSS; initialization does not borrow pages from an uninitialized allocator.

The API allocates one zeroed frame at a time and reports exhaustion distinctly. Range reservation, ownership queries, and permitted task-kind transfers are explicit. Release requires the matching task ID and frame kind, and refuses mapped frames, foreign owners, kernel or shared frames, and double release. Frames are cleared before they become allocatable again. Accounting reports capacity and counts for each owner state. The current critical section disables and restores local interrupts; it is a single-core contract and does not claim SMP synchronization.

The owner is intentionally not a general system allocator. Capacity is capped at 16 MiB, there is no reclamation of the kernel heap or firmware pages, and pages outside the arena cannot be allocated. Those limits avoid guessing that apparently unused RAM is free.

## Runtime boundary

`kernel/core/main.cpp` calls the AMD64 initializer only for a UEFI BootInfo handoff. Initialization reports a bounded accounting line and leaves the existing product path available when the arena is absent or rejected. The frame owner does not allocate for PacMan, alter Native ELF policy, switch CR3, execute user instructions, or grant a general application allocation API.

The hosted regression compiles and exercises the production owner and address-space implementation with callback-backed physical memory. Freestanding linking and symbol audit also pass; the specific evidence and limitations are recorded in [the qualification report](RELEASE_S22R27_MEMORY_FOUNDATION_QUALIFICATION.md).
