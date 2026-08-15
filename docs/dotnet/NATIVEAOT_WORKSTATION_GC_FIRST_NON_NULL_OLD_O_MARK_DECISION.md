# NativeAOT Workstation GC - C011EC14 First Non-Null old_o / marked(old_o)

Final classification: **Outcome E**. The source-valid bounded workload reached
the first real queue insertion and the first null `old_o` decision, then
NativeAOT failed through `RaiseFailFastException` before another queue
insertion. No naturally valid non-null displaced `old_o` and no runtime
`marked(old_o)` read were obtained. The proof therefore did not claim a
C011EC14 safe stop and did not cross into `set_marked(old_o)`.

## 61-field handoff

1. **C011EC14 outcome:** Outcome E, a legitimate bounded continuation blocker.
2. **Starting branch:** `v1.1_DOTNET_SUPPORT`.
3. **Starting HEAD:** `684b6fb507e4158191e52489af32a894dde8fc75`.
4. **C011EC13 checkpoint:** committed as `684b6fb507e4158191e52489af32a894dde8fc75`.
5. **C011EC13 push:** succeeded to the configured upstream.
6. **Push mechanism:** Git was invoked with the existing TortoiseGitPlink/Pageant path after the default SSH invocation reported public-key denial; no credentials were changed or exposed.
7. **C011EC14 clean baseline:** the proof started from the clean C011EC13 HEAD.
8. **Locked identity:** NativeAOT `9.0.0`, AMD64, Workstation GC, interfaces `5.3 / 2`.
9. **Locked GC source:** `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`.
10. **Ordinary build hash before proof:** `161B83E992154422665B59D7E10447D6CFDC1B1AE2B33F3B54E349C3E10AA550`.
11. **Ordinary ESP hash before proof:** `161B83E992154422665B59D7E10447D6CFDC1B1AE2B33F3B54E349C3E10AA550`.
12. **Proof root:** `[ThreadStatic] byte[]? s_gcProofThreadRoot`.
13. **Additional roots/objects:** none retained; no synthetic object was created.
14. **Sentinel:** `0x100A01F38`.
15. **Storage object:** `0x100A02F50`.
16. **Fresh-run root slot:** `0x000000000393BBE0`; the earlier known baseline slot `0x394FBE0` is an address identity from the prior image, while the raw root/storage identity remained stable.
17. **Raw root:** `0x0000000100A02F50`.
18. **Queue capacity:** `16` entries.
19. **Queue semantics:** active `MARK_PHASE_PREFETCH` ring reads `old_o = slot_table[curr_slot_index]`, stores the new object, advances the cursor, returns null for null `old_o`, then otherwise reads `marked(old_o)`.
20. **Required non-null route:** a valid occupied displacement would require the first object plus sixteen additional real queue insertions before the reused slot; that route was not reached.
21. **Observed route:** one real root promotion entered `mark_object_simple`, called `queue_mark`, and inserted the real storage object into an initially empty selected slot.
22. **Observed slot/index:** source-initial ring slot `0`; the final fail-fast bridge did not emit a fabricated slot value.
23. **Cursor before/after:** source-initial `0 -> 1` for the observed first insertion; no later cursor write occurred.
24. **Old slot value:** `0x0000000000000000`.
25. **New slot value:** the real root object `0x0000000100A02F50`.
26. **Runtime `old_o`:** `0x0000000000000000`.
27. **Non-null provenance:** not applicable; no non-null displaced object was selected.
28. **Valid managed-object proof:** the retained raw root/storage object is valid; there is no claim that null `old_o` is an object.
29. **`old_o == nullptr`:** true on the observed first queue decision.
30. **Exact branch:** locked `gc.cpp:27321-27322`, the null return before `marked(old_o)`.
31. **`marked(old_o)` execution count:** `0`.
32. **Mark/header address read:** none; `rawMarkReads=0`.
33. **Raw header/method-table value:** not read and not supplied synthetically.
34. **Mark representation:** source contract is `marked(i) -> header(i)->IsMarked()`; `CObjectHeader::IsMarked` reads `RawGetMethodTable() & GC_MARKED` at `gc.cpp:4789-4792`.
35. **GC_MARKED mask/result:** not evaluated; no result was invented.
36. **Mark-state reads:** `0`.
37. **Mark-state writes:** `0`.
38. **Queue/worklist writes:** one real slot write, one real cursor write, and one observed worklist write.
39. **New C011EC14 mutation attempts:** `0`.
40. **New C011EC14 mutations executed:** `0`.
41. **Next mutation source:** locked `gc.cpp:27333`, `set_marked(old_o)`.
42. **Next mutation machine address:** not emitted because the non-null path was not reached; `currentRip=0x10004A7B` is fail-fast allocation context, not a safe-stop address.
43. **Next mutation execution:** confirmed not executed.
44. **Graph traversal:** `0`.
45. **Child-reference reads:** `0`.
46. **Child objects:** `0`.
47. **Second-object mark attempts:** `0`.
48. **Callback returns:** `0` after the target observation; callback count was `1`.
49. **Second callbacks:** `0`.
50. **ThreadStore/EE:** lock held, EE suspended, and managed entry prohibited: all `0x00000001`.
51. **Restart/resume:** restart `0`, managed resume `0`.
52. **C011EC14 marker:** not reached; no C011EC14 safe-stop marker was emitted.
53. **Safe-stop address:** not applicable; the run stopped at the permitted bounded fail-fast classification instead of a C011EC14 marker.
54. **Fail-fast boundary:** PAL reason `0x47435354`, classified as `RaiseFailFastException` before the second queue insertion.
55. **QEMU:** version `11.0.0` (`v11.0.0-12122-ga4bb4b10c9`).
56. **Fresh boots:** three fresh boots, all Outcome E with the same one-insertion/null-old_o result.
57. **Proof kernel hash:** `E4EA49AA2233E37E339F887A04F6DCC6E23DA6E8A5EC80B5378A5E1D15F7D317`.
58. **Manifest:** `out/dotnet/gc-first-root-first-non-null-old-o/run-20260814-180002257/manifest.json`.
59. **Report:** `docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_NON_NULL_OLD_O_MARK_DECISION.md`.
60. **C011EC13/C011EC12/C011EC10 regressions:** respectively Outcome B `3/3`, Outcome D `3/3`, and Outcome A `3/3` fresh QEMU boots; their latest manifests are retained under the corresponding `out/dotnet/gc-first-root-*` directories.
61. **Historical evidence, restoration, checks, files, Git, and next milestone:** historical stale-cache, first-64-KiB, native-stack, local-storage, C011EC11, and C011EC09 classifications remain retained and accurately labeled; ordinary build and ESP were restored to `161B83E992154422665B59D7E10447D6CFDC1B1AE2B33F3B54E349C3E10AA550`; `git diff --check` is required to pass; changed files are the C14 proof script, diagnostics/platform/PAL bridge, managed mode constants, this report, and selected generated evidence; the C14 checkpoint commit/push are recorded in the final handoff; **recommended next milestone: establish a source-valid pre-drain route that supplies multiple real root promotions before the continuation/fail-fast boundary, without queue seeding or graph traversal**.

## Source contract and bounded result

The locked queue declaration is `gcpriv.h:1487-1504`; the active one-argument
definition is `gc.cpp:27303-27335`. It captures `old_o` at `:27316-27317`,
writes the selected slot at `:27318`, advances `curr_slot_index` at `:27320`,
returns null at `:27321-27322`, reads `marked(old_o)` at `:27328`, and reaches
`set_marked(old_o)` at `:27333`. `GCHeap::Promote` is the locked
`gc.cpp:49474-49544` path, and `mark_object_simple` is `gc.cpp:27987-28029`.

The C14 observer recorded the real callback, the real storage object, one real
queue insertion, one null decision, and one mark-helper entry. It did not seed
the queue, fabricate a slot, fabricate an object, enumerate children, drain a
graph, complete a callback after the target, call `RestartEE`, or resume
managed execution. The fail-fast current RIP is retained only as context for
the blocked continuation and is not mislabeled as a C011EC14 safe stop.

## Retained evidence

The primary C14 evidence root is:

`out/dotnet/gc-first-root-first-non-null-old-o/run-20260814-180002257/`

It contains the manifest, exact command log, build/source traces, QEMU
version, three serial logs and hashes, and ordinary-restoration evidence. The
focused regression evidence is retained at:

- `out/dotnet/gc-first-root-post-queue-mark-decision/run-20260814-174936183/`
- `out/dotnet/gc-first-root-first-mark-mutation/run-20260814-175210499/`
- `out/dotnet/gc-first-root-condemned-generation-decision/run-20260814-175443587/`
