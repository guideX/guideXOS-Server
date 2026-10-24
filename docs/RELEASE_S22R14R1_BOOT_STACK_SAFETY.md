# S22R14R1 AMD64 Boot Stack Safety

## Disposition

**Outcome B — STACK_REPAIR_IMPLEMENTED_GUEST_PENDING.** The two measured stack-pressure sources are reduced, the compiler-generated stack audit and hosted suites pass, and a new PacMan-only candidate has been generated. QAC guest execution is queued behind an existing verified exclusive acceptance. No result from the queued candidate run is claimed here. The static, hosted, and candidate-build evidence closes the identified implementation work; the candidate's actual guest execution remains pending QAC admission.

This closes neither overall v0.3 release readiness nor the independent release blockers listed below. The candidate is for qualification only and was not published.

## Repository and candidate provenance

Preflight started on branch main at 4bd91efad08359f0e2f0f1d38ce9c07f247bd878, with a clean tracked worktree. The S22R13R5 evidence checkpoint 530e2144cc63156d239925ff8a81f7d969e60dae is an ancestor. The stack remediation and tests are in local commit **5b62216efe78158fb12707c126ec38582faecf59**. The release report is being added as a separate local commit.

The approximately 10,683 pre-existing untracked paths were preserved. Build, hosted-test, and QAC-run files created for this phase remain under out/; no earlier untracked path was removed or moved. The tracked worktree was clean after the code commit and before this report. No branch change, worktree, reset, clean, stash, rebase, merge, cherry-pick, push, readiness edit, release-channel edit, or dist modification occurred.

The previously accepted frozen ISO remains byte-identical: SHA-256 b6b7b0faeef779a4ac06abb96ad749d2ce906df2e7751c1b9c329dd0a23fc2af.

The new candidate is:

| Item | Identity |
|---|---|
| Build ID | 6ad9bb77-12ce-4b4d-8ed0-56ff3944ee5d |
| Release profile | ServerV03PacManOnly |
| Candidate status | Candidate only |
| Source branch / commit | main / 5b62216efe78158fb12707c126ec38582faecf59 |
| ISO | out/release-profile/6ad9bb77-12ce-4b4d-8ed0-56ff3944ee5d/artifacts/guideXOS-Server-v0.3.0-amd64-pacman-only-candidate.iso |
| ISO size / SHA-256 | 90,245,120 bytes / 2fe2f5eda87c9a80164715a156af5de12c7cf3891d29a961c40012c9d032241e |
| Kernel ELF size / SHA-256 | 2,795,344 bytes / b5e029d23f9c21b5ff0838d9388322bf95b211bf3ef98145ddd344170dfe95dd |
| Symbol-audit SHA-256 | 5ea2693bae92ffd3bbc45358b0b9d5db6765fb4cb259d1486f6387ef6c2fee3c |
| ISO verification | Bootable UEFI no-emulation image; PyCdlib structural verification passed |

The candidate manifest records exactly one native ELF package, PacMan. Developer Studio is not selected. The package file inventory is:

| PacMan file | Size | SHA-256 |
|---|---:|---|
| Apps/PacMan/app.json | 956 | 3f3592e6c76053e323f16d48073e98c67d7f5702b8e47f0b507d6bfb3d126449 |
| Apps/PacMan/bin/amd64/pacman.elf | 63,640 | eeb544bbb2e2574eeaed7bb7f0bad61ea24f6945231b7757223a225b30b8e1cc |
| Apps/PacMan/resources/level1.gximg | 888,860 | 535b37e66d7159078df8a16ce059a6f7ab038588cc9875e152033d046ff47fe1 |
| Apps/PacMan/resources/pacpics.gximg | 360,476 | 57a605df8f24ac9641ff6c9d39df551a17ffbcd68466a561adf5f045de259d50 |
| Apps/PacMan/resources/audio/EatPill.wav | 7,156 | 510a52e5776573197aef14d77e393185b6c7148d1b59dac535d45c5ca0498686 |
| Apps/PacMan/resources/audio/extralife.wav | 21,248 | 767db1552130e676398c121ec605f90f20c89bbbab272305bc781adc8e91d1cd |
| Apps/PacMan/resources/audio/fruiteat.wav | 5,120 | 82c96a997ac2e837de4e418f29f4f4fa6142dd2a0c38f764b6d989d8bd98605d |
| Apps/PacMan/resources/audio/ghosteat.wav | 9,090 | 225168072b38671c89c1fc775d513ca86fbb899e3e82c8032445f6a6dc5e1b95 |
| Apps/PacMan/resources/audio/killed.wav | 17,008 | 1c3e91df8e19041ea5783185c5a8a9f2321993b43481c215d8caf3df590290de |
| Apps/PacMan/resources/audio/StartMusic.wav | 68,318 | 062cdc7196c54033f8bf11878d6659fd5b0ba11cd6945640ec8fac4376919421 |
| Apps/PacMan/resources/audio/startmusicold.wav | 46,592 | a6e1c14f6cbc59cb5244c0119c728d412252370eea48bc574c1307a774c83caf |

The ISO manifest's worktreeCleanAtPackagingStart field is false. The tracked source commit is identified, but the package inputs also include the local PacMan audio assets and other pre-existing untracked workspace content. This candidate is therefore not claimed to be reproducible from the Git commit alone.

The canonical release build script initially failed under Windows PowerShell 5.1 because its build step requires Get-FileHash. A temporary output-only script copy selected PowerShell 7 and the repository script root; the canonical build.ps1, release profile, kernel build, symbol audit, package checks, and ISO backend were otherwise used. The wrapper copy is retained under out/s22r14r1-stack-audit/temp/. No toolchain-wide workaround was added.

## AMD64 stack and memory contract

The audited kernel ELF and linker map show:

| Region or symbol | Address / size |
|---|---|
| .bss start | 0x00340000 |
| Fixed AMD64 kernel heap | 0x006271d8, 512 MiB |
| VFS mount table symbol | 0x22b228e0 |
| IST1 IRQ/exception stack | 0x21946240, 16 KiB |
| Boot stack bottom | 0x22b70000 |
| Boot stack top | 0x22b74000 |
| .bss end | 0x22b76a60 |

The boot stack is 16,384 bytes, grows down, and sits near the end of .bss. stack_bottom and stack_top are ordinary BSS labels, with no guard page or canary between the stack and neighboring kernel data. The linker map places the VFS mount table 0x4D720 bytes below the boot-stack bottom; an overflow can still corrupt other BSS before reaching that table.

Disassembly of _start confirms rsp = 0x22b74000, followed by sub rsp, 0x20 and call kernel_main. The top is 16-byte aligned. The 32-byte subtraction reserves the Windows x64 home area, and the call adds an 8-byte return address; the regression guard reserves 40 bytes for this entry/alignment requirement. Compiler flags include -mno-red-zone, -fno-stack-protector, -fno-stack-check, and -mno-stack-arg-probe; those settings do not provide overflow detection.

Settings and the COM2 service run synchronously on kernel_main's boot stack. No per-service stack switch occurs. The AMD64 IRQ IDT entries select IST1, and TSS.IST1 points to the separate 16 KiB IRQ/exception stack; regular IRQ dispatch and handlers therefore run on that stack. CPU exception IDT entries use IST index 0. Their assembly stubs first save the normalized vector/error value and general registers on the interrupted stack, then switch rsp to gxos_exception_stack before entering the C++ diagnostic dispatcher. That bounded entry prologue still touches the boot stack. Neither stack has a runtime high-water marker or guard. The existing context-switch code does not move these built-in paths to a thread stack.

Settings input is processed from the kernel_main event loop, which calls desktop::handle_mouse and then the app/window callbacks. The COM2 IRQ handler only drains UART bytes into a bounded 2 KiB ring and acknowledges IRQ3. system_service_bridge::poll consumes the queued request in the ordinary mainloop and performs the synchronous service call there. Heap use is limited to that normal context; no allocation was added to the IRQ handler.

## Settings Display path and repair

The affected path is reachable from both Settings initialization and navigation:

- Opening Settings: kernel_main → desktop::handle_mouse → desktop::launch_app → AppManager::launchApp → SettingsNativeApp::init → display refresh/provider callback → readDisplaySnapshot → DisplayConfigurationService::submit.
- Navigating to Display: kernel_main → desktop::handle_mouse → compositor mouse dispatch → SettingsNativeApp::onMouseDown → display refresh/provider callback → readDisplaySnapshot → DisplayConfigurationService::submit.

The original compiler frames were 9,296 bytes for readDisplaySnapshot and 11,120 bytes for DisplayConfigurationService::submit, a 20,416-byte nested subtotal before desktop and kernel callers. The provider frame contained an 8,320-byte DisplayConfigurationResponse; submit contained a 5,016-byte topology-reconciliation plan. The subtotal exceeded the entire stack by 4,032 bytes, without counting its callers.

The provider now keeps its response in a scoped heap allocation and returns Unavailable if allocation fails. Submit allocates the fixed reconciliation plan on the heap, checks failure, returns the existing topology-unavailable result, and deletes the plan after reconciliation. This reduces the compiled frames to 992 and 6,144 bytes. The AMD64 allocator is a fixed 512 MiB BSS-backed arena, initialized on first allocation; each new object has a compile-time size and a checked failure path. Settings reaches these calls on the serialized desktop event path. COM2 reaches its workspace allocation from the ordinary main loop, after its IRQ handler has only queued bytes; no allocator call was added to interrupt context.

The legacy DisplayOptionsApp also had roughly 9 KiB response locals in its query, refresh, and submit handlers. It now owns one reusable response member for the app lifetime. The app is created through its heap-backed factory. The refresh method saves its first result before issuing a nested query that reuses the response member; this prevents the second call from overwriting the first result. Its measured frames are now:

| Legacy frame | Before | After |
|---|---:|---:|
| DisplayOptionsApp::queryDisplayConfiguration | 9,232 | 912 |
| DisplayOptionsApp::queryPendingTopologyChange | 9,216 | 896 |
| DisplayOptionsApp::refreshPendingTopologyChange | 9,216 | 896 |
| DisplayOptionsApp::submitDisplayConfiguration | 9,264 | 944 |
| DisplayOptionsApp::init / loadSelection / onMouseDown | 80 / 96 / 128 | 80 / 96 / 128 |

No global scratch object was introduced. Each allocated response or plan has one synchronous owner and is released on all exits.

## COM2 storage path and repair

The original COM2 storage service subtotal was 23,936 bytes: serveRequest 4,944, dispatcher 9,856, and readStorageSnapshot 9,136. It excluded the poll and kernel_main caller frames. The device route was at least 20,144 bytes. readStorageSnapshot copied a full StorageSnapshot local; the dispatcher also kept network, device, and storage snapshots in a local workspace. These fixed value objects multiplied across nested frames.

The kernel now allocates one fixed 9,624-byte DispatchWorkspace for a serialized request. The dispatcher receives a workspace pointer, provider callbacks fill caller-owned device/storage snapshots directly, and the same workspace is reused for the diagnostic decode. A null workspace returns a framed internal-error response. The old overload remains for existing hosted callers, but the freestanding COM2 bridge uses the pointer-owned overload. The workspace is scoped to serveRequest, not shared mutable global state.

The resulting GCC frames are 272 bytes for serveRequest, 96 for poll, 1,024 for readDeviceSnapshot, and 352 for readStorageSnapshot. The MBR reader still uses a bounded 4,096-byte sector buffer and measures 4,176 bytes; this frame remains in the budget. The bridge's IRQ handler remains a 64-byte queue-and-acknowledge path, not a dispatcher or allocator path.

## Compiler stack budget and guard

The audit build produced GCC .su files and callgraph data from the freestanding AMD64 kernel. The guard parses the actual .su records, fails if required records are missing, uses the larger record when GCC emits duplicate body/clone entries, and sums explicitly identified paths. It also enforces frame ceilings for the known critical provider, service, decoder, MBR, and legacy Settings frames.

| Path | Raw sum | Guarded sum | Capacity | Remaining |
|---|---:|---:|---:|---:|
| Modern Settings launch | 9,936 | 11,512 | 16,384 | 4,872 |
| Modern Settings navigation | 9,760 | included in 11,512 worst | 16,384 | — |
| Legacy Display Options initialization | 9,536 | — | 16,384 | — |
| Legacy Display Options query / apply | 9,440 / 9,472 | — | 16,384 | — |
| Legacy Display Options refresh with nested pending query | 10,320 | 11,896 | 16,384 | 4,488 |
| COM2 storage dispatch / diagnostic decode | 7,472 / 7,768 | 7,768 worst | 16,384 | 8,616 |
| COM2 device dispatch / diagnostic decode | 4,656 / 7,296 | included in 7,768 worst | 16,384 | — |

The Settings guard includes 1,024 bytes for unmodeled call glue, 512 bytes for interrupt/exception entry, and 40 bytes for boot entry/alignment. Regular hardware IRQs switch to the separate IST1 stack and do not consume the boot stack while these paths run. The 512-byte reserve covers the CPU exception frame and the stub's normalized vector/error and register saves on the interrupted boot stack before its manual switch to the separate exception stack. It is a static reserve, not runtime instrumentation; the separate 16 KiB IRQ/exception stack also has no high-water or guard. Every guarded path must retain at least 4,096 bytes after these reserves. The exact invocation passed:

pwsh -NoProfile -File tests/boot_stack_budget_guard_test.ps1 -StackUsageRoot out/s22r14r1-stack-audit/kernel/obj

Output: Settings guarded worst 11,512/16,384; legacy Display Options 11,896/16,384; COM2 7,768/16,384; minimum required headroom 4,096 bytes.

These are compiler estimates for the enumerated call chains, not observed high-water measurements and not a whole-kernel proof. The guard does not automatically discover all indirect calls, recursion, arbitrary future branches, or every kernel frame. Separate audit warnings include 10,272- and 9,232-byte startup restore/fallback routines gated behind the QEMU VirtIO GPU probe configuration; that configuration is not selected by ServerV03PacManOnly. Other large frames remain outside this phase's path-specific regression guard.

## Hosted regression results

All suites below passed without weakening existing expectations:

| Suite | Result |
|---|---:|
| System service bridge | 82/82; device 14/14; storage 15/15 |
| Settings native runtime | 1,426/1,426; lifecycle repetitions 100, provider refresh cycles 250, notification/state transitions 1,000 |
| Settings native layout | 1,037/1,037; resize transitions 1,000 |
| Settings inventory model | device 21/21; storage 24/24 |
| Settings presentation core | 3,659/3,659 |
| Settings center model | 89/89 |
| VFS mount-table integrity | 16/16 |
| VFS hosted read | 4/4 |
| Block-device generation | 5/5 |
| Navigator smoke stack contract (S22R12 regression) | pass; seven heap-scoped fixtures, no automatic NavigatorApp objects |
| Navigator URL-resolution smoke | pass |
| PacMan source-registration fixtures | pass |

The first default Settings layout-test run refused to overwrite an existing output. It was rerun successfully using a unique output directory under out/s22r14r1-stack-audit/host/settings-native-layout/.

## Freestanding AMD64 build and symbol audit

The production-profile candidate's build identity records successful bootloader, kernel, symbol-audit, ramdisk, PacMan, and package stages, with Developer Studio not selected. The separate stack-audit build used the same AMD64 freestanding target and added -fstack-usage, -fcallgraph-info=su, -Wframe-larger-than=4096, and -Wstack-usage=8192 for evidence. The stack-analysis flags did not enable a debug-only kernel configuration.

The candidate symbol report records an ELF64/EXEC/AMD64 image, no ELF relocations, no .dynsym, .dynamic, or PT_DYNAMIC, no PE imports, and no remaining unexplained external runtime dependency. It reports 45 undefined ELF symbol-table rows representing 21 names; the retained entries are either link-resolved, weak ABI bookkeeping, or unused non-loadable table entries. The audit is limited to this candidate ELF/PE pair and its inspected inputs.

The stack map confirms the BSS, fixed heap, exception stack, VFS mount table, and boot-stack addresses listed above. The audited kernel.elf hash matches the packaged candidate kernel.

## Isolated build reproducibility finding

The earlier _wcsicmp failure came from a bare-metal translation unit including windows.h and stralign.h because _WIN32 was tested without excluding GXOS_BARE_METAL. That failure was observed on the older source revision ea786e2da4da01a57b1ab00429c8453edc9446ad. Current bitmap_font.h uses the condition _WIN32 and not GXOS_BARE_METAL, and the current isolated compile of native_elf_baremetal.cpp succeeded without a workaround.

This phase did not complete a clean end-to-end source-and-content reproduction from the current commit. The broader isolated release reproducibility gate therefore remains open as recorded by S22R14; the candidate build above does not close it.

## QAC guest qualification

The QAC coordinator was online with zero foreign QEMU processes. At the latest status check on 2026-10-24 18:22:49 UTC, QAC still listed one existing guideXOSServer exclusive acceptance: verified owner PID 31704, QEMU child PID 24052, reservation 8b84465adf184cc6a1f416744168127c. That reservation was left untouched. A second project's acceptance remains queued behind this run and was not changed.

The new request was admitted to the QAC queue as reservation 3db20d3f6ad94171916b2a470a64ca01, position 1. Its owner process PID 16936 is verified and waiting; no R14R1 QEMU child has launched. The runner has a 1,800-second admission timeout from 2026-10-24 18:11:04 UTC. The staged private ESP hash is cf8fe881beb41ea5b3114a39d3dcd029cd9a96b129463bfe7dfffd9009bf4e4f; its candidate ISO hash matches the new candidate. The QAC .phase file is unchanged.

**Guest coverage status at report preparation:** boot, Desktop/VFS integrity, Settings Display repetition, COM2 read-only storage request, PacMan gameplay/exit, Navigator file URL lookup, and a second cold boot are not yet verified against this new kernel. The existing accepted R5 guest evidence remains historical evidence for the frozen ISO only. The S22R10 runner's automatic PacMan PASS marker is distinct from manual targeted stack-path evidence; no marker will be manufactured.

The candidate-specific guest paths remain unverified because QAC has not admitted this request. The request is retained in QAC's managed queue, and the runner will release its own waiting reservation when its bounded admission timeout expires if the slot does not become available. No unmanaged QEMU fallback was used.

## Outstanding release blockers and next phase

The independent S22R14 findings remain open and release readiness stays blocked:

- /Documents creation is unsupported.
- /.startmenu_recent persistence fails.
- Native ELF applications execute with kernel privilege.
- Runtime application packages are writable.
- Runtime ABI negotiation is insufficient.
- Enforceable native application isolation is absent.
- Clean end-to-end isolated release reproducibility has not been established.

Do not change the S22R14 readiness matrix or treat this stack report as publication approval. If QAC guest qualification completes, the next Main Integration phase should close the remaining release gates and revisit the matrix with new evidence. If QAC remains occupied, record Outcome B and complete the candidate guest checks in a later authorized QAC window.
