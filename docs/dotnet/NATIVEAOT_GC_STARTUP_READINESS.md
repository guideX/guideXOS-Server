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

## First subsequent allocation-context refill

The follow-up repeated primitive-array experiment is now complete and is
reported in [NATIVEAOT_WORKSTATION_GC_FIRST_REFILL.md](NATIVEAOT_WORKSTATION_GC_FIRST_REFILL.md).
Three fresh QEMU processes each performed 15 real Workstation-GC-backed
`byte[256]` allocations: one initial real-GC allocation, 13 fast context
allocations, and one later real-GC allocation that supplied the first
subsequent context refill. The refill-2 object, source-derived `0x118` / 280
byte geometry, context publication, and GC ownership all passed. Collections,
finalization scans, and managed finalizers remained zero. The decision is
**Outcome A** for this bounded boundary only; no post-refill allocation or
same-process shutdown is authorized.

## 4 KiB exit regression classification

The separate bounded 4 KiB closure audit preserved the reported opaque
`0xC0000419` observation and then ran the immutable proof through raw Server,
runner, and three fresh execution layers. All captured executions returned
`0x00000000` and reproduced 14 allocations, controlled OOM, zero collections,
zero GC-backed allocations, zero expansion, and normal teardown. No captured
layer generated the reported value, so no runtime, GC, ABI, teardown, or runner
correction was applied. See
[NATIVEAOT_4K_PROOF_EXIT_REGRESSION.md](NATIVEAOT_4K_PROOF_EXIT_REGRESSION.md).

The first-refill experiment remains technically Outcome A, but milestone
closure is pending the unbound 4 KiB exit classification. No multiple-refill
or segment-transition experiment is authorized.

## Final 4 KiB provenance closure - 2026-08-01

The separate exit audit closed the remaining provenance question without
changing GC startup, allocation, PAL, or teardown behavior. The original
`0xC0000419` remains a historical opaque observation; it initially had no
source association because the failing record did not preserve every same-run
process, guest, cleanup, WER, staging, and PowerShell layer.

One immutable 4 KiB PE/ELF pair was captured in five fresh authoritative
Server launches. PE:
`46BFE1192562DE8ABAC4A87D94BADA93E3115568AC5898E01DB2F7D584555DAB`; ELF and
staged ELF:
`17E7CE9DB772B0117FA04F0ED9669CDC191C401011308E929D309B1CAB7A082B`;
runtime-pack identity:
`guidexos-nativeaot-runtime-pack-amd64-hostlog-repeated-allocation-nocollection-v1`;
kernel:
`D68791B66BF268425B6E646F3EA94CE7B3777B97D9CCED6682C10A9E1389066C`; ESP
tree manifest:
`1A3C951A184A6FD0FA1FA730265B4D861E3FC4FE5B15F82B98B7219CBB04B894`;
proof runner:
`4E5CEC37209A533F888FBFC4F0B93C20130F75009B0E63AF9E73526A2E1B1D10`; capture
runner:
`AFE90B4722D4633FFDA64B88D24EC51236A22D238191098BFDF506884ADAF1E8`.

PIDs 21872, 11108, 20536, 9564, and 25832 each returned signed 0, unsigned
0, and `0x00000000`; PowerShell exit 0; pipeline success; runner return 0;
cleanup PASS; and guest PASS. Each recorded managed entry 1, 15 attempts,
14 successful allocations, controlled OOM 1, managed/native/Server returns 0,
zero collections, zero GC-backed allocations, and zero heap expansion. This
proof is hosted by guideXOS Server, so QEMU and serial are explicitly recorded
as not applicable for these same runs. One WER Application Error event matched
PID, executable path, and time window and reported `0xC0000005`; the other
four runs had no correlated WER event. No WER or execution layer reported
`0xC0000419`.

The historical value therefore remains a nonreproducible exit observation with
no source-backed association to the current guest, runtime, teardown, staging,
QEMU, or PowerShell paths. Focused 4 KiB, 64 KiB, startup-only, first
allocation, first subsequent refill, HostLog, runtime-pack state/hash, generic
ELF, and inventory-isolation regressions passed; the ordinary kernel was
restored to the hash above. The first-refill milestone is closed under
Provenance Outcome A.

The next experiment is authorized but was not run here: continue bounded
primitive-array allocations through multiple allocation-context refills until
the first new heap-segment commitment or segment transition, without allowing
collection. Review checkpoint:

```text
dotnet: validate first Workstation GC context refill
```
