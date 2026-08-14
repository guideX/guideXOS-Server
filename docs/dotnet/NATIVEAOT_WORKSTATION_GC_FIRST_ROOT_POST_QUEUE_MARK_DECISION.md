# NativeAOT Workstation GC — C011EC13 Post-Queue Mark-State Decision

Final classification: **Outcome B**. The genuine NativeAOT thread-static
storage-object root was inserted into the real Workstation prefetch queue. The
displaced `old_o` was null, so the source-valid null branch completed and
`marked(old_o)` was skipped. The proof stopped before any later mutation,
logical marking, child traversal, second object, callback return, restart, or
managed resume.

## 57-field handoff

1. **Outcome:** B — `old_o == nullptr`; no new mutation after C011EC12.
2. **Starting HEAD / branch / worktree:** `cae40ef7df63f41c5fbfcf32d67400fc933f0998` / `v1.1_DOTNET_SUPPORT` / clean at task start.
3. **C011EC12 committed:** yes; it was already included in starting HEAD.
4. **Ending worktree:** intentionally uncommitted; source instrumentation, this report, and retained proof evidence are present.
5. **Ordinary starting hashes:** build and ESP both `161B83E992154422665B59D7E10447D6CFDC1B1AE2B33F3B54E349C3E10AA550`.
6. **Locked identity:** NativeAOT `9.0.0`, AMD64, Workstation GC, interfaces `5.3 / 2`, source `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`.
7. **Proof-root field / sentinel / storage object:** `[ThreadStatic] byte[]? s_gcProofThreadRoot`; sentinel `0x100A01F38`; storage object `0x100A02F50`.
8. **Root slot / raw root:** slot `0x394FBE0`; raw root `0x100A02F50`.
9. **Find-range:** true (`0x00000001`).
10. **WKS heap sentinel:** `MULTIPLE_HEAPS=0`, `hpt=0`, `heap_of(o)=0`, heap count `1`, null heap sentinel valid.
11. **Condemned generation:** true; object generation `0`, condemned generation `0`, maximum `2`.
12. **C011EC12 prerequisite:** one authentic queue-slot write and one cursor write; first mutation executed once; no mark-state read/write, traversal, or second object.
13. **`queue_mark` contract:** `gc.cpp:27316` captures `old_o = slot_table[slot_index]`; `:27318` stores `o`; `:27320` advances the cursor; `:27321-27322` returns null for null `old_o`; otherwise `:27328` reads `marked(old_o)` and `:27333` may set its mark bit.
14. **Queue slot address / index:** `0x10256260` / `0`.
15. **Cursor before / after:** `0 / 1`.
16. **Old slot value:** `0x0`.
17. **New slot value:** `0x100A02F50`.
18. **`old_o` meaning:** the previous contents of the selected `slot_table[slot_index]`, captured before insertion; it is the displaced queue entry.
19. **Runtime `old_o`:** `0x0`.
20. **Equality:** `old_o == oldSlotValue` is true.
21. **Null test:** one test, result true (`nullTests=1`, `nullResult=1`).
22. **Branch:** source null return (`if (old_o == nullptr) return nullptr`), branch counter `1`.
23. **`marked(old_o)` executed:** no; request, entry, return, and mark-state-read counts are all zero.
24. **`marked` declaration / definition:** not runtime-reached; source contract is `gc.cpp:11587` macro `marked(i) -> header(i)->IsMarked()`, with `CObjectHeader::IsMarked` at `gc.cpp:4789-4792`.
25. **Mark-state representation:** object-header raw method-table word tested with `GC_MARKED`; not a bitmap, region table, worklist, or card/brick read at this boundary.
26. **Mark-state reads:** `0`.
27. **`marked` result:** not evaluated; no synthetic result was supplied.
28. **Object-header / method-table reads added:** `0 / 0`.
29. **Segment / region reads added:** `0 / 0`.
30. **Inherited C011EC12 writes:** `2` total — one slot write plus one cursor write.
31. **New post-C011EC12 mutation attempts / executions:** `0 / 0`.
32. **Mark-bit writes:** `0`.
33. **Additional queue/worklist writes:** `0`; no second insertion, overflow, or traversal work item.
34. **Logical mark completion:** false / `0`; queued is not claimed to mean marked.
35. **Next mutation-capable operation:** `set_marked(old_o)` after the non-null/unmarked path, source `gc.cpp:27333`; it was not executed.
36. **Next mutation helper / instruction:** inline `mark_queue_t::queue_mark` / `or QWORD PTR [r13+0x0],0x1` at `0x1004DAA3`; the false-boundary helper is non-returning in the proof image.
37. **Graph traversal:** `0`.
38. **Child-reference reads:** `0`.
39. **Child objects discovered:** `0`.
40. **Second-object mark attempts:** `0`.
41. **Callback returns / second callbacks:** `0 / 0`.
42. **ThreadStore / EE:** lock held, EE suspended, managed entry prohibited; one registered, enumerated, and included thread.
43. **Restart / resume:** `0 / 0`.
44. **Object/sentinel validation:** zero sentinel failures, zero before/after-fixup validation failures, zero duplicate addresses, zero history overflow.
45. **C011EC13 marker / reason:** `C011EC13`; one post-queue decision completed, then safe-stopped in the null branch before `marked`, later mutation, traversal, or second object.
46. **QEMU runs / results:** QEMU `11.0.0` (`v11.0.0-12122-ga4bb4b10c9`), three fresh boots, all Outcome B / C011EC13.
47. **Proof-kernel / serial hashes:** proof kernel `82794EBBD42842814EF4E4DC36F2CFC767972E2B8F96F159872553C130606B9F`; serials `1A9CFC0664B2A617DC5840EA73E15FC863FBE096B43C744071B591ABB5AA6CF9`, `C3A5577E717DE709FE94C37ECE4812947F326DF2B3229474370B1336D7C1BEAC`, `D2A32411C41262DB34D8D6BECE1084D51F3D5F3C793CDFD38C6B81F4AD08132C`.
48. **Regression results:** C011EC12 fresh focused run completed three C011EC12 boots in `run-20260812-213216189`; C011EC10 fresh focused run completed three Outcome-A boots in `run-20260812-213937814`; C011EC13 passed 3/3.
49. **Historical / non-clean validators:** C011EC11 fresh retry was non-clean before QEMU because of a concurrent kernel dependency-file race; C011EC09 reached QEMU and emitted its valid marker but its strict wrapped-line suspension validator rejected the serial text. Earlier committed/historical evidence is retained, not relabeled by this report. Older validators remain historical/blocked where their hashes or assertions are obsolete.
50. **Retained failures:** stale-cache attempts, initial runtime-pack identity mismatch, historical first-64-KiB failure, native-stack PowerShell/compiler-stderr non-clean result, local-storage teardown non-clean evidence, the fresh C011EC11 build-race evidence, and the fresh C011EC09 wrapped-line validator failure.
51. **Ordinary restoration:** build and ESP restored to `161B83E992154422665B59D7E10447D6CFDC1B1AE2B33F3B54E349C3E10AA550`; no QEMU proof process remained after final checks.
52. **Documentation:** `docs/dotnet/NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_POST_QUEUE_MARK_DECISION.md`.
53. **Manifest:** `out/dotnet/gc-first-root-post-queue-mark-decision/run-20260812-212842987/manifest.json`.
54. **Files changed / diff summary:** three proof-instrumentation files plus this report; generated evidence is retained under the C011EC13 evidence root; no production-only behavior was changed.
55. **`git diff --check`:** pass.
56. **Commit created:** no.
57. **Recommended next milestone:** one bounded continuation from the null branch only if a proof-safe boundary can be placed before callback return; otherwise preserve C011EC13 as the queued-but-not-logically-marked frontier.

## Source and machine trace

The locked `mark_object_simple` declaration is `gcpriv.h:2729` and its
definition is `gc.cpp:27987-28029`. `mark_queue_t` is `gcpriv.h:1487-1504`;
the active one-argument `queue_mark` is `gc.cpp:27303-27335`. The selected
AMD64 proof image recorded:

- helper `mark_object_simple`: `0x1004D990`;
- inherited slot store: `0x1004DA10`, `mov QWORD PTR [rbx],rsi`;
- inherited cursor store: `0x1004DA21`, `mov QWORD PTR [rip+0x2088b8],rax`;
- first active null test: `0x1004DA69`, `test r13,r13`;
- null branch: `0x1004DA6C`, `je 0x1004E1E2`;
- null observer call: `0x1004E1E4`, helper `0x100164D0`;
- safe-stop address: `0x10011A20`;
- next non-null-path mark-bit instruction: `0x1004DAA3`.

`marked(old_o)` is read-only in this configuration: `marked(i)` expands to
`header(i)->IsMarked()`, and `IsMarked` tests `RawGetMethodTable() &
GC_MARKED`. Because the selected old slot was null, that expression was not
evaluated. The new root therefore remains only queued; no complete-marking or
graph-traversal claim is made.

## Evidence and cross-references

Final C011EC13 evidence is under:

`out/dotnet/gc-first-root-post-queue-mark-decision/run-20260812-212842987/`

The manifest records per-boot roots, queue state, decision counters, machine
trace, QEMU/serial hashes, restoration, regressions, retained failures, and
blocked/non-clean checks. Related checkpoint reports include
`NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_FIRST_MARK_MUTATION.md`,
`NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_PRE_MARK_BOUNDARY.md`,
`NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_CONDEMNED_GENERATION_DECISION.md`,
`NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_HEAP_RESOLUTION.md`,
`NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_MEMBERSHIP_CLASSIFICATION.md`,
`NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_CALLBACK_ENTRY.md`,
`NATIVEAOT_WORKSTATION_GC_FIRST_NON_NULL_ROOT_CALLBACK_BOUNDARY.md`,
`NATIVEAOT_THREAD_STATIC_RUNTIME_SUPPORT.md`,
`NATIVEAOT_GC_STARTUP_READINESS.md`, and
`NATIVEAOT_WORKSTATION_GC_FEASIBILITY.md`.

No commit was created.
