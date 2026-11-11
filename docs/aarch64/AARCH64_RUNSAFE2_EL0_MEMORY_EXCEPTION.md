# AArch64 RUNSAFE2: EL0 Memory and Exception Foundation

- **Repository:** `guideXOSServer_AARCH64`
- **Branch:** `AARCH64_SUPPORT`
- **Scope:** trusted, internally constructed EL0 qualification payload only
- **Studio Run:** disabled
- **Outcome:** `EL0_NATIVE_ISOLATION_DENIAL_VERIFIED` — both native denial probes passed under QAC; the run's shutdown monitor exceeded its intended 300-second bound
- **R2 QAC supervision:** `QAC_SUPERVISION_DEFECT_LOCALIZED` — the single authorized rerun produced the expected EL0 evidence but missed the bounded QMP cleanup deadline; a socket-table fallback correction now passes host regressions

## Status

RUNSAFE2 adds a bounded child-owned AArch64 address space, EL0 user mappings,
the lower-EL exception bridge, and a single SVC exit. Its qualification payload
is embedded in the kernel and cannot be supplied by Developer Studio or an ELF
package. Hosted controls execute the production address-space and exception
logic against structural MMU and allocator stand-ins. They do not execute an
EL0 instruction or prove hardware permission enforcement.

The diagnostic guest uses the existing `virt`, GICv2, `cortex-a53`
configuration. The earlier positive guest used QAC acceptance reservation
`57d93f568f5843c0b64d06a1ac02b8cf` and direct QEMU child PID 27520; its actual
SVC result was 42, followed by cleanup, scheduler continuation, Phase 12
service, and QAC-verified exit/release. The new negative candidate also ran
that positive SVC control before testing kernel RAM and GIC Distributor MMIO.

The negative run was granted QAC acceptance reservation
`ce4e86b32a8a4cd5880c514486a0f22d` and launched QEMU as managed child PID
16576. Both accesses produced exact lower-EL level-3 permission faults, both
children restored the kernel root and cleaned their pages, and timer-driven
scheduler service continued. QAC logged QEMU exit code 0 and reservation
release. The evidence and its monitor timing deviation are recorded in
[AARCH64_RUNSAFE2_EL0_NEGATIVE_QUALIFICATION.md](AARCH64_RUNSAFE2_EL0_NEGATIVE_QUALIFICATION.md).
No QEMU launch was made outside QAC.

The preserved positive candidate is
`out/aarch64-runsafe2-phase12-candidate-r4` (kernel SHA-256
`29dc680d7831c6b4fc0b193be2a4e76492ba2789185516baedd7624c096ee4f3`, EFI
SHA-256 `1605c55a4d7c0c7b0589bd181e28d2e32f7ef9da31a6aeb23695c237ff7bfe42`).

The earlier r5 negative candidate and its canceled queued request remain
preserved. Source review found that r5 targeted a live mutable address-space
counter and could continue to the MMIO child after a kernel-data probe failed.
The r6 candidate replaced that target with an inert kernel rodata sentinel,
validated both exact EL1-only descriptor policies before entry, recorded the
full child/fault/cleanup evidence, and stopped after any kernel-data failure.
Its candidate identity and native evidence are in the focused report.

## Existing MMU and extension

The existing kernel uses a four-level, 4 KiB stage-1 translation rooted at
`TTBR0_EL1`. Its low physical RAM, kernel image, stacks, and discovered device
windows use identity virtual addresses. `TCR_EL1.T0SZ` is 16 (48-bit VA),
`IPS` selects 40-bit PA, and `EPD1` disables `TTBR1_EL1`. `MAIR_EL1` assigns
AttrIdx 0 to device memory and AttrIdx 1 to normal write-back/write-allocate
RAM. `SCTLR_EL1.WXN` remains enabled. Kernel and device leaves retain their
EL1-only access permissions; device leaves are execute-never. The existing
framebuffer mapping and any Phase 5 permission updates are unchanged.

The extension keeps that kernel layout and adds a 2 MiB child window at
`0x0000400000000000`, in TTBR0 L0 slot 128. The current shared kernel root
must have that slot empty. Address-space creation allocates a private root,
copies the shared root entries, and retains the kernel's existing lower-level
tables as shared EL1 mappings. Only the child-owned user slot and its lower
tables are added. The shared root and kernel descriptors are never edited by
the child mapper. A second child cannot inherit the first child's private
tables or leaf mappings.

The address-space object tracks a monotonically assigned identity, owner ID,
kernel and private roots, lifecycle and cleanup states, at most 8 owned table
pages (including the root), 16 owned data pages, and 16 user mappings. Allocation
uses the common physical-page allocator; every mapped frame must appear in that
space's owned-page list. The mapper rejects duplicate frames and virtual pages,
misalignment, overflow, out-of-window ranges, invalid or overlapping mappings,
unowned frames, and capacity exhaustion. The fixed arrays and bounded loops
avoid dynamic runtime dependencies.

## User mapping policy

The current qualification layout uses:

| Window offset | Mapping | EL0 permission | Execute | Purpose |
| --- | --- | --- | --- | --- |
| `+0x0000` | One owned page | Read-only | EL0 only | Trusted payload code |
| `+0x1000` | Unmapped | None | No | Guard page |
| `+0x2000` | One owned page | Read/write | No | Child data |
| `+0x3000` | Unmapped | None | No | Guard page |
| `+0x4000` | One owned page | Read/write | No | Child stack |

Code leaves use EL0 read-only AP permissions, UXN clear, and PXN set. Data and
stack leaves use EL0 read/write AP permissions and set both UXN and PXN. The
mapper rejects writable-executable mappings and executable stack flags. All
child leaves use normal memory and non-global translations. The kernel and
device mappings copied into the child root retain their EL1-only descriptors;
no child mapping is created for kernel-owned pages or MMIO. The window slots
between code, data, and stack remain unmapped to provide guard regions.

## Root activation and teardown

RUNSAFE2 does not add ASIDs. Every root change uses the same conservative
sequence: `DSB ISHST`, write `TTBR0_EL1`, `ISB`, `TLBI VMALLE1`, `DSB ISH`,
`ISB`. This flushes translations from a prior child before it can be reused.
The new root preserves the kernel identity mappings, so the EL1 instruction
stream, current EL1 stack, VBAR, exception code, and kernel data stay mapped
while the child root is active. Owned table pages are cache-cleaned before
activation. The payload page is synchronized to the instruction cache before
entry.

Exit and fault handling switch back to the saved kernel root before returning
to the qualification caller. Cleanup refuses to free the active root, releases
each owned data page and table page through the common allocator, records each
successful release for retry safety, and marks the address space cleaned only
after all releases succeed. A full root-switch invalidation occurs before
those pages are returned to the allocator.

## EL0 context and exception bridge

The fixed context stores x0-x30, `SP_EL0`, entry PC, intended PSTATE, address
space ID, owner ID, lifecycle state, actual exit value, captured fault state,
and the saved EL1 continuation stack pointer. Compile-time offsets bind the
record to the assembly bridge. Before entry, C validates the owner and space
identity, an aligned stack pointer in a mapped writable/non-executable stack
page, an aligned executable entry in a read-only user code page, and the exact
EL0t PSTATE value `0x3c0` (DAIF masked).

`phase3_el0_enter` saves EL1 callee-saved registers (including the platform
register x18) and its continuation stack, loads `SP_EL0`, `ELR_EL1`, and
`SPSR_EL1`, restores the child GPRs, and executes `ERET`. On terminal return it
clears `SP_EL0` before the owned stack page is released. Thus the qualification
instructions begin at EL0. Lower-EL AArch64
synchronous and IRQ vector slots save the complete x0-x30 frame and dispatch
with distinct vector classes. FIQ, SError, AArch32 entries, and lower-EL
exceptions without the RUNSAFE2 build define remain fatal. A lower-EL IRQ is
acknowledged and completed before the current policy terminates the child as
an unexpected interrupt.

The qualification call masks interrupts for its short synchronous interval;
that is not a scheduler interrupt or cancellation design. An exception from
an active, identity-matched EL0 context is terminal. The dispatcher captures
`ESR_EL1`, `ELR_EL1`, `SPSR_EL1`, and `FAR_EL1`, switches back to the kernel
root, and never retries the faulting child instruction. EL0 instruction abort
(EC `0x20`), data abort (EC `0x24`), unsupported SVC, unexpected synchronous
exception, invalid ownership, and unexpected interrupt all end the context.

## SVC exit and trusted payload

The only supported service is `SVC #0` with service ID 1 in x8. The child
result is read from the actual x0 register saved by the exception frame. The
handler verifies the lower-EL context and its owner/space IDs, the SVC
exception class and immediate, and that the instruction preceding the saved
ELR is in an executable child mapping. It records the exit, restores the
kernel root, and returns that x0 value through the assembly continuation. No
kernel function pointers or `gx_host_calls` table are exposed to EL0.

The embedded 20-byte AArch64 payload is in
`kernel/arch/arm64/phase2_el0.cpp`. Its instructions set x9 to 19, x10 to 23,
add them into x0, load service 1 into x8, and execute `SVC #0`:

```text
69 02 80 d2  ea 02 80 d2  20 01 0a 8b  28 00 80 d2  01 00 00 d4
```

SHA-256: `9adb67ab60e0329266473bb0c419ff4167df6fb77c59fec2aff1de57421dd442`.
The native guest returned 42 from the executed `ADD` through the child-provided
x0. Hosted tests also supply register-frame values to exercise the production
dispatcher, but that simulated result is separate from the observed QEMU
execution.

## Hosted evidence and limits

The production address-space and exception module is compiled into a hosted
control program with test-only allocator and MMU seams. Its 169 assertions cover
separate roots, retained kernel mappings, child ownership, code/data/stack
permissions, W^X, guard pages, unauthorized kernel/MMIO pages, invalid ranges
and descriptors, context identity, entry and stack validation, SVC ownership
and actual x0 capture, abort and unsupported-service faults, cleanup retry,
capacity bounds, and repeated allocation/cleanup. These checks are structural
simulations; the host does not enforce ARM64 page permissions.

The implementation also builds the real AArch64 freestanding kernel and UEFI
loader, and the diagnostic Phase 12 profile compiles the same payload and
exception bridge. ABI adapter and Studio Run source-gate checks continue to run
in the Phase 12 build. The captured DTB parser result remains consistent with
the previously recorded capture: SHA-256
`5aec32b925cb7259d651aca355106e58d77f7f1c553d80f0420c5b3e3036f311`, FDT v17,
model `linux,dummy-virt`, and 223 properties.

Hosted and build regressions completed for this change include 166 EL0
structural assertions, 51 Phase 2 assertions, the Phase 3 scheduler contract,
Phase 4 allocator/VFS and scheduler controls, Phase 9 NativeElf lifecycle and
ABI layout controls, Phase 10 resolver/ELF controls, the Phase 11 compiler and
Developer Studio ARM64 build, 181 Studio v0.5 adapter assertions, 62 GICv3
assertions, and the native filesystem contract test. Phase 2, 3, 4, 9, 11, and
the Phase 12 diagnostic ARM64 EFI/kernel build completed. The separate R1e,
R2A, and R2B1 qualification artifacts were not overwritten.

Hosted tests do not prove ARM64 permission enforcement. The positive native
guest proved actual EL0 execution and SVC return of x0=42. In the separately
identified r6 negative guest, the same positive control passed before the
kernel-data and discovered GIC Distributor read probes. Both produced exact
EL0 permission faults at their target FAR, restored the kernel root, and
released their owned pages. The two expected lower-EL data aborts are distinct
from the kernel's fatal/unexpected exception counter, which remained zero; the
post-probe scheduler log also reported zero unexpected IRQs.

## Scheduler, Build identity, and existing execution paths

This is a synchronous qualification harness, not general scheduled child
execution. It runs only after the Phase 12 diagnostic has observed graphics
ready and a VFS read, then returns to the existing scheduler task. The
qualification payload is fixed and short, with interrupts masked. There is no
per-task TTBR ownership, timer-enforced deadline, child cancellation, or
non-cooperative timeout. Unexpected IRQ currently terminates the qualification
context after interrupt acknowledgement. Scheduler integration must add
address-space ownership to task contexts and switch/flush roots at every task
transition before arbitrary child code is considered.

The R2B1 compiler and ELF image-base contract is unchanged. RUNSAFE2 does not
bind a successful Build generation to immutable validated bytes or close the
path-replacement window between validation and loading. Future Run must bind
the Build owner, project, source membership, target, ABI, exact artifact size
and digest, then load the same immutable bytes that passed validation.

Phase 9 NativeElf applications remain on their legacy privileged EL1 path,
including `gxos_native_call_on_stack`. RUNSAFE2 does not change that application
model or claim isolation for those applications. Developer Studio Run remains
rejected before package resolution and entry dispatch, with the Phase 9 loader
backstop intact. No Studio-generated or arbitrary ELF is routed to the new
qualification harness. Both the 400-byte ABI-v1 layout and 464-byte Studio
v0.5 layout remain separate and unchanged.

## Next milestone

The negative isolation milestone remains guest verified for the tested QEMU
configuration. RUNSAFE2 R2 records the monitor defect, the one authorized rerun,
and the unqualified cleanup result in
[AARCH64_RUNSAFE2_R2_QAC_SUPERVISION.md](AARCH64_RUNSAFE2_R2_QAC_SUPERVISION.md).
The preserved r6 candidate produced the expected positive SVC, both permission
faults, cleanup, and scheduler evidence. The runner missed its 45-second QMP
cleanup deadline because the Windows socket-table query failed; exact QMP
identity was later verified and QAC logged exit code 0 and reservation release.
The corrected fallback passes host regressions, but the authorized attempt has
been used and no bounded-supervision pass is claimed. The R1 negative guest
reached all permission and scheduler checks in 23.286 seconds; its full child
runtime was 365.984 seconds.

General scheduled child execution, per-task address-space switching,
cancellation/deadlines inside the guest, physical-hardware qualification,
immutable Build-to-Run binding, and any Studio Run enablement remain later
milestones. The R2 harness result does not change the privileged Phase 9
NativeElf path or enable arbitrary application isolation.
