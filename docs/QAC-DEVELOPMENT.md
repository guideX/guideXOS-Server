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

## Future adoption

QAC3C can assess `scripts/run-qemu-x86.bat` and the serial-marker UEFI smoke runners after their build outputs and shutdown paths are isolated and use QAC's direct-child contract. Broader Server conversion remains out of scope for QAC3B.
