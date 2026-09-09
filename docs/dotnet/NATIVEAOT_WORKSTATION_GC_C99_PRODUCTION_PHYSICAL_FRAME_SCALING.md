# NativeAOT Workstation GC C99 — Production Physical-Frame Scaling

1. Milestone: C99 production physical-frame scaling from the discovered UEFI memory map.
2. Requested outcome: replace the fixed physical-frame pool with production tracking sized from actual firmware memory.
3. The change covers the bootloader handoff, ordinary kernel boot, and the NativeAOT-facing virtual-memory path.
4. The implementation is intentionally a small production-path change plus a bounded QEMU validation harness.
5. The authoritative workspace is `D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT`.
6. The authoritative branch at start was `v1.1_DOTNET_SUPPORT`.
7. The authoritative starting `HEAD` was `2c8dc6b052c2129051f6bfb01d21470d2f86dad5`.
8. The starting commit subject was `Classify NativeAOT physical memory capacity`.
9. The starting upstream was `origin/v1.1_DOTNET_SUPPORT`.
10. Starting ahead/behind was `0/0`.
11. The starting worktree was clean.
12. The pasted expectation mentioned a clean tree ahead by one commit, but the live repository was clean and ahead/behind `0/0`.
13. The live repository state was used as authoritative for this milestone.
14. Two unrelated QEMU processes were present at the initial baseline.
15. Unrelated PID 2504 belonged to `D:\dev\guideXOS_NET10_nativeaot-managed-kernel-integration`.
16. Unrelated PID 2504 was the phase53c provenance run and used `-m 128M`.
17. Unrelated PID 5624 belonged to `D:\dev\guideXOSServerV0.5_DEVELOPER_STUDIO`.
18. Unrelated PID 5624 used `-m 1024M`.
19. Neither baseline process was killed or modified.
20. Later checks found those exact original PIDs naturally exited; only runner-owned QEMU children were stopped.
21. The old production design used a `4096`-page frame pool.
22. The old represented capacity was `4096 * 4096 = 16 MiB`.
23. The old pool was used by the generic address-space allocator.
24. The same allocator served VM regions, page tables, and the NativeAOT-facing PAL/VM adapter.
25. The old capacity therefore affected both ordinary and opt-in runtime-facing paths.
26. The UEFI BootInfo already carried a final memory-map pointer.
27. The UEFI BootInfo already carried the memory-map entry count.
28. The UEFI BootInfo already carried the descriptor size.
29. The old final BootInfo fields were repurposed at the same offsets for metadata base and metadata pages.
30. The layout remains packed and version-compatible for the existing handoff.
31. The bootloader now snapshots the pre-ExitBootServices map for sizing.
32. The bootloader classifies types 3, 4, and 7 as allocator-usable.
33. Type 3 is `EfiBootServicesCode`.
34. Type 4 is `EfiBootServicesData`.
35. Type 7 is `EfiConventionalMemory`.
36. Runtime, reserved, ACPI, MMIO, unusable, and loader metadata ranges are excluded.
37. The final kernel parser repeats the same type classification from the handed-off map.
38. The parser validates descriptor size before reading descriptor fields.
39. The parser validates descriptor-count multiplication for overflow.
40. The parser validates each descriptor’s physical range for overflow.
41. Physical frame zero remains reserved as the public allocation failure sentinel.
42. Adjacent usable descriptors are normalized into one logical range.
43. The allocator stores normalized ranges as physical start, page count, and packed first-frame index.
44. The range table has 256 entries.
45. The 256-entry bound limits descriptor fragmentation only.
46. The 256-entry bound is not a RAM capacity limit.
47. Per-frame state is no longer represented by a fixed `FrameState[4096]` array.
48. Per-frame state is stored in one metadata byte per enrolled frame.
49. The low metadata bits store ownership.
50. A metadata bit stores whether a frame is currently mapped.
51. Metadata allocation is `ceil(discoveredFrames / 4096)` pages.
52. Metadata is allocated as `EfiLoaderData` before ExitBootServices.
53. Metadata is explicitly identity-mapped for the kernel.
54. The metadata range is not re-enrolled as usable RAM by the final parser.
55. Allocation scans packed enrolled frames rather than a fixed physical window.
56. Physical-to-packed-index lookup walks normalized firmware ranges.
57. Packed-index-to-physical lookup reverses the same mapping.
58. Owner mismatch remains rejected.
59. Double release remains rejected.
60. Releases of non-enrolled physical addresses remain rejected.
61. Mapped frames cannot be released until unmapped.
62. VM-region and page-table counters remain separate.
63. Decommit and release accounting remains separate.
64. Mapping changes continue to invalidate the local TLB.
65. Page-table creation may leave empty hierarchy pages allocated, as before.
66. The C99 probe accounts for those persistent hierarchy pages explicitly.
67. No fixed frame-capacity constant is used to determine production capacity.
68. The bootloader’s page-table-page array remains only bounded bootstrap bookkeeping.
69. Its bound was raised to 2048 pages for the tested QEMU maps.
70. The loader now identity-maps every selected usable descriptor range.
71. This ensures every enrolled frame can be zeroed and every page-table frame can be edited after handoff.
72. The loader preserves explicit mappings for the stack, framebuffer, ramdisk, ACPI, loader, and trampoline.
73. The loader keeps overflow guards on identity and virtual range construction.
74. The loader fails closed when its bounded bootstrap range bookkeeping would overflow.
75. The kernel reports firmware-derived physical-frame initialization on the ordinary BootInfo path.
76. Ordinary builds do not enable the C99 probe.
77. Ordinary builds still call the same `address_space::initialize(bootinfo)` production initializer.
78. C99 builds call the production initializer before the boundary probe.
79. C99 builds optionally call the existing NativeAOT-facing VM/PAL probe afterward.
80. The C99 path initializes the IDT before deliberate protection-fault checks.
81. The C99 probe starts with accounting reconciliation.
82. It verifies capacity is map-derived.
83. It verifies capacity exceeds the historical 4096-frame boundary.
84. It verifies metadata sizing is compact.
85. It verifies usable descriptor enrollment.
86. It verifies representative reserved/MMIO exclusion.
87. It verifies frame zero remains a sentinel.
88. It verifies metadata pages are excluded from usable accounting.
89. It allocates every frame through packed index `0xFFF`.
90. It allocates a VM frame at packed index `0x1000`.
91. It allocates a page-table frame above the same boundary.
92. It maps and queries the high physical frame through a previously free virtual page.
93. It unmaps the temporary mapping.
94. It exercises bounded VM allocation rollback.
95. It checks owner-mismatch rejection.
96. It checks page-table and VM frame release.
97. It checks double-free rejection.
98. It checks final counter reconciliation with persistent page-table hierarchy state.
99. The C99 source is `kernel/core/native_physical_frame_scaling_qemu_test.cpp`.
100. The C99 public declaration is `kernel/core/include/kernel/native_physical_frame_scaling_qemu_test.h`.
101. The production allocator implementation is `kernel/core/address_space.cpp`.
102. The production allocator interface is `kernel/core/include/kernel/address_space.h`.
103. The handoff changes are in `guideXOSBootLoader/guidexOSBootInfo.h`.
104. The memory sizing and identity-map changes are in `guideXOSBootLoader/main.cpp`.
105. The page-table bootstrap bound and overflow guards are in `guideXOSBootLoader/paging.cpp`.
106. The kernel dispatch is in `kernel/core/main.cpp`.
107. The reproducible runner is `scripts/smoke-c011ec99-physical-frame-scaling-qemu.ps1`.
108. The runner defaults to a 256/512 MiB matrix.
109. A 128 MiB ordinary boot is insufficient for this image plus its 64 MiB ramdisk.
110. The runner therefore uses at least 256 MiB for ordinary validation.
111. The runner creates isolated per-run ESP directories.
112. The runner captures build logs, serial logs, QEMU debug logs, and summaries.
113. The runner stops only QEMU processes created by that invocation.
114. The runner requires both C99 and NativeAOT-facing VM/PAL terminal markers.
115. An incomplete VM/PAL run cannot classify the production fix as complete.
116. The runner restores the ordinary kernel artifact after the C99 matrix.
117. The C99 image remains retained under the evidence run root.
118. The full authoritative run was `run-20260909-142050`.
119. Its evidence root is `D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT\out\dotnet\c011ec99-production-physical-frame-scaling\run-20260909-142050`.
120. The 256 MiB run reported `trackedFrames=27583`.
121. The 256 MiB run reported `discoveredUsablePages=27583`.
122. The 256 MiB run reported `metadataPages=7`.
123. The 256 MiB run reported first post-boundary physical frame `0x116A000`.
124. The 256 MiB run reported C99 `ALL_PASS`.
125. The 256 MiB run reported NativeAOT-facing VM/PAL `ALL_PASS`.
126. The 512 MiB run reported `trackedFrames=92975`.
127. The 512 MiB run reported `discoveredUsablePages=92975`.
128. The 512 MiB run reported `metadataPages=23`.
129. The 512 MiB run reported first post-boundary physical frame `0x116A000`.
130. The 512 MiB run reported C99 `ALL_PASS`.
131. The 512 MiB run reported NativeAOT-facing VM/PAL `ALL_PASS`.
132. The tracked-frame change from 27583 to 92975 proves RAM-size-sensitive capacity.
133. The metadata change from 7 to 23 proves metadata scales with discovered capacity.
134. Both matrix values exceed the old 4096-frame limit.
135. Both matrix runs passed the high-frame page-table mapping proof.
136. Both matrix runs passed leak, TLB, rollback, and ownership checks.
137. The ordinary 256 MiB boot reached `[KERNEL] Entering main loop (waiting for input)...`.
138. The ordinary boot also logged firmware-map physical-frame initialization.
139. This validates the normal boot path without the C99 test flag.
140. The NativeAOT relevance proof is the existing PAL/VM adapter path against the shared allocator.
141. A direct ordinary managed NativeAOT application launch remains opt-in and is not wired by this repository milestone.
142. That limitation is recorded rather than overstated as a managed application launch.
143. No C69–C97 historical harness was reopened or rerun.
144. No B02 expansion was performed.
145. No remote fetch or push was performed.
146. The build toolchain was MinGW `mingw32-make` and Visual Studio MSBuild.
147. The normal kernel build completed successfully.
148. The combined C99 plus NativeAOT-facing VM/PAL kernel build completed successfully.
149. The UEFI bootloader Release x64 rebuild completed successfully.
150. `git diff --check` was run before final commit preparation.
151. The final local commit is intentionally not pushed.
152. The final classification is `PRODUCTION_CAPACITY_FIXED`.
153. The implementation fixes production physical-frame capacity rather than masking the old constant.
154. The implementation keeps the same allocator shared by ordinary and runtime-facing paths.
155. The evidence tree contains the requested architecture, firmware-map, metadata, boundary, NativeAOT, ordinary-boot, and classification categories.
156. The final handoff records the local commit identifier and post-commit worktree state.
