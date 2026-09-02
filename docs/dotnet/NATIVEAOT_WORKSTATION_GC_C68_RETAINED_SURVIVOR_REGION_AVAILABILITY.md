# C011EC68 — Retained-Survivor Influence on Post-Debit Gen0 Region Availability

## 1. Outcome

C011EC68 stopped at its mandatory baseline gate. The requested four-survivor
control did not reproduce the C67/C66 promotion/debit baseline, so no reduced
survivor variant was run and no region-availability conclusion was drawn.

The durable classification is **Outcome E / Success Level 0**: the requested
four-reference C68 control is not a valid starting point for the primary
comparison in the current source workload.

The important discovery is a baseline mismatch. The C67 report describes its
next variable as a four-reference retained cohort, but the unchanged C67
artifact actually records a sixteen-reference retained cohort at the
promotion boundary.

## 2. Exact question and selected variable

C68 asks whether decreasing only the number of ordinary managed survivor
references entering the authentic promotion collection changes the later
Workstation-GC lifecycle enough to leave a production-discoverable entry in
`gc_heap::free_regions[basic_free_region]` at the decisive post-debit
`get_new_region(0)` request.

The independent variable is:

> retained survivor references entering the promotion collection

The requested baseline is `4`; the requested downward series is `3`, `2`, and
`1`. The series was not started because the four-reference precondition failed.

## 3. C67 provenance carried forward

C67 completed as Outcome A / Level 3 on the unchanged C66 workload. Its
source-backed candidate container was the per-heap
`gc_heap::free_regions[basic_free_region]` `region_free_list`, not a
generation-specific list. The relevant production paths were:

* `gc_heap::return_free_region` → list insertion;
* `region_free_list::unlink_region` / front unlink → candidate removal;
* `gc_heap::get_free_region` → selection;
* `gc_heap::get_new_region(0)` → the decisive gen0 request.

C67’s decisive state was request size `0x4018`, candidate count `0`, null
`get_new_region(0)` result, and `commit_failed=1`. The final observed candidate
loss was `1 → 0` through `region_free_list::unlink_region`, with state
`0xA → 0x8`. The inherited downstream path was reason 5,
`last_gc_before_oom=1`, requested generation 2, and `n_initial=2`.

The unchanged C67 rerun used for this report is at:

`out/dotnet/c011ec67-gen0-region-availability-provenance/run-20260902-045521390`

Its three serial SHA-256 values were:

* `3C95C625531EB0E8ECF520AAD50A3D8DF516CA243703B03FEEFF3097FE5B594F`
* `2F69186CA5942FA41D9716BE5634A2A34737E4EACFB5154A13D099C9B5D73A08`
* `4530757D9646A8959EF3F8761A89E3A47837416043B38026030EB4CCD6D58608`

All three agreed semantically. C67 retained zero invariant failures,
zero sensitive diagnostic allocations, zero event/snapshot overflow, and all
mutation guards passed.

## 4. Baseline mismatch found before C68 variants

The unchanged C67/C66 source workload is `C64=W3`, `C66=P2`, and
`C66TailAllocations=216`. Its managed schedule is:

* payload size: `65536` bytes;
* early retained cohort: `16` references;
* two later main cohorts with `16` survivors per cohort;
* `48` transient allocations per cohort;
* post-debit payload: `16384` bytes;
* post-debit tail: `216` allocations.

The unchanged C67 serial evidence records:

* `C011EC57-COHORT survivorCount=0x10` and retained aligned bytes
  `0x100180`;
* `C011EC62-PROMOTE survivorCount=0x10`;
* `C011EC62-GEN1-DEBIT survivorCount=0x10`;
* authentic C61 promotion/debit and the C67 Level 3 region result.

Thus the source-observed C67 baseline at the promotion boundary is 16, not 4.
The C67 report’s four-reference recommendation is not consistent with that
artifact. C68 records this discrepancy instead of silently treating a new
four-reference workload as an unchanged C67 control.

## 5. C68 managed retention implementation

C68 added a compile-time count limited to `1`, `2`, `3`, or `4`. The workload
still allocates the same early sixteen `byte[]` objects and performs the same
later P2 transient allocation schedule. Only the early `byte[][]` ownership
array retains the requested number of references; the remaining early objects
are ordinary dead managed allocations.

The later cohort allocation schedule remains inherited (`32` and `48`
cohort metadata, with the same transient waves). No fake native root, handle
table edit, forced promotion, collector policy change, candidate insertion,
candidate reorder, region-state edit, allocation-pointer edit, or
`commit_failed` edit was added. Existing `GC.KeepAlive` calls remain ordinary
managed lifetime validation.

## 6. Four-reference control evidence

Two corrected one-boot controls agreed. The final durable manifest is:

`out/dotnet/c011ec68-retained-survivor-region-availability/run-20260902-052707953/manifest.json`

The final control serial is:

`out/dotnet/c011ec68-retained-survivor-region-availability/run-20260902-052707953/first-run/serial.log`

Serial SHA-256:

`99294630C0855F382D73BD09479A331E53E4A3DDFC28D93F62D9F4984C3ACB01`

The repeated corrected control at
`run-20260902-052231472` had serial SHA-256
`24A412305440BDDCE94A30DF34B74501797033247D5F20486749BB0C3C5EFB6F` and
the same semantic result.

| Field | Corrected C68 control |
|---|---:|
| requested/early retained references | `4` |
| retained aligned bytes | `0x40060` |
| sentinel/readback integrity | pass |
| C67 baseline marker | `0` |
| authentic promotion observed | `0` |
| direct gen1 debit bookkeeping observed | `1` |
| RestartEE | `1` |
| managed resume | `1` |
| C67 outcome | `B / Level 2` |
| C68 outcome | `D / Level 0` |
| C68 observed promotion survivors | `0` |
| invariant failures | `0` |
| sensitive diagnostic allocations | `0` |

The exact C68 completion marker was:

`COMPLETE marker=C011EC68 outcome=D successLevel=00000000 retainedSurvivors=00000004 observedSurvivors=00000000 promotionObserved=00000000 debitObserved=00000001 restartObserved=00000001 managedResumeObserved=00000001 candidateAvailable=00000000 regionSelected=00000000 normalRefill=00000000 commitFailed=00000000 fullOosPreemption=00000000 requestedGeneration=00000000 nInitial=00000000 invariantFailures=00000000 sensitiveDiagnosticAllocations=00000000 completionObserved=00000001`

## 7. Promotion/debit validity

The four-reference control did not preserve the required promotion/debit
anchor. The native trace shows the inherited direct accounting debit and the
EE restart/resume path, but no C61/C62 authentic promotion transfer and no
C62 promotion record. C68 therefore sets
`authenticPromotionDebitRestartResume=false` and
`validForPrimaryComparison=false`.

This is not evidence that four survivors solve or fail region availability.
It is evidence that four references, as implemented against the current
source workload, do not reach the same promotion boundary as the unchanged
C67 workload.

## 8. Region chronology observed in the invalid control

The control still emitted bounded observational records, but they are not
used as a valid survivor comparison:

* last nonzero basic-list count: `1`;
* final pre-decisive count: `0`;
* decisive count: `0`;
* final candidate unlink: event `0x53`, count `1 → 0`, state `0xA → 0x8`;
* candidate structure: `gc_heap::free_regions[basic_free_region]`;
* lifecycle totals: 10 candidate adds, 11 removes, 0 transfers, 5 region
  creates, 0 generation transitions, 12 free-region gets, and 9 new-region
  gets;
* candidate loss was attributed to ordinary allocation, not promotion or GC;
* decisive `get_new_region(0)` saw count `0` and result `1` in this invalid
  control;
* no region was selected and no normal refill completed.

The C68 native summary reports all mutation guards false, meaning no allocator,
region, region-list, candidate, policy, or OOS-suppression mutation was used.

## 9. Causal survivor-to-region conclusion

No causal survivor-to-region link can be claimed for C68. The only supported
causal statement is the earlier gate failure:

> Reducing the source-observed early retained cohort from 16 to 4 changed the
> managed retention shape enough that the C67 authentic promotion marker was
> not reached. Because the requested four-reference control itself is invalid,
> its later candidate chronology cannot be compared to C67 as a survivor-count
> intervention.

The next evidence-backed question is therefore not “did 3 work?” It is how to
reconcile the C67 report’s stated baseline of 4 with the source-observed C67
baseline of 16. Retained-lifetime timing should remain a later milestone, not
be folded into C68.

## 10. Inherited safety and restoration checks

The corrected control retained the inherited C18/C26/C28/C46/C48 design and
the durable FP repair. It reported valid sentinel/readback integrity, zero
invariant failures, zero sensitive diagnostic allocations, no fail-fast, and
no page fault. The smoke harness restored the ordinary kernel and ESP image;
the ordinary kernel SHA-256 remained
`75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6`.

## 11. Runtime and source identity

* NativeAOT: `9.0.0`, AMD64;
* GC: Workstation, interfaces `5.3 / 2`;
* locked NativeAOT source: `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`;
* durable FP repair patch SHA-256:
  `4185495724D48E2962BA9042AF352718BF9188032DEE4C9DE6FFE9F145A1DC31`;
* C68 runtime-pack object SHA-256:
  `E4662269BECA916151F02999C40A8E80401BFC5F4A18E542B4991244FD87071A`;
* C68 specialized proof-kernel SHA-256:
  `4CAE01B43D59ABA03EF1EFDA1D03E9958775F6BD76537513320901FA8F270201`;
* semantic C46/C47/C48 rewrites: disabled.

## 12. Related commit identities

* C64: `962a83568b531e86856fd1943023fa4c6194f371`;
* C65: `5c8898de96be15c1bd99334ebe67ed0e6794be88`;
* C66: `1f23785fd22a3f74a62b017f185b0ab8511eb58e`;
* C67: `3dc01629dda0016208f7b84c06f50feafc205064`.

## 13. Final C68 status

No reduced values `3`, `2`, or `1` were run. No smallest successful survivor
count exists. No `REGION_AVAILABILITY_CHANGED`,
`POST_DEBIT_GEN0_CANDIDATE`, `POST_DEBIT_REGION_SELECTED`, or
`POST_DEBIT_NORMAL_REFILL` predicate is true.

The correct next milestone is to reconcile the source/report baseline and
then rerun a four-reference control only after the requested count and the
authentic promotion boundary agree. C68 did not test zero survivors and did
not enter the forbidden B02 search.
