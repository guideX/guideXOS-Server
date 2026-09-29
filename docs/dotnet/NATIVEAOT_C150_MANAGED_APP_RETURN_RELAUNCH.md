# NativeAOT C150 — Managed Application Return and Relaunch

## Result

**Outcome A — bounded Managed Notes return and fresh App Model relaunch.** C150 closes the C149 surface boundary by saving one canonical caller identity, destroying Settings Center, and launching a new Notes instance through the ordinary built in App Model path. This is relaunch after teardown; it is not live repaint or instance resume.

The feature keeps one active managed application and one native managed surface. It adds no host ABI entries, multitasking, application stack, or additional focus, ownership, or capture system. The host ABI remains v1/table 104 and the settings file remains version 2.

## Existing lifecycle and C149 boundary

The production launcher resolves a built in application identity, looks up its managed selector and composite image, establishes the launch context and host capabilities, then calls the production NativeAOT composite. That composite owns a single active managed application object. Its managed surface is a single kernel window whose controls and actions are registered through the existing host table.

When a new app opens a managed surface, `NativeAotManagedSurface::open` closes its current window before registering the new one. The compositor unregisters the old window and removes it before the next managed surface appears. Thus the effective model is one active managed application with one active graphical surface, not several concurrent app surfaces.

C149 established that Settings Center Apply publishes shared runtime settings, but the currently open Notes window cannot repaint after its surface has been destroyed. C150 adds one bounded return launch around that existing handoff.

## Return contract

The return target is a fixed 96-byte native character buffer containing one canonical built in `appId`. It holds at most one identity and does not retain a managed object, raw pointer, surface address, registration ID, or dynamic collection. `Settings Center` itself, unknown/unresolvable IDs, malformed IDs, and a second acquisition while occupied are rejected. The occupied Notes identity survives Settings Center's lifetime and is cleared when consumed.

Notes invokes Settings Center using managed action 24 from its Options area. After the managed action succeeds, the native adapter verifies the active Notes identity, Notes surface, catalog records, selectors, and empty return slot. It copies the canonical Notes `appId` before it asks the existing desktop App Model to launch Settings Center. That launch uses `desktop::launch_app_with_context`; it does not call a managed entry point directly. A failed Settings Center launch clears the target and returns to the shell if the Notes surface was already replaced.

Direct Settings Center launch begins with no target. An unrelated managed app launch clears any stale target. The preferred nested-invocation policy is enforced: only one return hop is supported, and a second target acquisition is rejected without replacing the first.

On a Settings Center close with a target, the surface marks a return pending. `KernelApp::requestClose` unregisters and deletes the window, sets its owned window pointer to null, and then reports `onWindowClosed`. After the managed callback has returned and the active host context is clear, C150 takes and clears the target before resolving and launching it. Resolution or launch failure falls back to the shell without retrying. One close can produce at most one return.

The returned app uses the ordinary launcher again, including catalog validation, launch context, capability setup, lifecycle initialization, normal control registration, and normal fresh focus setup. Managed app descriptors use factories. The lifetime holder clears its prior active reference before invoking the next factory, so Notes A is not resumed and its managed controls, caret, selection, scroll position, focus, and unsaved edits are not restored. The return carries application identity only.

## Settings and close behavior

- Clean Close returns to Notes when a Notes target is armed. A direct launch closes with no Notes launch.
- Apply persists the working snapshot, publishes it to the shared runtime settings, then Close returns to Notes.
- Discard closes and returns while leaving the prior applied and persisted snapshot in effect.
- Cancel keeps Settings Center open and retains the target; a later successful close returns once.
- Reset and its confirmation dialog do not consume the target. Only closing Settings Center returns.
- Failed persistence keeps Settings Center open, the target armed, and the old runtime and persisted snapshots intact. A successful retry followed by Close returns once.
- Returning to Notes does not write the settings file. `NaturalScroll`, `ScrollLinesPerNotch`, and `ShowKeyboardTips` continue to come from the shared runtime snapshot.

The C150 proof uses the existing Notes document in its safe initial state. It does not attempt to preserve unsaved Notes edits. Document-session persistence remains separate future work.

## Focused checks and production evidence

The in-kernel return-target suite covers empty state, set/copy isolation, occupied-slot rejection, invalid and unresolved IDs, self-target rejection, take-and-clear, repeated take, explicit clear, and catalog resolution (10 cases). The managed lifetime suite covers one active instance, fresh factory instances, selector matching, replacement, ignored unrelated clear, idempotent clear, and generation advancement (8 cases).

The physical QEMU proof begins with a direct Settings Center launch and verifies it closes without launching Notes. It then launches Notes, performs 25 production-equivalent Notes → Settings Center → Notes clean-close cycles, and checks each cycle advances the application and surface generations by two, leaves one window, leaves no return target, and ends in Notes. It opens Settings Center once more for the production workflow in that boot. Serial assertions require at least 25 armed, consumed, and completed return markers, the direct-launch marker, the stress summary, fresh Notes instance evidence, and the Settings Center-destroy / Notes-create ordering.

The three production workflows exercise v1 read-only verification followed by v1-to-v2 Apply, dirty-close Discard/return without a settings-file rewrite, and Reset Cancel followed by dirty-close Cancel, an injected persistence failure, and a successful Apply retry/return. The first workflow applies `ShowKeyboardTips=false`; the second verifies Discard preserves the prior false value; the third applies `true`. Each full C150 boot also runs the C148/C149 startup and Notes consumer checks and C146 persistence suites supported by that boot. C146–C149 allocation-heavy focused suites run once on the first Settings Center launch of a boot; C150-wide flags prevent rerunning them on each repeated Settings Center instance.

| Evidence | Result and scope |
|---|---|
| Return target tests | 10 focused cases, as recorded by `C150-RETURN-TARGET-TESTS` |
| Managed lifetime tests | 8 focused cases, as recorded by `C150-MANAGED-LIFECYCLE-TESTS` |
| Direct Settings Center launch | Must show no target and no Notes launch after close |
| Repeated cycles | 25 clean-close cycles in each full UI proof boot; generation, window count, target, and active app checked each cycle |
| Sequence 1 | Read authentic v1 without rewriting it, then Apply `ShowKeyboardTips=false`; returned Notes hides its keyboard-tip row |
| Sequence 2 | Edit `ShowKeyboardTips=true`, then Discard; returned Notes retains `false` and the settings-file hash |
| Sequence 3 | Reset Cancel, edit `true`, dirty-close Cancel, injected Apply failure and dismissal, then successful retry; returned Notes shows the applied `true` value |
| Settings continuity | Existing C148/C149 checks cover NaturalScroll, ScrollLinesPerNotch, ShowKeyboardTips, runtime publication, and Notes consumers |
| C145/C146 workflows | Reset Cancel, dirty-close Cancel and Discard, persistence failure/dismissal/retry, Apply, and close paths are exercised through the production Settings Center UI; the standalone C145 suite is historical |
| Historical evidence | Earlier-phase artifacts not explicitly listed as current reruns remain historical evidence |

The NativeAOT composite uses a fixed **4 MiB** image-backed no-collection allocation heap for C150. The repeated fresh app graphs and the C146–C149 focused suites share this bounded arena; unreachable objects are not held by the managed active-app reference, but this runtime configuration does not reclaim their allocation space. The larger fixed bound is required for the 25-cycle proof. Earlier phases keep their existing heap configurations.

The proof runner saves the C150 composite ELF and proof kernel variants, restores the canonical kernel and ESP kernel, verifies the protected ramdisk, and performs three ordinary boots. The generated C150 manifest records artifact hashes, serial hashes, sequence outcomes, ordinary boot results, and restoration status.

## Evidence classification

Current reruns are the C150 return-target and managed-lifetime focused suites, the direct-launch check, each completed 25-cycle stress run, the three production persistence sequences, the startup compatibility matrix, and three ordinary boots. C146–C149 focused suites are current where their markers appear in the C150 serial logs. The C145 dialog paths are integrated in the Settings Center suite; standalone C145 results and earlier phase artifacts are historical unless separately named above.

## Deferred boundaries

C150 does not add simultaneous applications, multiple managed surfaces, inactive-app repaint, suspension or resumption, a navigation history, a task switcher, application-state serialization, background apps, IPC, or a general activation service. It preserves one return identity and recreates one fresh managed application through the existing App Model.
