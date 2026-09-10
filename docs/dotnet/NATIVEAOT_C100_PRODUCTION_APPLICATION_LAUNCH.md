# C100 — Production NativeAOT Application Launch Design

## Decision

C100 is recorded as **Outcome G: production NativeAOT launcher design established; implementation deferred**.

This is an intentional non-claim. The ordinary UEFI kernel has a usable C99 physical-frame and page-mapping foundation, but it does not yet have the production seams required to discover an executable from the ordinary filesystem, create an isolated application address space, map an image with authentic permissions and ownership, initialize a general NativeAOT runtime instance, enter managed code, and tear the instance down safely. Connecting the proof-only QEMU harness to the ordinary shell would make the result look like production launch while violating the C100 acceptance rules.

No C100 application was staged into the ordinary ESP, no C100 launcher command was added, and no C100 production boot/app-launch acceptance is claimed. The C69–C99 investigation and repair history remains intact. C98 identified the fixed physical-frame ceiling; C99 repaired it with firmware-derived frame capacity. The next milestone is a real production executable-image and process/address-space seam, followed by a shared NativeAOT startup service.

## Repository and runtime identity

- Repository: D:/dev/guideXOSServerV1.1_DOTNET_SUPPORT
- Branch at start: v1.1_DOTNET_SUPPORT
- Starting HEAD: 7a250e65ef8805d745508173ddb48541eef9b4af
- Starting HEAD subject: Implement C99 firmware-derived physical frame scaling
- Starting upstream: origin/v1.1_DOTNET_SUPPORT
- Starting relation: ahead 1, behind 0
- Locked runtime-pack identity: amd64, net9.0, win-x64, Microsoft.DotNet.ILCompiler 9.0.0, Workstation GC, runtime-pack source commit 9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3
- Locked SDK identity: 10.0.301, commit 96856fd726
- Live host observed during audit: .NET SDK 10.0.401, host runtime 10.0.12, Windows 10.0.26200, x64

The starting worktree was clean. The ordinary baseline artifacts were captured before this design record:

- ESP/kernel.elf — 2,174,392 bytes — SHA-256 75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6
- ESP/EFI/BOOT/BOOTX64.EFI — 51,200 bytes — SHA-256 1158801E02A9125C3137B8A4D549DB6D34FF8E08A49E328B422EE22117E46984
- ESP/ramdisk.img — 67,108,864 bytes — SHA-256 7E93E34732526DBFFEE097912F776467B0DD03C989CBDA06F4AA24B0945A969B

## Architecture audit

### Ordinary kernel path

The ordinary UEFI path enters kernel/core/main.cpp, initializes the kernel process subsystem, validates BootInfo, and initializes kernel::memory::address_space. It then follows the desktop, interrupt, storage, VFS, networking, and input path. Storage initialization creates devices and mounts filesystems, but it does not scan for executable applications or create an application process image.

The existing ordinary application model is kernel/core/kernel_app.cpp and kernel/core/include/kernel/kernel_app.h. AppManager owns statically registered built-in KernelApp factories. It constructs a built-in object, calls its virtual init method, and later calls shutdown and deletes the object. This is a GUI object lifecycle, not an executable image loader.

kernel/core/shell.cpp exposes desktop.launch, nativeapp.inspect, and nativeapp.smoketest diagnostics. Its own user-facing text states that the NativeElf samples are hosted/server-side and are not executed by bare-metal builds. There is no ordinary run command that turns a VFS path into an executable process.

### Hosted NativeElf path

The hosted/server path has app_registry, app_manifest, app_launch_resolver, native_elf_launch_pipeline, native_elf_image_loader, native_elf_executor, native_app_runtime, and desktop_service. It can resolve a NativeElf manifest, read a host file, validate ELF, load static ET_EXEC PT_LOAD segments, allocate host executable memory, protect host pages, and invoke gx_entry_fn through a Windows x64 trampoline.

That path is not the ordinary UEFI kernel path. It uses host filesystem APIs, host executable memory, a host process table, and an experimental execution gate. Its loader rejects ET_DYN/PIE because relocations are not implemented. It is useful prior art, but it cannot be promoted into C100 by adding a shell alias.

### Proof-only NativeAOT path

kernel/core/nativeaot_pal_qemu_test.cpp is compiled and used only under NativeAOT/QEMU proof macros. Its loadArtifact helper maps an embedded ELF artifact into the current kernel address space, zeroes pages, copies PT_LOAD bytes, and later unloads the mapped artifact. runStartupImpl invokes generated fixed-address startup symbols, and runFirstRealAllocationImpl invokes a generated managed entry after installing proof hooks and TLS.

The proof path depends on generated fixed addresses, embedded generated artifacts, proof diagnostics, and test-only ownership. It is not a production application launcher. The runtime-pack README also describes the current PAL, VM, GC startup, and GC environment adapters as proof or readiness infrastructure rather than a general runtime.

### C99 foundation

kernel/core/address_space.cpp and kernel/core/include/kernel/address_space.h now provide a production-scalable physical-frame and page-mapping foundation. The allocator derives usable capacity from the firmware memory map, tracks one byte per frame, supports VmRegion ownership, maps and unmaps pages, updates page flags, and reports accounting. The accepted C99 matrix included 27,583 tracked frames with 7 metadata pages at 256 MiB and 92,975 tracked frames with 23 metadata pages at 512 MiB.

The foundation currently represents the kernel address space. It does not yet provide application address-space creation, a user/kernel execution boundary, process-image ownership, executable discovery, or general NativeAOT startup and teardown.

## Smallest production architecture still required

- A VFS-backed executable artifact abstraction that can discover a declared application, read bounded bytes, validate architecture and ABI, authenticate the artifact, and report a stable entry contract.
- A freestanding NativeAotElfLoader that parses ELF headers and PT_LOAD segments in the kernel, derives the entry point from the artifact, allocates VmRegion frames through the C99 allocator, zero-fills BSS, copies file bytes, applies authentic R/W/X flags, rejects unsupported relocations, and records every owned mapping.
- An application address-space and process-image abstraction. The current AddressSpace singleton must be extended or wrapped so an application has its own page-table root, image mappings, stack/TLS mappings, teardown ownership, and transition back to the kernel.
- A production NativeAOT launch context that factors reusable PAL/VM/thread/TLS plumbing from the proof harness but does not depend on qemu_test symbols, fixed generated addresses, diagnostic-only entrypoints, or embedded proof artifacts.
- A runtime module/startup contract that initializes the generated NativeAOT module, code manager, method metadata lookup, static constructors, root scanning, GC startup, and managed entrypoint with explicit lifetime and failure semantics.
- A scheduler/thread and termination contract that records an application thread, handles managed return and fail-fast, releases image and stack resources, and leaves the ordinary kernel loop alive.
- A shell or desktop launch command whose errors distinguish missing artifact, malformed artifact, unsupported relocation, insufficient frames, startup failure, managed exception/fail-fast, and clean return.

The first implementation milestone should stop after artifact validation and image mapping. It should include negative tests and accounting deltas before NativeAOT startup is attempted. The second milestone should introduce the shared production startup service. The proof harness should remain a separate regression target throughout.

## Candidate artifact status

samples/managed/HostLogProof is a NativeAOT candidate and targets net9.0, win-x64, x64, with PublishAot and SelfContained enabled. It is not a C100 ordinary application: it contains extensive C69+ forensic modes, diagnostic imports, proof markers, and generated fixed-address integration assumptions.

An existing prior proof artifact was observed in ignored output only:

- out/dotnet/managed-hostlog/artifacts/HostLogProof.exe — 669,696 bytes — SHA-256 D5BD1C56D79877714683C5A27B8037F6CE29978D913F8DA259F4B387DD798FBA
- out/dotnet/managed-hostlog/artifacts/HostLogProof.elf — 688,128 bytes — SHA-256 B77482E9A02FB95C9344FD23BFB46C8FC3446FC90E33E74F5268A60AC75EA7A7
- Observed ELF: 64-bit little-endian AMD64 ET_EXEC, entry 0x10001a10, seven PT_LOAD segments, no dynamic section, no relocations, approximate virtual range 0x10000000 through 0x100dd600

Those artifacts were not staged into ESP, were not mounted as an ordinary app, and are not C100 acceptance evidence. No new C100 sample, manifest, or binary was created because the production loader contract is not yet present.

## Validation boundary

This was a design-only C100 outcome. No QEMU acceptance boot was run, no proof-only NativeAOT entry was relabeled as production, and no B02 claim was made. Read-only source, runtime, artifact, and configuration checks are captured under out/dotnet/c011ec100-production-nativeaot-application-launch. The ordinary baseline files remain unchanged.

The report below deliberately records deferred and not-applicable values instead of inventing managed execution evidence.

## Numbered report

1. C100 decision: Outcome G.
2. Design status: production launcher design established.
3. Acceptance status: implementation deferred.
4. Claimed success level: Level 0 only; no Level 1 or higher claim.
5. Repository path: D:/dev/guideXOSServerV1.1_DOTNET_SUPPORT.
6. Starting branch: v1.1_DOTNET_SUPPORT.
7. Starting HEAD: 7a250e65ef8805d745508173ddb48541eef9b4af.
8. Starting subject: Implement C99 firmware-derived physical frame scaling.
9. Starting upstream: origin/v1.1_DOTNET_SUPPORT.
10. Starting relation: ahead 1, behind 0.
11. Final relation: recorded after the local C100 documentation commit.
12. Starting worktree: clean.
13. Final worktree: required to be clean.
14. Locked architecture: amd64.
15. Locked target framework: net9.0.
16. Locked runtime identifier: win-x64.
17. Locked ILCompiler: Microsoft.DotNet.ILCompiler 9.0.0.
18. Locked runtime-pack commit: 9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3.
19. Locked SDK version: 10.0.301.
20. Locked SDK commit: 96856fd726.
21. Live host SDK: 10.0.401.
22. Live host runtime: 10.0.12.
23. C98 full SHA: 2c8dc6b052c2129051f6bfb01d21470d2f86dad5.
24. C99 full SHA: 7a250e65ef8805d745508173ddb48541eef9b4af.
25. C69-C97 status: closed per repository history.
26. C98 finding: fixed 16 MiB physical-frame ceiling.
27. C99 repair: firmware-derived physical-frame capacity.
28. C99 allocator ownership: VmRegion metadata is available.
29. C99 map ownership: page mappings can be tracked.
30. C99 accepted 256 MiB result: 27,583 tracked frames and 7 metadata pages.
31. C99 accepted 512 MiB result: 92,975 tracked frames and 23 metadata pages.
32. B02 status: still premature and not claimed.
33. C100 question: whether an ordinary production NativeAOT application can be launched.
34. Ordinary app registry: AppManager.
35. Ordinary app source: kernel/core/kernel_app.cpp.
36. Ordinary app lifecycle: factory, init, running list, shutdown, delete.
37. Ordinary app image loading: absent.
38. Ordinary ELF parser: absent.
39. Ordinary PE parser: absent.
40. Ordinary application discovery: absent.
41. Ordinary application address-space creation: absent.
42. Ordinary managed runtime startup: absent.
43. Ordinary managed runtime teardown: absent.
44. Existing shell launch: desktop.launch.
45. Existing shell inspection: nativeapp.inspect.
46. Existing shell smoke command: nativeapp.smoketest.
47. Ordinary run command: absent.
48. Bare-metal NativeElf behavior: explicitly disabled/unavailable.
49. Hosted NativeElf resolver: present.
50. Hosted NativeElf loader: present.
51. Hosted NativeElf executor: present.
52. Hosted NativeElf execution gate: experimental.
53. Hosted NativeElf filesystem: host filesystem.
54. Hosted NativeElf memory: host executable memory.
55. Hosted NativeElf process model: host ProcessTable.
56. Hosted relocations: ET_DYN/PIE rejected.
57. Proof loader: nativeaot_pal_qemu_test::loadArtifact.
58. Proof artifact source: embedded/generated artifact.
59. Proof entry source: generated fixed addresses.
60. Proof PAL source: nativeaot_pal_qemu_test.
61. Proof runtime source: runtime-pack adapters.
62. Proof diagnostic source: C011EC and NativeAOT test callbacks.
63. Proof production status: not a production launcher.
64. Shared C99 source: address_space.
65. Shared PMM status: production ordinary-kernel foundation.
66. Shared VM status: production ordinary-kernel mapping foundation.
67. Shared NativeAOT launch service: absent.
68. Application image owner: absent.
69. Application page-table owner: absent.
70. Application stack owner: absent.
71. Application TLS owner: absent.
72. Artifact authentication contract: absent.
73. Artifact ABI contract: absent.
74. Artifact VFS contract: absent.
75. NativeAotElfLoader: required, not implemented.
76. PT_LOAD mapping: proof-only today.
77. BSS zero-fill: proof-only today.
78. Segment permissions: proof/hosted-specific today.
79. Relocation policy: hosted rejection only; production policy absent.
80. Entry-point policy: proof fixed symbols; production artifact-derived policy absent.
81. Code-manager registration: production path absent.
82. Method metadata lookup: production path absent.
83. Static-constructor policy: production path absent.
84. Root-scanning policy: production path absent.
85. GC startup policy: production path absent.
86. PAL installation policy: production path absent.
87. VM callback policy: production path absent.
88. Thread attach policy: production path absent.
89. Managed exception policy: production path absent.
90. Fail-fast policy: production path absent.
91. Managed return policy: production path absent.
92. Image teardown policy: production path absent.
93. Stack teardown policy: production path absent.
94. TLS teardown policy: production path absent.
95. Page-table teardown policy: production path absent.
96. Ordinary kernel resume policy: production path absent.
97. Candidate project: samples/managed/HostLogProof.
98. Candidate target: net9.0.
99. Candidate RID: win-x64.
100. Candidate architecture: x64.
101. Candidate NativeAOT: enabled.
102. Candidate role: proof candidate, not C100 ordinary app.
103. Candidate PE observed: yes, ignored output only.
104. Candidate PE size: 669,696 bytes.
105. Candidate PE SHA-256: D5BD1C56D79877714683C5A27B8037F6CE29978D913F8DA259F4B387DD798FBA.
106. Candidate ELF observed: yes, ignored output only.
107. Candidate ELF size: 688,128 bytes.
108. Candidate ELF SHA-256: B77482E9A02FB95C9344FD23BFB46C8FC3446FC90E33E74F5268A60AC75EA7A7.
109. Candidate ELF class: ELF64.
110. Candidate ELF machine: AMD64.
111. Candidate ELF type: ET_EXEC.
112. Candidate ELF entry: 0x10001a10.
113. Candidate ELF load segments: seven PT_LOAD entries.
114. Candidate ELF relocations: none observed.
115. Candidate ELF dynamic section: none observed.
116. Candidate ELF virtual range: approximately 0x10000000-0x100dd600.
117. Candidate ordinary staging: none.
118. Candidate ordinary manifest: none.
119. Candidate ordinary VFS location: none.
120. Candidate ordinary launch: not attempted.
121. Ordinary artifact discovery: not exercised.
122. Ordinary artifact validation: not exercised.
123. Ordinary image mapping: not exercised.
124. Ordinary entry derivation: not exercised.
125. Ordinary managed entry: not reached.
126. Managed marker: not emitted.
127. Managed allocation: not attempted.
128. Managed calculation: not attempted.
129. Managed PASS marker: not emitted.
130. Managed return: not observed.
131. Process exit: not observed.
132. Kernel survival after app return: not tested for C100.
133. Second launch: not attempted.
134. Ordinary frame count before image: not applicable.
135. Ordinary frame count after image: not applicable.
136. Ordinary VmRegion delta: not applicable.
137. Ordinary page-table delta: not applicable.
138. Ordinary teardown frame count: not applicable.
139. Ordinary residual allocation: not applicable.
140. C99 high-frame use in C100: not exercised.
141. PMM overflow behavior in C100: not exercised.
142. VM fault behavior in C100: not exercised.
143. NativeAOT startup behavior in C100: not exercised.
144. NativeAOT GC behavior in C100: not exercised.
145. NativeAOT code-manager behavior in C100: not exercised.
146. NativeAOT root-scan behavior in C100: not exercised.
147. NativeAOT method lookup behavior in C100: not exercised.
148. NativeAOT static-constructor behavior in C100: not exercised.
149. NativeAOT teardown behavior in C100: not exercised.
150. Negative malformed-image test: deferred with loader.
151. Negative unsupported-relocation test: deferred with loader.
152. Negative insufficient-frame test: deferred with loader.
153. Local C100 commit: recorded after commit.
154. Push status: not pushed.
155. Exact blocker: no ordinary executable-image, process/address-space, or NativeAOT production-startup seam.
156. Next smallest milestone: implement a VFS-backed, freestanding ELF artifact reader/mapper using C99 PMM/VM ownership, add negative tests and accounting, then factor a production NativeAOT startup service without calling the proof harness.

## Evidence index

The companion evidence root is:

out/dotnet/c011ec100-production-nativeaot-application-launch

It is organized into source-architecture-audit, application-artifact, ordinary-loader, production-pal-startup, pmm-frame-accounting, managed-execution, lifecycle, negative-test, validation, and final-classification. The evidence is intentionally explicit about which values are observed historical facts, which are design requirements, and which are deferred because no production C100 launch was implemented.
