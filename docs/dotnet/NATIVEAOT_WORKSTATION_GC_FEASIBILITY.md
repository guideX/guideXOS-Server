# NativeAOT Workstation GC feasibility

Status: 2026-07-26. The active PAL replacement, exact Win64/SysV PAL bridge,
and startup-only Workstation GC initialization probe pass under system QEMU.
The adapted-archive identity gate is resolved as **Identity B**: two clean
fresh builds have identical normalized members/symbols/imports; the one
historical semantic difference is the reviewed QEMU virtual-memory range
expansion.
See [NATIVEAOT_PAL_RUNTIME_REPLACEMENT.md](NATIVEAOT_PAL_RUNTIME_REPLACEMENT.md),
[NATIVEAOT_PAL_WIN64_QEMU_BRIDGE.md](NATIVEAOT_PAL_WIN64_QEMU_BRIDGE.md), and
[NATIVEAOT_GC_STARTUP_READINESS.md](NATIVEAOT_GC_STARTUP_READINESS.md).

The startup artifact calls `RhInitialize` from a probe-specific export and
returns successfully in first, repeat, and fresh disposable QEMU processes.
The authorized next image entered the real managed `byte[24]` path once in
each of three fresh QEMU processes, but the stock Workstation GC allocation
path did not return before the bounded timeout. No allocation diagnostics were
published, so object layout, zero initialization, pattern, ownership, and
no-collection claims are not promoted to PASS. The helper was parked before
managed entry; no managed finalizer callback was observed. The current
NativeAOT source has no public orderly shutdown operation, so same-process
teardown remains unsupported.
The completed shutdown-boundary audit selects Model C: GC and finalizer state
remain process-lifetime, and disposable QEMU process exit is the cleanup
boundary.

The preserved no-collection managed proofs remain healthy:

- HostLog: PASS
- 64 KiB allocations: 234
- 4 KiB allocations: 14
- Controlled OOM: PASS
- Collections: 0
- GC-backed allocations: 0
- Heap expansion: 0

Decision: **Outcome C for the real-allocation experiment** — Workstation GC
startup is usable in a disposable process, but the first managed allocation
did not complete. The historical no-collection proofs remain valid and are a
separate bounded image-backed mode; they do not validate the real collector.

Evidence and the exact real-allocation boundary are recorded in
[NATIVEAOT_WORKSTATION_GC_FIRST_ALLOCATION.md](NATIVEAOT_WORKSTATION_GC_FIRST_ALLOCATION.md).
No second initialization or live-process shutdown was attempted.

## First-allocation hang follow-up

The original Outcome C result remains preserved. The immutable artifact was
captured and stopped in the reverse-P/Invoke guard at RIP `0x10001CA7` with
`gs_base=0`; this was a terminal fail-fast self-loop with interrupts enabled,
not a GC lock, Event, helper, or collection wait. The exact corrections were
the current-thread TLS vector/runtime-cell bootstrap, PE-to-ELF mapping of the
zero-raw-size `hydrated` section, and the source-matching NativeAOT
`RehydrateData` call before `ManagedMain`. The real `byte[24]` size/range proof
was updated from the bounded-mode 40-byte geometry to the actual 48-byte
object.

The corrected artifact passed one real collector-backed allocation in the
first process and two additional fresh QEMU processes. The object was
non-null, length 24, zero-initialized, pattern-valid, aligned, in the
Workstation heap, and no collection or finalizer executed. The follow-up
decision is **Outcome A**. The exact hashes, stage record, disassembly,
watchdog snapshot, and remaining regression limitation are in
[NATIVEAOT_WORKSTATION_GC_FIRST_ALLOCATION_HANG.md](NATIVEAOT_WORKSTATION_GC_FIRST_ALLOCATION_HANG.md).

## Final closure validation — 2026-08-01

The Outcome C wording above describes the pre-correction artifact and remains
historical. The core implementation is authoritative in commit
`95580df6872e85527d27526b78ae3cdcee25dd53`. The original hang was a fail-fast
loop. GS/TLS was the first blocker; PE-to-ELF zero-fill mapping and NativeAOT
metadata hydration were subsequent blockers.

After those corrections, every named dedicated QEMU, PAL, stack, FLS,
process-teardown, managed-baseline, hosted-generic, and focused converter
closure suite passed. The allocation-specific runner enforces experiment
selectors, matching preprocessor definitions, fresh logs, allocation-specific
markers, and restoration of the ordinary kernel. Generated evidence is ignored
and untracked, with local copies preserved.

One final fresh disposable process performed exactly one collector-backed
`byte[24]` allocation. It returned `0x100A00028`; the source-derived aligned
object size was `0x30` / 48 bytes, with 24 initial zero bytes, pattern PASS, and
GC ownership PASS. Managed entry, `RhpNewArray`, and real-GC allocation entries
were 1; collections entered and managed finalizers were 0. OS process teardown
passed; runtime-level GC shutdown is NOT SUPPORTED.

The final decision is **Closure Outcome A — First collector-backed allocation
milestone fully closed**. Bounded primitive-array allocations through
Workstation GC are authorized until the first subsequent allocation-context
refill, without allowing collection. Repeated allocations were not run during
closure validation.

## First subsequent refill validation

The bounded repeated-allocation follow-up is documented in
[NATIVEAOT_WORKSTATION_GC_FIRST_REFILL.md](NATIVEAOT_WORKSTATION_GC_FIRST_REFILL.md).
It passed in three fresh disposable QEMU processes and stopped immediately
after validating the first object returned from the later allocation-context
refill. Each run recorded 15 requests, 13 fast allocations, two rare/real-GC
allocations, and two context refills. The refill-2 object was a valid
non-LOH `byte[256]` with aligned size `0x118` / 280 bytes and GC ownership;
collection and finalization counters were zero. This advances the bounded
authorization through that exact refill boundary, while preserving the
process-lifetime shutdown restriction.

## 4 KiB exit regression classification

The initially observed live bounded 4 KiB result `0xC0000419` remains
preserved as an opaque observation. Immutable-artifact raw Server capture and
three fresh runner executions all returned zero and passed the historical
14-allocation controlled-OOM contract. The current source path contains no
producer for the reported value, and the preserved failing execution lacks the
process/WER/serial record needed to identify its first layer. No speculative
runtime or runner correction was made.

The first-refill result therefore remains technically valid but its milestone
closure is pending a separate, source-backed classification of that 4 KiB
exit. Multiple subsequent refills, segment transitions, new page commitment,
collection, and allocation beyond the established boundary remain blocked.
The detailed evidence is in
[NATIVEAOT_4K_PROOF_EXIT_REGRESSION.md](NATIVEAOT_4K_PROOF_EXIT_REGRESSION.md).

## Final 4 KiB provenance closure - 2026-08-01

The separate capture-only audit completed the requested source-boundary check.
The historical `0xC0000419` remains preserved, and its initial source was not
assignable because the original report lacked a complete same-run process,
guest, teardown, cleanup, WER, staging, and PowerShell chain.

The five fresh launches used one frozen artifact pair: managed PE
`46BFE1192562DE8ABAC4A87D94BADA93E3115568AC5898E01DB2F7D584555DAB`, converted
and staged ELF
`17E7CE9DB772B0117FA04F0ED9669CDC191C401011308E929D309B1CAB7A082B`, runtime
identity
`guidexos-nativeaot-runtime-pack-amd64-hostlog-repeated-allocation-nocollection-v1`,
kernel
`D68791B66BF268425B6E646F3EA94CE7B3777B97D9CCED6682C10A9E1389066C`, and
four-file ESP tree
`1A3C951A184A6FD0FA1FA730265B4D861E3FC4FE5B15F82B98B7219CBB04B894`.
The proof runner hash was
`4E5CEC37209A533F888FBFC4F0B93C20130F75009B0E63AF9E73526A2E1B1D10`; the
capture runner hash was
`AFE90B4722D4633FFDA64B88D24EC51236A22D238191098BFDF506884ADAF1E8`.

PIDs 21872, 11108, 20536, 9564, and 25832 all returned signed/unsigned zero
(`0x00000000`), with PowerShell exit 0, pipeline success, runner return 0,
cleanup PASS, and guest PASS. Each run recorded managed entry 1, 15 attempts,
14 successful allocations, controlled OOM 1, managed/native/Server returns 0,
zero collections, zero GC-backed allocations, and zero heap expansion. The
authoritative path is hosted Server execution; QEMU and serial are explicitly
not applicable to these captures. One WER Application Error matched PID,
executable identity, and time window and reported `0xC0000005`; four runs had
no correlated WER event. No captured layer produced or propagated
`0xC0000419`.

Classification: **Provenance Outcome A - historical nonreproducible exit
observation with no source-backed association to the current guest, runtime,
teardown, staging, QEMU, or PowerShell paths.** The 4 KiB proof remains
`14` successful allocations with controlled OOM PASS; the 64 KiB proof remains
`234` with controlled OOM PASS. Startup-only, first allocation, first
subsequent refill (`15` total / `13` fast / `2` refill), HostLog, runtime-pack
state/hash, generic ELF, and inventory-isolation regressions passed. The
ordinary kernel was restored to the hash above, and generated evidence remains
ignored and untracked.

The first-refill milestone is closed. Multiple refills are authorized only as
the next bounded experiment: continue primitive-array allocations through
multiple allocation-context refills until the first new heap-segment commitment
or segment transition, without allowing collection. Review checkpoint:

```text
dotnet: validate first Workstation GC context refill
```

No multiple-refill or segment-transition testing was performed during this
closure pass.

## First post-startup segment boundary follow-up - 2026-08-01

The next bounded experiment was executed with the dedicated
`smoke-nativeaot-gc-multiple-refills-first-segment-boundary-qemu.ps1` runner.
The selected `byte[4096]` object has source-derived size `0x1018` / 4120
bytes, remains below the 85,000-byte large-object threshold, and uses a fixed
384-allocation hard limit.

The first collector-backed allocation committed the initial segment quantum;
that startup commitment was captured but excluded from the measured boundary.
The first later commitment occurred at allocation 16/refill 9 within the same
source segment identity `0x104014730`: address `0x100A11000`, requested and
actual size `0x10000`, old committed boundary `0x100A11000`, new committed
boundary `0x100A21000`. The run recorded 16 total allocations, 7 fast
allocations, 9 real collector/refill entries, one measured commit, and zero
segment transitions.

Three fresh QEMU processes reproduced the same boundary and stop-object
geometry. All reported zero collection requests/entries, zero collections,
zero suspension-for-collection requests, zero finalization scans, zero managed
finalizers, valid collector ownership, and no post-boundary allocation. The
normal kernel was restored to
`D68791B66BF268425B6E646F3EA94CE7B3777B97D9CCED6682C10A9E1389066C`.

The decision is **Outcome A** for the first post-startup GC commitment. The
exact next experiment is the first GC segment transition, still without
allowing collection. Physical-frame and mapping deltas remain a documented
limitation because the locked startup-platform hook exposes the exact virtual
commit operation but not those kernel counters to the NativeAOT diagnostic
record.

## First segment-transition gate - 2026-08-01

The source-backed transition follow-up is complete and is documented in
[NATIVEAOT_WORKSTATION_GC_FIRST_SEGMENT_TRANSITION.md](NATIVEAOT_WORKSTATION_GC_FIRST_SEGMENT_TRANSITION.md).
The selected 4120-byte primitive array remains on the SOH path. In the locked
standalone Workstation build, `soh_try_fit` can grow commitment within the
current reserved segment, but the non-region SOH path proceeds to collection
handling when the segment is unsuitable; the `uoh_get_new_seg` path is not
applicable to this sub-LOH object.

Three fresh QEMU runs used a fixed cap of 32 allocations, 20 refills, 4
post-startup commit observations, and one allowed transition observation. The
runs recorded 32 allocations (`15` fast / `17` rare), 17 refills, 2 commits,
zero collection requests/entries/suspensions, zero segment transitions, valid
stop-object ownership, and the same segment identity `0x104014730`. A
246-allocation calibration run entered six collections and was discarded as
unsafe evidence.

The final decision is **Outcome B — collection is required before a SOH segment
transition**. The exact next experiment is a first-GC collection-readiness
audit, not collection execution. The ordinary kernel was restored to
`D68791B66BF268425B6E646F3EA94CE7B3777B97D9CCED6682C10A9E1389066C`.

## Transition-gate update - 2026-08-02

The fresh bounded transition audit supports the feasibility boundary rather
than expanding it: sub-LOH `byte[4096]` allocations can commit more pages in
the current reserved SOH segment, but source inspection shows collection is
required before a standalone Workstation-GC SOH segment transition. The run
therefore ended with **Outcome B**, zero collection entry, zero suspension,
zero segment transitions, and three of three QEMU passes. The exact next
experiment is a separately authorized first-GC collection-readiness audit.
See [NATIVEAOT_WORKSTATION_GC_FIRST_SEGMENT_TRANSITION.md](NATIVEAOT_WORKSTATION_GC_FIRST_SEGMENT_TRANSITION.md).
