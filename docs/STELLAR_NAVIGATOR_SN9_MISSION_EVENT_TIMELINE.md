# Stellar Navigator SN9 — Mission Events and Closest Approach

**Status: Outcome B — partial.** The bounded event analysis, scene integration, synthetic numerical tests, and hosted Release builds pass. Guest qualification and live UI smoke testing remain open because the shared QEMU lane was already occupied. `.phase` remains `SN9`.

## Starting state

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

Before any guest launch, the existing QEMU process list was inspected and an active `qemu-system-x86_64.exe` process was present. No QEMU process was started, stopped, suspended, or otherwise changed. Guest qualification is therefore **blocked by the occupied shared lane**. A live UI smoke test and visual review were not performed.

## Remaining gaps and recommended SN10 scope

- Run the actual Stellar Navigator guest/UI smoke test when the shared QEMU lane is available; review target selection, warning readability, and event-to-epoch navigation.
- Establish and qualify the intended guideXOS Server hosting boundary for the portable scene/time/ephemeris APIs. Keep binary64 trajectory analysis out of freestanding kernel paths unless that runtime explicitly supports it.
- If that host can support the cost and interface cleanly, add a read-only maneuver-versus-baseline comparison and document its analysis window. Keep transfer optimization and flight-navigation claims outside that work.

SN9 remains open until guest qualification and the remaining acceptance gates are addressed.
