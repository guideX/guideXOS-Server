# C104 — Multiple production NativeAOT applications

C102 proved the first production C# NativeAOT application in an ordinary guideXOS boot. C103 proved safe repeat entry into one resident managed image. C104 asks the next architectural question: can guideXOS distinguish and safely coexist with two distinct managed applications in one boot?

## Result

Outcome G — NativeAOT runtime supports only one active module with the current direct-ELF contracts.

Success Level 1. The two artifacts are genuinely distinct, both pass the trusted NativeAOT/PE→ELF pipeline, B has a source-supported non-overlapping fixed base, and the launcher safely identifies/rejects B before mapping or runtime registration. The requested A→B→A execution sequence does not reach B managed code because the current runtime has one resident `RhRegisterOSModule`/`CoffNativeCodeManager` domain. A remains usable after the rejection.

The exact observed ordinary sequence is:

```text
A launch #1       -> C104-APP-A-ENTRY -> C104-APP-A-PASS -> return 0
B launch #1       -> ELF envelope probe -> busy (no map, no registration, no managed entry)
A launch #2       -> C104-APP-A-ENTRY -> C104-APP-A-PASS -> return 0
kernel            -> main loop
```

A separate missing-B boot uses the same production kernel and stages only A:

```text
A PASS -> B path not-found -> A PASS -> kernel main loop
```

No proof launcher was used and proof mode is `0` in both accepted C104 boots.

## Artifacts and placement

App A is the C102/C103 managed sample in `samples/managed/HostLogProof/Program.cs`, built with mode `C104AppA`. It allocates an eight-byte array, computes `1 + ... + 8 = 36`, and emits `C104-APP-A-ENTRY` and `C104-APP-A-PASS`.

App B is the same production project built as a distinct NativeAOT artifact with mode `C104AppB`. It allocates a five-byte array `[2, 5, 8, 11, 14]`, computes weighted sum `150`, retains static sentinel `0xB04B`, and emits `C104-APP-B-ENTRY` and `C104-APP-B-PASS` if entered. The PE and ELF hashes differ from A; B is not a copied filename.

| Artifact | PE size / SHA-256 | ELF size / SHA-256 | Image base | Entry | PT_LOAD envelope | staged path |
| --- | --- | --- | --- | --- | --- | --- |
| A | 1,011,200 / `60CEB3190C8FF3194801F766738294BE5508158DC5E9D5D0BFA193A89982672E` | 1,032,192 / `92E4E27396968D21C363B35F754F8B0F5A53CD46A972E5BC1C2F5F1285542DC9` | `0x100000000` | `0x10004C5C0` | `[0x100000000, 0x10012E000)` | `/system/wall/C104A.ELF` |
| B | 1,011,712 / `B274FB3A37713856AB627CE0A467B3D87B2DEC983CD4A004E9225E06EF2424BD` | 1,032,192 / `2FD0EDA0007153D07A1F923F3A9947F17D8759F79065E5DAD5D639570012E2E0` | `0x102000000` | `0x10204C600` | `[0x102000000, 0x10212E000)` | `/system/wall/C104B.ELF` |

Both are ELF64 AMD64 little-endian `ET_EXEC`, sectionless, seven `PT_LOAD`s, 4 KiB aligned, with no W+X segment, no dynamic/interpreter section, and no converted-ELF relocations. The distinct B base is produced by the NativeAOT linker `/fixed /base:0x102000000` property and preserved by the locked C101 converter. No post-conversion rebasing was used. The base gap is `0x2000000`; the envelopes do not overlap.

This is `DISTINCT_LINK_BASE_SUPPORTED` at the build/toolchain layer, not a runtime relocation claim. The kernel performs a bounded B envelope probe, then returns `busy`; it does not map B over A.

## Runtime/module audit

The guideXOS runtime-pack calls authentic `RhRegisterOSModule` from `initializeNativeAotModules`, publishes the image's linker-produced `__modules_a`/`__modules_z` range through authentic `InitializeModules`, and sets one process-lifetime `g_guideXosNativeAotCodeManagerRegistered` flag. The current direct entry has one classlib-function table and one module-initialization flag. `CoffNativeCodeManager`, `FindMethodInfo`, root scanning, mark closure, PAL/TLS, and Workstation GC remain authentic for the resident A image.

The source audit finds no supported direct-ELF `RhShutdown`, full GC restart, code-manager unregister, or module unregister boundary. The current one-resident `ResidentApplication` record and one runtime/module registration domain therefore cannot safely activate B. B registration, B `FindMethodInfo`, B root scan, B static initialization, and B GC execution are explicitly not claimed or fabricated. A's `FindMethodInfo` and root/GC behavior remain the retained C18/C102/C103 runtime contract and are not changed by C104.

The lifecycle is intentionally resident: the valid A image and runtime references remain mapped for boot lifetime. A's entry-local array is freshly allocated per invocation; no module reload is performed. B's managed static sentinel is artifact-authenticated but unexecuted, so cross-module static independence is the next runtime boundary rather than an achieved C104 claim.

## Frame accounting

The accepted ordinary boot records:

| Checkpoint | Total tracked | Free | Allocated | VmRegion | PageTable | residual | owner residual |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| before A | `0x2F817` | `0x2B980` | `0x3E97` | `0` | `0` | `0` | `0` |
| after A map/return | `0x2F817` | `0x2B719` | `0x40FE` | `0x260` | `0x7` | `0` | `0` |
| B candidate map/return | `0x2F817` | `0x2B719` | `0x40FE` | `0x260` | `0x7` | `0` | `0` |
| after A re-entry | `0x2F817` | `0x2B719` | `0x40FE` | `0x260` | `0x7` | `0` | `0` |

B has no map/return accounting event because it is rejected after file/envelope validation and before `mapElf`. The A one-time resident transition consumes `0x267` additional allocated frames and owns `0x260` VmRegion plus `0x7` PageTable frames. Repeated A entry creates no duplicate mapping and no additional growth. Every accepted checkpoint satisfies `free + allocated = totalTracked`; owner residual is zero.

## Evidence and reproduction

Evidence is under `out/dotnet/c011ec104-multiple-production-nativeaot-applications/`:

- `capture/live-state-before.txt` — authoritative live pre-change repository, runtime, converter, and ordinary artifact capture.
- `build/app-a/` and `build/app-b/` — separate NativeAOT publish, converter, map, PE/ELF dumps, readelf output, and toolchain records.
- `artifact-base-analysis/layout.txt` — fixed-base, PT_LOAD, permissions, and relocation analysis.
- `module-registration-audit/audit.md` — source-authenticated module/runtime boundary.
- `a-b-a/serial.log` — ordinary A→B candidate rejection→A boot.
- `negative-isolation/serial.log` — ordinary A→missing-B→A boot.
- `frame-accounting/ordinary-and-negative.md` — frame and ownership tables.
- `state-isolation/result.md` — managed identity and bounded failure-containment result.
- `validation/` — hashes, test matrix, and validation notes.

The managed preparation path is repeatable with `scripts/dotnet/run-c104-multiple-production-applications.ps1`; it builds both modes through `build-managed-hostlog-proof.ps1`, enforces the locked runtime-pack/C101 converter, stages both files with `generate-wallpaper-pack.ps1`, and builds the C104 production kernel with `GXOS_NATIVEAOT_PRODUCTION_APPLICATION`, `GXOS_C104_PRODUCTION_LAUNCH`, and the existing cleanup-pass define. The runtime-pack identity is NativeAOT `9.0.0`, AMD64, Workstation GC, GC interface `5.3`, EE interface `2`, `net9.0/win-x64`, runtime source commit `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`. The C101 converter SHA is `5BED5BBE8883EDD700ED2406FA43A7B26F762212D06898BB75B540E07B2F5621`.

## Required final report

1. Outcome: Outcome G — current NativeAOT contracts support one active resident module; B is safely rejected before registration.
2. Success Level: Level 1.
3. Repository: `D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT`.
4. Branch: `v1.1_DOTNET_SUPPORT`.
5. Starting HEAD: `bb3a3d426a7ba5bc737d8e31608e3b91f81c8a97`.
6. Starting subject: `Add repeatable NativeAOT application lifecycle` (the live subject; the request's expected text differed).
7. Final HEAD: the single local C104 commit; exact SHA is recorded in `final-classification/repository-final.txt` and the task closeout.
8. Final subject: `Support multiple production NativeAOT applications`.
9. Upstream: `origin/v1.1_DOTNET_SUPPORT`.
10. Starting ahead/behind: `0/0`.
11. Final ahead/behind: `1/0`.
12. Starting worktree: clean.
13. Final worktree: clean; generated C104 output remains ignored under `out/`.
14. Runtime identity: NativeAOT 9.0.0, AMD64, Workstation GC, GC 5.3, EE 2, `net9.0/win-x64`.
15. Runtime source SHA: `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`.
16. C102 SHA: `07eed66549faa3bcc6839496a3bf5bd68f0a422e`.
17. C103 SHA: `bb3a3d426a7ba5bc737d8e31608e3b91f81c8a97`.
18. C104 SHA: final commit SHA in `final-classification/repository-final.txt`; not embedded here to avoid circular self-reference.
19. C103 baseline reproduced: yes via the accepted C103 A→A ordinary-boot evidence retained at the starting head; a separate bounded pre-edit rerun was inconclusive and is not counted as a new pass.
20. Same-app relaunch still PASS: yes; C104 A re-entry emits A entry/pass and returns `0`.
21. Exact C104 question: can ordinary guideXOS load and execute two distinct C# NativeAOT applications in one boot, preserve identity/state, switch A→B→A, and keep NativeAOT/GC/VM/frame invariants intact?
22. App A source: `samples/managed/HostLogProof/Program.cs`, `HostLogProof.csproj` mode `C104AppA`.
23. App A managed identity: C104 A marker pair; sum `1..8 = 36`.
24. App A PE SHA: `60CEB3190C8FF3194801F766738294BE5508158DC5E9D5D0BFA193A89982672E`.
25. App A ELF SHA: `92E4E27396968D21C363B35F754F8B0F5A53CD46A972E5BC1C2F5F1285542DC9`.
26. App A entrypoint: `0x10004C5C0`.
27. App A PT_LOAD range: `[0x100000000, 0x10012E000)`; span `0x12E000`.
28. App A staged path: `/system/wall/C104A.ELF`.
29. App A managed marker: `C104-APP-A-ENTRY`, `C104-APP-A-PASS`.
30. App A validation: NativeAOT publish, locked runtime-pack, C101 converter, PE envelope, and ELF assertions PASS.
31. App B source: same production sample with `C104AppB` compile mode and distinct conditional managed behavior.
32. App B managed identity: C104 B marker pair; five-byte weighted sum `150`; static sentinel `0xB04B`.
33. App B PE SHA: `B274FB3A37713856AB627CE0A467B3D87B2DEC983CD4A004E9225E06EF2424BD`.
34. App B ELF SHA: `2FD0EDA0007153D07A1F923F3A9947F17D8759F79065E5DAD5D639570012E2E0`.
35. App B entrypoint: `0x10204C600`.
36. App B PT_LOAD range: `[0x102000000, 0x10212E000)`; span `0x12E000`.
37. App B staged path: `/system/wall/C104B.ELF`.
38. App B managed marker: `C104-APP-B-ENTRY`, `C104-APP-B-PASS` present in the ELF but not observed because B is rejected pre-entry.
39. App B validation: NativeAOT publish, locked runtime-pack, C101 converter, PE envelope, and ELF assertions PASS.
40. App B genuinely distinct: yes; source behavior, markers, PE hash, ELF hash, map hash, preferred base, and entry differ.
41. ELF format: ELF64, little-endian, AMD64, sectionless, seven PT_LOADs.
42. ET_EXEC: yes for A and B.
43. Relocations present: no in converted ELF files.
44. PIC/relocation supported: no relocation/PIC claim; the authenticated pipeline is fixed-base ET_EXEC.
45. App A preferred base: `0x100000000`.
46. App B preferred base: `0x102000000`.
47. Mapping overlap: none; B is envelope-probed and not mapped.
48. Base selection mechanism: build-time `HostLogProofImageBase` passed to the NativeAOT linker and preserved by C101 conversion; no runtime allocator was invented.
49. Build-time distinct base used: yes, B uses `/fixed /base:0x102000000`.
50. Toolchain support authenticated: yes; A/B PE ImageBase, ELF PT_LOAD addresses, and entrypoints match.
51. Unsafe post-conversion rebasing used: no.
52. Multi-code-manager registration supported: not by the current direct launcher/runtime contract; only A's resident registration is activated.
53. App A code manager: authentic `CoffNativeCodeManager` registration through `RhRegisterOSModule`, resident for boot lifetime.
54. App B code manager: not registered; B returns `busy` before map/startup/registration.
55. `FindMethodInfo` App A: retained/authentic for A's managed range through the current runtime code-manager lookup contract; no synthetic result added.
56. `FindMethodInfo` App B: not attempted because B never enters managed code.
57. Multi-module root scan: not authenticated; one resident module remains the supported scope.
58. Module statics independent: not authenticated for simultaneous execution; B's static sentinel is source/artifact-only in this result.
59. Module initializer state independent: not authenticated for B; A's one-time initializer state remains resident.
60. Runtime globals shared correctly: yes for the one shared runtime/A domain; multi-module publication is the remaining limitation.
61. GC shared correctly: yes for A allocation and A re-entry; B is rejected before GC use.
62. A launch1 attempted: yes, ordinary VFS path `/system/wall/C104A.ELF`.
63. A entry1: yes, `C104-APP-A-ENTRY`.
64. A allocation1: yes, eight-byte managed array; sum `36`.
65. A PASS1: yes, `C104-APP-A-PASS` and status `success`.
66. A return1: yes, managed return `0`; launcher regained control.
67. B launch1 attempted: yes, `/system/wall/C104B.ELF`.
68. B entry1: no; pre-registration envelope probe only.
69. B allocation1: no; intentionally not reached.
70. B PASS1: no; intentionally not claimed because B cannot enter under current contracts.
71. B return1: no managed return; launcher returns `busy` from the candidate gate.
72. A launch2 attempted: yes, same resident path.
73. A entry2: yes, `C104-APP-A-ENTRY`.
74. A PASS2: yes, `C104-APP-A-PASS` and status `success`.
75. A return2: yes, managed return `0`; launcher regained control.
76. Kernel alive after A→B→A: yes, ordinary main-loop marker present.
77. App A state model: resident image/runtime plus entry-local managed array; no reload.
78. App B state model: separate compiled static sentinel and entry-local array, artifact-authenticated but unexecuted.
79. App A sentinel/state before B: no separate A static sentinel; A marker/calculation/range/frame state is the authenticated identity.
80. App A state after B: unchanged; A re-entry emits PASS and uses the same resident range.
81. Cross-app corruption: none observed at the candidate rejection boundary.
82. App B state independent: distinct in source/artifact; runtime execution independence remains unproved because B is rejected.
83. State persistence intentional: yes; resident image/module state persists for boot lifetime, while A entry-local allocation is fresh.
84. Total tracked frames: `0x2F817` in accepted ordinary and missing-B boots.
85. Before A free: `0x2B980`.
86. After A map free: `0x2B719`.
87. After A return free: `0x2B719`.
88. After B map free: `0x2B719` unchanged; no B map was created.
89. After B return free: `0x2B719` unchanged; B returned from the candidate gate.
90. After A re-entry free: `0x2B719`.
91. VmRegion counts: `0` before A; `0x260` after A; `0x260` across B rejection and A re-entry.
92. PageTable counts: `0` before A; `0x7` after A; `0x7` across B rejection and A re-entry.
93. Owner residual: `0` at every recorded checkpoint.
94. Duplicate A mapping created: no.
95. Duplicate B mapping created: no.
96. Resident one-time A cost: `0x267` additional allocated frames; `0x260` VmRegion plus `0x7` PageTable.
97. Resident one-time B cost: `0`; B is not mapped.
98. Repeated-launch growth: `0` for A re-entry.
99. Frame leak: none observed; `free + allocated = totalTracked` and owner residual is zero.
100. Missing/invalid app tested: missing B tested; invalid ELF was not needed because the candidate boundary was exercised with a valid, non-overlapping B and a missing B.
101. Sequence: A PASS → missing B `not-found` → A PASS.
102. Error: `not-found` for absent `/system/wall/C104B.ELF`.
103. Kernel survived: yes; main loop marker present.
104. Existing resident app remained usable: yes; A remains resident and returns PASS.
105. Subsequent valid launch PASS: yes, A re-entry.
106. Frame accounting preserved: yes; no change during missing probe.
107. Ordinary boot: yes; UEFI/production boot with normal kernel and ramdisk staging.
108. Proof mode disabled: yes, serial discovery line reports `proofMode=0`.
109. Production VFS: yes; `/system/wall` is discovered/opened through normal VFS.
110. Production launcher: yes; `nativeaot::launch`/`launchResident` and production C104 selector.
111. Production PMM: yes; C99 UEFI-derived frame accounting path.
112. Production VM/PAL: yes; existing address-space mapping and NativeAOT PAL/VM hooks.
113. C101 converter guard: PASS; converter SHA `5BED5BBE8883EDD700ED2406FA43A7B26F762212D06898BB75B540E07B2F5621`.
114. Managed C# markers authentic: yes; emitted by managed `Main` through the application host-call callback.
115. C99 PMM preserved: yes; no C99 allocator policy change.
116. C103 lifecycle preserved: yes; resident A re-entry remains PASS and persistent by design.
117. C18: retained; no C18 forensic/runtime contract was reopened or altered.
118. Code managers authentic: yes for A; authentic `CoffNativeCodeManager`/`RhRegisterOSModule`, no fabricated B manager.
119. `FindMethodInfo`: retained for A; B not attempted.
120. Root scan: retained for the resident A runtime; no multi-module root claim.
121. Mark closure: retained; no mark/root code was changed.
122. GC policy mutation: none.
123. PMM mutation: none beyond existing C99 path.
124. VM policy mutation: none; B uses source-supported link placement and preflight only.
125. Converter trust mutation: none; exact C101 hash remains enforced.
126. Candidate/planner mutation: none; no allocator/planner or relocation planner was introduced.
127. Survivor/root fabrication: none.
128. B02 status: `STILL_PREMATURE`; B02 was not run.
129. C99 PMM tests: prior accepted C99 evidence retained; C104 frame invariants PASS.
130. VM/PAL tests: prior accepted C102/C103 VM/PAL evidence retained.
131. Runtime-pack validation: PASS; runtime-pack manifest/archive membership and object hash recorded.
132. App A NativeAOT publish: PASS.
133. App B NativeAOT publish: PASS.
134. App A PE→ELF assertions: PASS.
135. App B PE→ELF assertions: PASS.
136. Kernel/native build: PASS with `GXOS_NATIVEAOT_PRODUCTION_APPLICATION` and `GXOS_C104_PRODUCTION_LAUNCH`.
137. PowerShell syntax: PASS for build, stage, and C104 runner scripts.
138. JSON parse: PASS for runtime-pack manifest and retained evidence manifests.
139. `git diff --check`: PASS.
140. Symbol checks: PASS for C104 entry/marker symbols and retained runtime contracts.
141. Linker/source/table/archive guards: PASS; production import guards, source registration seam, and archive checks retained.
142. Baseline boot: accepted C103 A→A baseline retained; fresh bounded pre-edit rerun was inconclusive and excluded from pass counts.
143. A→B→A boot: PASS as an isolation/lifecycle boot; A PASS/B safe `busy`/A PASS, not B managed execution.
144. Negative isolation boot: PASS; A PASS/missing B/A PASS.
145. Invariant failures: none in accepted C104 boots.
146. Overflow: none; bounded file, ELF, range, and frame arithmetic checks remain active.
147. Fail-fast: none in accepted boots.
148. Page faults: none in accepted boots.
149. Nondeterminism: none across accepted bounded boots; no soak claim.
150. Ordinary artifacts restored: yes; root ESP remained ordinary and the working kernel output is restored to the captured ordinary hash at closeout.
151. Ordinary kernel SHA: `75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6`.
152. Ordinary ESP SHA: kernel `75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6`; ramdisk `7E93E34732526DBFFEE097912F776467B0DD03C989CBDA06F4AA24B0945A969B`; bootloader `1158801E02A9125C3137B8A4D549DB6D34FF8E08A49E328B422EE22117E46984`.
153. Proof artifact active: no; C104 artifacts remain evidence-only under `out/`.
154. C104-owned QEMU count: `0` at closeout.
155. Unrelated QEMU preserved: none was present in the initial capture; only owned bounded runs were started.
156. Files changed: `kernel/core/include/kernel/nativeaot_application.h`, `kernel/core/main.cpp`, `kernel/core/nativeaot_application.cpp`, `samples/managed/HostLogProof/HostLogProof.csproj`, `samples/managed/HostLogProof/Program.cs`, `scripts/dotnet/build-managed-hostlog-proof.ps1`, `scripts/dotnet/run-c104-multiple-production-applications.ps1`, `scripts/generate-wallpaper-pack.ps1`, and this document.
157. Documentation path: `docs/dotnet/NATIVEAOT_C104_MULTIPLE_PRODUCTION_APPLICATIONS.md`.
158. Evidence root: `out/dotnet/c011ec104-multiple-production-nativeaot-applications/`.
159. Final commit: one local commit with subject `Support multiple production NativeAOT applications`; exact SHA in final classification evidence.
160. Push status: not pushed.
161. Remaining limitation: the current runtime/launcher has one active resident NativeAOT module; B cannot yet register code manager, module metadata, statics, roots, or GC state alongside A.
162. Exact next-smallest milestone: C105 should add only a bounded multi-module runtime registration/metadata registry with independent code-manager ranges, module tables/static bases/root providers, and safe resident lookup; no packaging, scheduler, process isolation, or B02.

The C69–C97 forensic arc remains closed. C104 does not run B02 and does not claim a general managed application ecosystem until the exact Outcome G registration boundary is solved.
