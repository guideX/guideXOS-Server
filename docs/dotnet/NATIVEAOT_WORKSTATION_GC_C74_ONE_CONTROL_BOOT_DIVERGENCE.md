# guideXOS Server .NET Support — C74 ONE-Control Boot-Divergence Provenance

This is the final C74 report. C74 was a boot/build/launch restoration milestone only; it did not run a SIX-vs-ONE determinant crossover and did not tune `survived_per_region`.

## Result

1. **Outcome:** Outcome A + Outcome B. The original C73 ONE failure first diverged in harness launch provenance: SIX and ONE reused monitor port `44800`, leaving the ONE serial file empty and causing an invalid watchdog interpretation. A second build/configuration defect was also found: C73's managed tail selector was not property-driven and the C73-native proof composition did not reproduce the accepted C72 ONE semantics. The minimal restoration was to isolate monitor ports, record chronology, make the tail selector property-driven, and run ONE through the accepted C72 managed/native proof path with the bounded C73 finish-time observer compiled alongside it.
2. **Success Level:** Level 3.
3. **Repository:** `D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT`.
4. **Branch:** `v1.1_DOTNET_SUPPORT`.
5. **Starting full HEAD:** `36333cb757e31a59188d604d588e7df2865a0b57`.
6. **Starting subject:** `Record C73 closeout SHA`.
7. **Final HEAD:** The local C74 closeout commit; exact SHA is recorded in the final handoff because a commit cannot contain its own SHA.
8. **Final subject:** `Restore NativeAOT promotion-positive ONE control`.
9. **Upstream:** `origin/v1.1_DOTNET_SUPPORT`.
10. **Starting ahead/behind:** `0/0`.
11. **Final ahead/behind:** `1/0` after the local C74 commit.
12. **Starting worktree:** Clean at the required C74 baseline.
13. **Final worktree:** Clean after the local C74 commit.
14. **Runtime identity:** NativeAOT `9.0.0`, AMD64, Workstation GC, GC interfaces `5.3 / 2`; runtime-pack manifest SHA-256 `E8924AAE541F6C77E00EF7504504FBA5451366B0B15FDB2B0B2332006E1E8C5E`.
15. **Runtime source SHA:** `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`.
16. **FP patch SHA:** `4185495724D48E2962BA9042AF352718BF9188032DEE4C9DE6FFE9F145A1DC31`.
17. **C71 SHA:** `96253f28ff89b020ca129c24544af3efe823a8c5`.
18. **C72 SHA:** `9884f4a72223314e047bdda6c32a10663ee8a615`.
19. **C73 final SHA:** `36333cb757e31a59188d604d588e7df2865a0b57`.
20. **C74 SHA:** The final local closeout commit SHA recorded at delivery.
21. **Exact C74 question:** What is the earliest real divergence between the boot/build/launch path of the reproducible C73 SIX control and the failing C73 ONE control, and what minimal non-GC correction is necessary to restore the authentic promotion-positive ONE boot?
22. **C73 SIX status:** Valid historical SIX/C69-C70 anchor reproduced under C73: retained `16`, object size `0x10018`, retained-live/promoted bytes `0x100180`, authentic promotion, post-Restart `6`, post-resume `6`.
23. **C73 ONE failure symptom:** Original C73 ONE run `run-20260903-222840310` had an empty `serial.log` (SHA-256 `E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855`), early watchdog exit, no valid C73 markers, and no valid GC interpretation.
24. **Last-known-good C72 ONE evidence:** `out/dotnet/c011ec72-promotion-threshold-region-formation/high/run-20260903-174105410`; HIGH, 15 retained, object size `0x10E80`, retained-live `0xFD980`, tail `320`, authentic promotion, post-Restart `1`, post-resume `0` in the historical C72 marker stream.
25. **C72 ONE managed mode:** `PromotionDecisionLiveByteThreshold`.
26. **C73 ONE managed mode:** Original C73 selected `PromotionPositiveRegionCohort`; the C74 restored ONE deliberately selects the accepted C72 mode `PromotionDecisionLiveByteThreshold`.
27. **Expected ONE object count:** `15`.
28. **Expected ONE object size:** `0x10E80` actual measured object size.
29. **Expected ONE retained-live total:** `0xFD980`.
30. **Managed configuration difference found:** C73's `PromotionPositiveRegionCohort` property group hard-coded `HOSTLOGPROOF_C011EC66_T216`, even when the runner requested tail `320`; C66 selector conditions also omitted the C73 mode. The project now activates selectors by `HostLogProofC66TailAllocations`, and the restored ONE uses the exact C72 mode and tail `320`.
31. **Publish command SIX:** Exact command is preserved in `accepted-restored-proof-boots/six/build-run-20260904-141506292/build-single-thread-suspend-ee-artifact.bat`: `"C:\Program Files\dotnet\dotnet.exe" publish "D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT\samples\managed\HostLogProof\HostLogProof.csproj" -c Release -r win-x64 --self-contained true -p:PublishAot=true -p:InvariantGlobalization=true -p:IlcGenerateStackTraceData=false -p:IlcUseEnvironmentalTools=true -p:HostLogProofMode=PromotionPositiveRegionCohort -p:HostLogProofC70RetainedSurvivors=16 -p:HostLogProofC73Case=baseline16 -p:HostLogProofC66TailAllocations=216`, with the per-run runtime-support, map, output, runtime-pack, probe-object, and SDK paths shown in that recorded command.
32. **Publish command ONE:** Exact command is preserved in `accepted-restored-proof-boots/one/build-run-20260904-141237780/build-single-thread-suspend-ee-artifact.bat`: `"C:\Program Files\dotnet\dotnet.exe" publish "D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT\samples\managed\HostLogProof\HostLogProof.csproj" -c Release -r win-x64 --self-contained true -p:PublishAot=true -p:InvariantGlobalization=true -p:IlcGenerateStackTraceData=false -p:IlcUseEnvironmentalTools=true -p:HostLogProofMode=PromotionDecisionLiveByteThreshold -p:HostLogProofC70RetainedSurvivors=15 -p:HostLogProofC71Case=15mid8 -p:HostLogProofC66TailAllocations=320`, with the per-run runtime-support, map, output, runtime-pack, probe-object, and SDK paths shown in that recorded command.
33. **PE SHA SIX:** `101B921D91FEAA1FCF28BE53FF9A1A5846573644F626686B76DF4D67E641F5F2`.
34. **PE SHA ONE:** `AA5AB49B5359F8162A61DF10F30922A640516BE3FA9648DC513009917DAADA4D`.
35. **ELF SHA SIX:** `48414D14DECCE2AFF3662A4F1208AC9B075CB35E5CAEC9E195967B30CC050C47`.
36. **ELF SHA ONE:** `A904BD9BC8CC70A05E9AB5F5CD35C4190CDDFBAC00DB7C4FACADD66F54461CE9`.
37. **Kernel SHA SIX:** `F9863CA9E4759CF310FC3BB1B99B9EC2027381663C4F72E98E3196F35E59FAFE`.
38. **Kernel SHA ONE:** `EBE8225C7EE65FF71D2EC060A23C9B95C46ECC67EC85F9FF2A7FA61E12042499`.
39. **ESP SHA SIX:** `F9863CA9E4759CF310FC3BB1B99B9EC2027381663C4F72E98E3196F35E59FAFE`.
40. **ESP SHA ONE:** `EBE8225C7EE65FF71D2EC060A23C9B95C46ECC67EC85F9FF2A7FA61E12042499`.
41. **Stale artifact detected:** No. Each case had a fresh per-case build root and the staged ESP kernel hash matched that case's linked kernel hash.
42. **Artifact collision detected:** No in the accepted run. The C73 diagnostic work also found and isolated the shared monitor/output provenance risks; accepted paths are case- and run-scoped.
43. **QEMU command SIX:** Recorded exactly in `accepted-restored-proof-boots/six/run-20260904-141506292/commands.txt` and `first-run/qemu-chronology.json`: `C:\Program Files\qemu\qemu-system-x86_64.exe -accel tcg,thread=single -machine pc -smp 1 -drive if=pflash,format=raw,readonly=on,file="C:\Program Files\qemu\share\edk2-x86_64-code.fd" -drive file=fat:rw:"...\six\run-20260904-141506292\first-run\esp",format=raw,if=ide,index=0 -m 1024M -vga std -display none -serial file:"...\six\first-run\serial.log" -monitor tcp:127.0.0.1:47900,server,nowait -no-reboot -no-shutdown -rtc base=utc,clock=host -d int,guest_errors -D "...\six\first-run\qemu-debug.log"`.
44. **QEMU command ONE:** Recorded exactly in `accepted-restored-proof-boots/one/run-20260904-141237780/commands.txt` and all three chronology files; same executable, machine, CPU, memory, drives, OVMF, serial, and debug arguments, with ports `47800`, `47801`, and `47802` and the corresponding ONE ESP/log paths.
45. **Firmware identity SIX/ONE:** `C:\Program Files\qemu\share\edk2-x86_64-code.fd`, SHA-256 `33090CC07675BAA5190D9F1E84BF5176B33BCBFA9BACAC522961150CDB6DBB2A`; QEMU executable SHA-256 `A930E028F93D0FA47E4D58BDAD2432F7466DC2B6AF0AE376F77EF7A298FFDD02`.
46. **Serial configuration SIX/ONE:** QEMU `-serial file:<case-run>\serial.log`; monitor `tcp:127.0.0.1:<isolated-port>,server,nowait`; display disabled; debug output `-d int,guest_errors -D <case-run>\qemu-debug.log`.
47. **QEMU PID acquired ONE:** Boot 1 `22516`; Boot 2 `22820`; Boot 3 `17936`.
48. **Serial file created ONE:** Yes, before first serial byte on all three boots; creation/open events are ordinals 3/4 in each chronology.
49. **First serial byte observed:** Yes, on all three ONE boots; chronology ordinals 5, with first-byte observations at `21:14:17.951271Z`, `21:14:30.6827897Z`, and `21:14:43.1544913Z`.
50. **QEMU exited before watchdog:** No, on all accepted ONE boots; `qemuExitedBeforeWatchdog=false`.
51. **QEMU exit code:** No pre-watchdog exit code exists; the recorded `-1` is the post-cleanup process result after the harness stopped QEMU.
52. **Watchdog fired:** Yes, as the normal bounded safe-stop condition after the proof completion marker; it did not fire for timeout or early QEMU exit.
53. **Watchdog reason:** `safe-stop-marker`; timeout was not reached.
54. **Earliest execution boundary reached:** Firmware, UEFI bootloader, ELF load, native kernel entry, serial initialization, first native proof marker, managed NativeAOT entry, target managed workload, GC promotion, RestartEE, managed resume, and C73 finish-time reconciliation were all reached on restored ONE and fresh SIX.
55. **Earliest execution boundary not reached:** None on accepted boots. In the original empty-log C73 ONE record, no boundary was proven; the empty file alone was not treated as evidence of non-execution.
56. **Earliest proven SIX/ONE divergence:** First, C73's original shared fixed monitor port `44800` caused a launch/harness provenance divergence and the empty-log symptom. With isolated ports, both cases produced serial and native markers; the next divergence was native proof/build composition: C73-native ONE stalled after the retained cohort, while accepted C72 native instrumentation plus the C73 finish-time observer completed with C72 ONE semantics.
57. **Root cause:** Shared QEMU monitor-port reuse/misclassification caused the original empty serial result; a latent C73 managed selector defect and non-equivalent C73-native proof composition prevented the exact C72 ONE semantics from being reproduced under the original C73 path.
58. **Root-cause category:** Outcome A + Outcome B: harness/orchestration and build/proof-configuration defects; no production GC semantic defect was required for restoration.
59. **Files changed to repair:** `scripts/smoke-nativeaot-gc-single-thread-suspend-ee-qemu.ps1`, `samples/managed/HostLogProof/HostLogProof.csproj`, and new `scripts/dotnet/Invoke-C011EC74OneControlBootDivergence.ps1`; this report is the documentation change.
60. **Production runtime behavior changed:** No.
61. **GC behavior changed:** No. No planner, `survived_per_region`, promotion, budget, generation, region, free-list, candidate, allocator, OOS, root, or survivor behavior was modified.
62. **Restored ONE booted:** Yes; three fresh boots completed.
63. **Restored ONE retained count:** `15`.
64. **Restored ONE object size:** `0x10E80`.
65. **Restored ONE retained-live bytes:** `0xFD980`.
66. **Restored ONE authentic promotion:** Yes; `C70_PROMOTION_OBSERVED`, C73 promotion, nonzero debit, RestartEE, managed resume, and survivor integrity all passed.
67. **Restored ONE promoted objects:** `15`.
68. **Restored ONE promoted bytes:** `0xFD980`.
69. **Restored ONE post-Restart basic count:** `1`.
70. **Restored ONE post-resume basic count:** `1`.
71. **Restored ONE `survived_per_region`:** `0xFD980`, recorded by `C73_FIRST_PRODUCTION_STATE` with `quantity=survived_per_region`, source `gc_heap::add_to_promoted_bytes`, next `gc_heap::decide_on_promotion_surv`.
72. **Fresh SIX `survived_per_region`:** `0x100180`, recorded by the same source-level marker and instrumentation.
73. **Fresh SIX post-Restart basic count:** `6`.
74. **ONE/SIX pair restored:** Yes. ONE is the promotion-positive 15-region-class control; SIX is the promotion-positive 16-region-class control.
75. **`survived_per_region` causal interpretation attempted:** No. Values were preserved and recorded only; C75 owns causal discrimination.
76. **B02 evaluated:** No; premature and explicitly out of C74 scope.
77. **B02 future status:** Remains premature until C75 first determines whether `survived_per_region` is an input, consequence, or correlated diagnostic and then establishes a valid causal chain.
78. **C18:** PASS; authentic managed PC path retained.
79. **Code manager:** PASS; valid `CoffNativeCodeManager`.
80. **`FindMethodInfo`:** PASS.
81. **Root scan:** PASS; authentic root scan retained.
82. **Mark closure:** PASS.
83. **Planner authenticity:** PASS; planner behavior remained unmodified; direct planner observation remained diagnostic-only.
84. **Survivor integrity:** PASS on restored ONE and fresh SIX.
85. **C74 invariant failures:** `0`.
86. **Sensitive diagnostic allocations:** `0`.
87. **C74 diagnostic overflow:** `0`.
88. **Inherited C65 overflow:** `0` in each accepted C73 completion marker, tracked independently from C74-owned overflow/invariant counters.
89. **Fail-fast:** `0`.
90. **Page faults:** `0`.
91. **Restored ONE Boot 1:** PASS; `first-run`, C73 completion, promotion-positive, retained `15`, size `0x10E80`, live/promoted `0xFD980`, post-Restart/resume `1/1`.
92. **Restored ONE Boot 2:** PASS; `repeat-1`, same semantic fields.
93. **Restored ONE Boot 3:** PASS; `repeat-2`, same semantic fields.
94. **ONE semantic agreement:** PASS across all three fresh boots; raw serial hashes and addresses varied, semantic fields did not.
95. **SIX sanity boot:** PASS; fresh `first-run` completed C73 with retained `16`, size `0x10018`, live/promoted `0x100180`, post-Restart/resume `6/6`.
96. **SIX semantic agreement with C69/C70:** PASS; the fresh SIX result reproduces the accepted C69/C70 SIX class.
97. **Nondeterminism:** No semantic nondeterminism observed. Raw serial hashes, virtual addresses, process IDs, and event ordinals are not treated as semantic identity.
98. **Serial hashes:** ONE Boot 1 `FE483B9AE9B5870BA25AF94E289980873E53C7D15D010AE89844E545890DC08B`; Boot 2 `3D742404BD387CE476321DD6A99501BB0D48B0E3BAC19E864822E4B02325443D`; Boot 3 `A7193DF8BA85C03E2CD370582CBC77D5FC7ADE527D442B76B5820BD45AA560B6`; SIX `A615482ED50329F396FC4A6E7D3A8206F76F631515A6F9922F5D9EDE10C463F7`.
99. **Artifact hashes:** Complete managed PE, linked PE, ELF, map, kernel, and ESP paths/hashes are in `c74-final-manifest.json`; linked/artifact values are also listed in fields 33–40 above. Managed published PE: ONE `9088C86727082E338DCA1BB21CB5B42CFB74470C3B94C0A96886895C0731310C`; SIX `C4D24F7BCB4AC6D70B42A9B514288A6AFED39D8DD306BFD42299C7DFEEE8FA14`.
100. **Runtime-pack validation:** PASS; runtime-pack manifest identity and per-case runtime-pack build outputs recorded.
101. **Managed build:** PASS for exact restored ONE and fresh SIX.
102. **Native build:** PASS for exact restored ONE and fresh SIX.
103. **PowerShell syntax:** PASS for the modified smoke harness and C74 runner.
104. **JSON/XML parse:** PASS for runtime manifest, final C74 manifest, chronology/watchdog JSON, and `HostLogProof.csproj` XML.
105. **`git diff --check`:** PASS.
106. **PE -> ELF conversion:** PASS using `pe_to_elf_v2_fixed_base.py`; ELF hashes are recorded above.
107. **Symbol checks:** PASS; `ManagedMain` conversion/symbol checks and native symbol audits completed.
108. **Linker/source/table/archive guards:** PASS through the existing C011EC18 linker/source/table/archive guard chain.
109. **C52 Tier-All result or reason omitted:** Omitted because C74 is boot restoration only; running C52 Tier-All would add unrelated workload and interpretation scope.
110. **Ordinary restoration:** PASS; ordinary kernel and ESP were restored after proof work and proof artifact is inactive.
111. **Ordinary kernel SHA:** `75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6`.
112. **Ordinary ESP SHA:** `75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6`.
113. **Proof artifact active:** No; ordinary kernel/ESP are active at closeout.
114. **C74-owned QEMU cleanup:** PASS; C74-owned QEMU processes were cleaned in smoke `finally` blocks after each safe stop.
115. **Unrelated QEMU preservation:** No unrelated QEMU instances were present during the accepted final matrix; earlier diagnostic checks preserved an unrelated instance by command-line identity. No unrelated process was terminated.
116. **Files changed:** `samples/managed/HostLogProof/HostLogProof.csproj`; `scripts/smoke-nativeaot-gc-single-thread-suspend-ee-qemu.ps1`; `scripts/dotnet/Invoke-C011EC74OneControlBootDivergence.ps1`; `docs/dotnet/NATIVEAOT_WORKSTATION_GC_C74_ONE_CONTROL_BOOT_DIVERGENCE.md`.
117. **Documentation path:** `docs/dotnet/NATIVEAOT_WORKSTATION_GC_C74_ONE_CONTROL_BOOT_DIVERGENCE.md`.
118. **Evidence root:** `out/dotnet/c011ec74-one-control-boot-divergence/final-run-20260904-1415/`; accepted boots are under `accepted-restored-proof-boots/`; failed and diagnostic attempts are separated under sibling directories.
119. **Final commit:** Local commit with subject `Restore NativeAOT promotion-positive ONE control`; exact SHA is recorded in the final handoff after commit.
120. **Push status:** Not pushed.
121. **Remaining limitation:** C74 restored the controlled pair but did not determine the SIX-vs-ONE causal determinant. The C73-native composition itself remains a non-equivalent diagnostic path; the accepted ONE path intentionally uses proven C72 native instrumentation plus the bounded C73 observer.
122. **Exact next-smallest milestone:** C75: using only the restored pair, add or reuse one bounded source-authentic observation at the first downstream decision after `survived_per_region` and determine whether that quantity is an input, computed consequence, or correlated diagnostic before any mutation or crossover.

## Accepted manifest

The machine-readable accepted manifest is `out/dotnet/c011ec74-one-control-boot-divergence/final-run-20260904-1415/c74-final-manifest.json`. Its final-run QEMU chronology files preserve launch request, PID acquisition, serial creation/open, first byte, safe-stop, cleanup, and post-cleanup process exit events.

No C75 determinant experiment was performed in C74.
