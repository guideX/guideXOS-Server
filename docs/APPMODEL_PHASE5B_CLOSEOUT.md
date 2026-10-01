# App Model Phase 5B Closeout

Date: 2026-10-01
Result: **Outcome A — App Model v1 is ready**

## Starting state and baseline

- Starting commit: `22dff15679a42bb2016bb7ce696a8fd449450dac`
- Branch: `main`, initially 2 commits ahead and 0 behind `origin/main`
- Canonical command: `.\scripts\smoke-appmodel-phase5b-regression-closeout.ps1`
- Baseline result: failed at `Missing expected text for phase 5B status ready: appModelV1StatusReady=true`; published status was `not-ready`
- Baseline preview: 96 records; 84 resolved (76 ready, 0 alias, 8 shell actions); 12 unresolved; 12 high-risk
- The unresolved and high-risk sets were identical. The baseline did not expose a separate safely-unsupported bucket.
- Baseline manifest scan: 14 manifests, 3 loaded, 11 rejected
- Built-in fixture: expected 18, actual 19; the additional registration was the unified Settings app.

The task brief expected 94 preview rows. The actual checkout produced 96 before any source changes. The preview counts storage occurrences across persisted desktop configuration, service memory, recent documents, shortcuts, and registry or shell-derived menu targets. It is not a count of unique applications. The two-row difference is part of the recorded checkout baseline, not a reason to force the fixture to 94.

### The 12 baseline records

These were 12 occurrences of five distinct stale display labels. Each label appeared in config and then again in its loaded DesktopService mirror, except the first PacMan danger label, which also appeared once in both recent-program stores.

| Storage occurrence | Stored display label | Occurrences |
| --- | --- | ---: |
| `desktop.json:pinned[7]`, `DesktopService:s_pinned` | `Nexgen PacMan Danger Validation` | 2 |
| `desktop.json:pinned[8]`, `DesktopService:s_pinned` | `Nexgen PacMan Bare-Metal Fruit Validation` | 2 |
| `desktop.json:pinned[9]`, `DesktopService:s_pinned` | `Nexgen PacMan Fruit Validation` | 2 |
| `desktop.json:pinned[10]`, `DesktopService:s_pinned` | `Nexgen PacMan Level Validation` | 2 |
| `desktop.json:pinned[15]`, `DesktopService:s_pinned` | `Audio Beep Denied` | 2 |
| `desktop.json:recent[7]`, `DesktopService:s_recentPrograms` | `Nexgen PacMan Danger Validation` | 2 |
| **Total** | **5 unique stale labels** | **12** |

The labels were absent from built-in metadata, loaded manifests, registered aliases, shell actions, and path-like targets. They did not describe 12 missing app registrations. The records were visible persisted labels with no canonical identity; treating a label as a launch target left them unresolved and high-risk. The separate user pin `PacMan PGM1 Ready Input Validation` was a valid manifest identity once CRLF manifests loaded, and now has canonical ID `com.guidexos.pacman.pgm1.ready-input-validation`.

## Root causes and repairs

### Persisted identity and lifetime

Pinned and recent-program persistence retained labels without stable App Model IDs. Startup copied those strings into service state, where future launches could re-resolve a stale label against whichever app happened to claim it. `PinnedItem` and `RecentProgramEntry` now own both their display label and canonical `appId`. `desktop.json` writes aligned `pinnedAppIds` and `recentAppIds` arrays beside the compatibility labels.

On load, a stored ID is authoritative and must still resolve to that exact ID. A legacy config with no ID arrays is migrated by an unambiguous current registry match. Unknown, ambiguous, mismatched, or duplicate stale entries are dropped; they cannot later be claimed by another registration. A present but mis-sized ID array cannot fall back to pairing by display text: affected entries fail closed and are dropped. Save writes the migrated canonical labels and IDs together. Recent writes resolve supported shell dispatches back to their registered identity, so Control Panel stores `ControlPanel`, not a shell label that the registry cannot resolve.

Hosted launch targets and registry records use owned `std::string` values. Registration copies source manifests. App Model dispatch is synchronous and has no separate pending-launch queue. Development launch validates owner and generation, resolves and publishes under the service lock, and captures owned registration/decision snapshots in the process callback. Temporary development IDs can be reused only after the previous owner/generation is unregistered; a stale generation cannot unregister or launch the replacement.

There is no generic launch-context object waiting between enqueue and app startup. A resolved target carries its owned identity, display/dispatch name, target type, shell action or bounded path, backend availability, and diagnostics. It does not retain the caller's source object or stack storage. App ID resolution is fresh at dispatch. Normal built-in, desktop/Start, Settings Open, Control Panel, and Recent Programs routes converge on the DesktopService resolver/launcher; startup compatibility names resolve through the same registration data. File/folder opens remain separate tagged routes with bounded paths. Unsupported or failed dispatch returns before recording a successful recent entry. Runtime registrations are the mutable exception to the otherwise stable scanned registry, and they require owner plus generation for launch/unregister.

### Bounds and deterministic failure

Shared App Model bounds are centralized in `app_model_limits.h`:

| Value | Limit |
| --- | ---: |
| App ID | 128 bytes |
| Display name | 128 bytes |
| Launch target / entry path | 4096 bytes |
| Manifest entries | 64 |
| Registered apps / scanned manifests | 512 |
| Manifest file | 1 MiB |
| Desktop config file | 1 MiB |
| Pinned items / desktop shortcuts | 128 each |
| Recent programs | 10 |

Config parsing and serialization reject over-limit values, controls and embedded NULs, malformed target tags, and misaligned identity arrays. Overflow does not overwrite a live registry entry. IDs remain case-sensitive. Exact duplicate temporary IDs fail with a deterministic error; scanned duplicate IDs are reported and use explicit source policy, with a lexically stable winner for same-source duplicates.

### Manifest loader defect

The manifest loader obtained a raw byte count with `file_size` but opened files in text mode. Windows CRLF translation shortened reads, so valid manifests failed the exact-byte `gcount()` check as “Manifest changed while it was being read.” It now opens in binary mode, reads exactly the measured byte count, rejects growth beyond the 1 MiB cap, and passes the full byte sequence to the validator. The regression test writes and loads CRLF manifests. The scan now loads all 14 manifests and reports no invalid manifests.

### Truthful capability and readiness

Unsupported targets are now distinct from unknown targets in the preview. A known identity without an available backend is `safelyUnsupported` with low risk; an unknown label remains unresolved/high-risk. The readiness predicate was not bypassed: `appModelV1StatusReady` remains the result of `overallOk`, which includes duplicate and namespace checks, hosted and bare-metal metadata coverage, invalid-manifest count, launch-target comparison, storage preview and comparison, typed-dispatch flags, and the existing App Model v1 predicates.

The 19th hosted built-in is unified Settings, ID `gxos.builtin.settings`, introduced in commit `3e5aa43d0efd098b651d45afbece26bcf81f8c00` (“Add unified Settings Center foundation”). It is an intentional unique registration. The prior 18-count fixture was stale; the fixture now requires 19, checks the Settings ID and uniqueness, and retains count coverage. It also expects 12 active-dispatch-owned identities and 27 fallback/unsupported coverage cases based on the now-loaded manifest set.

Developer Studio remains registered as `com.guidexos.developerstudio` with `openSupported=false`. Native ELF apps, including Developer Studio and the Pac-Man registrations, remain unavailable to the ordinary hosted launcher while the experimental hosted runtime is off; attempts fail with the experimental-runtime explanation. Hosted built-ins use their built-in launchers. The code does not route Native ELF targets through hosted built-in handlers or hosted apps through the ELF loader. No arbitrary-path launcher was added.

## Final preview and readiness

The final canonical closeout passed with this post-smoke matrix:

| Classification | Count |
| --- | ---: |
| Total storage records | 86 |
| Resolved and available: ready + alias + shell action | 51 (43 + 0 + 8) |
| Safely unsupported for the current hosted target | 35 |
| Unresolved | 0 |
| High-risk | 0 |

The type diagnostic reports 82 inspected labels, 7 covered target types, 53 hosted-available and 41 bare-metal-available identities, 14 hosted-only, 2 bare-metal-only, 26 expected unsupported target identities, 0 unexpected unsupported identities, and 1 explicit unknown-label probe. The probe remains unknown by design and is not persisted; storage readiness checks the actual stored preview, where unresolved and high-risk are both zero. The summary reports `appModelV1StatusReady=true` and `appModelV1Status=ready` from the normal computed status.

At startup after migration, the preview was 84 records (33 ready, 8 shell actions, 43 safely unsupported, 0 unresolved, 0 high-risk). Launch exercises then changed recent storage, producing the final 86-record matrix. Both snapshots satisfy the same readiness invariant.

## Validation evidence

- Full hosted build: `cmd.exe /c build.bat` — passed after the final source edits.
- Canonical Phase 5B closeout — passed, `appModelV1Complete=true`, `result=PASS`; log `logs/appmodel-phase5b-regression-closeout-20261001-073626.log`.
- Focused identity test — **25/25** passed: exact and over-limit manifest/ID/path/entry bounds, CRLF load, registry ownership copy, case semantics, duplicate rejection, capacity and overflow, deterministic duplicate scan winner, and temporary owner/generation reuse.
- Startup App Model regression — passed. Startup registers canonical identities, launches Notepad, Calculator, Display Options, Settings, Control Panel, and returns with no unexpected persisted windows. Its generated directory is retained untracked.
- Recent Programs Phase 4D smoke — passed canonical recents, unsupported-target suppression, removal, and force-off/reset behavior.
- Settings regressions — Center 89/89; device model 21/21; storage model 24/24; S6 42/42; S7 Accessibility 21/21; S7 Developer 23/23; S7 navigation/search 14/14; S8 Users 11/11; S8 navigation 8/8; S8 interaction/layout 13/13.
- System service bridge 78/78; Network contract 45/45; Network config transaction 21/21; block-device generation 5/5.
- Synthetic display smoke 77/77; display configuration control-plane source smoke passed; display configuration persistence source smoke passed; clock/time settings smoke passed.
- Production hosted dispatch: 24 successful eligible launches across Settings, Notepad, Console, Disk Manager, Display Options, Control Panel, and On-Screen Keyboard, including 17 repeat/relaunch cycles. On-Screen Keyboard succeeds through its existing legacy fallback. Three negative dispatches safely rejected Developer Studio, a Native ELF Pac-Man registration, and an unknown ID; no wrong app launched and the unknown ID did not enter recents.

The production run verified dispatch and repeat behavior, not visual UI acceptance: the harness reported `windowCount=0` at its final snapshot. It does not establish screenshot-based close/reopen acceptance or multiple simultaneously visible instances. A QEMU run was not needed: the repaired code is hosted App Model/configuration/registry code; no `kernel/core` or native launch path changed. Physical hardware was not required.

## Outcome

Outcome A is supported by the baseline reproduction, identification of all 12 occurrences, production ownership and bounds fixes, truthful backend classification, passing computed readiness, focused boundary tests, the hosted launch matrix, and the preserved Settings/startup/recent/display regressions. Known v1 boundaries remain: experimental Native ELF hosted execution, GXApp execution, and visual UI acceptance are outside this closeout. None is classified as an unresolved persisted launch target or high-risk App Model storage condition.
