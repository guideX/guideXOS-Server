# NativeAOT C105 multi-module runtime registration

Status: **Outcome G — NativeAOT multi-module runtime limitation authenticated**
Success level: **1 — singleton provenance established**

## Scope and progression

C102 proved the first production managed application. C103 proved a resident application lifecycle and same-image relaunch without remapping. C104 built two genuinely distinct fixed-base NativeAOT artifacts, proved that their ELF ranges do not overlap, and safely rejected the second resident image before registration. C105 asks whether one shared NativeAOT runtime can register and execute both images.

The answer for the locked runtime used here is no. The outer guideXOS Busy branch is not the only barrier. The locked NativeAOT runtime has one `RuntimeInstance`, one `ICodeManager*`, one managed-code range, and no code-manager registry or multi-manager address dispatch. Removing the loader guard would not be a bounded guideXOS fix; it would expose the runtime's one-manager assertion and leave B without a valid `FindMethodInfo` route.

## Baseline before C105 edits

A fresh ordinary boot validator passed before tracked C105 edits. The canonical kernel/ESP hashes remained unchanged. A fresh isolated C104 production boot used a temporary ESP and proof mode `0` and produced:

`A managed PASS → B busy (non-overlapping candidate, no map/registration/entry) → A managed PASS`

The fresh boot reached the kernel main loop. Both A checkpoints and the rejected B checkpoint had `residual=0` and `ownerResidual=0`. The temporary C104 build was not left active; the canonical ordinary kernel was restored and rehashed.

## Exact Busy decision

The production decision is in `kernel/core/nativeaot_application.cpp`, `launchResident(const char* path, LaunchReport* report)`:

```text
if (!sameApplicationPath(path, g_application.path)) {
    readAndProbeApplication(path, &candidate);
    collision = rangesOverlap(g_application.artifactBase,
                              g_application.artifactSpan,
                              candidate.artifactBase,
                              candidate.artifactSpan);
    report->status = collision ? BaseCollision : Busy;
}
```

The state owner is the loader's single `ResidentApplication g_application`, with one path, mapped range, entry point, sequence, one artifact buffer, and one mapped-page array. The non-overlap result proves that placement is safe; `Busy` means the current loader/runtime composition is single-resident, not that B collided.

The runtime-pack bridge adds a second, deeper singleton seam: `g_guideXosNativeAotCodeManagerRegistered` and `g_guideXosNativeAotModulesInitialized`. `initializeNativeAotModules()` rejects a second code-manager registration, calls `RhRegisterOSModule()` once, then calls generated `InitializeModules()` once.

## Locked NativeAOT source audit

The locked source is NativeAOT 9.0.0, AMD64, Workstation GC, source commit `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`.

| State or behavior | Current source finding | Consequence |
|---|---|---|
| Runtime instance | `RuntimeInstance::Initialize` asserts `g_pTheRuntimeInstance == NULL` and says multi-instances are not supported. | The shared runtime is one process-lifetime instance. |
| Code manager | `RuntimeInstance` stores one `ICodeManager* m_CodeManager`. | There is no plural manager owner. |
| Managed range | One `m_pvManagedCodeStartRange` and one `m_cbManagedCodeRange`. | There is no plural range index. |
| Registration | `RegisterCodeManager` asserts `m_CodeManager == NULL` and writes the scalar pointer/range. | A second registration is not supported. |
| Lookup | `GetCodeManagerForAddress` tests the one range and returns the one pointer. | B cannot dispatch to B metadata. |
| Native image registration | `RhRegisterOSModule` constructs a `CoffNativeCodeManager` and passes it to scalar registration. | Per-image manager construction does not imply multi-image runtime support. |
| Type/module lists | TypeManager and OS-module lists are add-only. | These plural lists do not repair code-manager lookup. |
| GC roots | `GcScanRoots` walks the shared ThreadStore's thread statics and stacks. | The audited path has no separate multi-module code-manager/root-provider dispatcher. |
| EH/unwind | Each `CoffNativeCodeManager` has image-local runtime-function metadata and range checks. | Local metadata is distinguishable, but global lookup cannot select a second manager. |
| Startup | `RhInitialize` performs PAL/`InitDLL`; `InitDLL` initializes RuntimeInstance and GC. | Runtime-global startup must stay once-only. |

This is an authenticated lower-level runtime contract, not an unverified guess. The minimum safe generalization would require a source-supported code-manager/range registry, multi-manager lookup for EH/method metadata/classlib calls, a defined per-module `InitializeModules` and static-base/root model, and a rebuilt runtime-pack. That is a core NativeAOT extension, not a C105 loader-only patch.

## Startup classification

| Operation | Classification |
|---|---|
| PAL initialization | RUN_ONCE/runtime-global |
| GC initialization | RUN_ONCE/runtime-global |
| VM callback wiring | RUN_ONCE for shared hooks; module range/context data is per module/invocation |
| Runtime thread attach/TLS | Shared runtime/thread attachment reused; TLS installation is per invocation |
| Code-manager registration | Conceptually PER_MODULE, but currently RUN_ONCE singleton |
| Module initializer | Conceptually PER_MODULE, but current bridge is once-only |
| Managed entry | PER_INVOCATION |

The current wrapper already distinguishes same-image reentry from first startup: after `kStartupReady`, it installs TLS and calls `ManagedMain` without calling `RhInitialize` again. That is the correct C103 shape, but it is not a second-module registration contract.

## Why no registry was added

The requested capacity is two resident modules, but a guideXOS-only registry would not be safe with the locked runtime. The existing one-record loader is therefore intentionally unchanged. App A remains resident and reusable; App B is validated and rejected before map, registration, module initialization, managed entry, or allocation. No half-registered B state is created.

## Artifact and C104 facts retained

App A is fixed-base at `0x100000000`, entry `0x10004C5C0`, executable envelope `[0x100000000, 0x10012E000)`. PE SHA-256 is `60CEB3190C8FF3194801F766738294BE5508158DC5E9D5D0BFA193A89982672E`; ELF SHA-256 is `92E4E27396968D21C363B35F754F8B0F5A53CD46A972E5BC1C2F5F1285542DC9`.

App B is fixed-base at `0x102000000`, entry `0x10204C600`, executable envelope `[0x102000000, 0x10212E000)`. PE SHA-256 is `B274FB3A37713856AB627CE0A467B3D87B2DEC983CD4A004E9225E06EF2424BD`; ELF SHA-256 is `2FD0EDA0007153D07A1F923F3A9947F17D8759F79065E5DAD5D639570012E2E0`.

Both artifacts are ELF64/AMD64/`ET_EXEC`, fixed-base, no-relocation, non-overlapping, and contain no W+X load segment. C101 converter SHA-256 remains `5BED5BBE8883EDD700ED2406FA43A7B26F762212D06898BB75B540E07B2F5621`.

## Frame and negative evidence

Fresh C105 pre-edit C104 frame values vary with the ordinary memory map, so the run's own values are authoritative:

| Checkpoint | Free | Allocated | VmRegion | PageTable |
|---|---:|---:|---:|---:|
| Before A | `0x2C82D` | `0x3E96` | `0x0` | `0x0` |
| After A first load/return | `0x2C5C6` | `0x40FD` | `0x260` | `0x7` |
| After rejected B | `0x2C5C6` | `0x40FD` | `0x260` | `0x7` |
| After A second run | `0x2C5C6` | `0x40FD` | `0x260` | `0x7` |

One-time A mapping cost was `0x267` tracked frames, `0x260` VmRegion units, and `0x7` page-table units in that run. B mapping cost was `0`; duplicate A reentry cost was `0`. Frame and owner residuals were zero. The retained missing-B sequence is `A PASS → missing B not-found → A PASS`, with the kernel alive and no residuals. The current runtime cannot provide `B PASS` after the missing-file probe; it remains the same authenticated Busy boundary if B is requested.

## Decision and C106 boundary

No C105 runtime fork, GC policy change, PMM change, VM policy change, converter change, relocation, rebasing, Developer Studio work, or B02 run was performed. C69–C97 remains closed and B02 remains `STILL_PREMATURE`.

The smallest evidence-backed C106 milestone is to choose one architectural direction: a separate source-supported runtime/address-space instance per application, one composite managed image, or a deliberate bounded NativeAOT multi-module extension. C105 does not choose among them.

## Required numbered report

1. Outcome: Outcome G — NativeAOT multi-module runtime limitation authenticated.
2. Success Level: 1 — singleton provenance established.
3. Repository: `D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT`.
4. Branch: `v1.1_DOTNET_SUPPORT`.
5. Starting HEAD: `8c3e19035fa8e57b59daa9410fbd044dd6c9fd5a`.
6. Starting subject: `Support multiple production NativeAOT applications`.
7. Final HEAD: recorded at closeout after the local C105 commit.
8. Final subject: `Trace NativeAOT resident module limitation`.
9. Upstream: `origin/v1.1_DOTNET_SUPPORT`.
10. Starting ahead/behind: ahead 1 / behind 0.
11. Final ahead/behind: ahead 2 / behind 0 after the local commit, before any push.
12. Starting worktree: clean.
13. Final worktree: clean, with ignored C105 evidence under `out/dotnet`.
14. Runtime identity: NativeAOT 9.0.0, AMD64, Workstation GC, `net9.0/win-x64`.
15. Runtime source SHA: `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`.
16. C103 SHA: `bb3a3d426a7ba5bc737d8e31608e3b91f81c8a97`.
17. C104 SHA: `8c3e19035fa8e57b59daa9410fbd044dd6c9fd5a`.
18. C105 SHA: the final local commit recorded in item 178.
19. C104 baseline reproduced: yes, fresh isolated pre-edit proof-mode-0 boot.
20. A→busy-B→A preserved before fix: yes; A PASS, B Busy, A PASS; residuals zero.
21. Exact C105 question: whether one shared NativeAOT runtime can register and execute A and B in one boot.
22. Busy guard source: `kernel/core/nativeaot_application.cpp:1143-1200`.
23. Busy guard function: `launchResident`.
24. Busy guard condition: different requested path from `g_application.path`; non-overlap maps to Busy.
25. Singleton state field: `ResidentApplication g_application`, plus one artifact base/span and one page table.
26. Singleton owner: guideXOS production NativeAOT loader, backed by runtime-pack singleton registration.
27. Guard removal performed: no.
28. Guard generalized safely: no; runtime source proves that would be unsafe.
29. Multiple code managers supported: no, not by the locked source.
30. Code-manager registry source: none; `m_CodeManager` is scalar.
31. Module registry source: TypeManager/OS-module add-only lists exist, but not a code-manager registry.
32. `FindMethodInfo` multi-manager behavior: absent; lookup returns the one manager after one-range test.
33. Root enumeration multi-module behavior: not authenticated; audited GC scan is shared ThreadStore/thread roots.
34. Static-root multi-module support: not authenticated for this direct bridge; second `InitializeModules` is not supported by current path.
35. EH/unwind multi-module support: image-local `CoffNativeCodeManager` metadata exists, but global dispatch is scalar.
36. Runtime-global initialization model: PAL, RuntimeInstance, GC, ThreadStore and shared callbacks are once-only.
37. Module-specific initialization model: per-image metadata/code manager/`InitializeModules` is conceptually distinct but current bridge is once-only.
38. Second module registration supported by runtime source: no.
39. NativeAOT intrinsic single-module restriction found: yes; one manager/range plus no multi-instance runtime.
40. PAL initialization classification: RUN_ONCE.
41. GC initialization classification: RUN_ONCE.
42. VM callback classification: shared wiring RUN_ONCE; module range/context data per module/invocation.
43. Runtime thread attach classification: shared thread/runtime state reused; TLS installation per invocation.
44. Code-manager registration classification: currently RUN_ONCE; required future semantics would be PER_MODULE.
45. Module initializer classification: current bridge RUN_ONCE; required future semantics would be PER_MODULE.
46. Managed entry classification: PER_INVOCATION.
47. Run-once/per-module/per-invocation table completed: yes, in the C105 doc and evidence.
48. Registry implemented: no; intentionally not attempted against the scalar runtime.
49. Registry capacity: not applicable; current resident capacity remains one.
50. Registry key: current loader path key only for same-image reuse.
51. App A resident record: yes, existing `g_application` record.
52. App B resident record: no; B never becomes resident.
53. Duplicate A load avoided: yes.
54. Duplicate B load avoided: no second B exists to duplicate; B is rejected before load/map.
55. Partial-registration state protected: yes; transactional C104 path never marks B resident.
56. Registry residual/invariant failures: none observed; no B record was created.
57. A base: `0x100000000`.
58. A PE SHA: `60CEB3190C8FF3194801F766738294BE5508158DC5E9D5D0BFA193A89982672E`.
59. A ELF SHA: `92E4E27396968D21C363B35F754F8B0F5A53CD46A972E5BC1C2F5F1285542DC9`.
60. A executable range: `[0x100000000, 0x10012E000)`.
61. A entrypoint: `0x10004C5C0`.
62. A code manager: one authentic `CoffNativeCodeManager` registered by `RhRegisterOSModule`.
63. A module registration: yes, once.
64. A managed state identity: `C104-APP-A-ENTRY/PASS`; deterministic sum 36; same-image reentry passes.
65. B base: `0x102000000`.
66. B PE SHA: `B274FB3A37713856AB627CE0A467B3D87B2DEC983CD4A004E9225E06EF2424BD`.
67. B ELF SHA: `2FD0EDA0007153D07A1F923F3A9947F17D8759F79065E5DAD5D639570012E2E0`.
68. B executable range: `[0x102000000, 0x10212E000)`.
69. B entrypoint: `0x10204C600`.
70. B code manager: none registered; B stops at the loader Busy decision.
71. B module registration: no.
72. B managed state identity: not reached; C104 artifact identity includes `C104-APP-B-ENTRY/PASS`, weighted sum 150 and sentinel `0xB04B`, but those are not claimed in C105 as boot execution.
73. A `FindMethodInfo`: source-authenticated through the one registered manager/range; no separate serial lookup was required for Outcome G.
74. B `FindMethodInfo`: not attempted because B has no registered manager.
75. A lookup returns A metadata: yes by the scalar A manager contract.
76. B lookup returns B metadata: no; no B lookup path exists.
77. Code ranges overlap: no.
78. Code-manager aliasing: no A/B aliasing occurred; B was not registered.
79. Multi-module root scan valid: no proof; not claimed.
80. GC remains single shared instance: yes.
81. Runtime reinitialized for B: no, correctly avoided.
82. Runtime reused for B: no B registration was attempted; same runtime remained resident for A.
83. A launch1: success.
84. A entry1: PASS marker.
85. A allocation1: PASS managed allocation/diagnostic path.
86. A PASS1: yes.
87. A return1: launcher regained.
88. B launch1: Busy before map/registration.
89. B entry1: no.
90. B allocation1: no.
91. B PASS1: no; intentionally not faked.
92. B return1: no managed entry; bounded Busy report returned.
93. A launch2: success via resident reuse.
94. A entry2: PASS marker.
95. A allocation2: PASS managed allocation/diagnostic path.
96. A PASS2: yes.
97. A return2: launcher regained.
98. Kernel alive: yes; main-loop marker observed.
99. A static/sentinel state: A marker and deterministic state retained across same-image reentry.
100. B static/sentinel state: not established because B did not enter managed code.
101. A state after B: unchanged and PASS.
102. B state independent: not testable without B registration.
103. Cross-app corruption: none; B made no claims and no mappings.
104. State persistence intentional: A resident-module semantics are intentional; B isolation is a C106/runtime-extension requirement.
105. Total tracked: fresh pre-edit C104 `0x306C3`.
106. Before A free: `0x2C82D`.
107. After A map free: `0x2C5C6`.
108. After A return free: `0x2C5C6`.
109. After B map free: `0x2C5C6` because B did not map.
110. After B return free: `0x2C5C6`.
111. After A reentry free: `0x2C5C6`.
112. VmRegion counts: before `0x0`, after A and thereafter `0x260`.
113. PageTable counts: before `0x0`, after A and thereafter `0x7`.
114. A one-time mapping cost: `0x267` frames, `0x260` VmRegion, `0x7` PageTable in the fresh run.
115. B one-time mapping cost: `0`.
116. Duplicate reentry mapping cost: `0`.
117. Frame residual: `0`.
118. Owner residual: `0`.
119. Leak detected: no.
120. Missing-app sequence: `A PASS → missing B not-found → A PASS`.
121. Error result: `NotFound` for missing B.
122. B remains launchable after missing: not as a managed module; it remains cleanly validated/rejected Busy under the same singleton contract.
123. A remains launchable after missing: yes, A PASS.
124. Kernel survives: yes.
125. Registry remains valid: yes, the single A resident record remains valid.
126. Frame accounting preserved: yes; missing B added no mappings and residuals stayed zero.
127. A/B ET_EXEC: yes.
128. A/B relocations: none.
129. A/B W+X: none.
130. A/B overlap: none.
131. Build-time bases preserved: yes, A `0x100000000`, B `0x102000000`.
132. Unsafe rebasing used: no.
133. C101 converter guard preserved: yes; converter source and hash unchanged.
134. C99 PMM preserved: yes; production frame allocator path unchanged.
135. C103 lifecycle preserved: yes; same-image A reentry remains resident/no duplicate map.
136. C18: unchanged; no C105 source touched the C18 surface.
137. Code-manager authenticity: A uses authentic `CoffNativeCodeManager`/`RhRegisterOSModule`; B has none.
138. Root scan: shared NativeAOT thread-root scan remains the only authenticated path.
139. Mark closure: no C105 GC/mark changes.
140. GC policy mutation: none.
141. PMM mutation: none.
142. VM policy mutation: none.
143. Converter trust mutation: none.
144. Candidate/planner mutation: none.
145. Survivor/root fabrication: none.
146. B02 status: `STILL_PREMATURE`; not run.
147. C99 PMM tests: prior C99/C104 accepted results retained; no C105 PMM change.
148. VM/PAL tests: fresh ordinary boot PASS and fresh C104 production boot PASS/BUSY/PASS.
149. Runtime-pack validation: prior C104 runtime-pack identity retained; no C105 runtime-pack rebuild was claimed.
150. App A publish: retained C104 production artifact.
151. App B publish: retained C104 production artifact.
152. App A PE→ELF assertions: pass/retained.
153. App B PE→ELF assertions: pass/retained.
154. Kernel/native build: C104 flag build completed for the isolated boot; ordinary canonical kernel restored by exact hash.
155. PowerShell syntax: no PowerShell source changed; runner and validator were executed.
156. JSON parse: fresh ordinary validator manifest parsed successfully.
157. `git diff --check`: run at closeout.
158. Symbol checks: source-symbol/guard searches passed; no C105 symbol replacement was needed.
159. Linker/source/table/archive guards: inspected; scalar runtime guard remains intact.
160. C103 regression boot: C104 fresh A reentry covers the resident lifecycle; no regression observed.
161. A→B→A boot: baseline boundary reproduced as A PASS/B Busy/A PASS, not Level 3.
162. Negative sequence boot: retained accepted missing-B boot passed A/not-found/A and reached main loop.
163. Invariant failures: none.
164. Overflow: none.
165. Fail-fast: none in accepted baseline; source audit shows a second code-manager path would be unsafe.
166. Page faults: none.
167. Nondeterminism: no semantic variance; frame totals vary only with boot memory-map layout.
168. C69–C97 remains closed: yes.
169. Ordinary artifacts restored: yes.
170. Ordinary kernel SHA: `75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6`.
171. Ordinary ESP SHA: `75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6`.
172. Proof artifact active: no; canonical ordinary ESP is active and C105 did not modify it.
173. C105-owned QEMU count: `0` at closeout.
174. Unrelated QEMU preserved: no unrelated process was present at capture or closeout.
175. Files changed: tracked C105 documentation only; ignored evidence was added under `out/dotnet/c011ec105-multi-module-runtime-registration/`.
176. Documentation path: `docs/dotnet/NATIVEAOT_C105_MULTI_MODULE_RUNTIME_REGISTRATION.md`.
177. Evidence root: `out/dotnet/c011ec105-multi-module-runtime-registration/`.
178. Final commit: local `Trace NativeAOT resident module limitation`; hash recorded after commit.
179. Push status: not pushed.
180. Remaining limitation: locked NativeAOT supports one resident code manager/managed-code range and one runtime instance in the current composition.
181. Exact next-smallest milestone: C106 must choose, with evidence, between separate runtime/address-space contexts, one composite managed image, or a source-justified bounded multi-module NativeAOT extension.
