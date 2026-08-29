# C011EC55 — NativeAOT Workstation GC natural older-generation transition

## Result

C011EC55 extended the productionized C52/C54 NativeAOT 9.0.0 AMD64
Workstation-GC workload with a bounded ordinary allocation profile. Three fresh
QEMU 11.0.0 boots completed 631 source-correlated collections each. Every
authoritative collection selected generation 0. No condemned-generation-1 or
full collection, ephemeral-segment replacement, segment retirement, or segment
recycling occurred.

The C55 classification is Outcome E, Success Level 0: no older-generation
collection occurred within the bound. The no-escalation classification is Code
4 — the collector repeatedly preferred gen0 within the bounded workload. This
is a natural-policy result, not a direct-reuse claim.

## Repository and locked identity

Repository: D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT

C55 preflight started on branch v1.1_DOTNET_SUPPORT at
8302d9c08feefe3821b440e742ff178c76306d69, subject Trace NativeAOT reclaimed gen1
ephemeral transition. This is the full C54 commit and is an ancestor of C55.
C54 was already pushed at preflight; starting divergence was 0 ahead / 0 behind
against origin/v1.1_DOTNET_SUPPORT. C55 was not pushed.

~~~
NativeAOT       9.0.0
Architecture    AMD64
GC              Workstation
GC interfaces   5.3 / 2
Runtime source  9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3
FP patch        nativeaot-amd64-fp-handoff.patch
FP patch SHA    4185495724D48E2962BA9042AF352718BF9188032DEE4C9DE6FFE9F145A1DC31
~~~

Only the productionized runtime-pack path was used. No C46/C47/C48 semantic
smoke-harness rewrite or FP repair change was made.

## C54 reclaimed-tail baseline

The semantic C40/C53/C54 range was re-established before later observations:

~~~
start              0x100900028
end                0x100943000
size               0x42FD8 / 274392 bytes
heap               0x103877B0 (representative final boot)
segment            0x104010668
initial generation  1
mapped             yes
allocator-visible  yes
initial eligible    no
~~~

The range stayed [0x100900028, 0x100943000) on all three boots. Its segment
stayed 0x104010668, generation stayed 1, and tailStillMapped=1 at the terminal
C55 eligibility snapshot. The active ordinary allocation domain stayed on
segment 0x104010710, generation 0. The C53/C54 mismatch therefore remained:
the reclaimed gen1 tail was visible to diagnostics but not eligible for the
ordinary gen0 request domain.

## Bounded natural workload

All limits were fixed before boot:

~~~
maximum allocation waves        8
maximum allocations per wave    256
maximum managed bytes           0x0000000008000000 / 134217728
survivors                       8
maximum collection observations  1024
maximum diagnostic records      3080
allocation type                 ordinary managed byte[65536]
~~~

The workload retained eight explicitly rooted byte[] survivors and released
transient peers by ordinary scope. It performed no GC.Collect, internal GC
entrypoint, condemned-generation setter, planner override, segment rotation
helper, pointer targeting, free-range injection, or allocator preference
change. Pattern write/readback was checked for every allocation. Survivor
generation and address state was sampled only after GC.CollectionCount(0)
changed. The run ended at the fixed 2,048-allocation bound.

## Generation-selection audit

The locked source function is:

~~~
gc_heap::generation_to_condemn(int n_initial,
                                BOOL* blocking_collection_p,
                                BOOL* elevation_requested_p,
                                BOOL check_only_p)
~~~

C55 inserted a scalar callback immediately before the locked return n; in
gc.cpp. It captured initial and selected generation, reason, check-only state,
blocking/elevation flags, promotion, low-ephemeral and memory/fragmentation
flags, and gen0/gen1/gen2 desired allocation, new allocation, current size,
and fragmentation state.

The audited escalation inputs and branches are:

- higher-generation allocation budgets can raise n when get_new_allocation(i)
  <= 0;
- low ephemeral space, fragmented ephemeral generations, and fragmentation
  estimates can raise the condemned generation;
- high memory load, last-GC-before-OOM, induced blocking, and elevation policy
  can raise the generation, with elevation able to reach max generation;
- gen2 budget exhaustion can raise a max-gen-1 decision to max generation;
- latency and hard-limit policy can cap or alter the result.

The stable C55 selection state was:

~~~
initial/selected generation  0 / 0
collection reason            0
check-only                   0
blocking/elevation          0 / 0
promotion                    0
low ephemeral                0
high/very-high memory       0 / 0
high fragmentation           0
gen0 desired/new             0x28000 / 0xFFFFFFFFFFFF5F50
gen1 desired/new             0x1CDB68 / 0x14DAA8
gen1 fragmentation           0xC0
gen2 desired/new             0x40000 / 0xFFFFFFFFFFFFE0A8
~~~

The three boots agreed on the selection signature and on the absence of an
older-generation selection. The natural profile repeatedly refilled/consumed
the ordinary gen0 domain without satisfying a gen1/gen2 escalation predicate.

## Collection chronology and generation boundaries

Each boot recorded 631 authoritative collection ordinals 1 through 631, all
condemned generation 0, collection reason 0, planner decision 1, and compact
phase 1. Four startup mirror records have ordinal 0 in the raw fixed-size
stream and are excluded from that chronology count.

The terminal generation relationship before and after fix_generation_bounds
was unchanged:

~~~
gen0 start  0x100A00028  segment 0x104010710
gen1 start  0x100900028 segment 0x104010668
gen2 start  0x100800028 segment 0x1040105C0
~~~

fix_generation_bounds was observed. adjust_ephemeral_limits produced no
observed invocation or change in the C55 lifecycle record
(sourceAdjust=0); no ephemeral transition marker was emitted. The first
older-generation collection ordinal, condemned generation, trigger, and
reason are not applicable: no C011EC55-OLDER-GC event occurred.

## Segment lifecycle and eligibility

The reclaimed segment state machine reached only:

~~~
S0 — gen1 reclaimed / mapped / allocator-visible / ineligible
~~~

It did not reach S1 older-generation participation, S2 boundary movement, S3
ephemeral eligibility, S5 retirement, S6 recycling, S8 consideration, S9
selection, or S10 consumption.

~~~
segment before/after        0x104010668 / 0x104010668
generation before/after     1 / 1
tail mapped after           yes
tail allocator-visible      yes
tail eligible               no
tail considered             no
tail selected               no
tail consumed               no
eligibility transition      no
retired/recycled            no / no
~~~

No allocator candidate enumeration, competing region, selection, consuming
allocation, object address, aligned size, source-generation change, or
consumption sentinel/readback event exists. The mismatch was not resolved,
and the resolving event was not observed within the bounded workload.

## Survivor promotion and integrity

Eight survivors were tracked. In the representative final boot, all selected
survivors moved from generation 0 to generation 1 while retaining valid
sentinels and managed readback:

~~~
ordinal  initial       final         gen   segment       sentinel
0        0x100A10330   0x10090040    0->1  0x104010668   0
1        0x100A20360   0x10091070    0->1  0x104010668   1
2        0x100A00028   0x100920A0    0->1  0x104010668   2
3        0x100A100C8   0x100930D0    0->1  0x104010668   3
4        0x100A200F8   0x100940100   0->1  0x104010668   4
5        0x100A30128   0x100950130   0->1  0x104010668   5
6        0x100A40158   0x100960160   0->1  0x104010668   6
7        0x100A50188   0x100970190   0->1  0x104010668   7
~~~

The inherited C26 root scan recorded four roots, four promote entries, two
category-3 roots, one register root, and one stack root. C28 mark closure
completed with zero queue invariant failures and an empty final queue. The
larger survivor set changed residency, but not root or mark correctness.

## C18, planner, restart, and safety regression

The C18 path remained valid: CoffNativeCodeManager was non-null,
FindMethodInfo returned 1, and durable caller-FP/iterator-FP repair semantics
were unchanged. No stale pRbp, null re-home, 0xFFFFFFFFFFFFFF90 runtime fault,
C18 fail-fast, or page fault occurred.

Planner and compaction evidence remained authentic for all 631 collections:
planner decision 1, compact phase 1, followed by RestartEE return and managed
resume. The C55 stream emitted 631 resume records per boot. Terminal state was:

~~~
invariant failures                0
sensitive diagnostic allocations  0
fail-fast                         none
page fault                        none
~~~

## Three-boot proof artifacts

Evidence root:
D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT\out\dotnet\c011ec55-natural-older-generation-transition\run-20260828-180815981

QEMU: QEMU emulator version 11.0.0 (v11.0.0-12122-ga4bb4b10c9)

Serial SHA-256 values, in boot order:

~~~
949D0C54022B7C3FB82EF8FF3B5BA53A9BC1AE9C424E2C9FB89EEDBF16020009
D432FCA9B4252EA9828CF442008BB40B7DD36203545073AFD860FBEB176A2257
6939E8112BD4AFFF44AAF92015F7A924C6450809B9BF6FCB0B208CEE3F165EF0
~~~

Proof payload SHA-256 values:

~~~
proof kernel  A0F51032664CD64485188BCE22E2410E84BC252F6E926ECD98F10806E4889E3C
PE            EC487AFB5B196E29B9D6370F0839E42EC912210A9AC912371041045A75C2813F
ELF           5B0FD8F9D32D48BE5D7D7EE447A3E35EACDEBC8C82B73C35A5C1CBC2289AB3D8
MAP           63700F40D5B2E18E4E96ADB17271F491BD2CA290EFC041221435EDFA80852225
~~~

The final JSON manifest is parseable and reports semantic agreement across all
three boots. The proof-only artifact is inactive after finally restoration.
Both ordinary artifacts match the canonical hash:

~~~
kernel/build/amd64/bin/kernel.elf  75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6
ESP/kernel.elf                     75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6
~~~

## C55 markers

The raw proof serial contains the bounded marker contract:

~~~
C011EC55-PREFLIGHT
C011EC55-RECLAIMED
C011EC55-LIFECYCLE
C011EC55-COLLECTION
C011EC55-OLDER-GC       absent; not proven
C011EC55-SELECTION
C011EC55-PLANNER
C011EC55-GEN-BEFORE
C011EC55-GEN-AFTER
C011EC55-EPHEMERAL      absent; no transition
C011EC55-RETIRED        absent
C011EC55-RECYCLED       absent
C011EC55-SURVIVOR
C011EC55-ELIGIBILITY
C011EC55-CONSIDERED     absent
C011EC55-SELECTED       absent
C011EC55-CONSUMED       absent
C011EC55-REUSE          absent; no consumption
C011EC55-RESUME
C011EC55                  outcome=E, successLevel=0
~~~

Markers were emitted only for states supported by the fixed-size scalar
record. The C55 wrapper reuses the C54 record without retaining a runtime
pointer or mutating generation, segment, free-list, allocation-context, or
allocator-preference state.

## Release-gate and validation posture

C55 changes only the bounded managed proof workload, fixed-size diagnostics,
source-correlated observation callbacks, and smoke-harness/parser support. It
does not change production GC semantics. The inherited C52 release baseline is
therefore the applicable control; full C52 Tier All is not claimed.

The proof runtime-pack build and three-boot validation passed the existing
production-pack identity, source/patch identity, semantic rewrite, PE-to-ELF,
linker/source/table, planner, root/mark, restart, and ordinary-restoration
guards. Managed build, PowerShell parse, JSON parse, and git diff --check were
run for the final tree. MASM was not applicable because no assembly source
changed.

## Remaining limitation and next smallest milestone

The bounded profile produced hundreds of authentic compacting gen0 collections
and promoted the eight survivors, but did not satisfy locked generation-
selection conditions for gen1/gen2 escalation. The next smallest milestone is
a slightly larger, still fixed and safe ordinary pressure window that reaches
the first condemned-gen1/full collection or equivalent ephemeral/segment
lifecycle event. If that event retires or recycles the segment, report it as
segment lifecycle rather than direct tail reuse. No Level 3–5 claim is made by
C55.
