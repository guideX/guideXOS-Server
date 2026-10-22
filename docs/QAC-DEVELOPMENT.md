# QAC development launcher integration

## Scope and selected workflow

QAC3B integrates one existing Server development workflow: `scripts/run-qemu-x86-with-build.bat`, the build-and-run entry point documented for Visual Studio F5/Ctrl+F5. It builds the classic x86 kernel through `kernel/build-x86.bat`, then invokes `scripts/run-qemu-x86-qac.ps1`. The PowerShell runner starts the QAC CLI, and QAC starts QEMU as its direct child.

The selected checkout was `D:\dev\guideXOSServer`, branch `main`, starting HEAD `2fde8142a183ea7a4c1ba39bc764c604c0484fcc`. This workflow is a frequent, ordinary development path with a distinct final QEMU spawn and a build output under `kernel/build`; it does not rebuild or replace the tracked ESP image. Newer UEFI smoke runners can rewrite `ESP/ramdisk.img` and staged ESP packages, so they remain outside this first integration.

## Invocation and reservation mode

Use the existing Visual Studio workflow:

```powershell
.\scripts\run-qemu-x86-with-build.bat
```

If the x86 kernel is already built, the managed launch step can be run directly:

```powershell
pwsh.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\run-qemu-x86-qac.ps1
```

The launcher always requests `--project guideXOSServer --mode development`. It does not promote a development run to acceptance. Formal exclusive qualification must continue to use its acceptance launcher and `--mode acceptance`.

The interactive QAC coordinator must already be online under the same Windows user. The runner does not start, install, or repair a coordinator or service. It resolves the CLI in this order: `-QacCliPath`, `GUIDEXOS_QAC_CLI`, `qac.exe` on `PATH`, then the sibling QAC source-tree apphost. A missing CLI or unavailable coordinator stops the run before QEMU; there is no unmanaged fallback.

QAC applies its existing development capacity and queue policy. The coordinator's default admission wait is 30 minutes; queue time is separate from this interactive guest run because the original launcher had no guest runtime deadline. The session ends when the user closes the QEMU window or presses Ctrl+C.

## QEMU arguments and lifecycle

The original emulator and guest configuration remain in place:

| Setting | Value |
| --- | --- |
| QEMU executable | `C:\Program Files\qemu\qemu-system-i386.exe` |
| Guest input | `kernel/build/x86/bin/kernel.elf` |
| ISO or disk | None |
| Machine | `pc,usb=off` |
| RAM | `128M` |
| VGA/display | `std`, GTK window |
| Serial | `stdio` |
| VNC | `:0` |
| Other flags | `-no-reboot -no-shutdown -d guest_errors` |

`qac-managed-qemu.psm1` constructs a `ProcessStartInfo` for the QAC CLI and adds every argument through `.ArgumentList`. The QAC invocation is `run --project guideXOSServer --mode development -- <QEMU> <arguments>`. QAC then uses its own argument list to start the direct QEMU child. Spaces, embedded quotes, empty arguments, and trailing backslashes are not reconstructed through a shell. The existing serial stream remains attached to the console, and the QAC child exit code is returned through PowerShell and the batch launcher.

This interactive launcher had no QMP socket, serial file, guest marker protocol, or runtime timeout. None was added. QEMU shutdown uses the existing GUI close or Ctrl+C behavior. QAC waits for the verified direct child to exit before releasing the reservation. The launcher does not kill processes by name, terminate process trees, or create Windows Job Objects. If QAC cannot verify release, its existing recovery behavior remains authoritative.

QAC prints reservation, queue, child PID/executable identity, exit, and release diagnostics. The runner adds a coordinator status preflight and concise development-mode messages. A build error prevents the QAC command from running.

## Inspected launcher inventory

| Launcher | Existing behavior | QAC3B disposition |
| --- | --- | --- |
| `scripts/run-qemu-x86-with-build.bat` | Builds x86 kernel, then starts fixed `qemu-system-i386.exe`; kernel only; GTK/VNC; serial to console; no QMP or guest marker | Selected and routed through QAC |
| `scripts/run-qemu-x86.bat` | Uses the same classic x86 QEMU settings with an already-built kernel; starts QEMU directly | Future adoption candidate |
| `scripts/smoke-live-directory-desktop-runtime.ps1` | Builds with a smoke define, boots x64 UEFI from OVMF and the ESP directory, records serial markers, has a 120-second guest deadline, and force-stops its direct QEMU process | Future candidate; its full build rewrites tracked and untracked ESP inputs |
| `scripts/smoke-imageviewer-baremetal-runtime.ps1` | Builds/stages an x64 UEFI image, records serial markers, has a 240-second guest deadline, and force-stops its direct QEMU process | Future candidate; requires isolated build and cleanup design |
| `scripts/smoke-desktop-startup-sync.ps1` | Builds the x64 UEFI image, writes a serial log, waits up to 120 seconds for startup markers, and force-stops its direct QEMU process | Future candidate; requires isolated build and cleanup design |

The main checkout contains other direct-QEMU scripts as well; none in this checkout previously used QAC. This phase changes only the selected build-and-run workflow. Developer Studio Phase 28Q/28O and the .NET Support C181/C182 qualification lanes are not modified.

## Automated regression results

Run the focused contract checks with:

```powershell
pwsh.exe -NoProfile -ExecutionPolicy Bypass -File .\tests\qac_development_launcher_contract_test.ps1
```

The script passed **53 assertions** for project and mode selection, direct-child argument order, path and argument preservation, the original QEMU settings, coordinator preflight ordering, exit propagation, and guards against unmanaged launch or process-tree cleanup. These are launcher contract checks; they do not mock or prove coordinator queue transitions, guest execution, or QEMU process recovery.

QAC source and policy were not changed. The historical QAC3A 98-check result remains the recorded baseline and was not rerun for this integration.

## Windows qualification on LEON2_RDT

On 2026-10-20, the interactive coordinator reported Online (QAC 2.0.0, coordinator PID 408). Its CLI census reported no foreign QEMU process. During later inspection, the WPF dashboard's accessibility tree showed the active .NET Support `guideXOSServer` acceptance session with a verified owner, child PID, executable identity, and duration. That session was left untouched. The dashboard observation applies to that protected acceptance session, not to a QAC3B development run.

The selected build-and-run command was attempted. The x86 build failed before QAC admission because the installed `i686-elf` compiler was configured with `--without-headers` and has no target `string.h`; compilation stopped in existing kernel sources. The launcher correctly stopped after the build failure and did not start QAC or QEMU. No source or tracked ESP input was changed by the failed build. Generated `kernel/build/x86` intermediates are ignored build output.

As a result, the following remain unqualified: a real Server guest boot through the selected launcher, a QAC3B development reservation in the dashboard, queue release/cancellation through the selected launcher, guest exit and reservation release for that run, and dashboard independence after closing/minimizing the UI. No claim of a Server smoke pass is made. The current outcome is **partial; QAC3B remains open**.

### QAC3B-R1 x86 recovery follow-up — 2026-10-20

The active checkout was `main` at starting HEAD `41ed479f5f4db414595262e4d0bcca500452e2bb` (`docs: add S22R5 DeveloperStudio portability audit`), one local commit ahead of the pasted expected HEAD `0965387b970254035ffce81f1e58aa6893c57936`. That existing commit and its files were preserved. The first integrated retry confirmed that the compiler is an intentional no-sysroot `i686-elf` compiler and the kernel needed its own freestanding `string.h`. A project `assert.h` was also required by the pinned Mbed TLS sources. The old hand-written batch build path dropped Mbed TLS include flags and omitted x86, shared settings, pinned crypto, and `libgcc` link inputs, so `kernel/build-x86.bat` now delegates to the canonical `kernel/makefile` without cleaning existing outputs. The Mbed TLS profile's configured `snprintf` callbacks were missing their required alternate-platform macros; those were added and the overlay hash was updated in `guidexos/mbedtls_profile.json`. Details are in [the toolchain diagnostics](evidence/qac3b-r1-20261020/toolchain-diagnostics.md).

The canonical full build completed with exit 0, 0 errors, and 14 warnings (13 compiler warnings and the RWX LOAD-segment linker warning). The output is a freestanding ELF32 Intel 80386 executable with no unresolved symbols; the full log is preserved at `E:\QAC3B-R1\kernel-make-build4.log`. The first live guest reached `kernel_main` but halted with `No valid boot method detected`. The first cdecl-order correction still failed because `mov ax, 0x10` clobbered the Multiboot magic in EAX before argument transfer. The final entry code preserves EAX in ECX before segment setup and pushes ECX before EBX. The final integrated build produced SHA-256 `3566D010B087DD9309593ACAF59D6CF2CD48B15BCBDBC0C39F207AFB0774FC00`; its ELF32 i386 header and empty undefined-symbol list were verified.

The original integrated launcher obtained QAC development reservation `9dc21cb291ec4ab5aa5a63f91f216adb` and QAC launched `C:\Program Files\qemu\qemu-system-i386.exe` PID 3476 as direct child of CLI PID 23188. The original QEMU arguments were preserved. The WPF dashboard was visually observed with the real `guideXOSServer` development row, verified owner, child PID, executable, duration, coordinator state, and empty queue. Windows Firewall displayed a network-access prompt over part of the dashboard; it was left for the desktop owner. A Ctrl+C input did not exit the child, and QAC correctly retains the reservation while that child remains live. No process-name or process-ID termination was issued. QAC later detected foreign x64 QEMU PID 6156 and a subsequent census no longer listed it; its exit cause was not independently attributed because Windows process-audit logs were unavailable.

Live QAC scheduler checks used only childless `qac request` holders. A third development reservation waited at queue position 1 and was granted after a test-owned development reservation was released. A test-owned acceptance request waited at position 1; later development requests waited at position 2 behind it, including after one development slot became free. Queued development requests were cancelled and QAC reported `Cancelled`. QAC later detected a foreign x64 QEMU and blocked acceptance admission; a subsequent census no longer listed it, but its exit cause was not independently attributed. Detailed IDs and transitions are in [the live QAC evidence](evidence/qac3b-r1-20261020/live-qac-evidence.md).

**Interim disposition at this checkpoint: Outcome B.** This status was recorded before the final entry-register correction and successful live boot below. The earlier Windows Firewall prompt was not acted on by this task and was absent during the corrected run.

### QAC3B-R1 final live qualification — 2026-10-20

**Outcome A — QAC3B accepted.** The original integrated `scripts/run-qemu-x86-with-build.bat` completed the canonical freestanding x86 build, obtained QAC development admission, and ran `C:\Program Files\qemu\qemu-system-i386.exe` as QAC's verified direct child with the original QEMU arguments. Reservation `590747c0a9b846e3a2d6b34a97151c5a` owned child PID 24224; the guest reached the Multiboot boot method, produced its existing serial diagnostics, and displayed the existing `guideXOS Kernel` text console. The screen also reported `Framebuffer not available - text mode only`; this validates the text-console boot path, not graphical desktop readiness.

The WPF dashboard visibly showed the real development reservation, verified owner, child PID, executable, duration, and coordinator status. Minimizing the dashboard left QAC Online and the same child/reservation Running; reopening showed the current row. Closing the test-owned QEMU window produced child exit code 0, QAC verified the child exit and released the reservation, and the launcher returned 0. At release time, the dashboard showed `Managed child exited` with no QEMU, foreign, managed, or queued sessions. A later read-only census found a new foreign QEMU from `guideXOSServer_REXX` (PID 1940); QAC still showed no managed or queued Server session and blocked acceptance while preserving that foreign process. A subsequent final census showed zero QEMU and foreign processes, with no reservations or queued work; PID 1940's exit cause was not independently attributed. Screenshots and reservation details are in [the live QAC evidence](evidence/qac3b-r1-20261020/live-qac-evidence.md).

Live childless scheduler checks verified development queue admission, the acceptance fairness barrier, queue position reporting, and cancellation. The existing launcher contract suite passed 53/53. QAC source and policy were unchanged; the historical QAC3A 98-check suite was not rerun. A foreign QEMU process was observed during earlier censuses; no process-specific action was directed at it. Its exit cause was not independently attributed because process-audit logs were unavailable.

QAC `.phase` now records QAC3B accepted while overall QAC3 remains open; QAC3C has not started.

## Future adoption

QAC3C can assess the serial-marker UEFI smoke runners after their build outputs and shutdown paths are isolated and use QAC's direct-child contract. Broader Server conversion remains out of scope for QAC3B.

## QAC3C1 prebuilt x86 launcher — Outcome B (2026-10-21)

The prebuilt-kernel entry point is `scripts/run-qemu-x86.bat`. It now invokes `scripts/run-qemu-x86-prebuilt-qac.ps1`, which reuses `scripts/qac-managed-qemu.psm1` and runs `qac run --project guideXOSServer --mode development -- <QEMU> <arguments>`. Direct PowerShell invocation is also supported:

```powershell
pwsh.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\run-qemu-x86-prebuilt-qac.ps1
```

The default artifact is `kernel/build/x86/bin/kernel.elf`, resolved from the Server checkout regardless of the caller's current directory. `-KernelPath` can select another existing artifact; a relative path is resolved from the Server root, and an absolute path may contain spaces. Before requesting admission, the runner opens the file read-only, validates the ELF32 little-endian Intel 80386 executable header, and prints the resolved path, byte size, and SHA-256. A missing, unreadable, or incompatible artifact fails with a diagnostic. It does not invoke a kernel build, solution build, or replacement-artifact path. After QAC exits, the runner hashes the same file again and reports any change.

On this checkout the selected artifact was `D:\dev\guideXOSServer\kernel\build\x86\bin\kernel.elf`, 2,478,204 bytes, SHA-256 `3566D010B087DD9309593ACAF59D6CF2CD48B15BCBDBC0C39F207AFB0774FC00`. Its on-disk ELF header was read and classified as ELF32 Intel 80386 ET_EXEC. No build or live launcher run occurred in this qualification attempt, so a post-QEMU artifact hash is still pending.

The runner retains the prebuilt launcher's repository-root working directory and the root-relative QEMU kernel argument. It preserves the configured executable `C:\Program Files\qemu\qemu-system-i386.exe`, `-machine pc,usb=off`, `-kernel`, `-m 128M`, `-vga std`, `-serial stdio`, `-display gtk`, `-vnc :0`, `-no-reboot`, `-no-shutdown`, and `-d guest_errors`. No CPU override is supplied. Serial and existing debug output remain on the console streams; the launcher writes no kernel, ESP, shared image, log, or temporary artifact. It depends on the new PowerShell runner and the existing shared QAC argument helper; the batch file is only the entry point.

QAC must already be online for the same Windows user. The resolver honors `-QacCliPath`, then `GUIDEXOS_QAC_CLI`, `qac.exe` on `PATH`, and finally the sibling QAC apphost. Coordinator or CLI failure stops before QEMU and never falls back to the old direct invocation. The request always uses development mode. Existing QAC queue fairness and its 30-minute admission policy apply; Ctrl+C while waiting is handled by QAC as a queued cancellation. Once admitted, the runner waits for the QAC command to verify the exact child exit and release before returning the child's exit code. Closing the QEMU GUI or pressing Ctrl+C retains the existing interactive shutdown behavior. No process-name cleanup, QMP requirement, guest marker, or timeout was added to normal launches.

For bounded qualification only, `-QualificationQmpPort <port>` adds a QMP listener bound to `127.0.0.1`; the runner checks that the port is available first. The option is absent from the default argument list. It is reserved for a future test-owned shutdown when the acceptance lane is available.

The extended launcher contract suite passed **80/80 assertions**. It covers default and explicit path selection, valid and invalid ELF fixtures, missing-kernel failure, unchanged fixture hash, no-build behavior, the development project/mode, QEMU executable and argument forwarding, paths with spaces, coordinator failure/no fallback, queue/cancellation contracts, child launch and exit propagation contracts, unsafe-cleanup guards, and separation from the build-and-run launcher. These checks are deterministic launcher contracts; they do not simulate QAC admission, child process ownership, guest execution, or recovery. The actual batch entry point was invoked with a missing path containing spaces; it returned 1 with “No build or replacement artifact was generated” before coordinator access. A second batch invocation used a nonexistent `-QacCliPath`; after printing the real kernel's path, size, format, and hash, it returned 2 without starting QEMU.

At live preflight, QAC 2.0 was Online, but its census showed an active `guideXOSServer` acceptance reservation `844ca563d93249878a0368490bf0b9f8`, verified owner PID 16352, and managed QEMU PID 19004 (`qemu-system-x86_64.exe`). The reservation remained active at the final check. No development request was submitted, and that session was left untouched. Consequently there is no QAC3C1 development admission, QEMU direct-child identity, guest boot, exit/release transition, queue/cancellation run, or post-run hash evidence. QAC's own census showed no foreign QEMU process and no queued request at the last snapshot.

The QAC WPF GUI process started as PID 20368, but the computer-use window service did not expose a targetable dashboard window. No visual claim is made for the development row, minimized/reopened behavior, or completion transition. The active acceptance session was visible in QAC CLI state only; it is not substituted as QAC3C1 dashboard evidence. The missing dashboard observation and protected acceptance workload leave mandatory live qualification open.

**Outcome B — QAC3C1 implementation and contract checks are complete; QAC3C1 remains open.** The protected acceptance reservation prevented a safe development launch during this attempt. No QAC source, scheduler policy, kernel source, ESP image, Windows service, firewall rule, or foreign process was changed. QAC3C2 and later milestones have not begun.

### QAC3C1R1 prebuilt-kernel live qualification — 2026-10-22

**Outcome A — QAC3C1 accepted.** This record supersedes the preceding QAC3C1 Outcome B after completing its live gates. Server stayed on `main`, starting at HEAD `396a358ec60ae513240c44f5871e95a588b5bc03`, with no tracked modifications and two local commits ahead of `origin/main`. QAC stayed on `main`, starting at HEAD `9a92811bf745200153c0528cf338b8712584d964`, with no tracked modifications. Existing commits and pre-existing untracked artifacts were preserved; QAC source and scheduler policy were unchanged.

The actual prebuilt entry point `scripts/run-qemu-x86.bat` selected `kernel/build/x86/bin/kernel.elf`; the launcher reported that it does not build or replace kernel artifacts. No kernel build or regeneration occurred. The file was 2,478,204 bytes and validated as ELF32, little-endian, ET_EXEC, EM_386. Its SHA-256 before and after launch was `3566D010B087DD9309593ACAF59D6CF2CD48B15BCBDBC0C39F207AFB0774FC00`. The existing `tests/qac_development_launcher_contract_test.ps1` passed **80/80 assertions**, covering prebuilt selection, no-build behavior, QAC-only admission, argument forwarding, queue/cancellation contracts, unsafe cleanup guards, and zero/nonzero exit propagation.

QAC was Online. A test-owned childless acceptance reservation `6560ae4deaa74e259c13b3b4a1f8e56d` provided the bounded fairness barrier. The test-owned development cancellation fixture `a5311434ed7947728eb4987123746c1d` waited at queue position 1 and was cancelled by QAC; it never launched QEMU. The real launcher request, reservation `c3575e3bf6a249ddb6aba9326691a22b`, was `guideXOSServer` / development, queued at position 1 while the acceptance barrier was active, and kept QEMU census at zero. Once the test-owned acceptance fixture was released, that same launcher request was admitted. No other project's reservation was active or touched.

QAC CLI/owner PID 20264 launched `C:\Program Files\qemu\qemu-system-i386.exe` PID 1484 as its verified direct child. QEMU start time was `2026-10-22T21:14:04.3835797Z`. The reservation record grant time was `2026-10-22T21:14:04.3722084Z`; the QAC `ReservationGranted` event was `2026-10-22T21:14:04.3722103Z`. The live command retained the original QEMU machine, kernel, memory, VGA, serial, GTK, VNC, and debug arguments and appended only the explicitly test-scoped QMP listener at `127.0.0.1:44441`. Serial output reported `[KERNEL] guideXOS kernel_main entered` and `[KERNEL] Boot method: Multiboot`. The retained QMP framebuffer capture shows `guideXOS Kernel` and `Framebuffer not available - text mode only`, qualifying the existing text console rather than graphical desktop readiness.

The existing WPF dashboard visibly showed the real `guideXOSServer` development reservation, verified owner, child PID and executable, duration, coordinator status, and managed ownership. The dashboard was minimized and restored while QEMU was running; the same reservation and child remained Running. Test-only QMP `quit` shut down QEMU gracefully. The child exited 0 at `2026-10-22T21:16:28.2672534Z`; QAC then recorded `Managed child exited` and verified release. The runner log reported `QAC managed run completed with exit code 0`, and the dashboard showed completion with empty reservation/QEMU rows and zero sessions. The detached outer `Start-Process` wrapper's own `Process.ExitCode` was not retained, so only the runner result and contract-suite propagation checks are claimed for wrapper exit reporting.

Final QAC status was Online with zero managed, queued, blocked, or foreign QEMU sessions; QEMU and both test listeners were gone. The test-owned fixtures and live reservation were safely cancelled or released. No foreign process, Windows service, firewall configuration, or unrelated repository was modified. Existing QAC2 recovery evidence was reused without restarting the live coordinator. The full evidence package is `docs/evidence/qac3c1r1-live-20261022-141200-842/qualification-summary.md`; dashboard screenshots were inspected during the run but not retained as files.

QAC `.phase` now marks QAC3C1 accepted while overall QAC3 remains open and QAC3C2 is next. No QAC3C2 work began.

## QAC3C2 per-run UEFI artifact isolation

QAC3C2 adds an explicit staging step for the S22R10 PacMan UEFI qualification path. It does not launch QEMU. QAC3C3 will take the returned manifest, request QAC admission, allocate the loopback QMP endpoint, and start QEMU as the direct managed child.

Prepare a workspace with:

    pwsh.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\prepare-qac-uefi-artifacts.ps1 -EspPath .\out\release-profile\<build-id>\ESP -IsoPath .\out\release-profile\<build-id>\artifacts\<candidate.iso> -OvmfPath .\docs\evidence\<qualification>\OVMF-s22-readonly.fd -RunId pacman-smoke

The default scratch root is %LOCALAPPDATA%\guideXOS\QAC\artifacts. -OutputRoot selects a different local volume directory. The helper appends a random GUID to the requested run label, refuses an existing identity, validates the source formats and paths, checks available space, and hashes each source before and after staging. Writable artifact sources must be copied. The read-only ISO and OVMF image may be shared after identity verification. A source that changes during staging causes a retained failed workspace; it is not silently retried or marked complete.

The manifest is manifest.json in the returned workspace. It records the run and owner identities, schema version, source and private file inventories with SHA-256, access and staging modes, workflow, completion status, output paths, and a QEMU argument template. Its qacExecution.status begins as not-started. A consumer must reject incomplete manifests and call Test-QacUefiArtifactManifest before using their paths.

The selected S22R10 workflow uses a directory-backed FAT ESP with QEMU fat:rw. Its ESP copy contains EFI/BOOT/BOOTX64.EFI, kernel.elf, ramdisk.img, and application assets. Each run gets its own copied ESP tree; the test ISO and read-only OVMF image stay at their verified canonical paths. The workflow has no separate pflash VARS image or additional writable guest disk. The NvVars file observed after a prior boot was created in that run's disposable ESP; the canonical source ESP did not contain it. New staged runs therefore start from the 16-file, pre-boot source tree.

The launch template keeps the existing machine, storage, firmware, media, network, serial, and debug categories. Serial, QEMU debug, QMP transcript, and screenshot destinations are relative to the private workspace's output directory. No output file is created in a shared evidence directory. The historical S22R10 evidence used fixed QMP port 65253; the template deliberately leaves the port unbound and requires QAC3C3 to allocate a unique loopback port for each admitted run and hold that endpoint through verified QEMU child exit.

Cleanup is explicit and ownership-checked. Remove-QacUefiArtifactWorkspace requires the full generated run ID, matching owner marker, current Windows owner, a path inside the declared scratch root, and a complete manifest. It refuses active or uncertain execution states; a released run also needs verified child exit. It refuses reparse points and never purges unknown or failed workspaces automatically. Failed workspaces and their diagnostic manifest remain available for investigation.

The helper validates a directory-backed FAT payload's required UEFI paths; it does not build or convert a raw FAT image and does not claim a fresh guest boot. It makes source hashes comparable, but not a transactional filesystem snapshot: another writer that changes an input and restores the exact bytes during the bounded copy window cannot be ruled out. QAC admission still governs execution separately from artifact isolation.

See QAC3C2-ARTIFACT-ISOLATION.md for the collision inventory, manifest fields, qualification evidence, and remaining boundaries.
