# S22R28R1 — First AMD64 CPL3 Guest Acceptance

**Outcome B — CPL3_GUEST_EXECUTION_PARTIAL**

The corrected S22R28 diagnostic guest executed all three bounded tasks at CPL3, returned through the validated vector 0x80 path, reaped each task, and continued into the desktop event loop with PIT heartbeats. Overall qualification remains partial because the same boot did not mount the root or /Apps filesystem, so VFS-backed application and user-data readiness were not demonstrated. Input response was not exercised, and shutdown was host-initiated through QMP.

## Repository and preservation

- Repository: D:\dev\guideXOSServer
- Branch: main
- Source revision before documentation: 14be308b7282a2131b0f124eeccd662a5378014d
- Pre-documentation tracked worktree: clean. The sole pre-existing untracked file was assets/Backgrounds/troncity.png.
- Preserved wallpaper SHA-256: 444ac64211907d80f0266e2283960cefaa932803d0aee7e0ccfd414f6fae2fda.
- The accepted public-preview ISO baseline remained SHA-256 8d397659e0a6ec8f1be113ae4f5be14dbcaf1ef22ccda41a6e5ee5014919cf40, 90,245,120 bytes. It was not replaced.
- No source implementation files, dist artifacts, signing/publication metadata, or release-readiness markers were changed. The only repository additions for this work are this report and its evidence package.
- Diagnostic artifacts and build outputs were kept outside the repository under E:\CodexArtifacts\S22R28R1.

## Diagnostic artifacts and source mismatch

Both ISOs are explicitly labeled DIAGNOSTIC — NOT PUBLIC RELEASE CANDIDATE. Neither is a release candidate.

| Candidate | ISO SHA-256 | Kernel SHA-256 | Loader SHA-256 | Result |
| --- | --- | --- | --- | --- |
| Candidate 1, BootInfo v2 loader | 527533609310eafe0e7d2cec64444d0e06de918d7f96a49bc88dda8c1054c5d2 | 47d4409e9d37e6e593b2f6068f45c970ad666ecb3831706b6b4b52ce15336c3b | f3ef0f7ce57f707d06be74b657a51355507149ff4c9f675e6f5c4d5befcc5f6b | Gate A stopped before CPL3 |
| Candidate 2, current BootInfo v3 loader | 3168d943af704a730110ab1e669ab482c3e93f282bbd7d2f73283f490b2d785c | 47d4409e9d37e6e593b2f6068f45c970ad666ecb3831706b6b4b52ce15336c3b | 81258f7ad179494a3cdf289b87170370d3a281900e1e386caf41041e00fb00d4 | Three CPL3 cycles passed; Gate D partial |

Candidate 1 reused the previously accepted ESP and its older bootloader from source commit 67dc2e111ea4d6e7208a8d5fac7dbd1a389d8491. That loader supplied BootInfo version 2 and had no task-frame-arena fields. The S22R28 kernel requires BootInfo version 3, a valid task arena base and size, and the corresponding validity flag. The first guest therefore reported:

- [MEMORY-OWNERSHIP] status=boot-handoff-unavailable user-mode=disabled
- [CPL3] status=frame-owner-unavailable skipped=1

This was an artifact-composition mismatch, not evidence of a CPL3 kernel runtime defect. I preserved that run and did not activate CPL3.

For Candidate 2, I built the checked-out loader from revision 14be308b7282a2131b0f124eeccd662a5378014d, Release|x64. It is 57,856 bytes, uses BootInfo version 3, and reserves a 16 MiB EfiLoaderData arena (4,096 pages). Its SHA-256 is 81258f7ad179494a3cdf289b87170370d3a281900e1e386caf41041e00fb00d4. The corrected guest later reported the arena reservation and successful frame-owner initialization.

Candidate 2 contains a 90,177,536-byte embedded ESP with 17 files. Its embedded ESP SHA-256 is fb1eb4975b823a790d70fa2be4682a2e3a4ba1ec017ff95d324336b15ce0d049, independently verified against the staged ESP. The PacMan-only policy is unchanged: manifest SHA-256 3f3592e6c76053e323f16d48073e98c67d7f5702b8e47f0b507d6bfb3d126449 and PacMan ELF SHA-256 eeb544bbb2e2574eeaed7bb7f0bad61ea24f6945231b7757223a225b30b8e1cc. No allowlist entry was added. No PacMan process was launched.

The final candidate kernel was built with GXOS_ENABLE_CPL3_DIAGNOSTIC and GXOS_PUBLIC_PREVIEW_SECURITY_PROFILE. The public-preview profile keeps the production Native ELF security path and PacMan-only package policy in the diagnostic artifact. The candidate kernel is 2,846,392 bytes. Its diagnostic activation is compile-time only.

## Hosted and freestanding preflight

All focused checks requested for this qualification passed:

| Check | Result |
| --- | --- |
| scripts/test-amd64-protected-task-contract.ps1 | PASS; executable SHA-256 32f5e35ab1039f5c03f76ca83580cc6cc8b46136c6fb960ce065b73dc623e7f2 |
| scripts/test-amd64-memory-foundation.ps1 | PASS; executable SHA-256 412e3009c5ba674965673501bc4920748b201e2edc8bf17f3b081bf228c40f0b |
| scripts/test-amd64-context-switch.ps1 | PASS; executable SHA-256 877014e0baf515ccbd458028cdaee92dbb6f6f9092107579305a2358574fe4a8 |
| scripts/test-native-elf-security-policy.ps1 | PASS; 29 checks, zero failures |
| tests/boot_stack_budget_guard_test.ps1 | PASS; 16 KiB contract, minimum modeled headroom 4,520 bytes |

The diagnostic-off and diagnostic-on rebuilds without the public-preview profile matched the previously recorded hashes: 2b9c39a27d082e03f9b9c37eb1c091f6fe879c83275fd63c31fe5c5d002268e3 and 264701d1d30e8a44702da6f375b6beabe37e655f26cda7d82e735eadecfbad64 respectively. The separately packaged public-profile builds have hashes 3b1c383b4f141c43653054b58998492e55077df95c92b75cbaaf84a0ff9630eb with diagnostics off and 47d4409e9d37e6e593b2f6068f45c970ad666ecb3831706b6b4b52ce15336c3b with diagnostics on. The changed define is GXOS_PUBLIC_PREVIEW_SECURITY_PROFILE; the final guest used the latter diagnostic-on kernel.

The symbol audit passed: ELF64 AMD64 ET_EXEC, 55 undefined .symtab rows / 22 distinct names, with no relocation or import sections. The final diagnostic build reported 265 existing warnings and zero errors; diagnostic-off reported 267 warnings and zero errors.

Toolchain: MinGW-W64 x86_64 g++ 15.2.0, GNU ld 2.46.0.20260210, NASM 2.16.03, MSBuild 18.11.1.51303, and MSVC 19.51.36263.0. The bootloader build completed with no errors and two existing option-override warnings.

The stack guard retained the 16 KiB contract with 4,520 bytes minimum modeled headroom. C/C++ .su figures were kernel_main 864 bytes, run_cpl3_diagnostic 96, run_diagnostic_cycles 288, exit dispatcher 80, cleanup 144, and user-fault recorder 8. Separately, assembly provides a 16 KiB lifecycle stack, 16 KiB TSS RSP0/user-entry stack, 16 KiB IST1 exception stack, and 16 KiB boot stack. The exit stub uses the 40-byte hardware frame, saves 15 GPRs (120 bytes), and reserves 32 bytes of Microsoft x64 shadow space. These measurements do not prove the entire possible call chain.

Detailed receipts are in [preflight-results.json](evidence/s22r28r1-cpl3-guest/preflight/preflight-results.json), [kernel-symbol-audit.txt](evidence/s22r28r1-cpl3-guest/preflight/kernel-symbol-audit.txt), and [bootloader-current-build.log](evidence/s22r28r1-cpl3-guest/preflight/bootloader-current-build.log).

## QAC admission and guest execution

QAC was checked before admission. The coordinator was Online with zero managed or foreign QEMU processes, zero reservations, and zero queued requests. Every QEMU guest launch in this work used QAC acceptance mode; no unmanaged guest launch was used.

The first candidate was admitted as reservation 195e411106e445eb90a7e21c27e94097, with QEMU child PID 13252. QMP showed a running guest. Gate A failed at the BootInfo v2 handoff as described above. I stopped it with QMP quit; this was a host-driven stop, not guest shutdown. QAC verified child exit code 0 and released the reservation.

The corrected Candidate 2 guest was reservation 3f013306fc01476d9fcff14d6eaac41d. QAC verified owner PID 30508 and direct child PID 17308 at C:\Program Files\qemu\qemu-system-x86_64.exe. The child command line records the exact Candidate 2 ISO path and its hash was checked as 3168d943af704a730110ab1e669ab482c3e93f282bbd7d2f73283f490b2d785c. QMP loopback port 49286 was owned by that child.

QEMU used the x86_64 UEFI flash code image read-only, a disposable variable-store copy under the external candidate artifacts, the diagnostic ISO as CD-ROM, 1 GiB RAM, TCG acceleration, and no display backend. QMP reported status prelaunch and running=false before continuation. After cont, it reported running=true. Guest execution after continuation lasted about 174 seconds, within the 180-second bound. The serial transcript SHA-256 is C6750038FF91A2E78CA9562D9D0B276A3933BC2B7C7B250AD1D5DF578397E09E. The QEMU guest-errors log remained empty.

The diagnostic later reached desktop_event_loop_active=true, the kernel main loop, and PIT heartbeat ticks through 0x2134. I requested QMP quit at 2026-11-22 00:11:29 UTC. QMP recorded SHUTDOWN reason host-qmp-quit; QEMU exited with code 0, and QAC released the reservation as Managed child exited. This does not establish guest-initiated or ordinary OS shutdown. Immediately after our release, QAC was Online with zero managed sessions, queued requests, QEMU, or foreign QEMU processes.

A later read-only closeout snapshot at 2026-11-22 00:22:50 UTC showed a separate managed guideXOSServer acceptance reservation 9602b018c23b4ce08f7b2c77c4aba931, owner process PID 29120, child PID 30872, started at 00:19:21 UTC. Our reservation 3f013306fc01476d9fcff14d6eaac41d remained Released. The later session had queue position 0 and QAC reported zero foreign QEMU processes. I left it untouched. See [the later QAC snapshot](evidence/s22r28r1-cpl3-guest/candidate-v2/artifacts/qac-closeout-after-task.txt).

Two additional Candidate 2 QAC attempts exited before guest execution because the initial QEMU firmware argument was rejected. Their managed children exited with code 1 and their reservations were released. The successful run used the x86_64 UEFI pflash code and the separate disposable variable store. These launch-configuration attempts are infrastructure records, not guest failures. The initial QMP client also emitted one malformed-encoding request before a successful no-BOM QMP handshake; the guest stayed paused until the successful handshake and known prelaunch status were recorded.

## Guest gates and lifecycle evidence

### Gate A — boot and memory ownership: PASS

UEFI loaded the corrected loader and kernel. The loader reported a task-frame arena at 0x000000003CE28000 with 4,096 pages. The kernel reported:

- [KERNEL] guideXOS kernel_main entered
- [KERNEL] Boot method: UEFI BootInfo
- [MEMORY-OWNERSHIP] status=ready arena-pages=0x00001000 free-pages=0x00001000

No CPL3 task was attempted on Candidate 1; all Gate B and C evidence below comes from the corrected Candidate 2.

### Gate B — real CPL3 entry and controlled exit: PASS

The kernel printed three entry records with CS=0x0023 and SS=0x001B. Each cycle ran the bounded task-owned code and data path: user code incremented its private data value from 41 to 42 and saved its executing CS value. The kernel-mediated exit validated the task and reported:

- trap origin CPL3, vector 0x80
- kernel stack verified and kernel CR3 restored
- actual-CS 0x23
- result 0x2A (42)
- cleanup verified and state reaped

This is runtime evidence of processor-level CPL3 execution and controlled return. The diagnostic also validates its code as user-readable/executable, task data and stack as user-writable/non-executable, an unmapped stack guard, and supervisor-only kernel state. No deliberately unauthorized access was attempted, so this is not a dynamic proof of every page-protection boundary.

### Gate C — three lifecycle cycles and cleanup: PASS

| Cycle | Task identity | Address-space CR3 | Exit CS | Result | Teardown |
| --- | --- | --- | --- | --- | --- |
| 1 | 00522800 | 000000003CE28000 | 0x23 | 0x2A | reaped; cleanup verified |
| 2 | 00522801 | 000000003CE28000 | 0x23 | 0x2A | reaped; cleanup verified |
| 3 | 00522802 | 000000003CE28000 | 0x23 | 0x2A | reaped; cleanup verified |

The allocator reused the same CR3 root after each successful destroy; task identities advanced per cycle. Cleanup compares the complete FrameAccounting structure against the pre-cycle baseline and prints cleanup verified only after equality, so all tracked task, mapping, and page-table frame counts returned to baseline each cycle. The serial format does not print numeric before/after counters. The final marker was status=passed cycles=3 nonfaulting=1 preemption=0. No abandoned task or active task context remained, and the kernel continued after cycle 3.

### Gate D — continued system operation: PARTIAL

After the diagnostic, desktop_event_loop_active=true and the kernel printed Entering main loop (waiting for input). PIT heartbeat markers continued through tick 0x2134 with no kernel exception or panic marker in the serial transcript.

The same boot reported mountTable count=0, /Apps absent, Filesystem mount failed, and USERDATA mount=failed reason=trusted-system-source-unavailable. The desktop event loop ran, but VFS-backed applications and persistent user data were not available in this guest run. No data volume was attached or modified, so data persistence and user-data usability were not tested. This observed mount failure is recorded as a separate boot/runtime limitation; the evidence does not attribute it to the CPL3 diagnostic.

The guest was headless, and input responsiveness was not tested. PacMan was not launched. No arbitrary CPL3 fault, infinite-loop, timer preemption, or task-survivability experiment was performed.

## Outcome and unqualified behavior

The bounded CPL3 execution boundary is demonstrated in a real AMD64 QEMU guest: three tasks entered ring 3, mutated private user data, exited through vector 0x80, returned to the kernel, and had their private frames and page-table accounting restored. The kernel then continued into its desktop loop and serviced timer heartbeats.

Overall result is Outcome B — CPL3_GUEST_EXECUTION_PARTIAL. Gate D did not establish VFS-backed application or user-data availability, input response was not tested, and ordinary guest shutdown was not demonstrated. This is not a CPL3 runtime blocker. Candidate 1's Gate A failure was explained by the BootInfo version mismatch and resolved by using the current loader; Candidate 2 passed Gate A through C.

Still unqualified:

- Recovery from a CPL3 page fault or any arbitrary user exception; the current fault path records and halts.
- Recovery from a non-cooperating or infinite-looping task.
- Timer-driven preemption and interrupted-context capture/restore.
- Independent kernel scheduling and task termination under preemption.
- Kernel-mediated Native ELF services and protected PacMan execution.
- VFS-backed application launch, persistent user-data continuity, live input response, and guest-initiated shutdown in this diagnostic setup.
- Dynamic rejection of unauthorized user access to supervisor mappings.
- Complete worst-case call-chain stack usage.

The existing production Native ELF path remains on its established CPL0 route. The diagnostic does not change PacMan's policy, the read-only /Apps trust path, ABI validation, persistence, signing, or release readiness. It does not prove application survivability.

## Recommended S22R29 scope

Make S22R29 the smallest next step toward application survivability: design and qualify a safe user-origin exception path that validates the fault origin, returns to a known kernel CR3 and kernel stack, records the task fault, and continues in the kernel so the faulted task can be terminated and reaped off-stack with frame accounting restored. Keep arbitrary fault injection behind a bounded, disposable diagnostic fixture. Do not introduce timer preemption in the same change.

After safe fault return and reaping are proven, a later milestone can capture and restore interrupted user contexts on timer entry, add an independent scheduler, and only then move Native ELF services or PacMan toward protected execution. The final objective—an application can stop cooperating or fault while the kernel retains control and leaves the desktop and user data usable—remains unqualified.

## Evidence

The complete compact evidence set is indexed in [the evidence README](evidence/s22r28r1-cpl3-guest/README.md). It includes separate Candidate 1 failure and Candidate 2 success records, ISO/ESP verification reports, QAC admission/release records, the exact QEMU child command line, QMP state and shutdown transcripts, serial output, and build/preflight receipts. The original full external candidate artifacts remain at E:\CodexArtifacts\S22R28R1.
