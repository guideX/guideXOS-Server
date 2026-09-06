# AARCH64-2 Kernel Foundation

Status: implemented on `AARCH64_SUPPORT`.

AARCH64-2 extends the proven AARCH64-1 firmware handoff into a bounded,
guideXOS-owned EL1 environment.  The Phase 1 loader path remains recognizable
and is still built and tested separately by `scripts/build-aarch64-phase1.ps1`
and `scripts/test-aarch64-phase1.ps1`.

## Boot and platform discovery

The Phase 2 loader is the Phase 1 loader compiled with
`GXOS_AARCH64_PHASE2`.  It retains the fixed-address ELF64/`EM_AARCH64`
validation, segment loading, owned stack, memory-map acquisition,
`ExitBootServices`, and PL011 diagnostics.  Its appended handoff fields carry
the validated/copy-owned DTB address and size.  The DTB is obtained from the
UEFI Device Tree Configuration Table (`EFI_DTB_TABLE_GUID`), copied into
`EfiLoaderData` before `ExitBootServices`, and retained by the kernel after the
handoff.  The loader rejects an absent or malformed DTB in the Phase 2 path.

The QEMU configuration deliberately uses `virt,gic-version=2,acpi=off`.
With ACPI enabled in this ArmVirt UEFI build, the DTB configuration-table
entry was not exposed; with ACPI disabled, UEFI exposes the standard DTB GUID.
This is a documented Phase 2 test-platform assumption, not a claim that all
future AArch64 firmware must disable ACPI.

The parser in `kernel/arch/arm64/phase2_platform.cpp` is intentionally not a
general Device Tree library.  It validates the FDT magic, header version,
total size, reserve-map span, structure/string block spans and alignment,
bounded nesting, token lengths, property-name offsets, compatible strings,
and big-endian address/size cells.  It recognizes only the Phase 2 platform
data: RAM, `arm,pl011`, GICv2-compatible `arm,cortex-a15-gic`/`arm,gic-400`,
and `arm,armv8-timer`.  It parses the non-secure physical timer PPI from the
standard four-tuple timer property.  The parser accepts at most eight RAM
ranges and rejects a platform unless RAM, UART, GICv2 distributor/CPU
interface, and a timer PPI are all found.

Observed QEMU virt discovery:

| Item | Value |
| --- | --- |
| RAM | `0x40000000`, size `0x20000000` (512 MiB) |
| PL011 | `0x09000000`, size `0x1000` |
| GIC version | GICv2 |
| GIC distributor | `0x08000000`, size `0x10000` |
| GIC CPU interface | `0x08010000`, size `0x10000` |
| Timer | non-secure physical generic timer |
| Timer PPI | ID `30` |
| Counter frequency | `62500000` Hz |

The fallback PL011 address `0x09000000` remains in the handoff for the first
diagnostic bytes.  After DTB parsing, the polled console switches to and
validates the discovered address.  Console output remains polled and does not
depend on interrupts, locks, or scheduler services.

## Translation tables and MMU transition

Phase 2 uses a small permanent-enough translation-table service in
`kernel/arch/arm64/phase2_mmu.cpp`; it is not the final virtual-memory
manager.

* Translation granule: 4 KiB.
* VA policy: 48-bit TTBR0 VA (`T0SZ=16`); TTBR1 is disabled with `EPD1`.
* PA policy: 40-bit (`IPS=2`), matching the bounded QEMU test assumption.
* Strategy: four-level TTBR0 tables, L0/L1/L2 table descriptors, and L3
  4-KiB page descriptors; identity mappings are used for this early kernel.
* Normal RAM: MAIR Attr1 `0xff`, normal inner/outer write-back,
  write-allocate, inner-shareable.
* Device MMIO: MAIR Attr0 `0x00`, Device-nGnRnE, shareable, XN.
* Text and vectors: privileged read-only, executable.
* Read-only data: privileged read-only, privileged XN.
* Writable data, BSS, stack, handoff/map/DTB storage, and table storage:
  privileged read-write and XN; `SCTLR_EL1.WXN` is also enabled.
* UART, GIC distributor, and GIC CPU-interface pages: Device-nGnRnE and XN.

The table arena is 520 aligned 4-KiB tables in `.translation_tables`.  The
Phase 2 mapping code maps the DTB-discovered RAM ranges and only the UART/GIC
MMIO ranges it accesses, within a bounded low-2-GiB physical window.  The
QEMU image, stack, handoff, copied DTB, and tables are all within those
identity-mapped ranges.

The transition cleans the table arena, writes `MAIR_EL1`, `TCR_EL1`,
`TTBR0_EL1`, and an intentionally unused `TTBR1_EL1`, invalidates EL1 TLBs,
and orders the operation with `DSB`/`ISB` before and after `SCTLR_EL1`.  It
then enables `M`, `C`, `I`, and `WXN` together.  A post-transition register
read and translated PL011 output prove that execution continued after the
transition.  A representative passing register set is:

```text
MAIR_EL1 = 0x000000000000ff00
TCR_EL1  = 0x00000002b5903510
TTBR0_EL1 = 0x0000000040008000
SCTLR_EL1 = 0x0000000030d8198d
```

The low bits of the final `SCTLR_EL1` value show `M=1`, `C=1`, `I=1`, and
`WXN=1`; architecturally required/reserved implementation bits are retained
from the incoming register value.

## Exceptions and synchronous proof

`phase2_entry.S` provides a 2-KiB-aligned, 16-slot EL1 vector table and
installs it with `VBAR_EL1`.  The normal kernel path uses the current-EL
SPx synchronous and IRQ slots.  SP0 slots are identified separately; FIQ,
SError, and lower-EL slots enter bounded fatal diagnostics.  The common entry
allocates a 256-byte frame for x0-x30, passes the vector class to the C++
dispatcher, restores the frame, and returns with `ERET`.

The self-test records the exact address of an inline `BRK #0x2a` instruction.
The expected SPx synchronous handler requires EC `0x3c` (BRK), ISS `imm16`
`0x2a`, and an exact `ELR_EL1` match.  It advances ELR by four and returns.
Unexpected synchronous exceptions print category, ESR, ELR, SPSR, and FAR,
then stop with a Phase 2 error marker.  A passing boot reports ESR
`0xf200002a`, the test-site ELR, and the synchronous self-test PASS marker.

## Memory map and early allocator

The kernel revalidates the packed Phase 2 handoff before using it, including
magic/version/size/flags, kernel-entry containment, stack-end arithmetic,
DTB arithmetic, and exact UEFI memory-map descriptor arithmetic.  After EBS,
only `EfiConventionalMemory` (type 7) is eligible for this early allocator.
Loader, boot-service, runtime, reserved, ACPI, MMIO, and other non-conventional
types remain unavailable; no blanket “all QEMU RAM is free” assumption is
made.

Protected ranges include the kernel image, owned stack, handoff page, copied
memory map, copied DTB, and active translation-table arena.  Conventional
regions are clipped to DTB-discovered RAM, bounded to the mapped physical
window, split around protected ranges, page-aligned, and stored in a bounded
array.  The early allocator is a cursor allocator with bounded LIFO release,
chosen for deterministic bootstrap behavior; a production free-list/bitmap
belongs to a later allocator integration.

Its self-test allocates 3 and 2 pages, verifies alignment, separation and
protected-range exclusion, writes/reads patterns through the active identity
mapping, releases in LIFO order, reallocates the same addresses, and releases
again.  Passing boots reported approximately 121.5k free pages, with the
exact count allowed to vary with firmware allocations.

## GIC and timer IRQ

The parser selects GICv2 from the actual DTB; the implementation intentionally
does not claim GICv3 support.  IRQs remain masked until vectors, GIC state,
and the timer are ready.  The focused GICv2 setup disables and clears the
bounded interrupt register banks, assigns the timer PPI to Group 0, sets its
priority, enables it in `GICD_ISENABLER0`, sets the CPU priority mask to
`0xff`, enables the distributor and CPU interface, and uses `GICC_IAR` plus
`GICC_EOIR` for acknowledgement/completion.  QEMU virt exposes the Group-0
configuration used here at EL1.  Other IRQ IDs produce a diagnostic with the
ID and do not count as timer success.

The non-secure physical generic timer is programmed with `CNTP_TVAL_EL0` and
`CNTP_CTL_EL0` for a bounded 100-ms one-shot.  On timer IRQ, guideXOS reads
the GIC acknowledge value, checks ID 30, increments the counter, disables the
timer to prevent an interrupt storm, completes the interrupt with EOIR, and
returns through the vector epilogue.  The final marker is emitted only after
the interrupted code observes the counter after returning from the IRQ.

## Test configuration and results

The exact Phase 2 QEMU invocation is:

```text
qemu-system-aarch64.exe \
  -machine virt,gic-version=2,acpi=off \
  -cpu cortex-a53 -m 512M \
  -drive if=pflash,format=raw,unit=0,readonly=on,file=edk2-aarch64-code.fd \
  -drive if=pflash,format=raw,unit=1,file=edk2-aarch64-vars.fd \
  -drive file=fat:rw:<phase2>/esp,format=raw \
  -nographic -monitor none -serial stdio -no-reboot
```

The repository harness builds and checks PE machine `0xAA64`, ELF machine
`EM_AARCH64` (`183`), runs host malformed-input tests, stages a fresh ESP,
captures serial, rejects fatal/error markers, and requires all ordered
checkpoints plus `AARCH64_PHASE2_PASS`.

Final verification record:

| Boot | Result | Evidence |
| --- | --- | --- |
| Fresh boot 1 | PASS | MMU/cache/WXN marker, BRK self-test, allocator PASS, timer IRQ returned, final marker |
| Fresh boot 2 | PASS | MMU/cache/WXN marker, BRK self-test, allocator PASS, timer IRQ returned, final marker |
| Fresh boot 3 | PASS | MMU/cache/WXN marker, BRK self-test, allocator PASS, timer IRQ returned, final marker |

Host negative controls pass for malformed FDT magic/header bounds, out-of-
bounds FDT string block, inconsistent memory-map arithmetic, unaligned and
overflowing page descriptors, and protected-range overlap detection.  The
existing AARCH64-1 wrong-ELF-machine rejection remains in the Phase 1
harness.

## Limitations and AARCH64-3 recommendation

This phase is single-core, identity-mapped, polled-console bring-up.  It does
not provide SMP, a scheduler, context switching, userspace, a generic VM
manager, a production allocator, GICv3, interrupt nesting, device drivers,
filesystem, networking, or any desktop/App Model work.  The low-2-GiB and
40-bit PA assumptions, GICv2 Group-0 setup, QEMU `virt` layout, `acpi=off`
DTB exposure, and four-tuple timer binding are explicit temporary scope.

Recommended exact AARCH64-3 scope: retain this Phase 2 harness and introduce
the generic architecture-facing interfaces for page allocation/table
activation, interrupt masking/acknowledgement, timer deadlines, barriers and
CPU idle; then add a bounded GICv3 path selected by DTB on QEMU `virt`
without changing the generic kernel call sites.  Keep SMP, scheduler, userspace
and device-driver integration out of that first architecture-boundary phase.
