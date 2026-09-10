# C101 — NativeAOT PE→ELF Converter Provenance and Trusted Publish Recovery

## Decision

C101 reaches **Outcome A — stale expected hash**, with one related stale
post-conversion envelope assertion corrected. The preferred result is **Level 3**:
the managed NativeAOT publish now selects the repository converter, the strict
identity guard passes, PE→ELF conversion succeeds, the converted ELF passes the
structural and symbol guards, and converting the same input twice is byte
identical.

C100 did not fail because the production launcher design was invalid. C100
stopped because the NativeAOT artifact pipeline could not authenticate its
PE→ELF converter. C101 restores trust in that pipeline before production launch
implementation continues.

The smallest correct repair was to update the stale exact expected SHA to the
authenticated current converter source and reconcile the old first-`PT_LOAD`
assertion with the intentional `e2877920` converter contract. The hash check
remains strict equality. No wildcard, warning-only path, arbitrary binary, or
conversion bypass was added.

Evidence root:

`out/dotnet/c011ec101-pe-elf-converter-provenance/`

The final artifact manifest is:

`out/dotnet/c011ec101-pe-elf-converter-provenance/final/artifact-manifest.json`

## Provenance finding

The C100 guard was introduced/updated in commit `c4096f2f` and expected
`55994B674326D21A8FADE6FDDBA10D6A602E5605F67709C87F9AA57C9212F678`.
The converter source was then intentionally changed in `e2877920`
(`Register NativeAOT production code manager`) to add parser bounds checks and
to place the PE headers in the fixed-base first `PT_LOAD`. The guard was not
updated with that source change. The current tracked source is
`5BED5BBE8883EDD700ED2406FA43A7B26F762212D06898BB75B540E07B2F5621` on the
live Windows worktree, size 13,709 bytes, source commit
`e28779207a97409ed023e63aedd2c1a76f22ae27`, Git blob
`addce25858d56a7eb110838d30373aad7185e178`.

No committed source revision matching the old `55994B...` value was found. The
previous `5F21B...` value belongs to an older converter artifact. C100 selected
the repository-relative path directly, so this was not a stale executable or
PATH-order problem.

The original post-conversion assertion still required the older fileless
image-base reservation. C101 now requires the current authenticated contract:
the first `PT_LOAD` is read-only, page aligned, starts at the PE image base,
contains exactly the PE header span, begins with `MZ`, and byte-matches the
published PE headers.

## Numbered report

1. Outcome: Outcome A — stale expected hash, with the related stale ELF envelope guard reconciled.
2. Success Level: Level 3.
3. Repository: `D:/dev/guideXOSServerV1.1_DOTNET_SUPPORT`.
4. Branch: `v1.1_DOTNET_SUPPORT`.
5. Starting HEAD: `18a553995697393ba37b2e829bb032d893bc6173`.
6. Starting subject: `Design production NativeAOT application launch`.
7. Final HEAD: the local C101 closeout commit; the exact SHA is reported by the final task closeout after commit.
8. Final subject: `Restore trusted NativeAOT PE to ELF conversion`.
9. Upstream: `origin/v1.1_DOTNET_SUPPORT`.
10. Starting ahead/behind: `0/0`.
11. Final ahead/behind: `1/0` after the local C101 commit.
12. Starting worktree: clean.
13. Final worktree: clean after commit.
14. Runtime identity: AMD64, `net9.0`, `win-x64`, Microsoft.DotNet.ILCompiler `9.0.0`, Workstation GC.
15. Runtime source SHA: `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`.
16. C99 SHA: `7a250e65ef8805d745508173ddb48541eef9b4af`.
17. C100 SHA: `18a553995697393ba37b2e829bb032d893bc6173`.
18. C101 SHA: the final local commit containing this document and the two guard repairs.
19. Exact C101 question: why the NativeAOT PE→ELF converter failed its expected SHA guard, and what smallest trusted repair restores reproducible conversion.
20. C100 publish command: `powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/dotnet/build-managed-hostlog-proof.ps1 -RepoRoot D:/dev/guideXOSServerV1.1_DOTNET_SUPPORT -OutputRoot out/dotnet/c011ec101-pe-elf-converter-provenance/c100-publish-recovery-final/managed-hostlog -Clean`.
21. Converter selected path: `tools/dotnet/pe_to_elf_v2_fixed_base.py`.
22. Expected hash before repair: `55994B674326D21A8FADE6FDDBA10D6A602E5605F67709C87F9AA57C9212F678`.
23. Actual hash before repair: `5BED5BBE8883EDD700ED2406FA43A7B26F762212D06898BB75B540E07B2F5621`.
24. Hash mismatch reproduced: yes, exactly in the fresh C101 baseline run.
25. Failure occurs before conversion: yes; the guard throws before Python is invoked and before PE publish.
26. Existing guard source: `scripts/dotnet/build-managed-hostlog-proof.ps1`, lines 195–198 before repair.
27. Guard semantics: SHA-256 the selected file and throw unless actual equals one exact expected value.
28. Converter source project: no compiled converter project; the converter is tracked Python source.
29. Converter source paths: canonical `tools/dotnet/pe_to_elf_v2_fixed_base.py`; focused test `tools/dotnet/test_pe_to_elf_v2_fixed_base.py`.
30. Converter build project/config: `scripts/dotnet/build-managed-hostlog-proof.ps1`; runtime-pack PAL validation uses the same repository-relative converter path.
31. All converter copies found: the canonical Server source, its focused test, four inactive `D:/dev/bkup/inactive/guideXOSUEFI/Tools` copies, and no generated converter source copy under `out/`.
32. Canonical converter path: repository-relative `Join-Path $RepoRoot tools\dotnet\pe_to_elf_v2_fixed_base.py`.
33. C100-selected copy canonical: yes.
34. Expected-hash source file: `scripts/dotnet/build-managed-hostlog-proof.ps1`.
35. Last converter-source commit: `e28779207a97409ed023e63aedd2c1a76f22ae27`.
36. Last converter-build/guard commit: `c4096f2f3bc630dba4722a668030c3ed91536a13`.
37. Last expected-hash commit: `c4096f2f3bc630dba4722a668030c3ed91536a13`.
38. Last known matching revision: no committed revision matching `55994B...` was found; the prior recorded `5F21B...` matched an older artifact.
39. Provenance mismatch classification: primary `STALE_EXPECTED_HASH`; source/guard provenance was stale after the intentional converter source change.
40. Clean rebuild command: two independent `git cat-file blob HEAD:tools/dotnet/pe_to_elf_v2_fixed_base.py` materializations followed by Python unit tests and conversion.
41. Compiler/toolchain: Python `3.12.14`; converter is source-only; NativeAOT uses .NET SDK `10.0.401` with MSVC `19.51.36231` x64 and GNU binutils `2.46.0.20260210`.
42. Build configuration: NativeAOT `Release`, `net9.0`, `win-x64`, self-contained, `PublishAot=true`, non-allocating HostLogProof mode.
43. Rebuild 1 SHA: clean canonical source `43C1A71FE87AEE75EAE4F2BC1A3AD8DDDF22BD78FA94105FB2EBD80DA7BC6F3C`.
44. Rebuild 1 size: 13,460 bytes for the LF-normalized Git materialization.
45. Rebuild 2 performed: yes, from a separate clean materialization.
46. Rebuild 2 SHA: `43C1A71FE87AEE75EAE4F2BC1A3AD8DDDF22BD78FA94105FB2EBD80DA7BC6F3C`.
47. Rebuild hashes equal: yes.
48. Build reproducible: yes for the source-only converter and conversion output.
49. Nondeterministic bytes found: no.
50. Nondeterminism source: not applicable.
51. Determinism fix required: no converter determinism fix was required.
52. Determinism fix applied: no; the existing strict source hash was corrected to the authenticated live artifact.
53. Wrong path found: no.
54. Tool-resolution fix: none required; the repository-relative path was already canonical.
55. Stale artifact found: no stale converter binary; the stale item was the expected source hash and one old structural assertion.
56. Stale artifact replaced: no arbitrary artifact was copied; the generated C101 recovery outputs were rebuilt from source.
57. Expected hash stale: yes.
58. Expected hash updated: yes, to `5BED5BBE8883EDD700ED2406FA43A7B26F762212D06898BB75B540E07B2F5621`.
59. Basis for updating expected hash: intentional `e2877920` source change understood, clean source copies matched, focused tests passed, valid/negative conversion passed, and final NativeAOT output passed all structural guards.
60. Hash guard weakened: no.
61. Wildcard acceptance added: no.
62. Canonical source→binary provenance established: yes; source commit, Git blob, live SHA, toolchain, input PE, and output ELF are in the manifest.
63. Known-good valid fixture: existing NativeAOT `out/dotnet/track-b-hostlog-current-20260723/artifacts/HostLogProof.exe` and the fresh C101 NativeAOT PE.
64. Conversion result: valid fixture conversion exited `0`; final publish conversion exited `0`.
65. Negative fixture: truncated PE containing `MZ` but no `e_lfanew` span.
66. Negative result: deterministic exit `1`, clear truncation error, no output file, no crash.
67. ELF class: ELF64.
68. ELF architecture: AMD64 / x86-64.
69. ELF entrypoint valid: yes, `0x10001900`, equal to `ManagedMain` from the map.
70. PT_LOAD count: 7.
71. PT_LOAD layout valid: yes; all loads are within the PE image range and fixed-base order is preserved.
72. Alignment valid: yes; each load is page aligned with `p_align=0x1000`.
73. `filesz/memsz` handling valid: yes; no load has `filesz > memsz`.
74. Writable/executable flags valid: yes; separate `R-X` and `RW` loads, with no `RWX` load.
75. BSS/zero-fill valid: yes; the RW load has `filesz=0xE00`, `memsz=0x22D10`.
76. Required symbols valid: `ManagedMain`, startup symbols, TLS symbols, and the indirect host callback evidence passed.
77. Relocation assumptions valid: no relocations, no dynamic section, no interpreter, and no section table.
78. Historical known-good comparison performed: yes, against the prior seven-`PT_LOAD` ELF for the same sample input family.
79. Structural agreement: entry, preferred base, seven loads, page alignment, permissions, BSS, no dynamic dependencies, and no relocations agree; the current first load intentionally contains exact PE headers.
80. Same input converted twice: yes, using the fresh published PE and map.
81. Output 1 SHA: `84E5754F321A9DEA35775B3D7072030102545584AFFCC70B6017FB5B278AC80D`.
82. Output 2 SHA: `84E5754F321A9DEA35775B3D7072030102545584AFFCC70B6017FB5B278AC80D`.
83. Output hashes equal: yes.
84. Conversion deterministic: yes.
85. Managed NativeAOT publish rerun: yes, in the C101 recovery root.
86. NativeAOT publish passes: yes.
87. NativeAOT PE path: `out/dotnet/c011ec101-pe-elf-converter-provenance/c100-publish-recovery-final/managed-hostlog/artifacts/HostLogProof.exe`.
88. PE SHA: `B8A9F2884B46FDFF9C456C5570E44F7AE8FDC031B7EAF30CBE86F7D85ED41818`.
89. Converter guard passes: yes, expected equals actual `5BED5BBE...2F5621`.
90. PE→ELF conversion passes: yes.
91. ELF path: `out/dotnet/c011ec101-pe-elf-converter-provenance/c100-publish-recovery-final/managed-hostlog/artifacts/HostLogProof.elf`.
92. ELF SHA: `84E5754F321A9DEA35775B3D7072030102545584AFFCC70B6017FB5B278AC80D`.
93. ELF structural guards pass: yes, including the reconciled exact-header first-load guard.
94. Artifact manifest updated: yes, at the C101 final evidence path.
95. Converter provenance recorded in manifest: yes.
96. C100 candidate can now be staged: yes, by a future milestone using this authenticated pipeline.
97. Candidate actually staged: no; it remains C101 evidence only and ordinary ESP was not touched.
98. Production launcher implemented: no.
99. Production launcher intentionally deferred: yes; implementation resumes in C102.
100. GC mutation: none.
101. PMM mutation: none.
102. VM mutation: none.
103. Runtime behavior mutation: none.
104. Converter correctness mutation: no converter logic mutation in C101; the existing `e2877920` contract was authenticated and its guard was aligned.
105. Tool-path mutation: none.
106. Hash-guard mutation: one strict expected value update; equality semantics unchanged.
107. Ordinary ESP changed: no.
108. Ordinary kernel changed: no.
109. Ordinary artifacts restored: yes; recorded kernel, BOOTX64.EFI, and ramdisk hashes remain unchanged.
110. B02 evaluated: no execution; scope remains toolchain provenance only.
111. B02 status: `STILL_PREMATURE`.
112. Converter build: PASS, two clean source materializations and two identical conversions.
113. Converter tests: PASS, focused geometry test plus malformed-input negative test.
114. Runtime-pack validation: C100/C52 Tier-A baseline PASS is retained; C101 did not mutate or rebuild the runtime pack.
115. Managed build: PASS.
116. NativeAOT publish: PASS.
117. PowerShell syntax: PASS for changed assertion and publish scripts.
118. JSON parse: PASS for runtime-pack lock and C101 artifact manifest.
119. `git diff --check`: PASS.
120. PE→ELF conversion: PASS.
121. Symbol checks: PASS; map and object checks include `ManagedMain`, startup, TLS, and host callback evidence.
122. Linker/source/table/archive guards: PASS; the existing managed proof envelope passed all required checks.
123. C52 Tier-A result or reason omitted: C52 Tier-A was already PASS in C100 evidence; no rerun was needed because C101 changed only converter identity/envelope guards.
124. QEMU required: no; C101 acceptance is source/toolchain/artifact provenance and does not claim ordinary runtime launch.
125. QEMU result if run: not run.
126. C101-owned QEMU cleanup: none required.
127. Unrelated QEMU preservation: no C101-owned QEMU process was started or terminated.
128. Files changed: `scripts/dotnet/build-managed-hostlog-proof.ps1`, `scripts/dotnet/managed-hostlog-artifact-assertions.ps1`, and this document.
129. Documentation path: `docs/dotnet/NATIVEAOT_C101_PE_ELF_CONVERTER_PROVENANCE.md`.
130. Evidence root: `out/dotnet/c011ec101-pe-elf-converter-provenance/`.
131. Final commit: `Restore trusted NativeAOT PE to ELF conversion`.
132. Push status: not pushed.
133. Remaining limitation: C100 still lacks the ordinary production executable-image/process/address-space/NativeAOT launcher implementation and no ordinary managed `Main` execution is claimed.
134. Exact next-smallest milestone: C102 implements the minimal ordinary production NativeAOT launcher from the validated C100 design, requires actual managed `Main` execution, and does not redesign the launcher unless a new architectural constraint appears.

## Integrity and closeout

The ordinary production artifacts were not staged with the experimental
NativeAOT candidate. C101 changed no GC, PMM, VM, allocator, planner, survivor,
root, or runtime behavior code. The evidence outputs are ignored/generated
under the dedicated C101 root. The local result is committed once and is not
pushed.
