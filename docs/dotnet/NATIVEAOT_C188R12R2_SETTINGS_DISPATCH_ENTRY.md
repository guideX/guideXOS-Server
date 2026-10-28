# C188R12R2 — Settings AppModel Dispatch-to-Entry

## Outcome

**Outcome C — `DISPATCH_BOUNDARY_PREPARED`.** The source contract is internally consistent, and the retained C188R12R1R1 trace reaches managed selector 5 dispatch. It stops before the registry lookup and `ManagedSettingsCenter.Launch` boundaries. No dispatch defect or exact runtime cause is proven. This phase was hosted-only; no QEMU boot or QAC admission was performed.

## Repository state

- Branch: `v1.1_DOTNET_SUPPORT`
- Starting HEAD: `2a2726789c0f4a216432817c7dfd4e7f773bc5d0`
- Ending HEAD: the closeout commit created from the starting HEAD; its exact object ID is reported by `git rev-parse HEAD` in the task closeout.
- Preserved source `ESP/ramdisk.img` SHA-256 before and after this work: `AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8`.
- The pre-existing source ramdisk modification, generated outputs, and unrelated untracked files were preserved. No reset, restore, clean, stash, branch switch, or guest boot was performed.
- Only the registry diagnostics, hosted regression wiring and cases, qualification predicate, and this document are intended for the closeout commit. Generated outputs and `ESP/ramdisk.img` are excluded.

## Confirmed guest evidence

The retained run is `out/dotnet/c188r11-managed-state-provenance/runs/c188r12r1r1-settings-click-001/guest-001`. Its evidence records one screenshot-confirmed left click at `(815,550)`, one down/up pair, no key events, Notes action `0x18`, and a successful Settings AppModel request. The serial trace then records:

1. `[APPMODEL-MANAGED-LAUNCH]` for `com.guidexos.apps.managed.settingscenter`, selector 5, `/system/apps/GXOSAPP.ELF`.
2. `[C150-APP-LAUNCH]` with Settings generation 2 and selector 5.
3. `[NATIVEAOT-PRODUCTION-LAUNCH]` and `[C188R12R1-NATIVE] stage=before-managed-entry`.
4. Managed `main-entry` and `dispatch-entry` for selector 5.
5. No observed registration lookup, lifetime start, `ManagedSettingsCenter.Launch` entry, surface creation, or compositor presentation marker in the captured serial log.

The raw serial file SHA-256 is `8CFCFFC37BE6FC5574CFCD39B66456277073EA1DB9977967F9A7CE4C876F5167`, matching the retained handoff evidence and acceptance manifest. The earlier runner field `earliestMissingBoundary=settings-initialization` is broader than the source-level trace supports. The first useful new observation point is the bounded registry `dispatch-context` marker immediately after successful `GuideXosHost.TryCreate`; the trace cannot currently distinguish registry setup, registration/factory/lifetime behavior, or an unobserved diagnostic gap after that point.

The retained QAC run reported that its QMP quit request timed out, while the runner recorded QEMU exit code 0 and a final empty QAC census. This explains the shutdown warning; it does not establish a Settings launch failure cause.

## Source launch contract

Settings is a **hybrid native-host / managed NativeAOT logical application**. The native kernel owns AppModel resolution, the composite image, invocation context, and the surface bridge. Managed selector 5 selects the Settings implementation from inside the shared image. Settings is not a separate Settings ELF and is not a sixth independent NativeAOT image.

The route is synchronous:

1. The Managed Notes action binding uses selector 4 and action ID 24 (`0x18`). Its click handler calls the managed action and, on success, calls `launchSettingsCenterFromNotes()` after that callback returns.
2. `launchSettingsCenterFromNotes()` validates Notes and Settings metadata, selector values 4 and 5, the active Notes identity, and its surface. It arms the existing return target, requests the ordinary Notes close, then calls `desktop::launch_app_with_context(settings->displayName, nullptr)`.
3. Shared metadata identifies Settings as app ID `com.guidexos.apps.managed.settingscenter`, display name `Managed Settings Center`, launch name `Settings Center`, launch kind `ManagedNativeAot`, selector `5`, and image `/system/apps/GXOSAPP.ELF` (under the C144 Settings profile).
4. `launch_app_with_context()` calls `launch_managed_appmodel_record()`. The latter resolves `Managed Settings Center` with `appmodel::resolveLaunchTarget()`, validates a managed NativeAOT target, and synchronously calls `nativeaot::launchLogicalApplication()` with a null launch-context pointer, length 0, and activation kind 0.
5. The target resolver finds the shared built-in metadata by identity/display name, fills a `ManagedNativeAotApp` target, and validates the catalog, selector, composite image, and bare-metal availability. The launch router propagates `LaunchStatus` into its managed-launch result and ultimately the `launch_app_with_context()` boolean.
6. `launchLogicalApplication()` resolves the same managed record, reserves the native managed surface and generation, sets the invocation surface, and enters `launchLogical()` for the composite image and selector 5. The native bridge builds the existing `NativeGxAppContext`: native host table, packed generation and selector in `userData`, zero launch flags, null payload, payload length 0, activation kind 0.
7. `launchLogical()` invokes the image entry directly through its resolved entry pointer and returns the managed result synchronously. In `Program.ManagedMain`, selectors 4–8 use the production composite route; selector 5 reaches `GuideXosApplicationRegistry.Dispatch()`. The registry finds selector 5's `ManagedSettingsCenter` factory, runs shared runtime-settings initialization and applicable lifecycle checks, calls `s_lifetime.TryStart()`, then invokes `application.Launch(host)`. The Settings implementation resets its composition and requests a 560×340 managed surface.

There is no launch queue, queued consumer, or asynchronous completion mechanism in this route. Return status propagates through `application.Launch()` → registry `Dispatch()` → managed image entry → `LaunchStatus` → AppModel result → Notes handoff. If the synchronous Settings launch fails, the Notes helper clears its return state and follows its existing recovery path.

The source and existing native serial markers agree on the requested identity and selector. The raw guest trace reaches the native image entry and managed dispatch entry, so it does not support a target-resolution or registration rejection as the observed cause. However, neither it nor the source audit establishes whether the managed registry continued past the last captured marker.

## Changes and hosted regression

No source defect was proven, so no routing, identity, lifecycle, ABI, allocation, or surface behavior was changed. Under the existing `HOSTLOGPROOF_C188R11_MANAGED_STATE_PROVENANCE` diagnostic build flag, `GuideXosApplicationRegistry.Dispatch()` now emits a bounded `C188R12R2_LAUNCH` trace for at most four normal Settings launch requests. Each record includes app ID, selector, generation, stage, result, and a failure code when available. The stages distinguish:

- `dispatch-context` and `dispatch-prelude`
- `registration-lookup` begin/pass/fail
- `runtime-settings-init` begin/pass/fail
- conditional `managed-lifecycle-tests` begin/pass/fail
- `lifetime-start` begin/pass/fail
- `application-entry` begin and `application-return` pass/fail
- `dispatch-return` pass/fail

The entry and return markers are present in both managed lifetime compile paths. Logging uses stack storage and the existing host logging callback. It adds no ABI fields, background service, queue, or new managed allocation. Existing native request, image-entry, Settings launch, surface, and compositor markers remain available to bracket these managed stages.

The focused hosted test uses the production selector router and `GuideXosApplicationRegistry.Dispatch()` with the actual registered `ManagedSettingsCenter` factory; host callbacks are mocked. Its eight cases cover an invalid selector, missing registration, incompatible document activation, Notes close behavior, successful selector-5 launch and requested Settings surface, marker ordering, Settings close behavior, and a rejected surface request with explicit failure propagation. This validates the managed hosted contract only; it does not execute the native C++ resolver or prove guest execution.

## Hosted qualification

All requested hosted qualification completed successfully:

- Focused registry gate: C188R10 14 cases, C188R11 15 cases, and C188R12R2 8 cases.
- `run-c188r11-managed-provenance-tests.ps1`: PASS. R9 14, prior R10 14, R11 15, R12R2 8; runner predicates 30, output bounds 6, runtime lifecycle 7, QAC ownership predicates 15, QAC stream capture 2, keyboard contracts 27, and keyboard runner predicates 24.
- `run-c188-host-tests.ps1`: PASS, 25 cases; stress results: identity 1000, valid resize 500, invalid resize 250, stale generation 250, lifecycle 100, allocations 0.
- `run-c188-prior-host-regressions.ps1`: PASS for C182 15, C185 28, C186 15, and C187 22 cases.
- `test-c188r12r1-settings-handoff-predicates.ps1`: PASS, 37 cases.
- `run-managed-ui-regressions.ps1`: PASS, 38 suites / 932 cases, including the existing managed UI stress gates.
- Diagnostic NativeAOT candidate build: PASS; ELF SHA-256 `E589E66DBC58EE74A02689EA164DEC2AF7178AA94C161895F91BC2F5867E43B`; selectors 4–8 preserved and persistent composite lifecycle validated. The R12R2 marker string is present in this ELF.
- Proof-disabled production NativeAOT build: PASS; ELF SHA-256 `99102A20145E781B7BAEACC6424F8AF1A2A767EC8F4799EA5828C293CE219266`; the R12R2 diagnostic marker is absent and the persistent composite lifecycle is validated.
- No kernel source changed, so no kernel rebuild was required. `git diff --check` passed.

The diagnostic ELF was staged only into the separate generated candidate `out/dotnet/c188r11-managed-state-provenance/candidates/r12r2-settings-dispatch-001`. Its staged ramdisk SHA-256 is `AA692E80A7FE1EA478DA9C461986EC3BF8DD727192F5A9316887612C21FDC2DA`; 185 unrelated files were preserved and zero other files changed. The original source ramdisk remains at its expected SHA-256. This candidate has not been booted.

## One-boot guest acceptance plan

Use at most one authorized QEMU/QAC guest boot with the diagnostic candidate above. Do not perform additional pointer clicks or keyboard input.

1. Verify the candidate root and hashes before admission: diagnostic ELF `E589E66D…67E43B3`, staged ramdisk `AA692E80…C21FDC2DA`, and the existing kernel `568B2C08…7804E864` plus EFI `D72A35DA…D6C2D9B`. Keep the source `ESP/ramdisk.img` untouched.
2. Require a fresh empty QAC census before reserving. Start one owned QAC run, verify the reservation/run identity, owner and runner PIDs, QEMU child PID and executable identity, and matching process start times. Recheck exclusive ownership and a single-session census before the click and before QMP shutdown.
3. At desktop readiness, confirm Notes is the active app (selector 4, generation 1), Settings has not opened, the NativeAOT composite is resident with selectors 4–8, and the persistent heap and registry match the expected baseline. Capture one deterministic pre-click screenshot and confirm the visible Settings button target at `(815,550)`.
4. Perform exactly one left-button down/up pair at `(815,550)` for Notes action `0x18`. Send no other click, key event, or exploratory input.
5. Confirm the existing native sequence: `[C112-ACTION-DISPATCH]` selector 4/action `00000018`; `[APPMODEL-MANAGED-LAUNCH]` for the Settings identity and selector 5; `[C150-APP-LAUNCH]`; `[NATIVEAOT-PRODUCTION-LAUNCH]`; `[C188R12R1-NATIVE] stage=before-managed-entry`; managed `main-entry` and `dispatch-entry`.
6. Read the new `C188R12R2_LAUNCH` records in order. Require context/prelude and lookup PASS, initialization PASS, applicable lifecycle PASS, lifetime-start PASS, application-entry BEGIN, application-return PASS, and dispatch-return PASS. Stop at the first missing or failing stage; preserve the serial log and do not retry the click.
7. Only after application entry is established, check Settings surface request/readiness and its existing native surface/compositor evidence. Capture one deterministic Settings screenshot. Confirm Notes is closed as intended, Settings selector/generation is resident, and the composite selector catalog, resident pool, and persistent heap invariants remain valid.
8. Stop immediately on invalid ownership, a nonempty or inconsistent census, failed action dispatch, unexpected target identity/selector, failed marker/code, timeout, or registry/resident/heap invariant change. Do not use another boot or input attempt in this acceptance run. Release the owned guest and verify final QAC census is empty.

## Remaining uncertainties and next action

The new markers have not run in a guest, so registration/factory allocation, lifecycle start, application entry, and surface creation remain unobserved in this candidate. The previous serial state reported no GC and approximately 5.7 KiB of remaining room in the bounded managed heap before Settings creation; this is a possible stress condition only, not a proven failure or root cause. No allocator changes are authorized by this finding.

**Smallest next action:** after separate authorization for guest acceptance, run the single controlled boot above and classify the first missing or failing `C188R12R2_LAUNCH` boundary. Do not investigate surface rendering unless application entry is established.
