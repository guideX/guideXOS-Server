# NativeAOT Workstation GC startup readiness

Status: 2026-07-26. PAL bridge readiness and the first Workstation GC
initialization-only dry run pass. The identity gate is resolved as Identity B:
normalized fresh archives match, while the historical difference is the
reviewed QEMU virtual-memory range expansion. The runtime shutdown audit selects a
process-lifetime GC model: full orderly same-process GC shutdown is not
available in the locked NativeAOT source, so QEMU runs use disposable
processes and never signal the finalizer event.

Evidence: `out/dotnet/pal-win64-qemu-bridge/` and
`out/dotnet/gc-initialization-dry-run/`. See also
[NATIVEAOT_PAL_WIN64_QEMU_BRIDGE.md](NATIVEAOT_PAL_WIN64_QEMU_BRIDGE.md) and
[NATIVEAOT_WORKSTATION_GC_FEASIBILITY.md](NATIVEAOT_WORKSTATION_GC_FEASIBILITY.md).
The shutdown decision and candidate audit are in
[NATIVEAOT_RUNTIME_GC_SHUTDOWN_BOUNDARY.md](NATIVEAOT_RUNTIME_GC_SHUTDOWN_BOUNDARY.md).

| Required readiness item | Current status |
| --- | --- |
| Active PAL archive replacement | PASS |
| Exact symbol parity | PASS, 6/6, 38/38, 74/74, 3/3 |
| Replacement Windows imports | PASS, 0 |
| Win64 PAL hook-table versioning | PASS, ABI v1, 232 bytes |
| SysV hook implementation | PASS |
| SysV-to-Win64 callback bridge | PASS |
| Worker lifecycle bridge | PASS |
| FLS detach-callback bridge | PASS |
| Stack-bound bridge | PASS |
| ThreadStore bridge | PASS |
| Exact hosted PAL probe | PASS |
| Server PE-to-ELF PAL probe | PASS |
| System-QEMU exact PAL probe | PASS |
| HostLog | PASS |
| Managed allocation proofs | PASS, 234 / 14 and controlled OOM |
| Workstation GC initialization-only probe | PASS, `RhInitialize` returned 0 |
| Workstation GC orderly shutdown | UNSUPPORTED by locked source contract; Model C process-lifetime |
| Real managed first `byte[24]` allocation | FAIL/HANG in `RhpNewArray`; Outcome C |
| Historical bounded-mode collections | 0 |
| Historical bounded-mode GC-backed allocations | 0 |
| Historical bounded-mode heap expansion | 0 |

The startup QEMU matrix reports PASS for first, repeat, and fresh disposable
processes. The startup platform extension is ABI v1, 216 bytes, capability
mask `0x7`; the PAL table is ABI v1, 232 bytes, capability mask `0x1FF`.

The authorized first-allocation image called `RhInitialize` once and entered
`ManagedMain` once in each of three fresh disposable QEMU processes. The stock
real-GC allocation path did not return before the bounded timeout, so no
post-allocation diagnostics exist and no PASS is claimed for object layout,
zero initialization, pattern, ownership, or collection counts. The helper was
parked before managed entry; cleanup remains process-lifetime only.

Decision: **Outcome C for real managed allocation**. Startup-only readiness
remains Outcome B, but the branch is not ready to claim a real managed heap
allocation.

See [NATIVEAOT_WORKSTATION_GC_FIRST_ALLOCATION.md](NATIVEAOT_WORKSTATION_GC_FIRST_ALLOCATION.md)
for the bounded logs and artifact hashes. No shutdown or second `RhInitialize`
is authorized until the locked source exposes a supported contract.

## First-allocation follow-up

The original startup and Outcome C evidence above remains preserved. The
hash-specific follow-up captured the immutable baseline RIP at
`0x10001CA7` in `guideXosFailFast+0x7`; the missing current-thread GS TLS
vector caused a terminal fail-fast self-loop before the wrapper could publish
its allocation stages. The corrected image additionally required mapping the
zero-raw-size `hydrated` section and invoking the matching NativeAOT metadata
rehydration boundary before managed entry. These were image/runtime readiness
invariants, not collector-lock or collection changes.

The three-run startup-only QEMU probe was rerun and passed. The corrected
first-allocation report then passed exactly one real Workstation-GC `byte[24]`
allocation in three fresh disposable processes, with non-null object, length,
zeroing, pattern, and heap ownership proven; collection and finalization
counters stayed zero. The follow-up decision is **Outcome A**, while this
document's original Outcome C wording remains the historical record for the
pre-correction artifact.

## Final closure validation — 2026-08-01

The original Outcome C and the follow-up phase above remain historical. The
authoritative implementation is commit
`95580df6872e85527d27526b78ae3cdcee25dd53`. The original hang was a fail-fast
loop; GS/TLS was the first blocker, followed by PE-to-ELF zero-fill mapping and
NativeAOT metadata hydration.

The dedicated QEMU lifecycle suites, PAL system-QEMU probe, stack-bounds
runner, FLS-before-initialization harness, process-teardown policy harness,
allocation smoke-runner enforcement, managed baseline proofs, hosted generic
suites, and focused PE-to-ELF zero-fill test all completed. Generated evidence
is locally preserved under ignored `out/` paths and is no longer tracked.

The final immutable run performed exactly one real Workstation-GC-backed
`byte[24]` allocation. The object was `0x100A00028`, with source-derived size
`ALIGN_UP(baseSize + componentSize * length, pointerAlignment) = 48` bytes,
24 initial zero bytes, valid pattern and GC ownership. Managed entry,
`RhpNewArray`, and real-GC counters were 1; collections entered and managed
finalizers were 0. Process teardown passed, while runtime-level GC shutdown
remains NOT SUPPORTED. The final decision is **Closure Outcome A — First
collector-backed allocation milestone fully closed**. Bounded primitive-array
allocations are authorized only until the first subsequent allocation-context
refill and without allowing collection; repeated allocations were not begun in
this pass.
