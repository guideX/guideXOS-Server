# C103 Production NativeAOT Application Lifecycle and Safe Relaunch

Date: 2026-09-10
Branch: `v1.1_DOTNET_SUPPORT`
Evidence: `out/dotnet/c011ec103-production-nativeaot-application-lifecycle-relaunch/`

## Result

C103 selects Outcome C: resident NativeAOT runtime and resident application image with repeatable entry. The image is mapped and registered once. After managed `Main` returns, the launcher retains every address that the runtime may still reference and invokes the same authentic managed entrypoint again on a later same-image launch. PAL hooks, GC startup, module initialization, code-manager registration, TLS, and the current ThreadStore record are not repeated.

This is the smallest safe lifecycle supported by the audited NativeAOT 9.0.0 contract. It is intentionally not a full unload. The valid C103 production boot passed two managed launches, the negative boot passed valid → missing → valid, and both boots returned to the ordinary kernel main loop with zero frame-accounting residuals.

## Lifecycle decision

The locked runtime has no supported full in-process shutdown. Source audit found no `RhShutdown`, no GC restart boundary, no finalizer stop/join contract, no FLS namespace teardown for the runtime, no module/type-manager/code-manager unregister, and no supported image unload protocol. `RhInitialize` calls `PalInit` and registers process-exit cleanup; `GC_Initialize` and the runtime instance are one-time initialization paths. `PalGetModuleHandleFromPointer` deliberately pins the current module. The authentic `CoffNativeCodeManager` registration is add-only.

Therefore C103 does not unmap the valid image after `ManagedMain` returns. The loader's `releaseMappedPages()` remains available for an interrupted/invalid mapping before runtime registration, but it is not called for a valid resident application return. The wrapper in `samples/managed/HostLogProof/c102_startup.c` now has an idempotent state machine:

`Uninitialized → Initializing → Ready`

The second call checks `Ready`, reinstalls the current TLS base, skips hook installation and `RhInitialize`, and calls `ManagedMain` directly. A partial startup is terminal for that resident image rather than retried.

## Ownership audit

| Resource | Created by | Owner/class | Lifetime in C103 | Safe release or reuse |
| --- | --- | --- | --- | --- |
| ELF `PT_LOAD` pages | `mapElf()` in `kernel/core/nativeaot_application.cpp` | `ARTIFACT_STATIC` plus `KERNEL_SHARED` frame mappings | Resident after authentic module registration | `releaseMappedPages()` only on pre-registration mapping failure; no valid-return unload |
| ELF image range and entrypoint | `ResidentApplication` and `g_artifactBase/span` | `ARTIFACT_STATIC` | Resident while code manager/module metadata can point into it | Reuse recorded range; do not remap same image |
| ELF file handle | VFS open/read path | `APPLICATION_OWNED` | Per load operation | `vfs::close()` after read or probe |
| Launcher artifact byte buffer | `g_artifact` | `KERNEL_SHARED` cache supporting the resident image | Resident in current bounded loader | Not released or overwritten while resident |
| Application VM reservations | PAL/GC `virtualAlloc`/`gcReserve` callbacks | `RUNTIME_SHARED` with `KERNEL_SHARED` VM backend | Runtime-owned; per-operation release remains available to runtime | `virtualFree`/`gcRelease` only for runtime-owned regions when called by runtime |
| Application image page tables | `address_space::mapPage()` | `KERNEL_SHARED` | Resident mappings; loader records pages it created | `unmapPage()` and frame release only in safe failure cleanup |
| NativeAOT OS-module registration | `initializeNativeAotModules()` / `RhRegisterOSModule` | `RUNTIME_SHARED` and `ARTIFACT_STATIC` | Once per resident image | No supported unregister |
| NativeAOT code manager | authentic `CoffNativeCodeManager` registration | `RUNTIME_SHARED` and `ARTIFACT_STATIC` | Add-only, resident | No supported unregister; image remains mapped |
| Module/type-manager metadata | `InitializeModules` | `RUNTIME_SHARED` and `ARTIFACT_STATIC` | Resident after first reverse P/Invoke | No module unload API |
| Workstation GC | `RhInitialize`, `GC_Initialize` | `RUNTIME_SHARED` | Process/boot lifetime | No supported restart or shutdown |
| Managed bounded heap and allocation diagnostics | runtime-pack startup/reverse-P/Invoke boundary | `RUNTIME_SHARED` implementation with launch-scoped allocation state | Heap storage remains resident; allocation cursor/diagnostics reset on each production reverse-P/Invoke | Reinitialize launch-scoped heap state; do not tear down runtime |
| Current TLS block and vector | launcher `installTls()` plus runtime reverse-P/Invoke | `RUNTIME_SHARED` / `KERNEL_SHARED` | Current kernel thread lifetime | Reinstall same GS base; no runtime teardown |
| NativeAOT ThreadStore current record | `threadstore::attachCurrentThread()` | `RUNTIME_SHARED` | Remains attached across same-image relaunch | Reuse; worker records still attach/detach per worker |
| PAL callback tables | `fillLegacyHooks`, `fillPalTable`, startup wrapper | `RUNTIME_SHARED` | Install once; reused by later entry | No uninstall on valid return |
| FLS/local-storage namespace | PAL FLS adapter and local-storage runtime | `RUNTIME_SHARED` | Runtime lifetime | No full namespace teardown |
| Event/worker slots | PAL callback adapter | `KERNEL_SHARED` with runtime-shared entries | Runtime-operation lifetime; resident slot tables | Runtime worker/event APIs manage individual entries |
| Lifecycle state, path, sequence, mapping identity | `ResidentApplication` | `APPLICATION_OWNED` launcher metadata | Boot lifetime for resident image | Reuse same path/range; reject different resident image as `busy` |

The persistent post-return allocation is therefore authenticated: the mapped image and its page tables account for the loader-owned portion, while NativeAOT/PAL/GC startup accounts for the one-time runtime-support portion. There is no arbitrary address-range free and no direct manipulation of C99 PMM metadata.

## Runtime answers

1. Runtime startup is intended once per boot/resident image.
2. A second `RhInitialize` is not supported and is skipped.
3. A second application-entry call reuses the resident wrapper/runtime state and invokes `ManagedMain` again.
4. No supported runtime shutdown/uninitialize API exists.
5. GC restart is not supported.
6. Code-manager deregistration is not available in the locked source contract.
7. Module/type-manager unload is not available.
8. A second managed entry is safe here because the same resident image is re-entered, the runtime remains initialized, TLS is restored, and the production non-real-GC allocation state is reset at reverse-P/Invoke entry.

## Frame accounting

The C103 ordinary two-launch serial log is `launch1/serial.log` and is duplicated in `launch2/serial.log` and `teardown-reuse/serial.log`. Hex values below are the serial values.

| Checkpoint | Free | Allocated | VmRegion | PageTable | Total tracked |
| --- | ---: | ---: | ---: | ---: | ---: |
| Before launch 1 | `0x2C829` | `0x3E97` | `0x0` | `0x0` | `0x306C0` |
| After launch 1 map | `0x2C6F9` | `0x3FC7` | `0x12E` | `0x2` | `0x306C0` |
| After launch 1 return | `0x2C5C2` | `0x40FE` | `0x260` | `0x7` | `0x306C0` |
| After lifecycle 1 | `0x2C5C2` | `0x40FE` | `0x260` | `0x7` | `0x306C0` |
| Before launch 2 | `0x2C5C2` | `0x40FE` | `0x260` | `0x7` | `0x306C0` |
| After launch 2 map/reuse checkpoint | `0x2C5C2` | `0x40FE` | `0x260` | `0x7` | `0x306C0` |
| After launch 2 return | `0x2C5C2` | `0x40FE` | `0x260` | `0x7` | `0x306C0` |
| After lifecycle 2 | `0x2C5C2` | `0x40FE` | `0x260` | `0x7` | `0x306C0` |

Both `residual` and `ownerResidual` are `0x0` at every emitted checkpoint. Launch 1 mapping consumes `0x130` allocated frames (`0x12E` VM-region plus `0x2` page-table); one-time runtime/application support raises the retained total to `0x267` allocated frames over the pre-launch checkpoint. Launch 2 adds no frames. This is `ONE_TIME_RUNTIME_GROWTH` combined with `INTENTIONAL_RESIDENT_IMAGE`, not repeated growth.

## Production boot evidence

### Boot A — ordinary baseline

`boot-a-ordinary-baseline/` contains the ordinary validator result. It used the restored ordinary `kernel/build/amd64/bin/kernel.elf`, ordinary `ESP/kernel.elf`, and ordinary `ESP/ramdisk.img`; result was `PASS`, with main loop and navigator markers. No C103 application launch define was active.

### Boot B — two-launch lifecycle

`boot-b-ordinary-two-launch/serial.log` proves:

```text
[C103-LAUNCH-BEGIN] sequence=00000001 ... runtime=initialized image=mapped
[C102-MANAGED-OUTPUT] C102-MANAGED-ENTRY
[C102-MANAGED-OUTPUT] C102-MANAGED-PASS
[C102-LAUNCH-RETURN] status=00000000
[C103-LIFECYCLE] sequence=00000001 state=resident runtime=initialized image=resident reusable=1
[C103-LAUNCH-BEGIN] sequence=00000002 ... runtime=reused image=resident
[C103-LOADER] reusing resident ELF64 AMD64 ET_EXEC ...
[C102-MANAGED-OUTPUT] C102-MANAGED-ENTRY
[C102-MANAGED-OUTPUT] C102-MANAGED-PASS
[C102-LAUNCH-RETURN] status=00000000
[C103-LIFECYCLE] sequence=00000002 state=resident runtime=reused image=resident reusable=1
[KERNEL] Entering main loop (waiting for input)...
```

The managed program allocates `byte[8]`, fills `1..8`, validates sum `36`, emits the same managed markers on both invocations, and returns `0` on both invocations.

### Boot C — negative lifecycle

`boot-c-negative/serial.log` proves valid launch → `/system/wall/MISSING.ELF` → valid same-image relaunch. The missing probe returns `not-found`, does not alter the resident state or frame counts, and the subsequent valid launch reaches managed PASS and returns `0`. The kernel reaches its main loop.

## C102 baseline and retained integrity

`phase0-baseline/` records the accepted C102 first-launch reproduction before C103 source edits: ordinary boot, proof mode `0`, VFS discovery, managed entry/pass, return `0`, coherent residuals, and kernel survival. Its accepted C102 frame baseline was `totalTracked=0x306C3`, `allocated=0x3E97` before and `0x40FE` after return; the small total-tracked difference from C103 is the rebuilt proof-kernel image/firmware frame inventory, not a lifecycle leak.

The converter guard remains SHA-256 `5BED5BBE8883EDD700ED2406FA43A7B26F762212D06898BB75B540E07B2F5621`. The runtime identity remains NativeAOT `9.0.0`, AMD64, Workstation GC, GC interface `5.3`, EE interface `2`, `net9.0/win-x64`; runtime source SHA remains `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`. C99 scalable PMM, authentic PAL/VM callbacks, authentic `CoffNativeCodeManager`, `FindMethodInfo`, root scanning, and mark closure remain in use. No GC policy, PMM policy, VM policy, converter trust, planner, candidate, or survivor/root fabrication change was made. B02 was not run and remains `STILL_PREMATURE`.

## Validation and limits

Kernel/native compilation passed with `GXOS_NATIVEAOT_PRODUCTION_APPLICATION` and the C103 production/negative define. Managed NativeAOT publish, runtime-pack build, PE→ELF conversion, and ELF identity checks passed. PowerShell syntax, JSON parsing, `git diff --check`, source symbol guards, and no-QEMU checks are recorded under `validation/`.

The current lifecycle deliberately does not support loading a different NativeAOT image in the same boot; it returns `busy` rather than risking stale code-manager/module references. The current ThreadStore record is intentionally resident on the launching kernel thread. C104 should therefore test the next smallest application-facing limitation: two distinct NativeAOT modules and their module-registration isolation, or managed arguments/stdout if that is more immediately useful.

## Required final report

The following numbered report is the closeout index. Values marked “after commit” are filled in the final closeout patch.

1. Outcome: Outcome C — resident runtime and resident image, repeatable entry.
2. Success Level: Level 3.
3. Repository: `D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT`.
4. Branch: `v1.1_DOTNET_SUPPORT`.
5. Starting HEAD: `07eed66549faa3bcc6839496a3bf5bd68f0a422e`.
6. Starting subject: `Add production NativeAOT application launch`.
7. Final HEAD: exact value is recorded in `out/dotnet/c011ec103-production-nativeaot-application-lifecycle-relaunch/final-classification/repository-final.txt` and in the closeout response.
8. Final subject: `Add repeatable NativeAOT application lifecycle`.
9. Upstream: `origin/v1.1_DOTNET_SUPPORT`.
10. Starting ahead/behind: `2/0`.
11. Final ahead/behind: `3/0` after the single local C103 commit.
12. Starting worktree: clean.
13. Final worktree: clean after ordinary artifact restoration.
14. Runtime identity: NativeAOT 9.0.0, AMD64, Workstation GC, GC 5.3 / EE 2, net9.0/win-x64.
15. Runtime source SHA: `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`.
16. C101 SHA: `9194f0ef570e8ec03f8b72193f3909e56a01d0b6`.
17. C102 SHA: `07eed66549faa3bcc6839496a3bf5bd68f0a422e`.
18. C103 SHA: exact value is recorded in `out/dotnet/c011ec103-production-nativeaot-application-lifecycle-relaunch/final-classification/repository-final.txt` and in the closeout response.
19. C102 baseline reproduced: yes, `phase0-baseline/`.
20. Managed Main: reached in the accepted C102 baseline and both C103 boots.
21. Managed PASS: reached in the accepted C102 baseline and twice in Boot B/Boot C.
22. Return 0: yes for each valid launch.
23. Kernel survival: yes; ordinary main loop marker present.
24. C102 persistent mapping classification: intentional resident-by-design, not claimed as a leak.
25. Exact C103 question: classify post-return ownership and enable a safe second production launch.
26. ELF mapping owner: loader-recorded `VmRegion` mappings, retained as `ARTIFACT_STATIC`/`KERNEL_SHARED`.
27. Page-table owner: `kernel::memory::address_space`, `KERNEL_SHARED`.
28. Code manager owner/lifetime: NativeAOT runtime registration, `RUNTIME_SHARED`/`ARTIFACT_STATIC`, resident.
29. Module metadata owner/lifetime: `InitializeModules` runtime/module state, resident.
30. GC owner/lifetime: Workstation GC, runtime/process lifetime.
31. PAL owner/lifetime: PAL hook tables and callbacks, runtime lifetime.
32. TLS/thread-state owner/lifetime: resident current kernel thread/runtime state.
33. VM callbacks owner/lifetime: kernel PAL/VM backend, runtime-shared callback contract.
34. Runtime globals lifetime: once per resident image/boot.
35. Application state lifetime: bounded `ResidentApplication` metadata for boot lifetime.
36. Full ownership table completed: yes, above and `ownership-audit/`.
37. Runtime initialization one-time: yes; wrapper state is `Uninitialized/Initializing/Ready`.
38. Runtime can initialize twice: no supported contract; second initialization is skipped.
39. Runtime shutdown API exists: no supported full shutdown API.
40. GC shutdown/restart supported: no.
41. Code-manager unregister exists: no.
42. Module unregister exists: no.
43. Thread detach exists: yes for worker/current adapter, but current launch thread remains attached across resident relaunch.
44. Selected lifecycle model: Model C.
45. Why selected: unload would leave unsupported runtime/code-manager/module references; resident reuse is safe and stable.
46. Application mapping list recorded: yes, `g_pages[]`, image base/span, entrypoint, and segment count.
47. PT_LOAD mappings releasable: only in pre-registration failure cleanup.
48. Entry/module metadata remains referenced after Main: yes; source audit requires resident image.
49. Image unmap safe: not after valid runtime registration.
50. Image unmap performed: no for valid returns.
51. VM reservation released: no bulk release; runtime-owned individual release APIs remain available to runtime.
52. VmRegion frames released: no for resident valid image; failure cleanup uses the production release path.
53. PageTable frames released: no for resident valid image; empty tables are released by failure-path address-space unmap.
54. Persistent mappings remaining: `0x260` VmRegion and `0x7` PageTable frames after valid return.
55. Persistence classification: intentional resident image/runtime.
56. Total tracked: C103 `0x306C0`; C102 baseline `0x306C3`.
57. Before launch1 free: `0x2C829`.
58. Before launch1 allocated: `0x3E97`.
59. After map1 free: `0x2C6F9`.
60. After map1 allocated: `0x3FC7`.
61. After return1 free: `0x2C5C2`.
62. After return1 allocated: `0x40FE`.
63. After lifecycle1 free: `0x2C5C2`.
64. After lifecycle1 allocated: `0x40FE`.
65. Before launch2 free: `0x2C5C2`.
66. After map2 free: `0x2C5C2` (resident reuse; no remap).
67. After return2 free: `0x2C5C2`.
68. After lifecycle2 free: `0x2C5C2`.
69. VmRegion counts: `0x0 → 0x12E → 0x260 → 0x260`.
70. PageTable counts: `0x0 → 0x2 → 0x7 → 0x7`.
71. Owner residual: `0x0` at all C103 checkpoints.
72. Frame leak: none observed; no repeated growth.
73. One-time runtime retained cost: `0x137` allocated frames after map support is included; total first-launch retained delta is `0x267`.
74. Repeated per-launch growth: none; launch 2 is frame-stable.
75. Ordinary boot: yes in Boot A/B/C.
76. proofMode: `0`.
77. Artifact path: `/system/wall/C102.ELF`.
78. Launch1 begin: sequence `1`, initialized/mapped.
79. Managed entry1: yes.
80. Managed allocation1: `byte[8]`, values `1..8`.
81. Managed validation1: sum `36`.
82. Managed PASS1: yes.
83. Return value1: `0`.
84. Launcher return1: yes, status `success`.
85. Launch2 attempted: yes, same ordinary boot.
86. Same artifact: yes; same path and same image range.
87. Runtime reinitialized: no.
88. Runtime reused: yes; serial says `runtime=reused`.
89. Managed entry2: yes.
90. Managed allocation2: yes, same bounded allocation path succeeds.
91. Managed validation2: sum `36`.
92. Managed PASS2: yes.
93. Return value2: `0`.
94. Launcher return2: yes, status `success`.
95. Kernel alive after launch2: yes.
96. Module reloaded: no.
97. Module resident: yes.
98. Static state fresh: no module reload was performed.
99. Static state persistent: module/runtime static registration remains resident.
100. State semantics intentional: yes; launch-scoped allocation diagnostics reset while runtime/module state remains resident.
101. Negative case: missing `/system/wall/MISSING.ELF` after a successful valid launch.
102. Sequence relative to valid launch: probe occurs after launch1 and before valid relaunch; it does not advance resident application sequence.
103. Error expected: `not-found`.
104. Error observed: `not-found`.
105. Kernel survived: yes.
106. Lifecycle state remained valid: yes, resident state retained.
107. Subsequent valid launch works: yes, managed PASS/return `0`.
108. Frame accounting preserved: yes; no change during missing probe.
109. C99 PMM active: yes; scalable UEFI-derived frame accounting remains in use.
110. C101 converter guard PASS: yes, exact SHA retained.
111. NativeAOT publish PASS: yes.
112. ELF assertions PASS: yes, ELF64 AMD64 ET_EXEC, seven PT_LOADs, entry containment, alignment and permissions retained.
113. PAL authentic: yes, production callback contract.
114. Code manager authentic: yes, authentic `CoffNativeCodeManager` registration.
115. `FindMethodInfo`: retained and source-audited.
116. Root scan: retained and source-audited.
117. Mark closure: retained and source-audited.
118. GC policy mutation: none.
119. PMM mutation beyond C99: none.
120. VM policy mutation: none; lifecycle uses existing address-space APIs.
121. Converter trust mutation: none.
122. Candidate/planner mutation: none.
123. Survivor/root fabrication: none.
124. C99 PMM tests: prior accepted C99 evidence retained; C103 residuals remain zero.
125. VM/PAL tests: prior accepted C102 VM/PAL validation retained.
126. Runtime-pack validation: pass; dedicated runtime-pack manifest under `build/runtime-pack/`.
127. NativeAOT publish: pass; dedicated application build under `build/application/`.
128. PE→ELF conversion: pass; dedicated artifact identity generated.
129. Kernel/native build: pass with production capacity and C103 defines.
130. PowerShell syntax: pass for invoked build/boot/validator scripts.
131. JSON parse: pass for manifests and final evidence JSON.
132. `git diff --check`: pass.
133. Symbol checks: pass for lifecycle symbols and forbidden teardown symbol guard.
134. Linker/source/table/archive guards: pass; source retains authentic registration and no unsupported unload call.
135. Baseline boot: pass.
136. Two-launch boot: pass.
137. Negative lifecycle boot: pass.
138. Invariant failures: none observed.
139. Overflow: no overflow observed; bounded ELF/path arithmetic remains checked.
140. Fail-fast: none observed.
141. Page faults: none observed.
142. Nondeterminism: none observed in bounded acceptance boots; no soak claim.
143. C69–C97 remains closed: yes.
144. C98 defect remains fixed by C99: yes.
145. C100 design status: retained as prior design context.
146. C102 launch status: accepted first real ordinary NativeAOT application launch.
147. B02 evaluated: no; explicitly not run.
148. B02 status: `STILL_PREMATURE`.
149. Ordinary artifacts restored: yes.
150. Ordinary kernel SHA: `75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6`.
151. Ordinary ESP SHA: `75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6`.
152. Proof artifact active: no; proof kernels remain evidence-only under `out/dotnet/...`.
153. C103-owned QEMU count: `0` after each run/closeout.
154. Unrelated QEMU preserved: no unrelated QEMU process was present; only owned runs were started.
155. Files changed: `kernel/core/include/kernel/nativeaot_application.h`, `kernel/core/main.cpp`, `kernel/core/nativeaot_application.cpp`, `samples/managed/HostLogProof/c102_startup.c`, and this documentation.
156. Documentation path: `docs/dotnet/NATIVEAOT_C103_PRODUCTION_APPLICATION_LIFECYCLE_RELAUNCH.md`.
157. Evidence root: `out/dotnet/c011ec103-production-nativeaot-application-lifecycle-relaunch/`.
158. Final commit: recorded in `final-classification/repository-final.txt` after the final amend.
159. Push status: not pushed.
160. Remaining limitation: no different-image unload/isolation; same resident image only, with `busy` for a different resident path.
161. Exact next-smallest milestone: C104 should test two distinct NativeAOT modules and module-registration isolation, unless managed arguments/stdout is the nearer application-facing need.

## C104 boundary

C102 proved that a real managed C# application can run in one ordinary boot. C103 proves that one launch does not consume the only safe launch: the same resident NativeAOT application can run repeatedly with stable frame ownership. The next milestone must address the actual remaining limitation—most directly distinct-module isolation or managed argument/stdout plumbing—and must not reopen C69–C97 or run B02.
