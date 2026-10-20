# Stellar Navigator SN9 — Mission Events and Closest Approach

**Current status: Outcome A — SN9 Windows-hosted functionality accepted (2026-10-19).** The Release regression passes all 323 tests, the Release GUI build passes, and live Windows UI smoke tests pass. `.phase` advances from `SN9` to `SN10`. This accepts the Windows Forms host milestone only; it does not qualify a guideXOS Server frontend, native app lifecycle, native rendering or input, or guest boot/UI.

## Acceptance boundary

**SN9 host functionality:** Windows Forms app, SN8 trajectory and maneuver simulation, mission timeline, closest-approach analysis, target selection, and event-epoch navigation.

**Deferred to SN10:** guideXOS Server native frontend, native app lifecycle/window integration, native input/rendering, and actual guideXOS guest boot/UI qualification. Windows-host tests in this report are not guest tests.

## Implementation starting state (historical)

- Required branch: `STELLAR_NAVIGATOR`.
- Starting HEAD: `6314acd6886af5a6a9b84e63dee4c6801ffdc5af` (SN8).
- Starting `.phase`: `SN8`.
- SN9 had not started; no SN9 implementation or completion marker existed.
- The checkout already contained modified tracked `bin` / `obj` build outputs and untracked SN7/SN8 validation directories. They were preserved. All SN9 builds and tests ran from an isolated source copy under `E:\bkup\artifacts\SN9_20261019_01`.
- No branch, worktree, reset, clean, stash, merge, rebase, or cherry-pick operation was performed.

## Reused SN8 interfaces

SN9 leaves the SN8 propagator and maneuver definitions intact. It reuses `SimulatedTrajectory`, `TrajectorySample`, `ManeuverExecution`, `TrajectoryGenerator`, and each sample's `SegmentIndex` and pre/post maneuver kind. The analysis runs after `SceneBuilder` generates the trajectory and is attached to that scene's `TrajectoryVisualization`.

Target positions are read through the existing ET-native ephemeris provider. The selected target comes from the existing celestial-object definitions. The GUI target selector follows map/object selection, and selecting a timeline item sets the existing ET navigation controls to that event epoch.

## Event and ordering rules

The timeline covers the currently sampled trajectory window. It contains simulation start and end, scheduled maneuvers in the window, closest-approach candidates to the selected body, and warnings for incomplete or unsupported calculations. Enabled impulses are marked `Applied` only when SN8 reports a segment change; disabled entries are marked `Disabled` and retain their requested Δv magnitude.

Events sort by epoch, then by this stable type order for ties:

1. Simulation start
2. Maneuver
3. Closest approach
4. Warning
5. Simulation end

Equal-time events of the same type sort by object ID and then stable event ID, both ordinally. The ordered event list is capped at 64 entries. Up to 32 closest-approach candidates are shown; if more exist, the candidates with the smallest sampled distances are retained and an omission warning is added.

## Frames, units, and closest-approach method

Trajectory positions and velocities are Sun-relative in J2000-aligned axes. The selected target and Sun are queried at the same ET as barycentric J2000 states; subtracting the Sun state produces a Sun-relative target state in the trajectory axes. The relative spacecraft-to-target position and velocity are then differenced. Distances use metres, relative speeds use metres per second, and epochs are ET/TDB seconds past J2000.

The analyzer checks target coverage, returned frame, returned epoch metadata when present, and finite SI values. Samples are partitioned by trajectory segment. Invalid samples and failed ephemeris queries split a segment into separate valid runs; no interval is joined across such a break or across an enabled maneuver boundary. Window and segment endpoints are eligible closest-approach candidates.

Interior local minima are first identified from adjacent valid samples. At most 16 candidates are refined, using a golden-section search with at most 64 iterations. Every trial evaluates the SN8 propagator and ET ephemeris directly; no target-position interpolation is used. Refinement stops when the bracket is at most `max(1e-6 s, 2e-15 × |ET|)` or binary64 time resolution prevents further progress. A result is marked `refined` only when the bounded search finds a smaller distance than the sampled candidate. Otherwise the sampled epoch and distance remain labeled `sampled`.

The maximum valid sample gap is reported as a resolution warning. Minima narrower than the sampling interval, multiple minima within one sample bracket, or minima not bracketed by the samples may be missed. The 32-candidate and 16-refinement caps also limit completeness. These limits describe the implementation, not a guarantee of continuous global-minimum discovery. Results remain idealized, approximate SN8 simulation values and are not flight-certified navigation predictions.

## UI changes

The existing **Trajectory & maneuvers** tab now includes a closest-approach target selector and bounded mission-event list. Rows show event type/name, ET/TDB epoch, target, distance in kilometres, relative speed in metres per second when unambiguous, maneuver application status, and sampled/refined status. Warnings include incomplete coverage, invalid data, insufficient samples, and sample-resolution limits. Selecting an event moves the existing epoch navigation controls to its ET.

The list is rebuilt with each accepted scene generation. A changed maneuver or target therefore receives a newly computed timeline, and a superseded asynchronous scene cannot publish stale events through the existing generation gate. The read-only no-maneuver baseline comparison was deferred; no optimizer was added.

## Validation evidence

Commands ran against the isolated copy at `E:\bkup\artifacts\SN9_20261019_01\source\Apps\StellarNavigator`:

- From `StellarNavigator.Tests`: `dotnet test StellarNavigator.Tests.csproj --configuration Release --verbosity minimal` — **323 passed, 0 failed, 0 skipped**. This includes the inherited SN8 trajectory/maneuver tests and all existing ephemeris, time, frame, scene, and CSPICE fixture tests. New fixtures cover analytic closest approaches, boundary minima, multiple minima, equal-time ordering and repeatability, maneuver boundaries and status, ephemeris gaps, invalid samples and frames, zero/large distances, long intervals, target changes, and recomputation.
- From `StellarNavigator.Gui`: `dotnet build StellarNavigator.Gui.csproj --configuration Release --verbosity minimal` — **passed, 0 errors, 0 warnings**.

The first isolated test compilation reported the inherited xUnit2002 warning at `StellarNavigator.Tests/EphemerisTests.cs:51` for `Assert.NotNull` on a value tuple. That unrelated line was not changed; subsequent incremental test runs were warning-free. No kernel or freestanding runtime source changed, so a kernel-target compile was not applicable; the production .NET scene and Windows GUI targets compiled in the isolated copy.

Before the earlier guest attempt, the QEMU census found an active `qemu-system-x86_64.exe` process. No QEMU process was started, stopped, suspended, or otherwise changed. Guest qualification was blocked by the occupied shared lane; a live UI smoke test and visual review were not performed.

## Historical guest-only qualification attempt (superseded by SN9R2) — 2026-10-19

### Checkout and candidate identity

- Closeout starting and ending HEAD: `d5984403afe02b9f9ad8a4ff5f76da0924ac22d5`; branch: `STELLAR_NAVIGATOR`; `.phase`: `SN9` throughout.
- The worktree had 239 pre-existing status entries, consisting of modified tracked `bin` / `obj` outputs and the existing untracked `.sn7-validation`, `.sn8-validation`, and benchmark Debug output directories. They were preserved. No source change or commit was made.
- **Guest candidate: none.** `StellarNavigator.Gui` targets `net10.0-windows` and uses Windows Forms. The SN7 report describes it as a Windows demonstration host; the SN8 report defers the guideXOS-native presentation adapter and server hosting boundary. A repository scan found no Stellar Navigator launcher or manifest wiring in the guideXOS Server integration outside the app projects and documentation.
- The current Windows host-only Release artifact was built in the isolated checkout-byte staging copy at `E:\bkup\artifacts\SN9_closeout_checkoutbytes_d5984403`. `StellarNavigator.Gui.exe` SHA-256: `E1265F98684D8F8894AE9B211B0A864184A1782702497BBFEA30580DE4ACBBF4`; `StellarNavigator.Gui.dll` SHA-256: `1D1260A60941066EA00B517A93AE2361A5C0FC499712F2EFF095F070D14A4D72`. These identify the host demo only and were not presented as a guest candidate.

### QEMU ownership and QAC status

- The direct process census found one QEMU process: PID `1872`, parent PID `27080`, executable `C:\Program Files\qemu\qemu-system-x86_64.exe`, owner `LEON2_RDT\guideX`, started `2026-10-18 15:10:51 -07:00`. Its command line identifies `guideXOS-R2-allocator-locator-interlifetime-gap-20261018`, uses the R2 task-manager qualification OVMF/ESP/serial/debug paths under `C:\gxR3work\out\taskmanager-memorydetails-qualification-r3`, and specifies `-display none`. This process is foreign to SN9.
- No QAC coordinator process or QAC named pipe was present; the QAC GUI alone was running as PID `26244`. The QAC repository reports QAC1 and QAC2 accepted, but its QAC2 validation explicitly records live acceptance admission as deferred while PID `1872` is foreign. We therefore used direct ownership checks and did not request a reservation or start another QEMU session.
- PID `1872` was left untouched. No QEMU was started, stopped, replaced, suspended, or signaled by this qualification attempt.

### Current non-guest validation

- A byte-preserving copy of the current checkout was staged under `E:\bkup\artifacts\SN9_closeout_checkoutbytes_d5984403`, excluding existing build and validation directories. `dotnet test StellarNavigator.Tests.csproj --configuration Release` passed: **323 passed, 0 failed, 0 skipped**. This includes the SN8 trajectory and maneuver tests and SN9 deterministic ordering, closest-approach boundary, maneuver-boundary, ephemeris-gap, invalid-input, and scenario-recomputation fixtures. The clean test build emitted the inherited xUnit2002 warning at `EphemerisTests.cs:51`; no source was changed for it. The structured TRX result is `E:\bkup\artifacts\SN9_closeout_checkoutbytes_d5984403\test-results-trx\SN9-closeout-323.trx`.
- `dotnet build StellarNavigator.Gui.csproj --configuration Release` passed: **0 warnings, 0 errors**. This verifies the Windows demo build only.
- An initial test attempt from `git archive` produced 62 asset-integrity failures because that archive changed the exact bytes of two authenticated JSON assets. The rerun from checkout bytes preserved their expected SHA-256 digests and passed all 323 tests. No repository assets were changed.
- No SN8/SN9 regression was found in the passing host suite. No live application or guest UI interaction was performed. The computer-use inventory exposed no native app/window surface, and the foreign QEMU process is headless (`-display none`); therefore there are no live UI screenshots, guest serial logs, or guest structured run records from this attempt.

### Gates recorded for native guest integration

- Provide an isolated QEMU acceptance lane after the foreign R2 session has ended, with an operational coordinator/reservation grant or a clear direct-ownership-exclusive census.
- Establish and stage the guideXOS Server/native presentation integration for Stellar Navigator. The current Windows Forms executable is not a guideXOS guest candidate.
- In that guest, verify launch, SN8 trajectory/maneuver behavior, SN9 target selection, chronological event order, event-to-epoch navigation, closest-approach values and units, maneuver-driven invalidation/recomputation, empty/invalid/unsupported ephemeris intervals, and close/reopen lifecycle. Capture actual screenshots and logs where guest tooling supports them. Until a visible guest surface is available, UI interaction must be performed against that guest by an available automation surface or a human operator; hosted tests do not close this gate.
- At that historical closeout, SN10 had not started, Outcome B applied, and `.phase` remained `SN9`.

## Native integration gaps carried into SN10

- Run the actual Stellar Navigator guest/UI smoke test when the shared QEMU lane is available; review target selection, warning readability, and event-to-epoch navigation.
- Establish and qualify the intended guideXOS Server hosting boundary for the portable scene/time/ephemeris APIs. Keep binary64 trajectory analysis out of freestanding kernel paths unless that runtime explicitly supports it.
- If that host can support the cost and interface cleanly, add a read-only maneuver-versus-baseline comparison and document its analysis window. Keep transfer optimization and flight-navigation claims outside that work.

These are deferred native-integration gates. They do not change the completed SN9 Windows-host acceptance recorded below.

## SN9R2 Windows-host qualification and scope correction — 2026-10-19

### Checkout and preservation

- Branch: `STELLAR_NAVIGATOR`.
- Starting HEAD: `d5984403afe02b9f9ad8a4ff5f76da0924ac22d5` (the expected SN9R2 HEAD).
- Starting `.phase`: `SN9`.
- Before qualification, `git status --porcelain` had 240 entries: 239 existing build-output/validation-data entries plus this uncommitted SN9 report. The 239 entries were preserved. Release test/build outputs and UI probes were written under `C:\Users\guideX\.codex\artifacts\SN9R2_20261019`, outside the repository.
- No branch switch, stash, reset, clean, merge, rebase, cherry-pick, worktree change, or push was performed.
- SN7 and SN8 acceptance documentation was reviewed. SN7 identifies the Windows map as a hosted implementation and says it does not implement native guideXOS UI. SN8 defers the native presentation adapter and server hosting boundary.

### Host architecture

The implementation present at SN9R2 contains a `net10.0-windows` Windows Forms GUI that consumes portable `net8.0` astronomy, ephemeris, navigation, and scene projects. SN9 adds mission-event analysis after SN8 trajectory generation and presents its results in the existing host GUI. This is a Windows-hosted application architecture; it is not currently a guideXOS-native application frontend.

The tested host features are:

- Windows Forms application and the existing SN8 trajectory/maneuver simulation.
- Mission timeline and closest-approach analysis.
- Closest-approach target-body selection.
- Event selection that sets the host epoch controls to the event ET.
- Recalculation after target or maneuver changes.

The following were not exercised as guideXOS features because no native Stellar Navigator app is implemented: guideXOS Server launch integration, native window lifecycle, guest input/rendering, and guest boot/UI. No host result in this report is labeled or treated as guest qualification.

### Automated regression and Release build

- Release regression: **323 passed, 0 failed, 0 skipped**. This repeats the SN8/SN9 test suite and includes the existing trajectory, maneuver, mission-event ordering, closest-approach, ephemeris-gap, and recomputation cases.
- GUI Release build: passed with **0 warnings and 0 errors**.
- The clean test compilation emitted the inherited xUnit2002 warning at `StellarNavigator.Tests/EphemerisTests.cs:51`; it concerns `Assert.NotNull` on a value tuple and was not changed because it is unrelated to SN9 qualification.
- Structured test result: `C:\Users\guideX\.codex\artifacts\SN9R2_20261019\test-results\SN9R2-Release-323.trx`.
- GUI build log: `C:\Users\guideX\.codex\artifacts\SN9R2_20261019\SN9R2-GUI-Release.log`.

### Live Windows UI smoke test

The built Release `StellarNavigator.Gui.exe` was launched and inspected through visible Windows UI automation. The following checks passed:

1. The application opened and rendered its default solar-system scene with 10 ephemeris objects.
2. SN8 trajectory controls were available and the host reported its two-body trajectory, sample count, and simulation-only quality.
3. The closest-approach target was changed from Earth to Jupiter and back. The details followed the selection, and each accepted target change rebuilt the event list.
4. Timeline events appeared in deterministic epoch order, with stable tie ordering. Returning to Jupiter reproduced the same seven-row event list without stale Earth rows or duplicates.
5. Selecting the refined Jupiter approach set the epoch field to `3220824.2363945004` ET seconds and the displayed state reported that same epoch.
6. Closest-approach rows displayed distance in km and relative speed in m/s. Before maneuver insertion, the refined Jupiter result was approximately `5.99275E+08 km` at ET `3220824.236 s`, with relative speed `17471 m/s`.
7. An enabled `SN9R2-ui-check` maneuver at ET `1000000 s` with Δv `(25, 0, 0) m/s` appeared as `Applied`. The sample count increased to `52/512`; the refined Jupiter approach changed to approximately `5.99229E+08 km` at ET `3224873.714976245 s`, with relative speed `17457 m/s`.
8. A missing ephemeris file produced a visible “Scene not updated” warning, left the object list empty, and disabled trajectory/frame controls without crashing. A checksum-invalid ephemeris produced the trusted-manifest mismatch warning with the same controlled behavior. Both probes used external copies of the build artifact.
9. Target changes did not accumulate stale events.
10. The app closed and reopened from the Release artifact. The reopened instance returned to ET 0 and reported a ready 10-object scene.

Screenshot captures are actual Windows app captures. The UI code does not provide a persistent application-log sink; the status-banner warnings and observations were recorded in `C:\Users\guideX\.codex\artifacts\SN9R2_20261019\evidence\SN9R2-host-ui-observations.log`. The machine-readable TRX and GUI build log are listed above. Screenshots:

- `C:\Users\guideX\.codex\artifacts\SN9R2_20261019\evidence\SN9R2-host-runtime.jpg`
- `C:\Users\guideX\.codex\artifacts\SN9R2_20261019\evidence\SN9R2-host-timeline-maneuver.jpg`
- `C:\Users\guideX\.codex\artifacts\SN9R2_20261019\evidence\SN9R2-missing-ephemeris.jpg`
- `C:\Users\guideX\.codex\artifacts\SN9R2_20261019\evidence\SN9R2-invalid-ephemeris.jpg`

### SN10 technical investigation

- **Portable computation:** `Astronomy.Core`, `Astronomy.Time`, `Astronomy.Ephemeris`, `Navigation.Core`, and `StellarNavigator.Scene` target `net8.0` and contain the vector/state math, indexed ephemeris queries, two-body propagator, trajectory generation, mission-event analysis, and platform-independent scene models. They are portable across .NET hosts, but they are managed .NET assemblies.
- **Windows Forms dependencies:** `StellarNavigator.Gui` targets `net10.0-windows` with Windows Forms. `MainForm` owns controls, asynchronous UI work, selection, and diagnostics; `SolarMapControl` uses Windows drawing APIs. These classes cannot be reused as guideXOS native widgets.
- **Native interface fit:** `sdk/include/guidexos/abi.h` exposes the `gx_main(gx_app_context*)` entry point and host calls for window creation, text/rectangle drawing, frame presentation, event polling, logging, and file reads. `file_read` accepts an offset, so it can support a bounded random-access ephemeris reader. The interface can host a ported C/C++ computational core, but it cannot directly load the existing managed .NET assemblies without adding a .NET runtime or translating the code. The SDK sample documentation says Native ELF execution is experimental and the default hosted runtime stops at the executor gate before app code runs; SN10 must establish a runnable native test lane first.
- **Ephemeris access:** the required indexed DE440s file is `32,117,373` bytes and is validated against the trusted SHA-256 `d52b91573c29e6a4e5b6dc19c4af4d9747f6da2122f5b3aff4503684a96d2c0a`. The managed provider hashes the asset at open, reads coefficient records on demand, and uses a default 4 MiB LRU cache. A native reader should use the app file-read API in bounded chunks, perform the same trusted-manifest validation, and avoid loading the full file into a single buffer. The host also bundles a small authenticated solar-GM asset; leap-second and Earth-orientation assets are only needed for their respective optional time/frame features.
- **Memory and work bounds:** the live Windows host showed roughly 61–84 MiB process working set and a configured 4 MiB coefficient-cache budget. These are host measurements, not guideXOS resource guarantees. Existing computation bounds include 512 trajectory samples, 8 maneuvers, 64 timeline events, 32 closest-approach candidates, 16 refinements, and 64 iterations per refinement. A native port should preserve explicit limits and establish its own byte and allocation budgets.
- **Native GUI approach:** the inspected ABI provides primitive text/rectangle drawing, optional pixel-frame presentation, and polled input events. A small app-owned drawn interface is the direct fit: a fixed ET field, bounded body selector, scene viewport, and details/event panel. No WinForms control bridge is present in the inspected C ABI.
- **Smallest realistic first integration:** enable a dedicated amd64 Native ELF acceptance lane and build a read-only Stellar Navigator app shell that creates a window, redraws on paint, handles selection/input and close, and logs through the C ABI. Add one fixed-ET authenticated body-state query using bounded random-access reads. Defer the full trajectory editor, mission timeline, broad frame/time modes, and full core port until this seam is qualified.
- Keep this slice in the native app/runtime boundary rather than the kernel. If a later change must enter a freestanding/kernel target, prohibit unsupported `std::`/STL dependencies, exceptions, RTTI, and unsupported allocation; preserve deterministic bounded behavior and validate the actual kernel-target compilation.

### Acceptance and phase

**Outcome A — SN9 Windows-hosted functionality accepted.** Automated regression, Windows Release build, and live Windows UI smoke tests pass. Architecture and qualification limits are explicit; native guideXOS integration and guest qualification remain deferred. `.phase` advances to `SN10`.

## Supplemental SN9R3 host validation — 2026-10-19

This run added host-side UI and regression evidence. Its attempt to make guideXOS guest qualification an SN9 acceptance gate was a scope regression: native integration and guest qualification belong to SN10 and do not reopen the accepted SN9 Windows-hosted milestone.

### Baseline and preservation

- The requested expected starting HEAD was `d5984403afe02b9f9ad8a4ff5f76da0924ac22d5`, with `.phase` at `SN9`. The actual checkout started at `c7993bc6bc536a2c673839e20af8c8dd4eb93253`, branch `STELLAR_NAVIGATOR`, `.phase` `SN10`. The actual HEAD is the later `Accept SN9 Windows host qualification` commit; its parent is the requested HEAD and its changes are limited to `.phase` and this report.
- There were 239 pre-existing status entries at closeout start, consisting of modified build outputs and untracked validation data. They were left in place. The final normal porcelain count is 241: those 239 entries plus this report and `.phase`. No source code was changed, and no branch switch, reset, clean, stash, merge, rebase, cherry-pick, or push was performed.
- Test and build work ran from a byte-preserving copy under `C:\Users\guideX\.codex\artifacts\SN9R3_20261019\source\Apps\StellarNavigator`; generated output and logs stayed outside the repository.

### Guest candidate and QEMU ownership

- **Guest candidate at the time of this run: none.** `StellarNavigator.Gui` is a Windows Forms application targeting Windows. The repository also had a managed .NET console demonstration, but no Stellar Navigator guideXOS-native frontend or package had yet been started. This was SN10 work, not an unmet SN9 acceptance gate.
- A read-only census of all Windows processes named `qemu*` found **zero QEMU processes**. No QEMU was launched or changed. The separate guideXOS QAC repository records QAC2 acceptance (88/88 checks, including real QEMU lifecycle qualification), but this host had no `qac` CLI on PATH, coordinator process, QAC named pipe, or QAC service at census time. Direct process ownership checks were therefore used; no reservation was available to request.
- Structured census: `C:\Users\guideX\.codex\artifacts\SN9R3_20261019\evidence\SN9R3-qemu-census.json`. No guest boot, serial log, guest screenshot, or guest run record exists because there is no candidate to boot.

### Live Windows UI smoke — host only

- The interactive smoke used the staged Windows host executable `C:\Users\guideX\.codex\artifacts\SN9R2_20261019\bin\StellarNavigator.Gui\release\StellarNavigator.Gui.exe`, SHA-256 `8F3E6A297E0CA2D6A8218BC8012F4F4A1C9AF44E2B7F005D31154C5AA003198F`; its DLL SHA-256 is `DA1531C0881B9B9B570288EC28A7A06A9E78859B34E2D42F00A6D0566DEBD4C6`. The host is built from the same application source as the current HEAD; the later commit changes documentation and phase only.
- Visible Windows UI automation passed these checks: launch and ready 10-object scene; SN8 trajectory rendering; actual Earth-to-Jupiter dropdown selection; rebuilt six-row Jupiter timeline in chronological ET order; distance in km and relative speed in m/s; event selection setting the epoch and displayed state to `3220824.2363945004` ET; an enabled `SN9R3-ui-check` maneuver at ET `1000000` with Δv `(25, 0, 0) m/s` appearing as Applied and changing the refined Jupiter result from about `5.99275E+08 km` / `17471 m/s` to `5.99229E+08 km` / `17457 m/s`; controlled refusal of an ET of `1000000000000` outside DE440s coverage `[-4734072000, 4735368000]`; rejection of `NaN` with a finite-ET diagnostic; and clean close/reopen to a ready scene at ET 0.
- Existing host screenshots remain available at `C:\Users\guideX\.codex\artifacts\SN9R2_20261019\evidence\SN9R2-host-runtime.jpg`, `SN9R2-host-timeline-maneuver.jpg`, `SN9R2-missing-ephemeris.jpg`, and `SN9R2-invalid-ephemeris.jpg`. The missing-file capture shows an empty scene with a controlled warning; the checksum-invalid capture shows the manifest mismatch warning. New live observations are recorded at `C:\Users\guideX\.codex\artifacts\SN9R3_20261019\evidence\SN9R3-live-ui-observations.log`. The UI helper displayed current screenshots during automation but did not expose a screenshot save operation. All of this is host evidence only.

### Regression and numerical evidence

- Release regression rerun: **323 passed, 0 failed, 0 skipped**. The suite includes the inherited SN8 trajectory/maneuver checks and SN9 analytic closest approach, window and maneuver boundaries, deterministic equal-time ordering, target and maneuver recomputation, ephemeris gaps, invalid/non-finite samples, unsupported target/frame, extreme finite distances, and insufficient-sample handling. No regression was found.
- The clean test compile emitted the inherited xUnit2002 warning at `StellarNavigator.Tests/EphemerisTests.cs:51` for `Assert.NotNull` on a value tuple. No source change was made for that unrelated warning.
- Windows GUI Release build passed with **0 warnings and 0 errors**. Logs are `C:\Users\guideX\.codex\artifacts\SN9R3_20261019\test-results\SN9R3-Release.trx` and `C:\Users\guideX\.codex\artifacts\SN9R3_20261019\SN9R3-GUI-Release.log`.
- Numerical values remain idealized, approximate SN8 simulation results. The analyzer’s sampling, candidate, refinement, and binary64 resolution limits described above still apply; results are not flight-certified navigation predictions.

### SN10 handoff

- Establish the guideXOS-native Stellar Navigator presentation and launch boundary, then build and stage an actual guest candidate.
- Qualify that candidate in an exclusive managed or directly verified QEMU lane. Guest qualification is an SN10 gate. Host UI and hosted tests remain host evidence.
- The Computer Use helper supported direct mouse/keyboard automation for the Windows host, so no human interaction was needed for the host smoke. There is no guest window or guest input surface to automate yet.
- The SN9R3 closeout had proposed setting `.phase` back to `SN9`, despite confirming that its starting HEAD `c7993bc6bc536a2c673839e20af8c8dd4eb93253` already accepted SN9 and set `.phase` to `SN10`. That proposed rollback is superseded; the phase marker remains `SN10`.
