# AArch64 RUNSAFE2 EL0 Negative Qualification

- **Repository:** `guideXOSServer_AARCH64`
- **Branch:** `AARCH64_SUPPORT`
- **Starting source commit:** `65f0a0d5e79b08ba6d74aa49971635bb0321f071`
- **Outcome:** `EL0_NATIVE_ISOLATION_DENIAL_VERIFIED` for kernel-data and GIC Distributor reads in the QEMU ARM64 guest
- **Scope:** trusted, internally constructed EL0 probes; no Studio Run, arbitrary application loading, SMP, or general process scheduling

## Candidate identity

The preserved r5 negative candidate was not sufficient for acceptance: it used
a live mutable address-space counter as the kernel target, and the harness did
not stop the later MMIO probe after a kernel-data failure. The diagnostic was
repaired and rebuilt as immutable candidate r6. Its manifest records source
commit `65f0a0d5e79b08ba6d74aa49971635bb0321f071`, profile
`runsafe2-el0-negative-controls`, machine `virt,gic-version=2,acpi=off`, CPU
`cortex-a53`, and the fixed 20-byte positive SVC payload.

| Artifact | SHA-256 |
| --- | --- |
| Candidate manifest | `49a761874a25016f20a58c99f2b29932cd25e79193e5c7835a5478b240eaf1c7` |
| Kernel ELF | `dc6ddd44161e399e8f2342f7bfbc5b4702610465205a35d82b9b03a6601372bb` |
| EFI image | `1605c55a4d7c0c7b0589bd181e28d2e32f7ef9da31a6aeb23695c237ff7bfe42` |
| Ramdisk | `c06043cc48e8600b017325847827f564591335b5ce600e52bd394556997597a4` |
| Trusted positive payload | `9adb67ab60e0329266473bb0c419ff4167df6fb77c59fec2aff1de57421dd442` |
| QEMU executable | `8d2183e745155a303ffb9fd861c5da31c5e6c19fcbfd082081cc3e4bcdb0e748` |
| EDK2 code firmware | `47765fe344818cbc464b1c14ae658fb4b854f5c2ceffa982411731eb4865594d` |

Source hashes captured by the candidate manifest:

| Source | SHA-256 |
| --- | --- |
| `kernel/arch/arm64/phase2_el0.cpp` | `283dab1294efb9537a17de0b417fa77b50d7083a874ac24ff8e2dae2506c50fd` |
| `kernel/arch/arm64/phase2_el0.h` | `c7937448b3927620eae8edb0d2f692f62e5faf6ba933afa3ff5560ba46f1e270` |
| `kernel/arch/arm64/phase4_main.cpp` | `c8e7623f7d8fa730d5941ffd53382bc4280b7bf5fd6360a52a19265f59572d22` |
| `scripts/test-aarch64-el0-host.ps1` | `5781662efbbd48369902146c2ab7cb75412d983f83b95cc1bc3ff4844e4808e1` |
| `tests/aarch64_el0_isolation_host_tests.cpp` | `673422af3ba9d2adee28989788ce06ecf34812d40764a7ebb48c7d548d673217` |

## QAC authorization and execution

The native negative run used QAC project
`guideXOSServer-AARCH64-RUNSAFE2-EL0-Negative` in `acceptance` mode. QAC granted
exclusive reservation `ce4e86b32a8a4cd5880c514486a0f22d`, then launched direct
QEMU child PID `16576` (QAC CLI PID `22288`). The QEMU child used a fresh copy of
the EDK2 vars image, serial log, and QMP port `63407`. QAC's own log records
child exit code 0 and reservation release. The post-run QAC status showed no
QEMU processes, sessions, or reservations.

There was one execution-bound deviation. The reservation list briefly showed
`Starting`; the first monitor treated that transient state as terminal and
stopped supervising the already QAC-owned child. All guest evidence was
captured by `2026-11-11T04:23:18.3830054Z`, 23.286 seconds after launch. QMP
shutdown was sent at `2026-11-11T04:29:01.0812723Z`, for a managed runtime of
365.984 seconds, 65.984 seconds beyond the intended 300-second bound. QAC
continued to own the child throughout, logged its clean exit, and released the
reservation. A later monitor invocation accepted `Starting` as transient but
refused preflight because unrelated managed x86 QEMU PID `21688` was active
under reservation `6c9456eb9f9540019b2007da433cbaf5`; that invocation launched
no guest and did not alter that workstream. No further QAC attempt was made.

The first session evidence is preserved under
`out/aarch64-runsafe2-el0-native-20261110-201806/el0-negative-20261110-202250-421/`.
The guest log SHA-256 is
`44fc45e8905a0b0fe73fe4435d2549632679b36e907284c343151e055c3c459d`; QAC
stderr log SHA-256 is
`a51eb4126e71ce710e59fa2db943e1685e3252ed4396e3a70708ed2e17ab5334`.
The later no-launch record is in
`out/aarch64-runsafe2-el0-native-20261110-201806/el0-negative-20261110-203222-619/`.

## Mapping and fault evidence

The kernel-data probe targets the dedicated immutable rodata sentinel at
`0x0000000040066170`. The kernel-root page descriptor is present and maps the
identity page containing that address with AP=`10` (EL1 read-only, EL0
inaccessible), AttrIdx 1 (normal RAM), AF set, and PXN/UXN set. This address is
inside linked kernel `.rodata`; the probe validates the descriptor and
permissions before entering the child.

The guest DTB parser reported GICv2. The device probe receives the parsed
`gicd_base` from platform data (`0x0000000008000000`) and performs a read only.
Its present kernel-root page descriptor has AP=`00` (EL1 read/write, EL0
inaccessible), AttrIdx 0 (device), AF set, and PXN/UXN set. Neither probe
contains a store instruction or writes to the interrupt controller.

| Probe | Child | Target / FAR | Descriptor | ESR / interpretation | ELR / SPSR | State and cleanup |
| --- | ---: | --- | --- | --- | --- | --- |
| Kernel data | `2` | `0x40066170` | `0x0060000040066787` | `0x9200000f`: EC `0x24` lower-EL data abort, DFSC `0x0f` level-3 permission fault, WnR `0` read | `0x40000010` / `0x3c0` (EL0t, DAIF masked) | `FAULTED`, kernel root restored, 2 data pages and 4 table pages released |
| GIC Distributor | `3` | `0x08000000` | `0x0060000008000703` | `0x9200000f`: EC `0x24` lower-EL data abort, DFSC `0x0f` level-3 permission fault, WnR `0` read | `0x40000010` / `0x3c0` (EL0t, DAIF masked) | `FAULTED`, kernel root restored, 2 data pages and 4 table pages released |

Both records identify owner `0x454c305155414c31`, `read-value=0`, and exact
FAR equal to the requested target. ELR is the LDR instruction at code offset
16, not the fallback SVC. The handler marked each actual child context
`FAULTED`, restored the kernel root before return, and released the owned code,
stack, and table pages. No synthetic exit value was accepted. The negative
control's `PASS` requires all of these facts and the expected syndrome; a
generic exception or translation fault would not pass.

The positive control in the same guest first emitted
`AARCH64_EL0_QUALIFICATION_PASS observed-exit=2a cleanup=PASS`. After both
denials, the guest emitted `AARCH64_EL0_NEGATIVE_CONTROLS_PASS`, resumed the
scheduler completion task, and reported `AARCH64_PHASE12_PASS`. Final guest
statistics were 1,000 timer ticks, 11,006 context switches, 1,000 preemptions,
75,648 VFS reads, 4,727 enumerations, and zero unexpected IRQs. The existing
`phase3_exception_count()` is the fatal exception counter; it remained zero
while the two expected EL0 data aborts were recorded separately in the child
probe results. GICv2 remained operational for timer delivery and scheduler
progress.

## Hosted and integrated build evidence

`scripts/test-aarch64-el0-host.ps1` passed with 169 assertions and
`AARCH64_EL0_SOURCE_CONTRACT_PASS`. These hosted checks exercise production
address-space and exception code against structural MMU/allocator stand-ins;
they do not provide the native permission evidence above.

The r6 build completed the existing integrated Phase 12 qualification path:
Studio adapter controls (181 assertions), the EL0 structural suite (169),
Studio Run fail-closed controls with no synthetic exit-42 path, the 400-byte
native ABI and 464-byte Studio v0.5 ABI checks, Phase 10 controls, and the real
ARM64 Developer Studio Build. Studio Run remains rejected; Phase 9 NativeElf
applications remain privileged EL1 and are not reclassified as isolated.

## Remaining limits

This result establishes the tested privilege boundary in QEMU's ARM64 `virt`
GICv2 guest only. Physical ARM64 hardware has not been qualified. The harness
is synchronous, uses a fixed trusted payload, masks interrupts during its short
entry interval, and has no per-task address-space switching, cancellation, or
timer-enforced EL0 deadline. General application loading, immutable
Build-to-Run binding, and Studio Run remain separate work. The monitor's
300-second shutdown bound also needs a successful rerun with the corrected
`Starting` handling when QAC is free.
