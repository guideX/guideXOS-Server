# NativeAOT C181 Managed Application Residency Capacity

**Repository:** D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT
**Branch:** v1.1_DOTNET_SUPPORT
**Starting HEAD:** e3b2457c047481be30255c4be2a6c5a4a9a35e77
**Current classification:** Outcome D as of 2026-10-20 22:01 UTC. QAC has admitted three R1 focused attempts and released all three acceptance leases, but the focused lane has not passed. QAC now reports a managed development reservation and a foreign QEMU process, so the required empty acceptance preflight is not met. The three proof-disabled production boots remain outstanding.

## Decision

Gate A passed for a bounded increase from three to five managed logical application lifetimes. The five production descriptors and selectors already existed; the operative limit was a coordinated three-entry managed lifetime pool, native surface table, and C160 managed-record allowance. Increasing only one table would have caused incomplete snapshots or failed native routing, so C181 raises the shared managed capacity and preserves the existing ABI.

Host qualification and fresh freestanding AMD64 production and proof-control kernel builds pass. Earlier C181 QAC evidence demonstrated five applications resident together, including a Task Manager refresh and Notes close/reopen with a new identity. The first R1 reset attempt used `-no-reboot`, which prevented a second guest boot. Removing that argument produced a QMP `RESET` event and a fresh second input-loop readiness marker in the R1 continuation, but the focused scenario stopped later at Settings resize instrumentation. The production guest sequence has not run. This remains partial evidence, not acceptance.

## Capacity and ownership audit

| Layer | C180 bound | C181 bound | Ownership and dependency |
| --- | ---: | ---: | --- |
| Managed application lifetime objects | 3 | 5 | GuideXosManagedApplicationLifetime owns a fixed five-entry array; one active object per selector, with factory failure and full-capacity rejection handled before assignment. |
| Native managed surfaces | 3 | 5 | g_managedSurfaces is a fixed pointer array sized from the shared snapshot managed-capacity constant. Each initialized adapter owns one current KernelWindow; closing clears its app identity and allows slot reuse. |
| Established application descriptors | 5 | 5 | Static compile-time descriptor array; Notes 4, Settings 5, Calculator 6, Task Manager 7, Explorer 8. No dynamic discovery or sixth app was added. |
| AppManager applications | 16 | 16 | Separate bounded application-manager store; it is not the managed logical-app limit. |
| C160 snapshot records | 20 | 22 | 16 AppManager + 5 managed + 1 shell; the 168-byte C160 v1 record remains unchanged. Fixed storage is 3,696 bytes versus 3,360 bytes at C180. |
| Compositor windows | 16 | 16 | Global window-registration limit. Five managed windows plus the shell use six slots, leaving ten for other windows. Launch fails safely if the global compositor is full. |
| kernel_app.h MAX_WINDOWS declaration | 8 | 8 | Declaration only; the active compositor registration array is the 16-window limit above. |
| Widgets per KernelWindow | 32 | 32 | Fixed widget array within each window; managed apps register into the window they own. |
| Managed action bindings and draw rectangles | 8 each | 8 each | Fixed per-surface arrays; no full-window pixel backing buffer is allocated. Drawing uses the shared framebuffer. |
| NativeAOT host ABI | v4 / 128 bytes | v4 / 128 bytes | Callback offsets 104, 112, and 120 are unchanged. |

The earlier three-app behavior came from more than a single selector switch: the C# lifetime pool and native surface table were each bounded at three, and the C160 snapshot budget also reserved only three managed records. The catalog already contained five fixed descriptors. AppManager's 16 entries and the compositor's 16 windows are independent limits.

Selectors remain Notes 4, Settings 5, Calculator 6, Task Manager 7, and File Explorer 8. C181 preserves 64-bit C160 instanceId values and window IDs. Native surface generation remains 32-bit and monotonic. Managed lifetime generation remains 32-bit and monotonic; the existing userData value carries selector in its low 32 bits and the application generation in its high 32 bits. This adds no ABI field. Callback resolution checks both selector and generation, so a delayed action from a closed lifetime cannot target a later occupant of a reused slot. Generation exhaustion and window-allocation/registration failures return failure rather than wrapping or leaving a partial surface registered.

## Resource requirements

The accepted guest profile remains **1,024 MiB QEMU RAM** with the existing fixed **Primary8MiB** NativeAOT heap. C181 does not raise guest RAM or heap size. Managed application objects use the shared runtime and heap; each retains its own application state, while shared host services, NativeAOT runtime state, scheduler, and thread stack are not duplicated per application. C181 introduces no worker thread, per-app process, scheduler, garbage collector, or dynamic registry.

The exact snapshot buffer grows from 3,360 bytes at C180 (20 × 168) to 3,696 bytes (22 × 168), a bounded 336-byte increase. The complete five-application host constructor measurement is 110,344 bytes; constructors four and five add 29,976 bytes relative to the same three-app fixture. These are hosted .NET allocation measurements and serve as a startup-allocation proxy, not a direct measurement of the guest NativeAOT heap. The guest's fixed heap has headroom relative to this constructor proxy, but runtime document/content allocations still share the existing 8 MiB limit and can fail under ordinary existing exhaustion handling. Window/widget/action storage remains fixed-size; there is no per-window framebuffer copy.

## Implementation

C181 changes:

- kernel/core/include/kernel/application_snapshot.h: managed snapshot capacity 3 to 5 and total records 20 to 22; record version, size, alignment, and fields stay fixed.
- kernel/core/nativeaot_application.cpp: uses the shared five-entry capacity for native surfaces, preserves cleanup on failed window registration, and packs the application lifetime generation into the existing userData token for both initial and resident launches.
- samples/managed/HostLogProof/GuideXos/GuideXosManagedApplicationC150Tests.cs: expands the fixed managed lifetime pool and lifecycle fixtures from three to five.
- samples/managed/HostLogProof/GuideXos/GuideXosApplication.cs and GuideXosLaunchContext.cs: route the high-word lifetime generation to app callbacks and reject stale lifetime lookups.
- samples/managed/HostLogProof/GuideXos/GuideXosManagedApplicationResidencyC181Tests.cs: adds 24 focused cases and seven stress checks over the real five descriptors, bounded allocation, teardown, identity, stale generations, capacity exhaustion, and resize routing.
- samples/host/ManagedUiRegression/Program.cs and project files: include the C181 suite in the complete managed UI host gate.
- Historical managed proof scripts update their C150 expected marker to allow the new five-resident lifecycle result without changing selector assignments or old guest arguments.
- scripts/dotnet/run-c181-managed-application-residency.ps1: runs each lane through qac run --project guideXOSServer --mode acceptance, holds QAC's exclusive reservation for one complete three-reset lane, preserves QEMU arguments and process exit, and writes serial/QMP/screenshots/identities under the ignored out/dotnet/c181-managed-application-residency/guest-runs/ evidence root.

The C180 default-deny shared resize dispatcher and C174 transaction remain untouched. All five concrete applications keep their existing C180 opt-in and app-owned layout/rollback logic. C181 adds host resize routing over all occupied managed slots; it does not refactor app-specific layouts.

## Host and kernel qualification

The complete affected host gate passed **38 suites / 930 cases**. The C180 baseline remains green, including 1,000 resize transactions, 500 alternating requests, 250 rejected requests, 250 cross-app routing/isolation validations, 100 lifecycle cycles, zero hot-path allocation, and zero geometry drift.

C181's focused suite passed **24 cases**. Its seven deterministic stress checks passed:

- 1,000 slot allocation/release operations.
- 1,000 cross-application resize requests (500 routed requests plus 500 isolation validations).
- 250 exhaustion/recovery cycles.
- 250 stale-generation rejections.
- 100 full application lifecycles.
- Zero resize allocation and zero cumulative geometry drift.

Host log: out/dotnet/c181-managed-application-residency/host-regression.log.

Two fresh freestanding AMD64 kernel targets compiled successfully after the final native source changes. The builds use C++14 with -ffreestanding -fno-exceptions -fno-rtti -nostdlib -nostdinc++, no STL or new kernel dependency, and isolated output directories:

- Production, without GUIDEXOS_PROOF_CONTROL: out/dotnet/c181-managed-application-residency/kernel-production-c181/bin/kernel.elf, 2,348,392 bytes, SHA-256 22BC4AFE228263E4187B2AA57EAF21D1CEA46D3992A139F81EC185B1C22154AF.
- Proof-control: out/dotnet/c181-managed-application-residency/kernel-proof-c181/bin/kernel.elf, 2,357,296 bytes, SHA-256 B360905ED40EEDDA32E14851E5E980C7204D427F21ABE200C8D4FBBE61832C63.

The compiler emitted existing unused-parameter warnings in kernel_app.h; both targets linked. This is compile evidence, not a guest acceptance result.

## QAC and guest evidence

QAC was discovered at D:\dev\guideXOS_QAC\src\GuideXOS.QAC.Cli\bin\Debug\net10.0-windows\GuideXOS.QAC.Cli.exe. The installed QAC 2.0.0 coordinator reported Online. The C181 runner uses an **exclusive acceptance lease** and direct QEMU child; it does not modify QAC or introduce a private lock. QAC's direct-child contract rejects wrapper executables, so the PowerShell orchestration passes the QEMU argument list to `qac run --project guideXOSServer --mode acceptance -- <qemu> <original QEMU arguments>`. The lease remains held across all three `system_reset` guest boots in each lane, then is released only after the script requests QMP quit and QAC verifies QEMU exit.

A QAC census showed an unrelated DeveloperStudio acceptance session during preflight and again before later run attempts. Each time C181 stopped at the empty-census gate before starting a QEMU child; the foreign reservation/process was left untouched.

Preserved focused attempts are under out/dotnet/c181-managed-application-residency/guest-runs/:

| Attempt | QAC/guest result |
| --- | --- |
| focused-attempt-01-qac-sequence | QAC exclusive reservation acquired; QEMU rejected quoted -drive protocol path; QAC observed child exit 1 and released. No guest boot. |
| focused-attempt-02-qac-sequence | QAC exclusive reservation acquired; QEMU rejected quoted serial backend path; child exited 1; QAC released. No guest boot. |
| focused-attempt-03-qac-sequence | Guest reached the input manager; an initial CRLF-sensitive readiness matcher timed out. Runner requested QMP quit; QAC verified exit and release. |
| focused-attempt-04-qac-sequence | Guest reached desktop; an initial interaction omitted All Programs. The first menu selection did not launch Settings; runner requested QMP quit and QAC released. |
| focused-attempt-05-qac-sequence | Preflight saw an unrelated active DeveloperStudio acceptance reservation and aborted before QEMU launch. |
| focused-attempt-06-qac-sequence | QAC exclusive lane booted the focused guest. Serial and surface evidence shows Managed Notes and Managed Settings Center simultaneously resident (Notes instance 1, Settings selector 5, surface generation 2, window 0x3E9). Explorer activation did not complete; runner requested QMP quit, QAC verified child exit 0 and released. This is partial evidence only; maximum guest concurrency demonstrated so far is **two**. |
| focused-attempt-07-qac-sequence | Preflight saw an unrelated active DeveloperStudio acceptance reservation and aborted before QEMU launch. |
| focused-attempt-08-qac-sequence | QAC acquired the exclusive acceptance lease. Boot 1 emitted create markers for five distinct managed surfaces. Opening Settings closed Notes as required by the existing Notes-to-Settings transition. The runner reopened Notes (generation 6, window 0x3ED), briefly establishing five live surfaces, then closed it for lifecycle testing. The Task Manager capture taken before Notes was reopened showed four applications, and the subsequent Notes reopen wait timed out. The runner requested QMP quit; QAC observed QEMU exit 0 and released its reservation. This is partial five-instance evidence, not acceptance: no Task Manager refresh validated five rows and no other boots ran. |
| focused-attempt-09-qac-sequence | QAC acquired the exclusive acceptance lease. Boot 1 kept Settings, Notes, Explorer, Calculator, and Task Manager resident in distinct surfaces/windows; Task Manager reported `reg=5`, `n=5`, and a Ctrl+R refresh passed. Notes' explicit close ran the managed lifetime-close callback and destroyed window 0x3EA. The subsequent Start Menu activation did not launch Notes, and the keyboard fallback timed out. The runner requested QMP quit; QAC observed child exit 0 and released the lease. Maximum concurrent guest residency demonstrated is **five**, but the close/reopen lifecycle assertion and the remaining focused boots did not complete. |
| focused-attempt-10-qac-sequence | Stopped before QAC preflight while copying the isolated ESP: D: had insufficient free space. No QEMU launched and no QAC reservation was acquired. The incomplete staging directory was left untouched; all prior guest logs and media remain preserved. |
| focused-attempt-11-qac-sequence (`E:\guideXOS_C181_evidence\guest-runs`) | QAC reservation `75b79e65877a4739a44901e78b70cf52` held the exclusive acceptance lane across the full intended three-boot sequence. Boot 1 passed five-resident Task Manager launch/refresh (`n=5`) and Notes close/reopen with generation 7/window 0x3EE; the follow-up Task Manager refresh still reported five. QMP `system_reset` returned success after boot 1, but the guest did not emit a second input-loop-ready marker within 180 seconds, so boots 2 and 3 did not run. The runner requested QMP quit; QAC verified child exit 0 and released this reservation. QAC immediately admitted a separate `guideXOSServer` acceptance owner; it was not touched. |

Attempt 11's preserved boot-1 evidence includes the full serial log (`BD976B38BBE46C19E749A260204094B07F1B1CB33F7A5DF9C7E08A80B134632C`), QMP event trace (`0DF21ED0A70B1E35AE985DC7266CD3A2A771279E801D09E8C36F42927BDE1584`), instance/window identity JSON (`CC833260D6671939F273EE8A3386418E41F8B452EE81888E443F4D3535B3F38F`), and Task Manager's five-resident screenshot (`8C35EAD8B32A9DB7A0D61AAF368E37BDF58F064FA071EA5C007E9AEC9C4ED093`). QAC stderr records reservation grant, QEMU PID 8572, child exit 0, and release. The run's isolated ESP ramdisk remained identical to the staged source media (SHA-256 `5CC5C0E3D8D9677CE03CA9BBD339D35011970121E2DEF1A4B2C6F6780FD50CF2`); per-run firmware `NvVars` is retained separately.

The revised runner relaunches Notes after the existing Notes-to-Settings handoff, captures the Start Menu around Notes activation fallbacks, and runs Task Manager refresh before the five-resident capture. Boot 1 now verifies close/reopen and five residents. The current focused lane remains incomplete because QEMU's QMP system-reset did not lead to the second ready marker within the 180-second guest timeout. QAC released the exclusive lease normally after graceful QMP shutdown. Attempt 10 stopped while staging the per-run ESP because the repository volume had only 2.3 MiB free; it failed before QAC/QEMU launch. To preserve all existing run evidence and avoid touching protected media, the runner now accepts an optional evidence root. Guest-run evidence for attempt 11 is on E:, while staged source media remains the same protected copy under the repository's ignored out/ directory. The focused three-boot lane must still be completed, followed by the proof-disabled production three-boot lane. No production guest result is claimed. Final outcome stays B until those are complete.

## Protected artifacts and remaining qualification

The pre-existing modified ESP/ramdisk.img was preserved byte-for-byte at its starting SHA-256 AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8 (64 MiB). Existing untracked samples/host/ManagedUiRegression/obj/ was left intact. All C181 staged ESPs and QEMU evidence are under ignored out/; no canonical ESP media was replaced. Historical C166 raw proof serial limitations from C180 remain historical and are not reconstructed as C181 evidence.

Before treating five-instance support as accepted, finish focused boot 1 capacity/reopen checks, boot 2 five-app interaction and resize, boot 3 stress/recovery, and three proof-disabled production guest boots. Each lane must show all five distinct live surface/window identities at the same time, meaningful actions in Settings/Explorer/Notes/Calculator/Task Manager, Task Manager refresh while five are resident, and QAC-confirmed release. Verify production serial contains no proof-only diagnostics and preserve its lane artifacts and hashes.

## Recommended C182 direction

Keep the bounded five-app model. Follow with an acceptance-focused C182 pass that adds a stable guest-side five-instance census to Task Manager evidence, tests one accepted and one rejected resize while all five surfaces are resident, and records bounded runtime content allocations. Do not add process/thread semantics, dynamic registration, or a second runtime.

## C181R1 continuation

The investigation and runner corrections are recorded in [NATIVEAOT_C181R1_RESET_AND_GUEST_QUALIFICATION.md](NATIVEAOT_C181R1_RESET_AND_GUEST_QUALIFICATION.md). The preserved attempt-13 QMP trace establishes that the previous `system_reset` did not reboot the VM: QEMU was launched with `-no-reboot`, accepted the command, reported host-QMP reset `SHUTDOWN` and `STOP`, and never emitted `RESET` or a second guest readiness sequence. C181R1 removes that runner flag and now requires an observed `RESET`, a running QEMU status, and fresh readiness after reset. No kernel or product source was changed.

C181R1's proof-disabled production plan uses three independent QAC-launched cold boots, each from a hash-verified isolated copy. It records each reservation's exact release and permits a later unrelated managed reservation to exist after release. Focused same-process reset behavior is still evaluated by the focused lane; cold-boot results will not be represented as warm-reset evidence.

Boot 3 now exercises Notes edit/save on each freshly reopened generation and checks that no stale-generation dispatch or close rejection was emitted. The guest UI exposes only the five established managed applications, so a sixth live guest launch is unavailable; capacity rejection and recovery remain directly covered by the preserved C181 host suite, while the guest lane measures five residents and verifies 4-to-5 recovery after close/reopen.

The final C181R1 outcome and evidence identifiers are in [NATIVEAOT_C181R1_RESET_AND_GUEST_QUALIFICATION.md](NATIVEAOT_C181R1_RESET_AND_GUEST_QUALIFICATION.md). At 16:00:25 UTC, QAC still showed a verified `guideXOSServer` acceptance reservation (child PID 18800), so the empty preflight required for a new guest run was not met. QAC had reported unmanaged/foreign QEMU PID 26740 at 15:58:57 UTC; that process disappeared from the later census without being touched.

### Latest C181R1 continuation — 2026-10-20 21:57 UTC

QAC has since run three focused attempts and verified release of all three exclusive reservations. Repro14 reached a second boot after QMP `RESET`, passed the Boot 2 app interactions through Task Manager `n=5`, and stopped at a proof-marker assertion because the runner selected the proof-disabled kernel. Repro15 used the proof-controlled kernel but also used the proof-controlled managed app, which requires a C166 selector and left no Notes window for pointer calibration. Repro16 booted the C181 proof kernel with the regular focused app/ramdisk, but pointer calibration timed out because it waited for an app-local marker while the pointer was centered over Task Manager. The runner now uses the framebuffer-center start and records app-local input only as supplementary evidence; that change awaits guest validation.

The foreign QEMU PID 22636 from `D:\dev\guideXOS_SERVER_REXX` exited before repro16. After repro16 released its acceptance reservation, QAC admitted a separate `guideXOSServer` development reservation `9dc21cb291ec4ab5aa5a63f91f216adb` (child PID 3476, `qemu-system-i386.exe`). At 22:01:16 UTC it also reported unmanaged foreign QEMU PID 6156 from `D:\dev\guideXOSServer_REXX\build\rexx\rx20\qemu-81021805facf4277a3e4c73f751f6edf`. Neither process was touched. The QAC census is preserved at `E:\guideXOS_C181R1_evidence\qac-blocked-census-20261020T2200Z.txt` (SHA-256 `96B13F6F4B0BC2A8716F21491D5EA085CAA2FF2ECBB84DAE4D4DD00B54C24A9A`). The full focused three-boot lane and all three proof-disabled production cold boots remain outstanding. See the R1 qualification report for exact reservation IDs, evidence hashes, and gate status.
