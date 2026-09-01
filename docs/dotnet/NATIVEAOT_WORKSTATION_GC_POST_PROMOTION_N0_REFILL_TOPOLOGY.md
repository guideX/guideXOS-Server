# C011EC62 — Post-Promotion Normal-N0 Preservation / Refill Topology

Date: 2026-08-31
Repository: `D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT`

## Result

**Outcome C / Success Level 2.** C62 reproducibly proves the exact
post-promotion allocation/refill-to-N2 topology, but the tested production
workload does not preserve a safe natural post-debit `n_initial=0` entry.

The canonical R0 control is stable across three fresh QEMU boots:

`PROMOTE 0x15 → GEN1-DEBIT 0x19 → N2_COMMIT 0x0D → FIRST-N2 0x0E`

The C61 source event for `FIRST-N2` is `0x1A`; C62 event ordinals are its
bounded diagnostic timeline. R1 also preserves the promotion and debit, and
reaches the same first post-resume refill, but its smaller tail does not reach
another generation-selection entry within the bound. R2 was not run.

## Runtime identity and handoff

- NativeAOT/.NET `9.0.0`, AMD64, Workstation GC, interfaces `5.3 / 2`.
- Locked runtime source commit:
  `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`.
- Durable FP patch: `nativeaot-amd64-fp-handoff.patch`.
- FP patch SHA-256:
  `4185495724D48E2962BA9042AF352718BF9188032DEE4C9DE6FFE9F145A1DC31`.
- C61 ancestry: `3256b51aa43d9f7ba8c411f9f52a9b26e5b4f8c7`, subject
  `Trace NativeAOT promotion versus final-N0 topology`.
- C61 was already upstream before C62: branch
  `v1.1_DOTNET_SUPPORT`, upstream `origin/v1.1_DOTNET_SUPPORT`, divergence
  `0 ahead / 0 behind`.

C62 changes only the proof workload, bounded diagnostics, proof-only source
callbacks, and harness. The locked runtime checkout remains clean and the
production GC policy, allocator, free-region eligibility, and OOS path are
unchanged. No C46/C47/C48 semantic smoke rewrite is enabled.

## Bounds and strategies

The native preflight accepted these fixed bounds:

| Bound | Value |
| --- | ---: |
| maximum survivors | `48` |
| maximum retained bytes | `0x300480` |
| maximum transient allocations | `48` per bounded cohort |
| maximum transient bytes | `0x1200000` |
| maximum C62 events | `256` |
| QEMU timeout | `90 s` per boot |
| intentional GC/OOM request | none |
| strategies executed | R0 and R1; maximum allowed was 3 |

R0 retained C61 P1 geometry and used `65536`-byte ordinary requests with a
bounded `144`-allocation post-debit tail. R1 used `8192`-byte ordinary tail
requests with a `256`-allocation bound. The first retry observed after resume
is still the in-flight `0x10018` request that preceded the restart; therefore
R1 does not change that first refill request. R2, the optional earlier
headroom-preservation shape, was not run because R0/R1 already identified the
causal boundary without brute force.

## Exact R0 chronology

All three R0 boots agreed semantically. Representative first-run C62 events:

| C62 event | Event ordinal | Collection | Meaning |
| --- | ---: | ---: | --- |
| `PROMOTE` | `0x01` | `0x03` | source C61 event `0x15`, promoted `0x10018` |
| `GEN1-DEBIT` | `0x02` | `0x03` | source C61 event `0x19`, debit `0xE0150` |
| `RESUME` | `0x03` | `0x02` | `RestartEE` and managed resume observed |
| `POST-GC-ALLOCCTX` | `0x04` | `0x02` | valid context identity, empty allocation frontier |
| `ALLOC-ENTER` | `0x05` | `0x03` | first post-resume ordinary allocation |
| `GC-HEAP-ALLOC` | `0x06` | `0x03` | `GCHeap::Alloc` reached |
| `ALLOCATE-SOH` | `0x07`, `0x08`, `0x0A`, `0x0B` | `0x03` | state `0 → 4 → 1`, completion success |
| `SOH-TRY-FIT` | `0x09` | `0x03` | fit succeeds in a new/current region |
| `FIRST-ALLOC` | `0x0C` | `0x03` | rare/refill allocation succeeds |
| `N2-COMMIT` | `0x0D` | `0x04` | caller has committed to full/OOS `n_initial=2` |
| `FIRST-N2` | `0x0E` | `0x04` | source C61 event `0x1A`, entry 3 |

The logical source chronology is therefore still the C61 handoff
`PROMOTE 0x15 → GEN1-DEBIT 0x19 → FIRST-N2 0x1A`; C62 additionally records
the restart/allocation state between the debit and the caller-side entry.

## Post-GC context and first allocation

The debit-producing collection publishes:

- promoted bytes: `0x10018`;
- gen1 debit: `0xE0150`;
- gen1 new allocation before debit: `0x1AD658`;
- published gen1 new allocation after debit: `0xCD508`;
- desired gen1 allocation: `0x1CDB68`;
- `RestartEE`: observed;
- managed resume: observed.

Immediately after resume, `POST-GC-ALLOCCTX` reports a valid allocation-context
identity `0x3992CC0`, but pointer `0`, limit `0`, and remaining bytes `0`.
There is no nonempty active region in that snapshot. The corresponding C54
generation-bound snapshot for the representative R0 boot has:

- gen0 start before/after: `0x101300028 → 0x101500028`;
- gen1 start before/after: `0x100900028 → 0x100A00028`;
- gen2 start before/after: `0x100800028 → 0x100800028`;
- ephemeral segment after: `0x104010E48`;
- active allocation segment used by the successful refill: `0x104010E48`.

The first managed allocation has ordinal `1`, requested/aligned size
`0x10018`, context-fit `false`, helper path `RhpNewArray → rare/refill`,
pointer before `0`, and limit before `0x4ECEAD0`. It succeeds at representative
object address `0x101510058`; the post-allocation pointer and limit are both
`0x101520070`. The supplying region is
`[0x101500028, 0x101600000)`, with `0xD0F90` bytes remaining. It is a rare
allocation (`fast=0`, `rare=1`), not a normal-context fit.

The actual observed path is:

`RhpNewArray → RhpNewArrayRare/RhpGcAlloc → GcAllocInternal → GCHeap::Alloc → allocate_soh → soh_try_fit → successful region allocation`.

`GCHeap::Alloc` is observed, `allocate_soh()` completes successfully, and
`soh_try_fit()` returns success with `commitFailed=0`. The first allocation is
therefore not blocked by a failed SOH fit.

## Refill and N2 commit

C62 injects scalar observers into the locked-source copies of
`soh_try_fit`, `allocate_soh`, `try_get_new_free_region`, `GCHeap::Alloc`, and
`check_for_full_gc`; it does not edit the locked checkout. In R0, the first
allocation’s refill record is:

- requested generation/domain: gen0/SOH;
- requested size: `0x10018`;
- initial context fit: false;
- `soh_try_fit`: observed, result `1`, branch success, commit failure `0`;
- `allocate_soh`: observed, result `1`, terminal state `1`;
- region acquisition: a usable region is selected by the normal allocator;
- C62 `try_get_new_free_region` callback: not reached for this first refill;
- C62 free-region failure branch: not applicable and not claimed.

The predecessor C58 entry immediately surrounding the eventual N2 records
`freeRegionResult=2`, `freeRegionPath=2`, and `b12Eligible=1`. C62 intentionally
does not reinterpret that predecessor result as a successful or failed
`try_get_new_free_region` call. The exact safe statement is: the reclaimed
tail is visible to the predecessor accounting, but no suitable normal
candidate is available for this entry; no direct C62 free-region failure
branch was observed.

The first irreversible point is the caller-side C58 entry observed by
`guideXosNativeAotC011EC62EntryObserved`: C62 event `0x0D`, source branch `2`,
caller generation `2`, candidate generation `2`, collection reason `5`, and
`last_gc_before_oom=1`. The following C62 `FIRST-N2` event is `0x0E`, source
C61 event `0x1A`, entry `3`, call site `3`, `n_initial=2`, origin branch `4`.
The locked `check_for_full_gc()` callback remains available for paths that
enter that loop, but this caller-supplied full/OOS attempt does not enter it;
the C58 entry is the source/runtime commit boundary for this run.

## Pre-debit comparison

The earlier ordinary N0 entry is C58 entry 2, collection 3:

| State | Pre-debit normal N0 | Post-debit first N2 |
| --- | ---: | ---: |
| caller `n_initial` | `0` | `2` |
| request size | `0x10018` | `0x10018` |
| allocation-context remaining | `0` at C58 entry | `0` at C58 entry; C62 post-GC snapshot is also empty |
| allocation segment | `0x104010DA0` | `0x104011238` representative C58 state |
| gen1 budget | `0x1CDB68` | `0x1AD658` after publication |
| `last_gc_before_oom` | `0` | `1` |
| free-region result/path | `1 / 1` | `2 / 2` |
| B12 eligible | `0` | `1` |
| C40 tail | visible but not selected | visible but not eligible |

The exact difference is not that the fit routine suddenly fails. The debit
collection leaves no nonempty managed allocation frontier, so the first retry
must refill. The normal post-debit candidate/refill topology then has the
reclaimed C40 tail outside the requested normal domain, and the caller is
already in the OOS state (`last_gc_before_oom=1`). The caller therefore enters
`n_initial=2` before another ordinary N0 opportunity can be evaluated. This
is classified as T2 plus T6/T7 evidence: the first retry exceeds an exhausted
frontier, and the promotion/collection topology leaves the normal candidate
ineligible; it is not a `soh_try_fit` or allocator-policy failure.

## C40 reclaimed-tail relationship

The retained C40 tail is:

`[0x100900028, 0x100943000)`

with tail segment `0x104010668`. It remains mapped and allocator-visible, but
`tailEligible=0`, `tailSelected=0`, and `tailConsumed=0` in the C54/C55
predecessor records. Its prior generation is gen1; the representative later
C54 snapshot reports tail generation `1 → 2` after the subsequent collection
boundary. The tail is not forced into the C62 candidate set. Because the
first post-resume refill does not reach a direct C62 `try_get_new_free_region`
callback, C62 makes no stronger “enumerated/not enumerated” claim than the
authoritative predecessor classification: visible, mapped, but ineligible for
the observed normal request domain.

## POST_DEBIT_N0 and B02

`POST_DEBIT_N0` was not produced in R0 or R1. Consequently there is no
post-debit N0 event ordinal, call-site policy record, post-debit gen1 budget
record, or B02 evaluation at that entry. B02 did not cross; pre-B02 and
post-B02 `n` are not applicable; no later override occurred. The first N2
caller reason is `5`, and its `last_gc_before_oom` is `1`. The final condemned
generation is `2`.

This is a negative result about the tested production topology, not a claim
that no conceivable ordinary workload can ever preserve an N0. The smallest
C63 target is to alter only the ordinary *pre-restart/in-flight request and
refill timing* so the first retry after the debit has a nonempty normal
frontier or an eligible normal candidate, while retaining the same genuine
promotion and debit.

## R1 and R2

R1 retained `PROMOTE 0x15 → GEN1-DEBIT 0x19` on all three fresh boots and
recorded `RestartEE`, managed resume, the empty post-GC frontier, and the same
successful rare/refill path. Its smaller post-debit tail ran, but no later
post-debit N0 or N2 was reached within the bounded tail. R1 serial hashes:

- `D1C492794111147291A77E928D06A639285436901430991474B24230FF4BA413`
- `3C986E391A577260AC9F5134B9775FA4C158D4533EC8C55A0BCC350881B82E37`
- `50C53D92F133B827A2EBDA5F43409DA8A3D83112866DA1EC65668AA38178FAE9`

R2 was not run. No allocator, region, generation, policy, OOS, or collection
override was used in either strategy.

## Regression and release-gate posture

The R0/R1 manifests report semantic agreement and zero C62 event overflow,
invariant failures, sensitive diagnostic allocations, fail-fast, or page
fault. C18, C26, C28, C34, C37, C39, C40, C41, C53, C54, C55, C56, C57, C58,
and C61 predecessor checks pass. The valid `CoffNativeCodeManager`,
`FindMethodInfo=1`, durable FP handoff, root/mark closure, survivor readback,
planner, restart/resume, and tail observations are retained by the
productionized predecessor path. Direct gen1 was not selected, so no new
direct-gen1 compaction claim is made.

Required validation passed in the proof runs and final static checks:

- managed NativeAOT build and productionized runtime-pack build: pass;
- PowerShell parser: pass;
- JSON/XML parse: pass;
- locked runtime source and FP-patch identity: pass;
- semantic rewrite guard: pass;
- PE→ELF conversion and linker/source/table guards: pass;
- `git diff --check`: pass;
- C52 Tier All: not required; C62 did not change production runtime semantics;
- ordinary kernel/ESP restored after every proof run.

## Evidence

Canonical R0, three fresh boots:

`D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT\out\dotnet\c011ec62-r0-final2\run-20260831-195914918`

R0 serial SHA-256 values:

- `4E8FA5DF87F68B4D1FFFF57FE8CBBD7A488E9CC3D6605114784BEB8643F99E3B`
- `510518CF86DDCD953106591C64E5A82FFB7C913A6D048A518D8CF70F64F430C6`
- `AF0417158E4C91A097C9EAEAC376CBF289B4456DC26794D0E44EE7860B214DFF`

R0 proof payload hashes:

- proof kernel: `E31E267F5497F3962636A8AFD46183F5B4FA69193F857D0B8DEA6468E72F3CB0`;
- managed PE: `2D2CA3334FD44CEADD5A773A76275782EAB24C934DC010DE273F7EC953B1F23E`;
- ELF: `690304344601FB5FDAA9D96E8657DBE46D52E4D5FF99FBE0CEDEE9541DE18D05`;
- map: `17CDF48842AC4A8B1DDFB66F76D20EA093F8B180FA37A3CCEEEF6A73C38851DD`.

R1, three fresh boots:

`D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT\out\dotnet\c011ec62-r1-final4\run-20260831-193850790`

The exact commands are preserved in each evidence root’s `commands.txt`, and
each manifest preserves per-boot marker lines, event records, hashes,
restoration state, and predecessor regression results.

## Required final report

The numbered fields below correspond to the requested C62 closeout fields.

1. Outcome: C.
2. Success Level: 2.
3. Repository: `D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT`.
4. Branch: `v1.1_DOTNET_SUPPORT`.
5. Starting HEAD: `3256b51aa43d9f7ba8c411f9f52a9b26e5b4f8c7`.
6. Starting subject: `Trace NativeAOT promotion versus final-N0 topology`.
7. Final HEAD: the single focused C62 commit created after validation.
8. Final subject: `Trace NativeAOT post-promotion refill topology`.
9. Upstream: `origin/v1.1_DOTNET_SUPPORT`.
10. Starting divergence: `0 ahead / 0 behind`.
11. Final divergence: local C62 commit ahead by one; not pushed.
12. Starting worktree: clean before C62 changes.
13. Final worktree: clean after the focused C62 commit.
14. Runtime identity: NativeAOT/.NET 9.0.0, AMD64, Workstation, 5.3/2.
15. Runtime source SHA: `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`.
16. FP patch SHA: `4185495724D48E2962BA9042AF352718BF9188032DEE4C9DE6FFE9F145A1DC31`.
17. C61 ancestry: `3256b51aa43d9f7ba8c411f9f52a9b26e5b4f8c7`.
18. C61 upstream status: upstream before C62.
19. Semantic rewrite guard: pass.
20. Selected strategy: R0 control for final three-boot proof; R1 comparison also run.
21. Strategy count: two executed, R2 not run.
22. Safety bounds: 48 survivors, `0x300480` retained bytes, 48 transient allocations, `0x1200000` transient bytes, 256 C62 events, 90 s/boot.
23. PROMOTE event ordinal: C61 source `0x15`; C62 event `0x01`.
24. GEN1_DEBIT event ordinal: C61 source `0x19`; C62 event `0x02`.
25. Debit amount: `0xE0150`.
26. Published gen1 new allocation: `0xCD508`.
27. Debit-producing collection ordinal: C62/C61 `0x03` view.
28. Generation bounds after debit-producing collection: gen0 `0x101500028`, gen1 `0x100A00028`, gen2 `0x100800028`, ephemeral segment `0x104010E48` representative R0.
29. Active ephemeral segment: `0x104010E48`.
30. Active allocation segment: `0x104010E48` after refill.
31. Post-GC allocation pointer: `0`.
32. Post-GC allocation limit: `0`.
33. Post-GC remaining bytes: `0`.
34. Post-GC region: none in the restored-context snapshot.
35. Post-GC region remaining capacity: `0`.
36. `RestartEE`: observed.
37. Managed resume: observed.
38. First allocation after resume ordinal: `1`.
39. First allocation payload/requested field: `0x10018`.
40. First allocation aligned size: `0x10018`.
41. First allocation helper path: `RhpNewArray → RhpNewArrayRare/RhpGcAlloc → GcAllocInternal → GCHeap::Alloc`.
42. Fast/rare allocation: fast `0`, rare `1`.
43. Allocation-context fit: `0`.
44. `soh_try_fit` result: observed, success `1`.
45. `allocate_soh` result: observed, success `1`.
46. Refill required: yes.
47. Refill state: rare path creates/selects a normal SOH region; no allocator policy change.
48. Candidate normal regions: no C62 direct candidate count; predecessor reports no suitable normal candidate.
49. Free-region request generation/domain: gen0/SOH normal request.
50. `try_get_new_free_region` result: not reached on the first traced refill.
51. Exact free-region failure branch: not observed; not claimed.
52. Free-region failure classification: predecessor C58 result/path `2/2`, tail visible but ineligible, B12 eligible.
53. N2_COMMIT_POINT ordinal: C62 `0x0D`.
54. N2_COMMIT_POINT function: C62 observer at C58 caller entry; underlying generation-selection boundary.
55. N2_COMMIT_POINT branch: source branch `2`, caller full/OOS entry.
56. Caller `n_initial` at first N2: `2`.
57. First N2 collection reason: `5`.
58. `last_gc_before_oom`: `1`.
59. Pre-debit normal-N0 comparison state: C58 entry 2, `n_initial=0`, budget `0x1CDB68`, free-region `1/1`, B12 `0`.
60. Allocation-context difference: both C58 snapshots are exhausted; C62 post-GC identity snapshot is empty and requires refill.
61. Refill difference: pre-debit result/path `1/1`; post-debit predecessor result/path `2/2`.
62. Region-capacity difference: no nonempty post-GC frontier; refill obtains only the observed normal region, not the reclaimed tail.
63. Exact cause normal N0 disappeared: empty restored frontier plus ineligible normal candidate/OOS state, causing caller `n_initial=2`.
64. C40 tail start: `0x100900028`.
65. C40 tail end: `0x100943000`.
66. Tail segment: `0x104010668`.
67. Tail generation: gen1 before later boundary; later snapshot `1 → 2`.
68. Tail allocator visibility: visible/mapped.
69. Tail eligibility: `0`.
70. Tail enumerated by refill logic: no C62 candidate enumeration observed; no stronger claim made.
71. Tail free-region classification: visible but ineligible; predecessor path `2`.
72. R1 definition: smaller ordinary post-debit tail requests.
73. R2 definition: preserve an earlier normal refill headroom candidate; not used.
74. Request-size change: R0 65536-byte tail; R1 8192-byte tail; first in-flight retry remains `0x10018`.
75. Promotion preserved per strategy: yes, R0 and R1.
76. Debit preserved per strategy: yes, R0 and R1.
77. POST_DEBIT_N0 produced: no.
78. POST_DEBIT_N0 event ordinal: not applicable.
79. POST_DEBIT_N0 call-site ID: not applicable.
80. POST_DEBIT_N0 `n_initial`: not applicable.
81. POST_DEBIT_N0 reason: not applicable.
82. POST_DEBIT_N0 gen1 desired: no post-debit N0 record; published predecessor desired `0x1CDB68`.
83. POST_DEBIT_N0 gen1 new raw: not applicable; predecessor post-debit new raw `0x1AD658`.
84. POST_DEBIT_N0 gen1 new signed: not applicable; predecessor signed `0x1AD658`.
85. POST_DEBIT_N0 B02 margin: not applicable.
86. POST_DEBIT_N0 refill state: not applicable.
87. POST_DEBIT_N0 `last_gc_before_oom`: not applicable; first N2 is `1`.
88. FIRST_N2 event ordinal: C62 `0x0E`, C61 source `0x1A`.
89. Temporal invariant: true for the C62 causal ordering.
90. B02 crossed: no opportunity; not crossed.
91. Pre-B02 `n`: not applicable.
92. Post-B02 `n`: not applicable.
93. Later override: no.
94. Final condemned generation: `2`.
95. Collection reason: `5`.
96. Planner: predecessor planner observed; no C62 policy mutation.
97. Compact/sweep: predecessor C40/C54 chronology retained; no direct gen1 claim.
98. Compacting: no new direct-gen1 compacting result.
99. Relocating: no new direct-gen1 relocation result.
100. Direct-gen1 generation bounds: not applicable; C54 predecessor bounds retained.
101. `fix_generation_bounds`: predecessor observed/pass.
102. `adjust_ephemeral_limits`: no post-debit direct-gen1 invocation; predecessor value `0`.
103. Direct-gen1 tail generation after: not applicable.
104. Tail eligibility after: `0`, mapped and visible.
105. Survivor generation sequence: retained survivors move gen0 → gen1, with predecessor later gen2 observations.
106. Survivor integrity: reachability, sentinel/readback, movement, and zero invariant failures retained.
107. C18 result: pass.
108. Code manager: valid `CoffNativeCodeManager`.
109. `FindMethodInfo`: `1`.
110. Root scan: retained authentic C26 root scan.
111. Promoted root count: predecessor root/mark proof retained; no C62 mutation.
112. Mark closure: C28 retained and complete.
113. Managed continuation: pass after restart/resume.
114. Invariant failures: `0`.
115. Sensitive diagnostic allocations: `0`.
116. Fail-fast: `0`.
117. Page fault: `0`.
118. C62 markers: PREFLIGHT, BASELINE/STRATEGY, PROMOTE, GEN1-DEBIT, POST-GC-ALLOCCTX, RESUME, ALLOC-ENTER, GC-HEAP-ALLOC, ALLOCATE-SOH, SOH-TRY-FIT, FIRST-ALLOC, N2-COMMIT, FIRST-N2, COMPLETE.
119. Three-run QEMU result: R0 semantic agreement, 3/3 fresh boots; R1 semantic agreement, 3/3 fresh boots.
120. Serial hashes: R0 `4E8FA5DF...99E3B`, `510518CF...430C6`, `AF041715...14DFF`; R1 hashes listed above.
121. Proof artifact hashes: R0 proof kernel `E31E267F...72F3CB0`, PE `2D2CA333...B1F23E`, ELF `69030434...18D05`, map `17CDF488...851DD`.
122. Tier A/static result: pass.
123. Runtime-pack validation: productionized C52 manifest/source validation pass.
124. Full C52 Tier All: not required; runtime semantics unchanged.
125. Ordinary artifact restoration: pass after every run.
126. Semantic rewrite guard: pass.
127. PE→ELF: pass.
128. Linker/source/table guards: pass.
129. Managed build: pass.
130. PowerShell parse: pass.
131. JSON/XML parse: pass.
132. `git diff --check`: pass.
133. Ordinary kernel hash: `75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6`.
134. ESP hash: `75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6`.
135. Proof artifact active status: inactive after restoration.
136. QEMU cleanup: only C62-owned QEMU stopped.
137. Unrelated QEMU preservation: preserved.
138. Files changed: HostLogProof csproj/Program, C62 harness script, diagnostics header, C41 proof assembly observer, platform diagnostics implementation, and this document.
139. Documentation path: this file.
140. Commit hash: reported by the final repository closeout after commit.
141. Commit subject: `Trace NativeAOT post-promotion refill topology`.
142. Push status: not pushed.
143. Exact remaining limitation: no post-debit normal `n_initial=0` was observed; the first retry has an empty restored frontier and the normal reclaimed tail is ineligible, while caller OOS state is already set.
144. C63 handoff condition: preserve the same PROMOTE/debit, then change only ordinary pre-restart/in-flight request timing or earlier allocation shape until one eligible normal refill frontier survives.
145. Next smallest milestone: run one bounded R2-style earlier headroom shape, then recheck `POST_DEBIT_N0` before considering any B02 or direct-gen1 work.

## Closeout

C62 does not justify allocator, free-region, generation, OOS, or GC-policy
changes. The next experiment should remain an ordinary managed workload
shape, with the same locked runtime identity and the same C18–C61 release-gate
posture.
