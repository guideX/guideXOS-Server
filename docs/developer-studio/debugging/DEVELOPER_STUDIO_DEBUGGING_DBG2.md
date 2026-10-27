# Developer Studio Debugging — DBG2

**Date:** 2026-10-26 (Pacific)
**Outcome:** DBG2_POSTFIX_GUEST_VERIFIED_PHASE30B_PENDING (Outcome B)
**Track:** Developer Studio — Debugging

## Result

The committed DBG1 correction was rebuilt with the documented IDE production script, and the AMD64 ELF matched the DBG1 reference byte for byte. The rebuilt package was staged into a fresh production guest. One QAC-managed Phase28O boot reached the debugger, paused at a source breakpoint, continued, refreshed call-stack and variable views, stopped cleanly, and passed the Phase28O acceptance marker.

This is one focused Phase28O guest result. It does not satisfy Phase30B's separate five-boot acceptance gate. Formal Phase30B credit remains 0/5 because the required shared QEMU ownership contract is not in place across relevant launchers. Phase30B and Phase30C markers were not advanced.

## Repository state and isolation

| Repository | Starting branch / HEAD | Ending state |
|---|---|---|
| Server | D:/dev/guideXOS_DeveloperStudio_Debugging; DEVELOPER_STUDIO_DEBUGGING; 97f185b67d8f735a5e959c637456d2b5cde989f4 | Same branch. The debugger runtime test correction and this report are in the local DBG2 closeout commit; its exact HEAD is recorded in the completion response. |
| IDE source | D:/dev/guideXOS_Developer_Studio_Debugging; DEVELOPER_STUDIO_DEBUGGING; d09e8e1b8ed40fd7a1277ebe835acf56344241d1 | Same branch and HEAD; clean and unchanged. |

The Server checkout was clean at the requested starting HEAD. The IDE debugging checkout was clean at its expected HEAD. I did not switch either branch, alter the standalone IDE main checkout, or modify the Compilation or Tools tracks.

The production build used a separate detached Server worktree at 97f185b67d8f735a5e959c637456d2b5cde989f4:

    C:/Users/guideX/AppData/Local/Temp/guidexos-dbg2-postfix-20261026/server-staging

Only that staging worktree received the rebuilt AMD64 package. Its tracked package ELF remains modified there as the preserved build artifact; it was not included in the Server source commit.

## Production package provenance

The documented production process was run from the IDE checkout root:

    powershell.exe -NoProfile -ExecutionPolicy Bypass -File build.ps1 -ServerRoot C:/Users/guideX/AppData/Local/Temp/guidexos-dbg2-postfix-20261026/server-staging

An initial invocation from the Server directory failed before compilation because the script uses IDE-root-relative source paths. The rerun from the documented IDE root completed successfully. No production artifact in the primary Server checkout was replaced.

The build used IDE commit d09e8e1b8ed40fd7a1277ebe835acf56344241d1 and Server SDK headers from commit 97f185b67d8f735a5e959c637456d2b5cde989f4. Tool versions were Clang and LLD 22.1.8, MinGW g++ 15.2.0, and GNU readelf 2.46.0.20260210.

| Package artifact | Size | SHA-256 | Result |
|---|---:|---|---|
| AMD64 developerstudio.elf | 1,156,820 bytes | 97F6B011507C4A1F85C48C5A270355F3EFE4200E9BCF78AFF9DF9A6218CCD4C2 | Exact match to the DBG1 reference. ELF64 little-endian AMD64 ET_EXEC, entry 0x500c6890, no section table or debug sections. |
| app.json | 2,217 bytes | 47A9C855AE32A372CC7674350A9E7255BB58927FF943798CC929F6EDFCBCF1BE | Canonical manifest, id com.guidexos.developerstudio. |
| ARM64 developerstudio.elf | 1,286,380 bytes | AB0F3BDF55F8660F6542392DE4D9740FC1ADF91327892FFAC5B00847967A804F | Existing Server payload, not rebuilt or used by this AMD64 guest. |

The documented package validator passed the exact three-file package audit. The QEMU guest used a directory-backed FAT ESP, not an ISO. The staged ESP copies were independently hashed: its app.json was 47A9C855AE32A372CC7674350A9E7255BB58927FF943798CC929F6EDFCBCF1BE and its AMD64 developerstudio.elf was 97F6B011507C4A1F85C48C5A270355F3EFE4200E9BCF78AFF9DF9A6218CCD4C2. This verifies that the guest received the rebuilt package and manifest.

## QAC admission and Phase28O guest result

During the earlier host-test preflight, QAC reported one foreign QEMU, PID 23668, whose command line identified a PacManHostedSoak workload. No guest was launched while it was present. The process later exited without intervention. Immediately before the focused run, QAC was online with zero QEMU processes, zero foreign processes, zero managed sessions, and no queued or blocked reservations.

The documented Server runner was invoked once with Phase28OOnly, QacDeveloperStudioAcceptance, BootCount 1, a 600-second guest limit, and a 30-second admission bound. It ran from the isolated Server staging worktree. QAC granted reservation 8aef7ccd70d14d78bf718dd986a37772 to CLI owner LEON2_RDT\guideX (PID 23872), then launched its direct QEMU child PID 28036.

QAC briefly marked the reservation degraded because one process census was incomplete. No foreign QEMU was reported. The condition cleared before release: final inspection showed Released, ownerLiveness Verified, and degraded=false. The runner recorded QEMU exit code 0, released=1, recovery_required=0, and managed_success=1. QAC finished with zero QEMU processes, zero foreign processes, and zero managed sessions.

The QEMU run was 2026-10-26 18:47:00–18:47:55 Pacific. The guest serial log reports build_source_files=2. The Phase28O project metadata has sourceRoot src and no explicit source list; its two checked-in source files are src/main.cpp and src/helper.cpp. The following Debug Start events identify the successful built artifact as build/bin/amd64/p28o.elf, SHA-256 1cc07a1372108b07c79bdd171953e8733631f6824c635619a6b18cf3f5af544e:

- DEBUG_START_SERVER_ADMITTED
- DEBUG_START_EXECUTION_OWNER_CREATED
- DEBUG_START_FIRST_EXECUTION_DISPATCH_RETURNED
- DEBUG_START_API_RETURNED_GX_OK
- DEVELOPER_STUDIO_PHASE28U_DEBUG_START_READY_PASS
- debug_transition=Launching->Running

The guest then emitted the real breakpoint and controller evidence:

- debug_state=PAUSED_BREAKPOINT and debug_transition=Running->Paused
- DEVELOPER_STUDIO_PHASE28O_BREAKPOINT_HIT_PASS
- debug_call_stack=PASS, DEVELOPER_STUDIO_PHASE28M_CALL_STACK_DEPTH_PASS depth=3, and debug_stack_navigation=PASS
- debug_variables=PASS and DEVELOPER_STUDIO_PHASE28O_WATCH_REEVALUATE_PASS
- debug_transition=Paused->Running, followed by a second PAUSED_BREAKPOINT state and DEVELOPER_STUDIO_PHASE28O_LOG_OUTPUT_PASS
- DEVELOPER_STUDIO_PHASE28O_RESET_PASS, DEVELOPER_STUDIO_PHASE28O_TERMINAL_PASS, and DEVELOPER_STUDIO_PHASE28O_SECOND_GENERATION_PASS
- debugger_teardown=PASS, debug_target_teardown=PASS, debug_session_teardown=PASS, and DEVELOPER_STUDIO_PHASE28O_CLEANUP_PASS
- DEVELOPER_STUDIO_PHASE28O_PASS

These are emitted after the in-guest controller observes the corresponding service state, breakpoint pause, stack/variable model, and teardown. The source-discovered project contains two source files, and Debug Start reached Ready, Running, and a real breakpoint pause; the earlier no_runnable_target rejection did not recur. The current guest trace records the source count and artifact identity, but does not serialize the full build-request object or individual source paths.

| Guest capability | Result |
|---|---|
| Source breakpoint, hit, and paused state | Passed in the real Phase28O controller path. |
| Continue | Passed: Paused→Running→Paused transitions and log output were observed. |
| Call stack | Passed: depth 3 and mapped stack-source navigation were observed. |
| Variables and Watches | Variable model and Watch reevaluation passed; no specific variable name/value was captured in the serial evidence. |
| Step Into, Step Over, and Step Out | Not exercised by the focused Phase28O workflow; it skips the older Phase28M step-navigation gate. Hosted native runtime tests are not counted as guest proof. |
| Stop, cleanup, and second debugger generation | Passed. |

The runner used -display none, so it produced no guest screenshot. Serial and host-trace evidence were preserved.

Evidence paths:

- Runner output: C:/Users/guideX/AppData/Local/Temp/guidexos-dbg2-postfix-20261026/phase28o-qac-runner.log
- Build output: C:/Users/guideX/AppData/Local/Temp/guidexos-dbg2-postfix-20261026/ide-production-build.log
- Package audit: C:/Users/guideX/AppData/Local/Temp/guidexos-dbg2-postfix-20261026/package-validation.log
- QAC runner trace: C:/Users/guideX/AppData/Local/Temp/guidexos-phase28g-6c6ad3f73d664422bbe4b0948feab702/boot1.host-trace.log
- Guest serial: C:/Users/guideX/AppData/Local/Temp/guidexos-phase28g-6c6ad3f73d664422bbe4b0948feab702/boot1.serial.log
- Staged ESP: C:/Users/guideX/AppData/Local/Temp/guidexos-phase28g-6c6ad3f73d664422bbe4b0948feab702/esp-boot1-82bc925a727842efbbce4d96013c8c6d

## Continue assertion investigation

DBG1 recorded one initial native runtime Continue assertion failure, followed by an immediate passing rerun and a passing rerun after restoring the original Server test source. It did not reproduce in three additional pre-correction runs or three post-correction runs.

Source review found a timing-sensitive test predicate in tests/native_debugger_runtime_test.cpp. The Continue command restores the original instruction byte, signals the target thread through resumeEvent, then returns. The target may execute the instruction and rebind the breakpoint before the calling test thread reads breakpointByte. The test had required breakpointByte to still equal the original byte after Command returned, even though that value is transient after the target is released.

The smallest correction removes that post-release byte read from the command-acceptance assertion. The following existing single-step assertion still verifies the target reached the next instruction, the counter incremented once, the trap flag was handled, and the breakpoint was rebound. No delay or Server runtime change was introduced. The initial DBG1 console’s exact failed predicate was not retained, so the source establishes a harness race in the assertion but cannot prove which conjunct failed in that historical run.

## Regression qualification

| Qualification | Result |
|---|---|
| IDE documented production build and its hosted build checks | PASS. The exact AMD64 ELF matched the DBG1 reference. |
| Developer Studio package audit | PASS for the manifest and AMD64/ARM64 ELF headers and package contents. |
| Fresh IDE CMake/CTest suite after the Server test correction | 34/34 passed. |
| Debug-symbol capacity harness after the correction | PASS: 411 DIEs, 2 CUs, no truncation, DWARF 5; fixture SHA-256 A386C1B3CF60708157DB9AD3C0AC057204ADB3D4DD5F2293717C6892EEC4150D. Synthetic 511/512 DIE cases passed; 513 was limit_exceeded; the 159-byte source-path case passed and 160/161 were rejected; malformed DWARF remained rejected. |
| Server native debugger runtime test after the correction | 3/3 PASS, including Continue, internal single-step, breakpoint rebind, Step Out, and cleanup. |
| Server native debug policy, output, and Watch contract tests after the correction | All PASS. |
| Git whitespace validation | git diff --check PASS. |

IDE CTest logs, capacity output, and Server native test logs are under C:/Users/guideX/AppData/Local/Temp/guidexos-dbg2-postfix-20261026. No generated test artifact is included in the Server source commit.

## Phase30B acceptance status

The authoritative contract is the IDE report docs/DEVELOPER_STUDIO_PHASE30B_LONG_ROOT_DWARF_ARTIFACT_PATH.md and its continuation history. It requires five independently qualifying fresh production guest boots, per-boot debugger evidence, explicit pass/fail records, and verified ownership and cleanup. A successful Phase28O boot is not Phase30B credit.

The documented shared-QEMU prerequisite is not satisfied. QAC coordinates only participating clients; the existing cross-project review found direct QEMU launchers that do not honor an atomic shared reservation. One successful QAC reservation for this focused run cannot establish exclusive ownership across all relevant launchers for five boots. No formal Phase30B runner was started. Formal Phase30B qualifying boot count for DBG2 is 0/5; the earlier Phase30B history also remains 0/5 accepted. No phase marker was changed and Phase30C was not started.

## Remaining work and recommended DBG3 scope

- Establish a shared, atomic QEMU reservation honored by every relevant acceptance launcher, retained through all five Phase30B boots, with visible ownership, safe recovery, overlap monitoring, and recorded release.
- Under that contract, run the formal five-boot qualification with the rebuilt post-fix AMD64 package and explicit per-boot outcomes.
- Extend guest-level functional evidence to Step Into and Step Over. Step Out and restart should be included only if the existing focused workflow supports them without broadening the phase.
- Capture a concrete variable/local value during guest execution; DBG2 observed a successful variables model but did not retain a named value.

## Commits and publication

IDE implementation commit: d09e8e1b8ed40fd7a1277ebe835acf56344241d1. Server DBG2 closeout commit contains this report and the narrow native runtime test assertion correction. The final Server commit identifier is returned with the completion response. No push was performed.
