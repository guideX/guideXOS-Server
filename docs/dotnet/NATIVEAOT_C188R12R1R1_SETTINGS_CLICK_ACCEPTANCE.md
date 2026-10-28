# C188R12R1R1 Settings Click Acceptance

## Result

**Outcome B — `SETTINGS_SURFACE_DEFECT_LOCALIZED`.** One screenshot-verified click reached Notes action `0x18` and issued a Settings AppModel launch request. Settings did not become visible. The trace stops after Settings enters managed `Dispatch` and before the application-launch entry marker; it does not establish a failure in Settings surface creation or the compositor.

## Run identity and preservation

- Repository: `D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT`, branch `v1.1_DOTNET_SUPPORT`.
- Starting HEAD: `df39bbd39361a34a5bcabf5c06951ec66794f9b8`.
- HEAD at the end of qualification, before the closeout commit: `df39bbd39361a34a5bcabf5c06951ec66794f9b8`.
- The qualification used one QAC-authorized guest boot. It made no second click, sent no keyboard input, and launched no other managed applications.
- Existing worktree changes and generated/untracked build and smoke-test files were preserved. Only the three C188R12R1 runner/test scripts and this closeout document are intended for the closeout commit.
- The existing working-tree `ESP/ramdisk.img` still hashes to the required `AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8`; it remains unstaged.

## Pointer and click evidence

The Settings button is in Notes window `000003E8`, whose screen origin is `(340,220)`. Its source-defined client rectangle is `(430,292)` with size `90x28`, giving screen bounds `x=[770,860)`, `y=[536,564)` and center `(815,550)`. The valid safe inset is `x=[776,854)`, `y=[542,558)`.

The cursor finder reported one unique screenshot match at `(815,550)`, inside the safe inset. One pointer probe was recorded and no corrective movement was needed. The runner then sent exactly one left-button down and one left-button up, with zero other button or key events. The serial trace contains exactly one Notes action dispatch: `selector=00000004 action=00000018 managedReturn=00000000 result=PASS`.

The saved screenshots are [the pre-click capture](../../out/dotnet/c188r11-managed-state-provenance/runs/c188r12r1r1-settings-click-001/guest-001/before-settings.png) and [the post-click capture](../../out/dotnet/c188r11-managed-state-provenance/runs/c188r12r1r1-settings-click-001/guest-001/settings-check.png). The post-click image still displays Notes, with the cursor over Settings. The PPM hashes recorded by the runner are:

- Before: `BBA067583909C82C0E7C71C8FD2B1F05DC0D955844AE7E30BADCE8885E9FD5D4`
- After: `CAB88AF6FBAD6A6AC15228CBB366088FCBF539ECC873EE7F2B484DC4D68532D0`
- Changed pixels: `2,321 / 1,024,000` (`0.2267%`)

The serial trace records Notes surface destruction followed by the Settings AppModel request, while the post-click framebuffer still shows Notes. This proves no Settings surface was visible in the capture; it does not prove the compositor caused the failure.

## Settings handoff boundary

Observed in order:

1. Notes action `0x18` returned success.
2. The Settings AppModel launch request was issued for `com.guidexos.apps.managed.settingscenter`, selector `5`, image `/system/apps/GXOSAPP.ELF`.
3. Native managed entry, managed `main-entry`, and managed `dispatch-entry` markers were observed.
4. The last Settings-specific managed marker is `C188R12R1-MANAGED stage=dispatch-entry selector=5`; the corresponding C188R11 state marker reports five descriptors (`n=5`).
5. No `application-launch-entry`, Settings `launch-entry`, pre-surface-request, surface create/registration, activation, composition, presentation, AppModel result, or managed launch-return marker was captured.

The narrowest demonstrated boundary is therefore **managed dispatch to application launch**: the guest did not produce evidence that `ManagedSettingsCenter.Launch` began. The runner's raw `earliestMissingBoundary` value is `settings-initialization`, but that label is broader than the trace supports. The trace cannot distinguish a stall in dispatch setup or lifetime start from another failure before the app-launch marker. Surface creation and compositor stages were not reached in the captured evidence.

The Settings dispatch state shows the registry still reports `n=5`; it is an entry observation, not proof that a Settings resident instance was created. `settingsRegistryReturn` is false and `settingsResidentMask=-1` is the runner's unavailable sentinel. Persistent heap evidence remains positive: one initialization, preserved through the run, with no observed reinitialization. No managed-runtime regression was proven.

The AppModel request is present, but neither an AppModel success/failure result nor a managed launch return was captured. Accordingly, the request is confirmed; its completed result is unknown.

## QAC ownership and teardown

- Preflight: QAC online; zero QEMU processes, zero foreign processes, zero reservations, and zero managed sessions.
- Admission: exclusive reservation `1d1d778802a748b49b9bce96b441618c`; owner PID `5052`; QEMU child PID `26404`; all recorded owner and child identity checks passed, with zero foreign QEMU processes.
- Ownership was revalidated before the click and accepted.
- QAC logged QEMU exit code `0` and released the reservation. The runner's QMP `quit` request timed out, so guest shutdown was not confirmed through that request; QAC's release checks, recorded after-run status/list, and a fresh post-run status/list all show zero QEMU processes and zero reservations. No foreign QEMU process was touched.

The complete raw evidence is retained under:
`out/dotnet/c188r11-managed-state-provenance/runs/c188r12r1r1-settings-click-001/guest-001/`

Key records include `r12r1-settings-handoff-evidence.json`, `serial.log`, `qmp-control.log`, `acceptance-manifest.json`, the before/after screenshots, and the QAC ownership/status/list snapshots.

## Artifact validation

The diagnostic candidate was reused without rebuilding:

- Candidate: `out/dotnet/c188r11-managed-state-provenance/candidates/r12r1-settings-diagnostic-002`
- Diagnostic ELF SHA-256: `5177AF786422918E011612D75C69513863A7CEB196C9EF70375F086F6248C431`
- Diagnostic kernel SHA-256: `568B2C084387E45FE3718D422570EE761798542E461674AE8CE14A6B7804E864`
- Staged diagnostic ramdisk SHA-256: `7941E2F6CE66A0F0AD4ABCA02785A1C9BEA09B47976D23CBEFFA840E4D9AB543`
- EFI SHA-256: `D72A35DA882892BA97356A1C006562ABA2908D9DD909152B96DCBB655D6C2D9B`

The diagnostic verifier passed (`ELF64 LOADS=7`, `RELOCATIONS=0`, eight retained symbols); its stage manifest passed, preserving 185 unrelated files with `changedOtherFiles=0`. Both diagnostic and production runtime manifests retain `persistentCompositeLifecycle=true`, `managedHeapBytes=65536`, `Primary64KiB`, and `bounded-static-image-backed-no-collection`. The production artifact hashes were ELF `09CDE6E5D8E4F6032785F98473543965622C526D02A6474B7A43385B3F92BF59` and kernel `A21A951C8DF337CEA08A7FE93580C556F0AC87F33D1B1766070FFF204B1EA168`. Production qualification passed selectors 4–8 without CounterApp or a C188 selector route.

## Hosted checks

All recorded checks passed:

- C188R12R1 Settings handoff predicates: 37 cases, including action `0x18` success, action `0x17` rejection, failed action return, popup/modal blockers, exact-one-click checks, the prior `(815,520)` miss, and pointer-coordinate/hotspot contracts.
- C188 host integration: 25 cases; identity 1,000, valid resize 500, invalid geometry 250, stale generation 250, lifecycle 100, and zero allocations.
- Managed registry/provenance: C188R9 14, C188R10 14, C188R11 15, runner predicates 30, and runtime lifecycle 7.
- Host regressions: C182 15, C185 28, C186 15, C187 22.
- Managed UI gate and inventory: 38 suites / 932 cases; six invalid manifests rejected.
- Additional focused suites: runtime lifecycle 7, output bounds 6, R11 runner predicates 30, R11R2 runner predicates 24, QAC ownership 15, QAC stream capture 2, and keyboard contracts 27.
- PowerShell parsing passed for the three changed scripts; Python parsing passed for the QMP, cursor-finder, and screenshot-comparison scripts; `git diff --check` passed.

The test and build outputs remain in the existing generated output directories; they were not cleaned.

## Smallest justified next action

Use a separate, narrowly scoped diagnostic change to add before/after markers around selector-5 managed dispatch setup, runtime-settings initialization, managed lifetime start, and the call into `application.Launch`. The current run does not justify changing Settings surface code, the compositor, registry design, or NativeAOT lifecycle behavior. Any further guest run belongs to a separately authorized phase.
