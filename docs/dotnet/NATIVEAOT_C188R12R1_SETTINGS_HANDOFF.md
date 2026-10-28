# NativeAOT C188R12R1 — Settings Surface Handoff

**Outcome: D — `QAC_OR_EXECUTION_BLOCKED`.** QAC admission and guest ownership passed. The sole Settings action did not land on the source-defined Notes Settings button, so this run stopped before the Settings AppModel request. The earlier C188R12 AppModel-to-surface failure remains unresolved; this run does not qualify that downstream path.

## Baseline and preservation

- Branch: `v1.1_DOTNET_SUPPORT`.
- Starting HEAD: `28ab7265a9533dffc9228e01464e5fe2d39baf36`.
- Ending HEAD at implementation and evidence review, before this closeout commit: `28ab7265a9533dffc9228e01464e5fe2d39baf36`.
- The pre-existing modified `ESP/ramdisk.img` was not touched. Its final SHA-256 is `AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8`.
- Pre-existing generated `bin`/`obj` and smoke outputs remain unmodified and excluded from the commit. The isolated guest candidate and run outputs remain under `out/dotnet/c188r11-managed-state-provenance/`.
- No reset, clean, stash, branch switch, history rewrite, source ramdisk write, or foreign QEMU termination occurred.

## C188R12 history and the C188R12R1 observation

The retained C188R12 trace supports a specific order. The Notes Settings action callback returned success (`action=00000018`). Notes then approved its close, cleared its resident entry, and emitted `C150-SURFACE action=destroy ... reason=close-or-replace`; only after those events did the system emit the Settings AppModel launch record and load `/system/apps/GXOSAPP.ELF` for selector 5. That is evidence of a launch request and image dispatch, not proof that `ManagedSettingsCenter.Launch` ran. There is no Settings-specific initialization or surface-create marker in that trace. The following screenshot still showed Notes, but the preceding destroy marker means the image alone cannot establish that a live Notes window remained. It could be an old presented frame.

C188R12 also contained two pointer clicks. Its correction record confirms the first click reached the Settings action and that the later click happened after the launch request but before surface readiness was known. C188R12R1 was designed to replace that with a single source-derived action and explicit readiness evidence.

The C188R12R1 guest booted Notes as selector 4 at 1280×800. It reported five registered descriptors, `TryFind(4)=0`, Notes window `000003E8` at `(340,220)` with size `600×360`, visible/focused/front, resident mask `1`, and one 64 KiB NativeAOT heap initialization. The live Notes source defines the Settings button at client `(430, SettingsY)` with size `90×28`; source layout and the 24-pixel title bar produce the screen center `(815,550)`.

The run sent one left-button down/up and no key events. The saved QMP control trace contains 11 relative-motion events with totals `x=175`, `y=150`. Exact matching against the kernel cursor bitmap in the screenshots found the pointer at `(640,400)` before motion and `(815,520)` after the click. The Notes Settings button begins at screen `y=536`; therefore the click was 16 pixels above it, on a Notes popup. Serial evidence shows `C135-POPUP-OPEN invoke=Options`, then Notes action `00000017` returned `-2`; Settings is action `00000018`. The Settings AppModel launch, managed-entry, initialization, surface request, creation, composition, and presentation markers are all absent.

The retained `r12r1-settings-handoff-evidence.json` originally called the result `C_HANDOFF_BOUNDARY_LOCALIZED`, because it only classified the downstream Settings pipeline. Read-only cursor analysis is preserved separately in `r12r1-postrun-cursor-forensics.json`; it establishes the earlier runner-input failure. The corrected classification is Outcome D because execution did not deliver the intended action. QAC itself was healthy and released normally.

This is also why the phase does not answer the C188R12 question about Settings initialization or surface presentation. The current guest never entered that path, and no source defect in AppModel, Settings initialization, window registration, activation, or composition is established.

## Settings architecture and source contracts

Settings is a managed NativeAOT application, not a native settings window. Its built-in identity is `com.guidexos.apps.managed.settingscenter`, selector 5, backed by the static `GuideXosApplicationRegistry` factory for `ManagedSettingsCenter`.

The source launch path is synchronous up to surface creation:

1. The Notes control action ID 24 is dispatched by `NativeAotManagedSurface`; `launchSettingsCenterFromNotes()` validates the Notes and Settings identities and arms the Notes return target.
2. Notes uses its ordinary close path. The NativeAOT host calls `desktop::launch_app_with_context` for the Settings display name.
3. AppModel resolves the managed built-in metadata and invokes the NativeAOT entry for selector 5. The registry dispatches selector 5 to `ManagedSettingsCenter.Launch`.
4. `ManagedSettingsCenter.Launch` initializes its composition and synchronously calls `GuideXosHost.TryCreateSurface("Managed Settings Center", 560, 340, ...)`.
5. The native PAL validates the active managed context and window dimensions, opens the `NativeAotManagedSurface`, registers a `KernelWindow`, assigns its centered geometry, and sets compositor focus. The returned surface marker reports identity, generation, window, visibility, minimized/focus/front state, and dirty state.
6. On the next desktop draw, the compositor draws the identified visible Settings window. After the framebuffer present, the Settings-specific presentation marker records that exact window ID.
7. `Launch` returns success only after `TryCreateSurface` succeeds. Thus a Settings managed launch return is synchronous with its window registration request; frame composition/presentation follows in the desktop draw loop. The acceptance runner waits for identity-correlated markers, not a sleep as readiness evidence.

The existing success contract is therefore: AppModel request → native managed entry → managed dispatch and `Launch` → successful surface request and registration → visible, non-minimized, focused, front Settings window → Settings-specific composition → framebuffer presentation for that window → successful managed/AppModel return with the five-descriptor registry and persistent heap preserved. A generic Notes frame or a successful AppModel record does not satisfy this contract.

## Diagnostic instrumentation and runner correction

The diagnostic-only source changes add bounded, correlated markers for selector 5 native entry/return, managed dispatch and application launch, Settings launch stages, surface request/create/failure and identity/state, compositor composition, and presentation. Logging is capped per window or stage. The instrumentation does not alter the ABI, managed lifecycle, registry, or heap behavior; no Settings source defect was proven or corrected.

The current run exposed a runner defect: the QMP helper treated relative PS/2 motion counts as framebuffer pixels and recorded its requested coordinates as if they were observed. The target itself was correct; the cursor did not reach it.

The runner now:

- Paces relative motion steps.
- Captures a screenshot after moving and locates the cursor with an exact template match against the kernel's 12×19 cursor bitmap.
- Allows one bounded correction using the observed cursor position as its start.
- Requires the cursor to be inside a six-pixel inset of the source-defined Settings button before clicking.
- Sends the single click with `click-current`, which sends no further pointer motion, and fails closed if the position cannot be verified.
- Reports `runner-input-target-delivery` when a Notes popup action is observed without a Settings AppModel request, and classifies an unverified runner execution as Outcome D.

The corrected cursor-position workflow was validated against the retained screenshots and hosted predicates only. It was not exercised in another guest because the phase's one-boot limit was already used. A future guest must be separately authorized.

## Hosted qualification and builds

All hosted qualification completed before the one guest boot; after the runner correction, its focused predicates and static checks were rerun:

- C188 host integration: **PASS**, 25 cases; 1,000 identity lookups, 500 valid resizes, 250 invalid-geometry cases, 250 stale-generation cases, 100 lifecycle iterations, zero resize-stress allocations.
- Prior host regressions: **PASS** — C182 15, C185 28, C186 15, C187 22.
- C188 managed provenance: **PASS** — R9 14, R10 14, R11 15; runner predicates 30; output bounds 6; lifecycle 7; QAC ownership 15; QAC stream capture 2; keyboard contracts 27; keyboard runner predicates 24.
- Managed UI regressions: **PASS** — 38 suites, 932 cases.
- C188R12R1 focused Settings/pointer/runner predicates: **PASS**, 29 cases after the cursor-position correction was added.
- PowerShell parsing, Python bytecode compilation for the QMP helper/cursor finder/screenshot comparator, both retained-screenshot cursor detections, and `git diff --check`: **PASS**.

Final diagnostic builds and staged candidate:

| Artifact | Result | SHA-256 |
|---|---|---|
| Diagnostic NativeAOT `HostLogProof.elf` (`nativeaot-003`) | PASS | `5177AF786422918E011612D75C69513863A7CEB196C9EF70375F086F6248C431` |
| Diagnostic kernel (`kernel-002`) | PASS | `568B2C084387E45FE3718D422570EE761798542E461674AE8CE14A6B7804E864` |
| Proof-disabled production NativeAOT | PASS | `09CDE6E5D8E4F6032785F98473543965622C526D02A6474B7A43385B3F92BF59` |
| Proof-disabled production kernel | PASS | `A21A951C8DF337CEA08A7FE93580C556F0AC87F33D1B1766070FFF204B1EA168` |
| Candidate staged ramdisk | PASS | `7941E2F6CE66A0F0AD4ABCA02785A1C9BEA09B47976D23CBEFFA840E4D9AB543` |
| Candidate `EFI/BOOT/BOOTX64.EFI` | PASS | `D72A35DA882892BA97356A1C006562ABA2908D9DD909152B96DCBB655D6C2D9B` |

The candidate was staged at `candidates/r12r1-settings-diagnostic-002`; the staging manifest reports 185 unrelated files preserved and `changedOtherFiles=0`. Its diagnostic managed application hash matches `nativeaot-003`. The source ramdisk remains the distinct, unchanged file recorded above. An earlier diagnostic NativeAOT attempt failed before qualification due to trace-field placement; the field scope was corrected and the final diagnostic build passed.

## QAC and one-guest evidence

The fresh preflight reported an online QAC coordinator, zero QEMU processes (zero foreign), zero reservations, zero sessions, and no queued request. The single accepted run used reservation `f394842bfd6e4d62a13002973dc49766`, run identity `46bfde94f2ec4fb1a3d5f2257f2c936b`, owner PID 22164, runner PID 29132, and QEMU child PID 13544. Ownership and process identity gates passed before input and before shutdown. The guest reached Notes readiness and the framebuffer was 1280×800.

Exactly one left click was sent, with zero key events. The post-click image shows Notes with its popup; the pixel comparator reports 22,116 changed pixels (2.1598%), which confirms a visual change but not Settings activation. QAC recorded QEMU exit code 0 and release of the same reservation. Final census: zero QEMU processes, zero reservations. No foreign process was touched.

Raw evidence is under `out/dotnet/c188r11-managed-state-provenance/runs/c188r12r1-settings-handoff-001/guest-001/`, including `serial.log`, `qmp-control.log`, screenshots, QAC identity/release manifests, and the cursor-forensics supplement. The serial SHA-256 is `03F12E3C68D533EB1FBEEDAA6F7B9531C9535E19AFCBCEBBA4C4276B09422D93`. The before/after PPM SHA-256 values are `A2718A5052E581502DA033FF23A13A993AB5075F5F6C5C69BF413200F235C69F` and `FEA9D9AF86B5A73420C1DE2D3EC831FB9C99EE71F95A58F8F3113A432F78EEE7`.

## Managed runtime state

Before the action, the guest reported five descriptors, successful selector-4 lookup, Notes resident mask `1`, registry array/pool storage, one heap initialization at `0x00000001001F4600`, and 64 KiB capacity. Five `action=preserve` markers retained the same heap base and reset count 1 through the Notes popup action. The rejected Notes callback returned `-2` with `gc=0`. No Settings launch/registry return was reached, so this run cannot claim a Settings resident mask or a Settings-specific post-launch heap snapshot. The previous C188R12 trace observed Notes teardown and mask `0`; it did not produce a Settings instance-start or Settings surface marker.

## Final outcome and next action

**Outcome D — `QAC_OR_EXECUTION_BLOCKED`.** QAC ownership was healthy. The execution path failed earlier: the one click missed the Settings button and opened a Notes popup. The earliest observed failing boundary is `runner-input-target-delivery`; Settings AppModel dispatch, initialization, surface creation, activation, composition, and presentation remain unqualified. No managed runtime regression or Settings source defect was demonstrated.

The smallest justified next action is one separately authorized QAC guest run using the corrected screenshot-verified pointer workflow. If that click produces action `00000018` and the Settings AppModel request, use the bounded C188R12R1 markers to follow the synchronous Settings launch and exact surface through presentation. After Settings handoff is accepted, resume the remaining five-resident checks from the established C188R12 matrix rather than restarting the earlier investigation.
