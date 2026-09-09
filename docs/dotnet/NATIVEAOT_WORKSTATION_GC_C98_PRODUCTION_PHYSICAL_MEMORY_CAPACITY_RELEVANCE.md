# C98 — NativeAOT production physical-memory capacity relevance

Status: complete, source-backed integration audit. No production allocator,
VM, GC, heap, loader, planner, candidate, promotion, survivor, or root code
was changed.

## Decision

Outcome C — `PRODUCTION_CAPACITY_DEFECT`, with a proof-only NativeAOT launch
and proof-image footprint as contributing factors.

The `0x1000` value is not a C95–C97-only test constant. The UEFI bootloader
unconditionally allocates `4096` pages for `RuntimeFramePoolPages`, and the
ordinary AMD64 kernel accepts no larger pool because
`kernel/core/address_space.cpp` has a fixed `kMaxFramePoolPages = 4096` and
fixed-size frame-state arrays. The bare-metal VM-region implementation is
built into the ordinary kernel and commits `VmRegion` pages and page-table
pages from this same allocator. No firmware-map aggregation, dynamic frame
state, or later allocator transition was found.

The current ordinary Server path does not launch a NativeAOT collector or load
a NativeAOT image: those paths are explicitly gated by opt-in QEMU test
defines. Therefore C98 does not claim an observed ordinary managed OOM or an
ordinary post-image census. It does establish that any production NativeAOT
execution using the existing bare-metal VM backend would inherit the same
hard 16-MiB backing ceiling. The historical OOM was authentic VM resource
pressure under that ceiling, not a rollback or accounting defect, but it was
manifested by the deliberately instrumented proof image/workload.

This document is not another ONE-vs-SIX provenance phase. C97 closed that arc
and exposed the next practical question: was the OOM a realistic production
NativeAOT limitation, or a deliberately bounded proof-machine resource
ceiling?

## Source findings

The frame-state array is declared in `kernel/core/address_space.cpp` as
`FrameState g_frameState[kMaxFramePoolPages]` and
`bool g_frameMapped[kMaxFramePoolPages]`. The capacity expression is the
literal `constexpr uint64_t kMaxFramePoolPages = 4096`. `initialize()` rejects
zero, unaligned, or larger-than-`kMaxFramePoolPages` boot pools, then sets
`g_poolPages` from `BootInfo::RuntimeFramePoolPages`. Allocation scans only
`index < g_poolPages`, and accounting reports `totalKnownFrames = g_poolPages`.

The bootloader’s source expression is independently literal:
`constexpr UINTN runtimeFramePoolPages = 4096`, followed by UEFI
`AllocatePages(AllocateAnyPages, EfiLoaderData, runtimeFramePoolPages, ...)`.
It stores the resulting base and count in BootInfo. This allocation is outside
the C95–C97 preprocessor guards. The final UEFI memory map is obtained through
`BootServices->GetMemoryMap` before `ExitBootServices`, and the pointer/count/
descriptor size are passed in BootInfo, but no code found converts that map
into a scalable frame pool.

The ordinary kernel builds
`runtime/memory/guidexos_virtual_memory_region_baremetal.cpp` under the
ordinary `GXOS_BARE_METAL && GXOS_TRUE_VIRTUAL_MEMORY` configuration. Its
`commit()` calls `address_space::allocateFrame(FrameOwner::VmRegion)` and its
page-table mapping path uses the same address-space allocator. Its statistics
surface reports physical backing accounting. The Makefile comment says the
NativeAOT PAL ownership adapter is inactive and that normal kernel execution
does not call collector startup; this is an execution-path absence, not a
different physical allocator.

The UEFI memory map is therefore discovery/handoff metadata, not the source
of the current frame capacity. `boot_diagnostics.h` contains a memory-map dump
helper, but no call to it was found in the ordinary boot path, so exact usable
RAM totals were not captured in retained serial evidence. The QEMU command
used by C97 and the inherited ordinary smoke environment configured `1024M`;
that configured value is not substituted for a measured usable-map total.

## C97 closeout

The accepted C97 facts are: one identical proof artifact was used for both
selectors; selectors 216 and 320 were authenticated; six fresh interleaved
boots ran as 216, 320, 216, 320, 216, 320; runtime216 ran three times and
runtime320 ran three times; the shared-prefix physical-frame state converged;
`tail:217` was absent for 216 and present for 320; the worktree was clean;
ordinary kernel/ESP artifacts were restored; and B02 remained premature.

The same-image conclusion is preserved. The first legitimate semantic
workload difference is `tail:217`, not `tail:80`; future tail count cannot
cause the earlier shared-prefix divergence. C96’s separate-image
`PT_LOAD`-footprint result remains the explanation for the historical
T320-vs-T216 pre-managed headroom difference. C97 removed that confound.

## Numbered closeout report

1. Outcome: `Outcome C — PRODUCTION_CAPACITY_DEFECT`; proof-only NativeAOT launch and proof footprint are contributing factors, not the allocator classification.
2. Success Level: Level 3, integration relevance closed by source evidence; the ordinary managed census is explicitly unavailable because ordinary NativeAOT launch is not wired in this tree.
3. Repository: `D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT`.
4. Branch: `v1.1_DOTNET_SUPPORT`.
5. Starting HEAD: `167288cf1f54e468d7a3244e0d2a8d86eac99555`.
6. Starting subject: `Isolate NativeAOT tail count from image footprint`.
7. Final HEAD: the C98 commit containing this document; exact SHA is recorded by the final Git closeout.
8. Final subject: `Classify NativeAOT physical memory capacity`.
9. Upstream: `origin/v1.1_DOTNET_SUPPORT`.
10. Starting ahead/behind: `0/0`.
11. Final ahead/behind: `1/0` after the local C98 commit.
12. Starting worktree: clean.
13. Final worktree: clean after commit.
14. Runtime identity: NativeAOT `9.0.0`, AMD64, Workstation GC, GC interface `5.3`, EE interface `2`, `net9.0` / `win-x64` pack identity.
15. Runtime source SHA: `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`.
16. C96 SHA: `d391f8a1dcb4f0a164cf63897f4634d844f2af9d`.
17. C97 SHA: `167288cf1f54e468d7a3244e0d2a8d86eac99555`.
18. C98 SHA: the final commit containing this document; the commit SHA is intentionally not embedded here because embedding a self-hash would be circular.
19. Historical ONE-vs-SIX arc status: closed; no contradictory evidence found.
20. C69–C97 closed: yes — `C69–C97 historical ONE-vs-SIX shared-prefix causal investigation: CLOSED`.
21. Exact C98 question: what physical-memory allocator and capacity does ordinary/bare-metal guideXOS Server use for NativeAOT VM backing, and how does it relate to the C95–C97 `0x1000` pool?
22. Frame-state array source: `kernel/core/address_space.cpp`, namespace-scope `g_frameState` and `g_frameMapped` declarations.
23. Frame-state capacity expression: `kMaxFramePoolPages = 4096`; arrays are `...[kMaxFramePoolPages]`.
24. `0x1000` literal or derived: literal in both the kernel cap and bootloader pool producer; it is not RAM-map-derived.
25. Total represented bytes: `0x1000 * 0x1000 = 0x1000000` bytes, 16 MiB.
26. Physical RAM discovery source: UEFI `BootServices->GetMemoryMap` in `guideXOSBootLoader/main.cpp`.
27. Firmware memory-map source: final map buffer returned by `ExitBootServicesWithMemoryMapInBuffer`, handed off as `BootInfo::MemoryMap`, count, and descriptor size.
28. Ordinary QEMU configured RAM: `1024M` in the inherited ordinary QEMU environment.
29. Ordinary usable RAM: not numerically captured in retained ordinary serial evidence; no kernel map aggregation reports a usable total.
30. Proof QEMU configured RAM: `1024M` in the C97 QEMU command.
31. Proof usable RAM: not numerically captured in the retained C97 firmware/serial evidence.
32. Proof frame allocator: `kernel/core/address_space.cpp` generic address-space allocator initialized from BootInfo’s runtime pool.
33. Ordinary frame allocator: the same generic `kernel/core/address_space.cpp` allocator, initialized on ordinary UEFI BootInfo boots.
34. Same allocator implementation: yes.
35. Same capacity: yes — current bootloader producer and kernel consumer both enforce 4096 pages.
36. Production capacity source: unconditional `runtimeFramePoolPages = 4096` in `guideXOSBootLoader/main.cpp`, bounded by `kMaxFramePoolPages` in the ordinary kernel.
37. Production capacity value: `0x1000` pages / 16 MiB represented by the tracked allocator.
38. Dynamic scaling supported: no.
39. Bootstrap-only pool: no evidence; the pool is handed to the generic allocator and remains the only tracked source.
40. Later allocator transition: none found.
41. NativeAOT VM proof path: proof kernel → `nativeaot_pal_qemu_test.cpp` PAL `startupCommit` → `gxos::runtime::virtual_memory::commit()` → `address_space::allocateFrame(VmRegion)` plus page-table allocation.
42. NativeAOT VM production path: no enabled ordinary collector startup/image-load path exists; the ordinary VM backend is compiled, but NativeAOT invocation is opt-in under `GXOS_NATIVEAOT_*_QEMU_TEST` defines.
43. Lowest common allocator function: `kernel::memory::address_space::allocateFrameInternal(FrameOwner)` in `kernel/core/address_space.cpp`.
44. Proof total known frames: `0x1000`.
45. Ordinary total known frames: `0x1000` after source-defined address-space initialization; no ordinary NativeAOT census was possible.
46. Proof total represented bytes: 16 MiB.
47. Ordinary total represented bytes: 16 MiB in the tracked address-space pool; this is not a statement that the machine has only 16 MiB of physical RAM.
48. Proof pre-image free: `0x1000` in the C97 pre-managed checkpoint.
49. Ordinary pre-image free: `0x1000` source-initialized state; no NativeAOT image was loaded.
50. Proof post-image free: `0x7E1`.
51. Ordinary post-image free: not applicable; ordinary NativeAOT image load is absent.
52. Proof post-image VmRegion: `0x81A`.
53. Ordinary post-image VmRegion: not applicable.
54. Proof post-image PageTable: `0x5`.
55. Ordinary post-image PageTable: not applicable.
56. Pool invariant proof: PASS for C97 — `free + allocated = 0x1000`, and owner sums reconcile on all six boots with zero residual.
57. Pool invariant ordinary: source invariant is preserved by `free = g_poolPages - allocated` and owner counters; no observed ordinary NativeAOT census exists to provide a runtime row.
58. Proof NativeAOT PT_LOAD pages: `0x81B` page-rounded pages from the identical C97 managed ELF.
59. Ordinary NativeAOT PT_LOAD pages: not applicable; no ordinary NativeAOT artifact is embedded or loaded by the default kernel.
60. Proof-only loaded-page overhead: no controlled ordinary-image delta exists; the proof managed image is `0x81B` pages, while the default ordinary path has no comparable image. The proof kernel itself is also larger: `0x65F4` versus `0x4D04` page-rounded kernel PT_LOAD pages, a delta of `0x18F0`; its `.bss` is `0x1762000` bytes larger.
61. Proof-only physical-frame overhead: C97 post-image consumed `0x81F` tracked frames relative to pre-managed zero — `0x81A` VmRegion plus `0x5` PageTable; there is no ordinary NativeAOT image baseline.
62. Diagnostic footprint relevant: proof diagnostics are relevant to proof-kernel image size, but C97’s observer added no static frame pool or per-run ledger and used allocation-free instrumentation.
63. Runtime test tables relevant: yes, proof-only runtime selectors/test tables contribute proof image/code footprint; they are not present in the default NativeAOT launch path.
64. Embedded artifact footprint relevant: yes; the embedded managed proof artifact is committed through the same VM backend and accounts for the `0x81A` VmRegion frames at C97 post-image.
65. C97 same-image conclusion preserved: yes.
66. Future tail count no longer causes shared-prefix divergence: yes; the first semantic difference is `tail:217`.
67. Historical tail80 divergence classification: pre-managed image-footprint/headroom divergence from separately built images, as established by C96; not future-tail causality.
68. Historical VM OOM authentic: yes; the VM commit exhausted the actual tracked pool and returned `VmResult::OutOfMemory`.
69. Historical VM OOM production defect: yes in capacity relevance — the same fixed allocator is the ordinary bare-metal backend that a production NativeAOT path would use; no observed ordinary managed OOM is claimed.
70. Historical VM OOM proof-resource artifact: not as the primary capacity classification; proof-only execution and footprint made the pressure observable, but the 16-MiB cap is not proof-only.
71. Ordinary NativeAOT can exceed old pressure point: not demonstrated and not currently possible to authenticate from this tree because ordinary NativeAOT launch is absent; the current allocator itself cannot represent more than `0x1000` frames.
72. Optional production stress run performed: no.
73. Production stress workload: none; a stress rerun would reopen C97 and would not answer the missing ordinary-launch-path question.
74. Production stress result: not applicable.
75. Production free frames after workload: not applicable.
76. Production OOM observed: no ordinary NativeAOT OOM observed in C98.
77. Rollback path reused from C96: yes; same allocator implementation, so C96 rollback proof is inherited.
78. Frame-owner accounting production: no runtime production row; source model has only `VmRegion` and `PageTable` owners and reconciles counters by construction.
79. Kernel heap shares pool: no; the kernel heap is a separate fixed static arena (`KERNEL_HEAP_SIZE = 32 MiB`) and no call path routes it through `address_space::allocateFrame`.
80. Page tables share pool: yes for address-space VM mappings; `allocatePageTableFrame` uses the same allocator with `FrameOwner::PageTable`. Bootloader-created early tables are separate preexisting mappings.
81. NativeAOT image shares pool: yes in the proof path; its VM commits use `FrameOwner::VmRegion`. No ordinary NativeAOT image exists in the default path.
82. Applications share pool: no ordinary application VM consumer was found; any future application region using the generic bare-metal backend would share it, but that is not an authenticated current path.
83. Intended PMM architecture: current implementation intends an explicit bootloader-owned pool for generic VM data/page tables; it is a bounded startup-QEMU-oriented design, not a discovered-RAM-scalable PMM.
84. Fixed production PMM limitation found: yes — producer literal, fixed state arrays, upper-bound rejection, and no map-based extension.
85. Repair required: yes before treating NativeAOT as a production workload on this backend.
86. Repair category: `PRODUCTION_FIX_REQUIRED`; C99 should design the smallest safe scaling frontier.
87. Test-harness adjustment warranted: eventually, yes, for more realistic/less fragile proof sizing; not changed in C98.
88. Production adjustment warranted: yes; connect the allocator to discovered usable RAM or an existing scalable PMM before production NativeAOT rollout.
89. No-fix classification: no; the capacity limitation is production-relevant even though no C98 code repair is made.
90. Strongest supported causal chain: UEFI `AllocatePages(4096)` → BootInfo runtime pool → fixed 4096-entry address-space state → VM/page-table frame allocation → no free frame → `VmResult::OutOfMemory` → authentic NativeAOT commit failure; C96 then proved rollback restores the pool.
91. First remaining unsupported link: an ordinary non-test NativeAOT image/collector startup and post-image frame census are not present in the current tree.
92. Next genuine NativeAOT integration blocker: production physical-memory scaling for the generic VM backend; exact stock Workstation GC PAL/VM binding remains the subsequent launch-enablement blocker.
93. B02 evaluated: yes; no independent authentic candidate rejection was found.
94. B02 status: `STILL_PREMATURE`.
95. B02 next milestone: none; C99 should address the capacity frontier / actual NativeAOT enablement blocker first.
96. Production mutation: none.
97. Frame-capacity mutation: none.
98. VM mutation: none.
99. GC mutation: none.
100. Loader mutation: none.
101. Heap mutation: none.
102. Planner mutation: none.
103. Candidate mutation: none.
104. Promotion mutation: none.
105. Survivor fabrication: none.
106. Root fabrication: none.
107. C18: preserved and inherited; allocation-free diagnostics remained the integrity requirement.
108. Code manager: preserved/authentic in C97.
109. `FindMethodInfo`: preserved/authentic in C97.
110. Root scan: preserved/authentic in C97.
111. Mark closure: preserved/authentic in C97.
112. Planner authenticity: preserved/authentic in C97.
113. Survivor integrity: preserved/authentic in C97.
114. Invariant failures: zero in the accepted C97 frame census.
115. Sensitive diagnostic allocations: zero observed/required; C98 added no runtime census.
116. Overflow: zero.
117. Fail-fast: zero.
118. Page faults: zero.
119. Ordinary boot 1: inherited ordinary boot smoke showed normal UEFI BootInfo initialization and `Runtime frame pool ... pages=4096`, with no C97 marker.
120. Ordinary boot 2 if needed: not needed; the source audit resolved the allocator relationship and no ordinary NativeAOT launch path exists.
121. Proof comparison boot: C97’s six fresh interleaved boots, with runtime216 ×3 and runtime320 ×3, are the comparison evidence.
122. Semantic agreement: C97 shared-prefix semantics agreed; only selector-tail continuation differed at `tail:217`.
123. Nondeterminism: none in the accepted C97 shared prefix; C98 ran no new workload.
124. Artifact hashes: C97 managed PE `42547983D8F9E7971D2637F7AE34FCA05238B66BBDE8F5D3CB00E82E672A1590`; managed ELF `5741F8A05451F4C6CF0A9EA29A6F041FCCDB46045E86E325FE4AFFB3AD7C1350`; proof kernel `DB5CF5817D8D5433D65678D8BB8020059652324021AF68B70166BE612F15F0D1`; ordinary kernel/ESP `75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6`.
125. Runtime-pack validation: inherited C51/C97 runtime-pack identity, source revision, archive membership, stale-artifact, and semantic-rewrite guards are PASS; C98 made no runtime-pack change.
126. Managed build: inherited C97 PASS; no managed source changed in C98.
127. Native build: inherited C97/C96 PASS for proof artifacts and ordinary restoration; no native source changed in C98.
128. PowerShell syntax: PASS for the C98-relevant validation scripts/source audit; no script changed.
129. JSON parse: PASS for retained C97 manifests, runtime-pack manifest, and evidence manifests.
130. `git diff --check`: PASS after the documentation change.
131. PE → ELF conversion: inherited C97 PASS.
132. Symbol checks: inherited C97 PASS, including required NativeAOT exports and runtime identity checks.
133. Linker/source/table/archive guards: inherited C97/C51 PASS; no C98 mutation required.
134. C52 Tier-All result or reason omitted: omitted because a documentation/source-only capacity audit cannot semantically benefit from a new Tier-All rebuild; C97’s relevant artifact guards are inherited.
135. Ordinary restoration: PASS; ordinary kernel and ESP were restored before C98 closeout.
136. Ordinary kernel SHA: `75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6`.
137. Ordinary ESP SHA: `75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6` for the restored ESP `kernel.elf`.
138. Proof artifact active: no; final state is ordinary artifacts with proof inactive.
139. C98-owned QEMU cleanup: count zero; C98 launched no QEMU.
140. Unrelated QEMU preservation: no unrelated QEMU was present or disturbed; final count zero.
141. Files changed: one tracked documentation file; evidence copies are under ignored `out/dotnet`.
142. Documentation path: `docs/dotnet/NATIVEAOT_WORKSTATION_GC_C98_PRODUCTION_PHYSICAL_MEMORY_CAPACITY_RELEVANCE.md`.
143. Evidence root: `out/dotnet/c011ec98-production-physical-memory-capacity-relevance/`.
144. Final commit: local C98 commit with subject `Classify NativeAOT physical memory capacity`.
145. Push status: not pushed.
146. Remaining limitation: exact firmware usable-RAM totals and an ordinary NativeAOT post-image census are unavailable because the map is handed off but not aggregated/logged and ordinary collector startup is not wired.
147. Exact next-smallest milestone: C99 designs a minimal source-backed physical-frame scaling frontier that consumes the UEFI usable map, preserves owner accounting, and connects NativeAOT VM backing to scalable production capacity without merely raising the test constant.

## Evidence layout

The C98 evidence root separates:

- `C97-closeout/` — accepted C97 document, manifest, selectors, shared-prefix comparison, and representative serial evidence;
- `allocator-source-audit/` — address-space, BootInfo, UEFI, bootloader, VM backend, Makefile, and launch-gate sources;
- `proof-allocator-path/` — C97 proof path, checkpoint summaries, ELF inspection, and proof-kernel evidence;
- `ordinary-allocator-path/` — inherited ordinary boot smoke and restored ordinary kernel evidence;
- `physical-memory-map-analysis/` — UEFI map handoff evidence and the absence of usable-RAM aggregation;
- `final-classification/` — this C98 classification and validation summary.

No C98 QEMU workload was needed or started. The evidence deliberately keeps
the C97 proof artifact and ordinary artifacts distinct and does not alter the
`0x1000` capacity.
