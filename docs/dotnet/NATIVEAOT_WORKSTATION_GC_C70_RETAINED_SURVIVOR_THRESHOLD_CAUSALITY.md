# NativeAOT Workstation GC C70: Retained-Survivor Threshold and Gen0 Region-Availability Causality

Date: 2026-09-03
Repository: `D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT`
Branch: `v1.1_DOTNET_SUPPORT`
Experiment: `C011EC70`
Result: **Outcome A / Level 3**

## Result

C70 established an exact adjacent threshold in the tested domain:

* `N_anchor = 16`: the smallest tested count that reproduced the C69 Level-3 promotion-and-region-exhaustion anchor.
* `N_below = 15`: the adjacent count that did not produce authentic promotion and did not enter the C69 region/OOS chronology.
* `N_promotion = 16` within the tested boundary interval `[15,16]`.
* `N_region = 16` for the C69 production class of six basic free regions after `RestartEE`.
* `N_oos = 16`.
* A standalone global `N_exhaust` is not claimed. The C69-comparable decisive-exhaustion threshold is `16`; the altered no-promotion paths at tested counts 4–15 also performed a terminal 1-to-0 candidate unlink, but they did not reach the C69 decisive context-exhaustion/OOS state. Counts 1–3 were correctly skipped after the boundary was proven.

The first directly survivor-dependent production GC accounting state was promotion accounting. At 16, the run promoted 16 unique objects (`0x100180` bytes); at 15, it promoted zero objects and zero bytes. The C26 stack-derived/promoting-root metric remained `4` in both cases and is a separate metric, not the C70 independent variable.

The complete observed boundary chain is:

`16 retained references -> 16 promoted objects / 0x100180 bytes -> six basic free regions after RestartEE -> last candidate consumed -> get_new_region(0) sees zero candidates and fails commit -> no region selection/refill -> reason-5 full/OOS path -> requested and condemned generation 2`.

At 15, the same controlled workload retained 15 references but had no authentic promotion anchor; it resumed normally, selected a region on the later path, and did not enter OOS. The promotion-to-region and downstream arrows are supported by this controlled adjacent pair, but are not independently isolated structural equations because the lower boundary run is a no-promotion path.

## C69 anchor and controls

C69's proven control was the exact 16-reference retained managed cohort on NativeAOT 9.0.0, AMD64, Workstation GC. Its relevant measurements were:

| Measurement | C69 control |
| --- | ---: |
| Retained managed references | 16 |
| C26 stack-derived/promoting-root metric | 4 |
| Unique promoted objects | 16 |
| Promoted bytes | `0x100180` |
| Gen1 budget before / debit / after | `0x1AD658` / `0xE0150` / `0xCD508` |
| Generation boundary | Gen0 -> Gen1 |
| RestartEE / managed resume | yes / yes |
| Region transitions | 3 |
| Basic free regions before / after promotion | 0 / 0 |
| Basic free regions after RestartEE / managed resume | 6 / 6 |
| Decisive candidate count | 0 |
| Normal refill | no |
| OOS / reason | yes / `0x5` (`reason_oos_soh`) |
| Requested / condemned generation | 2 / 2 |

C70's first 16-reference boot was a fresh build and fresh QEMU boot, not inherited C69 evidence. The first control reproduced these semantic facts, and the final confirmation pair was rebuilt after the complete C70 marker set was enabled.

## Independent variable and held-constant configuration

The only intended behavior-changing variable was the number of ordinary managed references retained across the promotion boundary. The managed proof allocates the same 16 objects, with the same object type, size, payload, schedule, and pressure waves for every variant; only the number of entries retained in the ordinary managed `survivors[]` array changes.

Held constant:

* C61/C65 promotion and post-debit schedule;
* C66 strategy `P2`;
* C64 variant `W3`;
* C66 tail allocation count `216`;
* object type, allocation size, payload, collection timing, and later pressure;
* NativeAOT 9.0.0, AMD64, Workstation GC;
* locked runtime pack and durable FP repair;
* C46/C47/C48 semantic rewrites disabled;
* all native observer code source-accounting only.

The C26 stack-derived metric stayed `4` across the tested sweep. The C70 managed control marker reported 16 allocated objects and 16 distinct objects for every variant, with aliases `0`; the retained count is therefore not a fabricated root count or a duplicate-reference count.

## Sweep and narrowing

The required coarse sweep ran in this order: `16 -> 12 -> 8 -> 4`. The count-16 control passed before lower counts were interpreted. The first changed interval was `[12,16]`; the adaptive midpoint `14` was tested, followed by `15` to make the boundary adjacent. No counts below 4 were run because the exact adjacent boundary was established at 15/16.

| Test order | Retained count | Discovery/confirmation role | Result |
| ---: | ---: | --- | --- |
| 1 | 16 | fresh C70 control | A / Level 3 |
| 2 | 12 | coarse sweep | D / Level 0 |
| 3 | 8 | coarse sweep | D / Level 0 |
| 4 | 4 | coarse sweep | D / Level 0 |
| 5 | 14 | midpoint in `[12,16]` | D / Level 0 |
| 6 | 15 | adjacent lower boundary | D / Level 0 |
| 7 | 16 | three-boot boundary confirmation | A / Level 3 |
| 8 | 15 | three-boot boundary confirmation | D / Level 0 |

The discovery boots were one fresh boot per count. The final 16 and 15 boundary variants were each run as three fresh QEMU boots, with semantic agreement required within each variant.

## Per-count authentic measurements

The table uses the latest named-marker manifest for each count. `Roots` is the C26 stack-derived/promoting-root metric. `Gen` is the observed survivor generation before/after; lower-count values are not interpreted as authentic C69 promotion boundaries when `promotionObserved=0`.

| Retained | Roots | Promoted objs | Promoted bytes | Gen1 before / debit / after | Gen | Before | After | Post-Restart | Resume | Transitions | OOS | Class |
| ---: | ---: | ---: | ---: | --- | --- | ---: | ---: | ---: | ---: | ---: | --- | --- |
| 16 | 4 | 16 | `0x100180` | `0x1AD658 / 0xE0150 / 0xCD508` | 0 -> 1 | 0 | 0 | 6 | 6 | 3 | yes | A / 3 |
| 15 | 4 | 0 | `0x0` | `0x0 / 0x0 / 0x0` | 0 -> 1 | 1 | 1 | 0 | 0 | 0 | no | D / 0 |
| 14 | 4 | 0 | `0x0` | `0x0 / 0x0 / 0x0` | 0 -> 1 | 1 | 1 | 0 | 0 | 0 | no | D / 0 |
| 12 | 4 | 0 | `0x0` | `0x0 / 0x0 / 0x0` | 0 -> 1 | 1 | 1 | 0 | 0 | 0 | no | D / 0 |
| 8 | 4 | 0 | `0x0` | `0x0 / 0x0 / 0x0` | 0 -> 1 | 1 | 1 | 0 | 0 | 0 | no | D / 0 |
| 4 | 4 | 0 | `0x0` | `0x0 / 0x0 / 0x0` | 0 -> 0 | 1 | 1 | 0 | 0 | 0 | no | D / 0 |

All six variants reported actual retained references equal to the configured count, 16 managed slots, 16 allocated objects, 16 distinct objects, and zero aliases. Every run retained the C26 value of 4 as a separate stack-derived metric.

## Boundary chronology: 16 retained references

The authoritative final boundary evidence is `out/dotnet/c011ec70-retained-survivor-threshold/run-20260903-041115165`.

Promotion provenance:

* configured and actual retained references: `0x10` / `0x10`;
* C26 stack-derived/promoting-root metric: `0x4`;
* authentic promotion: observed;
* unique promoted objects: `0x10`;
* promoted bytes: `0x100180`;
* gen1 budget before / debit / after: `0x1AD658` / `0xE0150` / `0xCD508`;
* generation boundary: `0 -> 1`;
* debit, RestartEE, managed resume, and survivor integrity: all observed/pass.

Basic-free-region snapshots:

* before promotion: count `0`, head `0`, tail `0`, total regions `12`, gen0 regions `9`;
* after promotion: count `0`, head `0`, tail `0`, total regions `12`, gen0 regions `9`;
* after RestartEE: count `6`, head/tail `0x104011040`, total regions `5`, gen0 regions `1`;
* managed resume: count `6`, head/tail `0x104011040`, total regions `5`, gen0 regions `1`;
* during allocation pressure: count `0`, head `0`, tail `0`;
* after allocation pressure: count `0`, head `0`, tail `0`.

Candidate and refill chronology:

* last nonzero candidate marker: count `6`, event ordinal `0x168`, checkpoint `7`;
* final candidate unlink: event ordinal `0xC8`, region `0x104011238`, count `1 -> 0`, state `0xA -> 0x8`, source `region_free_list::unlink_region`;
* decisive context exhaustion: observed, request size `0x4018`, active region `0x104011238`, active headroom `0`;
* `get_new_region(0)`: observed, entering candidate count `0`, head/tail `0`, result `0`, `commit_failed=1`;
* post-debit candidate: unavailable, count `0`, head/tail `0`, eligible `0`, identity count `0`;
* candidate selection: no; selected region `0`;
* selected generation field: `0` (no selection occurred);
* post-debit normal refill: result/observed/completed/succeeded all `0`, `commit_failed=0`;
* reason: `0x5`, full/OOS observed, `last_gc_before_oom=1`;
* requested generation: `2`; `GarbageCollectGeneration`: `2`; final condemned generation: `2`.

The last-nonzero marker intentionally records the semantic list count and event checkpoint; its legacy region field is `0`. The authoritative terminal candidate identity is the actual unlink region `0x104011238`, not pointer equality across builds.

## Boundary chronology: 15 retained references

The authoritative final boundary evidence is `out/dotnet/c011ec70-retained-survivor-threshold/run-20260903-041349938`.

Promotion provenance:

* configured and actual retained references: `0xF` / `0xF`;
* C26 stack-derived/promoting-root metric: `0x4`;
* authentic promotion anchor: not observed;
* unique promoted objects, promoted bytes, and gen1 budget/debit values: all `0`;
* observed survivor generation fields: `0 -> 1`;
* debit, RestartEE, managed resume, and survivor integrity: observed/pass, but without an authentic promotion anchor.

Basic-free-region snapshots:

* before promotion: count `1`, head/tail `0x104010E48`, total regions `5`, gen0 regions `3`;
* after promotion: count `1`, head/tail `0x104010E48`, total regions `5`, gen0 regions `3`;
* after RestartEE: count `0`, head/tail `0`, total regions `10`, gen0 regions `8`;
* managed resume: count `0`, head/tail `0`, total regions `10`, gen0 regions `8`;
* during pressure: count `0`, head/tail `0`, total regions `9`, gen0 regions `7`;
* after pressure: count `0`, head/tail `0`, total regions `10`, gen0 regions `8`.

Candidate and refill chronology:

* last nonzero candidate marker: count `1`, event ordinal `0xB8`, checkpoint `7`;
* final candidate unlink: event ordinal `0x3E`, region `0x104010E48`, count `1 -> 0`, state `0xA -> 0x8`, source `region_free_list::unlink_region`;
* decisive context exhaustion: not observed; request size `0x10018`, active region `0x104010710`, active headroom `0`;
* `get_new_region(0)`: observed, entering candidate count `0`, observed head/tail `0x1040110E8`, result `1`, `commit_failed=0`;
* post-debit candidate: available `0`, count `0`, observed head/tail `0x1040110E8`, eligible `1`, identity count `0`;
* candidate selection: yes; selected region `0x104010CF8`, result `1`, generation `0`;
* post-debit normal refill: result/observed/completed/succeeded all `0`, `commit_failed=0`;
* reason: `0x0`; full/OOS not observed; `last_gc_before_oom=0`;
* requested generation: `0`; final condemned generation: `0`.

This path demonstrates why the raw terminal 1-to-0 unlink must not be mislabeled as the C69 exhaustion anchor: 15 has no authentic promotion, no six-region post-RestartEE class, no decisive context exhaustion, and it selects a region instead of taking the C69 full/OOS path.

## First production divergence

The exact neighboring comparison is 16 versus 15.

The earliest directly survivor-dependent production GC state is the promotion census:

| State | 16 retained | 15 retained |
| --- | ---: | ---: |
| Actual retained references | 16 | 15 |
| C26 stack-derived metric | 4 | 4 |
| Authentic promotion observed | 1 | 0 |
| Unique promoted objects | 16 | 0 |
| Promoted bytes | `0x100180` | `0x0` |
| Gen1 debit | `0xE0150` | `0x0` |

The `C70_PRE_PROMOTION_BASIC_COUNT` snapshot also differs at the same logical boundary (`0` for 16 and `1` for 15), so the raw region state is recorded as an earlier observed difference. It is not used as the first causal arrow because it is a pre-promotion layout snapshot and is not independently normalized against the later no-promotion path. The first clean, attributable production state is therefore promotion accounting: the retained ownership change changes the authentic promoting cohort from 16 objects/bytes to none.

Downstream paired consequences are:

* post-promotion basic count: `0` versus `1`;
* post-RestartEE basic count: `6` versus `0`;
* final C69 candidate unlink: `1 -> 0` followed by context exhaustion at 16, versus a non-C69 terminal unlink and no context exhaustion at 15;
* decisive `get_new_region(0)`: no candidate/result `0`/`commit_failed=1` versus observed stale/list metadata with result `1`/`commit_failed=0`;
* region selection: no versus region `0x104010CF8`;
* full/OOS escalation: yes/requested generation 2 versus no/requested generation 0.

## Causal interpretation and limits

Supported arrows, in order of confidence:

1. Retained survivor count -> authentic promoted object/byte quantity. This is directly quantified at 16 versus 15, with the independent count marker and the C26 metric held separate.
2. Promotion quantity -> the paired post-promotion and post-RestartEE region classes. The 16 path has 0/0 then 6 basic free regions; the 15 path has 1/1 then 0. This is strong controlled evidence, but the adjacent lower path does not authenticate promotion, so it is not a standalone byte-to-region law.
3. Region class -> candidate chronology. The 16 path has three region transitions and consumes the C69 candidate path to zero; the 15 path has no transitions and selects a region on its altered path.
4. Candidate exhaustion -> refill result. At 16, no candidate is available and no normal refill occurs; at 15, no normal refill marker is claimed, but a region is selected and the C69 context-exhaustion predicate is absent.
5. Refill/selection result -> OOS/requested-generation behavior. The 16 path reaches reason-5/full/OOS and generation 2; the 15 path does not.

The strongest complete measured chain is the 16/15 paired chain shown at the start of this document. The first unsupported link is an independent, promotion-persisting region threshold: C70 did not find a count below 16 that still promoted while changing only the downstream region chronology. That is the correct next-phase question; C70 does not answer it by inference.

## Instrumentation restrictions and inherited invariants

Native C70 observers are bounded and source-accounting only. They do not allocate from, reserve, inject into, or mutate the allocator, allocation contexts, regions, free lists, states, candidates, policy, promotion, roots, mark closure, expansion, OOS behavior, reason codes, `commit_failed`, `last_gc_before_oom`, requested generation, or full-GC policy. No B02 experiment was performed.

Every authoritative run reported:

* allocator mutation: `0`;
* region mutation: `0`;
* region-list mutation: `0`;
* candidate mutation: `0`;
* policy mutation: `0`;
* survivor fabrication: `0`;
* root fabrication: `0`;
* OOS suppression: `0`;
* requested-generation mutation: `0`;
* invariant failures: `0`;
* sensitive diagnostic allocations: `0`;
* fail-fast: `0`;
* page faults: `0`.

The inherited C18 authentic managed-PC check, valid `CoffNativeCodeManager`, `FindMethodInfo`, authentic root scan, mark closure, planner authenticity, and survivor-integrity checks all passed in the proof manifests. The C26 stack-completion prerequisite remained present and semantically agreed in all C70 runs.

## Evidence and artifact identity

Every count has an independent evidence directory and manifest. The latest authoritative manifest for each count is:

| Count | Evidence directory | Class |
| ---: | --- | --- |
| 16 | `out/dotnet/c011ec70-retained-survivor-threshold/run-20260903-041115165` | A / 3 |
| 15 | `out/dotnet/c011ec70-retained-survivor-threshold/run-20260903-041349938` | D / 0 |
| 14 | `out/dotnet/c011ec70-retained-survivor-threshold/run-20260903-035932544` | D / 0 |
| 12 | `out/dotnet/c011ec70-retained-survivor-threshold/run-20260902-201313868` | D / 0 |
| 8 | `out/dotnet/c011ec70-retained-survivor-threshold/run-20260902-202030333` | D / 0 |
| 4 | `out/dotnet/c011ec70-retained-survivor-threshold/run-20260902-202811788` | D / 0 |

The proof artifact hashes recorded in those manifests are:

| Count | Proof kernel | PE | ELF | Map |
| ---: | --- | --- | --- | --- |
| 16 | `30729AE1EE5F061005C134585E2FE0A3DBAEBE8D8BE894FE97524EAB34744D38` | `5C943236BDE99F0ACAAE5F9617D3C32EBB108DD5730E63867E93B85B3EE5A151` | `05AB46304BC9AB1F0A0A472EAE2E9045E51A7720AA16261037CF851D5DCF8050` | `B0B7499B25EBA0DD13FD1BC0AF59545EEBDDF80F4DAF659FAB59225AA684BEEB` |
| 15 | `8DBFED0E3008D96E3E5EADDFD834F5644C68CB23812E67BDF32FBEF2EDABBADF` | `1DFE81B4A00E9B6806CD7BC880AE9770C4ACAD09D06396272427EC14414A23B1` | `9E89A14E2CEA20CEFA17A182C3372AF073543BC28A09BB837EA1AA70DE3318C6` | `355340054EF0DA183D175193AEE2EF8D6A553D50879D3CA96B21C21369F7B2B2` |
| 14 | `967E06D1D5B0419B981516CE58A9644C5437B859DFE5BD4334A2D0D3E24475AE` | `D0204BC6A351B0048CA3913DD3EE5BD40402465BD41B0ADCEEEC2A0A9C4A52CC` | `9A18948B03B24A24A02DBB5B0BBED97D48C53D5DA9DBE33ACA4DF1C026F7CA03` | `FD8B43348433516FFA7BB766F57B60A4E9D69B571FA92562DED2C933FDB35B3C` |
| 12 | `411710369612C9E19721B12F9BBFF6740B71259EDF0209CE6514F8A8E8C03402` | `B84BBC23A499390B48DE8E8BBCE324D57E686C138405B4B4A2C574C935898B4C` | `491C931E3A44D32D79FD6E44462B4AC9B31CD6CA5AAB20F0C915C8A46CB4DAAC` | `88A38B7F873E2C03D8BF933479DF211CA50CA1C17CEEF9FBE5873C7B89540EB5` |
| 8 | `7420C5173298C596D28E6244D21E6B6D9232300E8AFD34C2C93BAC8B7D61135F` | `105322BB0644903C2A0E0F3B5F0A8ECE84C84CBAAF3DD0227A1097CF4E4E0AA8` | `5C0818A327C7585984115E0C49B0164D3FE63096A73E9543599742BD8C5F63EC` | `1833E2363174DDCB1B58CF76D8D1E2A580B30488D067033E737DDCC173993329` |
| 4 | `AE7B7962C7DA093EBC3060D3D33B4DBC931874FE95C6205B44E0879238236439` | `84BB581D3F27DC804D170DEA85E396F1294CA1A8F7458E0F1EBC8F1639ABD15E` | `AD30AC6AAD925EB709146B069EA9123F50440BCB39F8501DB087066C878B1B0B` | `32BC302E5C19414132B9D8F41E9BCD805337A56285B468986CBF9A79944390B5` |

The manifests also retain three serial SHA-256 values per confirmation boundary. The 16 serial hashes are `C5DDB4FC0E96F1B604FB01A302907F8092A3D2BD1D2569A40B858056086E0F9C`, `109FBB8DD32B1F43AD042D51852F9A4C0D3575DC4B47E5F88CC74C9D0A56DB9E`, and `DE4714AEF2BC38B4F9EFF6C62DDA2194526F4E4935FF1D8751E6E0BCE8FB11B1`. The 15 serial hashes are `046E9E0C5F0436F820B6E406F18064239D94032A924DAAE20B1C211C0D081069`, `5FDDB4B02BAC932079C683EB003A1CB9B432938E2517F268F088EB47A235E38D`, and `4E1A291892A97EE5120FAB4A66E7C2836C841308F4724CACDD1231FD3A695C1C`.

## Validation and restoration

The C70 runs performed and recorded the applicable validation gates:

* locked runtime-pack identity and FP-repair guard: pass;
* managed build: pass for every proof artifact;
* native runtime-pack and kernel build: pass;
* PE -> ELF conversion: pass;
* symbol and linker checks: pass;
* source/table/archive guards: pass;
* serial marker parsing and semantic agreement: pass;
* proof kernel/PE/ELF/map hashing: pass;
* ordinary non-proof kernel and ESP restoration: pass;
* no C70-owned QEMU processes left running; unrelated QEMU instances were not targeted.

PowerShell syntax, JSON/XML parse, and `git diff --check` are rerun at closeout after this documentation change. C52 Tier-All was not rerun: C70 changes only a compile-time managed retained-count selector, bounded proof observers, marker parsing, and evidence documentation; it does not modify production GC behavior, and the applicable runtime-pack/source/FP guards and C70 full build gates passed.

The ordinary kernel and ESP were restored to the established hash:

`75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6`

The proof artifact is inactive after restoration.

## Files and closeout

Production behavior was not changed. C70-owned changes are limited to the compile-time managed selector, the C70 proof-mode script/build plumbing, bounded native observers, and this report. The final local closeout commit and exact final HEAD are reported by the task that created this document. No push was performed.
