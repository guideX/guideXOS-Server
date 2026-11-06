# S22R14R1R5 — Guest Interaction and COM2 Coverage

**Outcome: `RELEASE_GUEST_CORE_ACCEPTED_COVERAGE_PARTIAL` (Outcome B).** The unchanged repaired candidate passed modern Settings Display navigation, PacMan launch/render/gameplay-input response/normal cleanup, Navigator manifest lookup, and the five-mount VFS checks. The existing legacy Display Options route was reached and its query path ran, but the production QEMU display backend returned unavailable, so functional legacy display acceptance is not claimed. COM2 storage request/dispatch/provider runtime remains unexecuted because this release guest has no COM2 requester/backend. Preserve those two explicit coverage gaps for an owner decision.

## 1. Repository provenance

- Repository: `D:\dev\guideXOSServer`.
- Branch: `main`.
- Starting HEAD: `e172cae9d60cbefc42a444141482cb1a89e9e0ca`, the required R4 checkpoint.
- The tracked worktree was clean at preflight. The pre-existing untracked `assets/Backgrounds/troncity.png` remains untouched.
- Server repository `.phase` is absent. The sibling QAC phase file remains `QAC1` through `QAC3C2` accepted, `QAC3` open, next `QAC3C3`; it was read only.
- Live QAC preflight on 2026-11-05: coordinator online, zero active reservations/sessions, zero queued or blocked requests, and zero QEMU children. No guest was interrupted.
- No branch/worktree switch, reset, clean, stash, rebase, merge, cherry-pick, push, or publication action was performed.
- The R4, R1 stack-safety, release-wide, and S22R13R5 reports were reviewed. S22R13R5 acceptance belongs to its earlier ISO (`b6b7…c2af`) and is not transferred to this candidate.

Preflight captures are in `out/s22r14r1r5-guest-interaction-20261105/preflight/`.

## 2. Candidate and kernel identity

The existing receipt-selected candidate remains the exact R4 stack-repaired PacMan-only image:

| Item | Verified identity |
|---|---|
| ISO | `out/release-profile/6ad9bb77-12ce-4b4d-8ed0-56ff3944ee5d/artifacts/guideXOS-Server-v0.3.0-amd64-pacman-only-candidate.iso` |
| ISO SHA-256 | `2fe2f5eda87c9a80164715a156af5de12c7cf3891d29a961c40012c9d032241e` |
| Kernel SHA-256 | `b5e029d23f9c21b5ff0838d9388322bf95b211bf3ef98145ddd344170dfe95dd` |
| Build/profile | `6ad9bb77-12ce-4b4d-8ed0-56ff3944ee5d` / `ServerV03PacManOnly` |
| Receipt source commit | `5b62216efe78158fb12707c126ec38582faecf59` |
| Native ELF package set | PacMan only; Developer Studio not selected |

The ISO hash matches its existing manifest and R4 candidate verification. The kernel hash matches the manifest’s required input and the staged ESP. The package manifest and ELF are 956 and 63,640 bytes respectively. No ISO rebuild or byte change was made. The relevant kernel source paths are unchanged from the receipt source commit through the current checkpoint.

## 3. R4 runner termination attribution

The final R4 session was run `s22r14r1r4-qmp-20261105-inputfix2-d866acddd3a74f3ea23e33e2cc6c4e55`, reservation `5736effb619142fcbac2ff783bd68501`, QEMU child PID `27004`, endpoint `127.0.0.1:49711`.

QMP greeting, capability negotiation, instance identity, paused status, ownership recheck, `cont`, and `running` status all succeeded. The serial log shows firmware, kernel/desktop initialization, five VFS mounts, `/Apps`, PacMan registration, and no panic or exception marker. The last guest input was a paired pointer click; the last QMP transcript lines record the button-down and button-up acknowledgments. At `2026-11-05T23:16:10.4105814Z`, host execution changed to `RecoveryRequired` with `Cannot bind argument to parameter 'Text' because it is an empty string.`

This is a PowerShell host-side parameter-binding failure. It is not evidence of a guest crash or a QMP disconnect. The saved execution record has no QMP connection error, quit acknowledgment, QEMU exit code, or observed release timestamp. QAC inspection afterward reported the reservation released and the exact child and owner exited; the runner did not capture that release itself, so its workspace correctly remained recovery-required and cleanup-ineligible.

The QAC stream log contains the QAC CLI stderr lines and a QEMU warning, while `execution.json` and `host-execution.jsonl` retain the binding error and state transition. No PowerShell stderr capture, console-input transcript, or exception stack/line was retained. The available artifacts therefore establish the failure type and ownership disposition, but not the precise call site that supplied the empty string.

The runner now accepts empty text at the guest-command boundary and treats it as a no-op. It also records the first ignored empty console line and bounds that diagnostic to one event per session. Abnormal runner failures now retain stage, operation, exception type, message, script name, and line number in the execution record/state; active QAC ownership still transitions to `unknown`/`RecoveryRequired`, and no broad process cleanup was added.

Focused hosted runner validation passed **143/143** assertions, preserving the established 133-check baseline and adding four empty-input/failure-attribution checks plus six paired directional-key and fail-safe checks. Output: `out/s22r14r1r5-runner-hosted-validation.txt`. `git diff --check` passed.

## 4. Settings and COM2 reachability plan (completed before QAC)

### Settings modern Display

`SettingsNativeApp` is a built-in registered in `kernel/core/kernel_apps.cpp`. The normal supported path is Start → Settings → Display. Settings initialization refreshes its display snapshot, and Display navigation calls the corrected `settings_native::readDisplaySnapshot` and `DisplayConfigurationService::submit` path on `kernel_main`’s ordinary boot stack. The page in this build exposes a read-only framebuffer/topology snapshot; switching to another Settings page and returning to Display is the supported state-changing UI interaction available without changing hardware configuration.

### Settings legacy Display Options

`DisplayOptionsApp` is also a built-in registered in `kernel/core/kernel_apps.cpp`; it is not a Native ELF package. The existing guest route is Start → Control Panel → Display. Control Panel is an embedded desktop dialog. Its Display item closes the dialog and calls `launch_app("DisplayOptions")` in `kernel/core/desktop.cpp`. Selecting the legacy app’s Display tab calls `queryDisplayConfiguration()` and exercises the corrected response-member path.

The guarded worst case is the nested pending-topology refresh path (`refreshPendingTopologyChange()` followed by its pending-state query). That Refresh control is drawn and handled only when a pending topology change exists. The production QEMU setup uses standard VGA and does not configure the diagnostic hotplug/test controls. We will exercise the existing Control Panel → Display Options route and Display tab, but will not synthesize a pending topology event or claim coverage of the conditional nested-refresh case unless the guest presents it naturally.

### COM2 storage service

The COM2 bridge ships in the AMD64 kernel. `kernel_main` calls `system_service_bridge::init()` during startup and `system_service_bridge::poll()` from every normal main-loop iteration. Initialization configures UART COM2 (`0x2F8`), registers IRQ3, and emits `[SYSBRIDGE] initialized transport=uart-com2 protocol=1`; this marker was present in the production guest serial log. The source-level main-loop call establishes that poll is scheduled during normal guest execution; it has no per-poll serial marker. The IRQ handler only queues bounded UART bytes. A valid request frame is required before `serveRequest()`, the pointer-owned `system_service::dispatch()` workspace, or `settings_inventory_provider::readStorageSnapshot()` executes. The storage snapshot request is read-only (`GetStorageSnapshot`, request type `0004`). Thus initialization and polling are reached automatically, but the corrected storage dispatch/provider chain is not executed without an inbound COM2 request.

The R4 production-representative launch vector configures one `-serial file:...` endpoint for COM1 and has no second COM2 client/backend. There is no supported Settings, PacMan, or Navigator action that emits a COM2 service frame. This conclusion comes from the bridge source, main-loop call site, and the actual launch vector, not from absence of a UI diagnostic alone.

Existing evidence options are explicit:

- `tests/system_service_bridge_test.cpp` and its runner exercise hosted protocol/dispatch behavior, but not the candidate’s COM2 IRQ/main-loop call chain on the boot stack.
- `scripts/smoke-system-service-bridge.ps1` is the existing guest runtime harness: it builds a separate proof kernel, launches QEMU with COM1 plus a second TCP-backed serial channel, and uses `tests/system_service_runtime_client.cpp` to send storage snapshot request type `0004`. Its kernel is a diagnostic rebuild, not the unchanged release kernel or ISO.

We will not add a COM2 backend or synthesize service traffic in the production-representative app session. The smallest exact-image follow-up, if required, is a separate QAC-owned run of the same read-only ISO with an explicit COM2 serial backend and the existing storage snapshot client, verifying request type `0004`, response status, returned snapshot, and no panic. This needs no ISO modification. A diagnostic-only kernel/image is unnecessary unless the owner requires an altered kernel or guest-visible diagnostic surface; any such artifact must receive its own identity and cannot substitute for this ISO.

**Coverage recommendation:** retain COM2 as an open explicit release-owner coverage decision. Direct runtime qualification is mandatory before v0.3 claims the external COM2 host-runtime service as supported behavior. Based on the current `ServerV03PacManOnly` release contract and the production guest launch vector, it is not a prerequisite to complete the user-facing guest interaction gate; the passing static guard and hosted regressions remain evidence, not runtime execution. Do not mark the COM2 request/dispatch/provider runtime path qualified.

## 5. Static stack contract carried forward

Stack capacity is 16,384 bytes and minimum guarded headroom is 4,096 bytes.

| Path | Guarded static usage | Remaining headroom |
|---|---:|---:|
| Settings modern | 11,512 bytes | 4,872 bytes |
| Settings legacy | 11,896 bytes | 4,488 bytes |
| COM2 | 7,768 bytes | 8,616 bytes |

These compiler-derived path sums pass the configured stack contract. They are not runtime high-water measurements, whole-kernel call-graph proof, or whole-kernel memory-safety evidence. The guest session will establish whether the available corrected functions execute and what the guest visibly does; it will not measure dynamic stack high-water.

## 6. Focused guest plan

The reachability audit was completed before reservation. Three bounded QAC sessions followed, each with the unchanged ISO and the production-representative storage-backed AMD64 profile. The first focused session exercised Settings; the second exercised PacMan and Navigator, then a guarded same-child recovery helper completed those operations when noninteractive stdin ended; the third was a narrow PacMan input follow-up after the first gameplay screenshot was already at Game Over. No guest was interrupted and no broad process cleanup was used. The runner has a finite startup polling deadline; relative pointer steps are bounded to 49 units per axis; targets were screenshot-confirmed; mouse buttons and directional keys are explicitly paired with ownership checks and fail-safe release. Existing HMP keyboard support remains available for text and Escape. The screenshots and serial markers below, rather than QMP acknowledgments, establish guest-visible results.

## 7. QAC ownership, configuration, and boot results

Three managed QAC acceptance reservations were granted and later released:

| Focus | Reservation / run | QEMU child / QMP endpoint | End disposition |
|---|---|---|---|
| Settings and legacy-route reachability | `f56f99c1f92042f3880bbf4105aff71b` / `s22r14r1r5-guest-interaction-20261105-389901793bdb47f198ba81102bce9f2d` | PID `24996` / `127.0.0.1:64549` | QAC later reported `Released`, exact child exited; runner had retained `RecoveryRequired` after an invalid `move -46 49` operator command. |
| PacMan, Navigator, and VFS | `2755eece88ce4fd2a1f7cb85463f831e` / `s22r14r1r5-app-regression-20261105-427b4a206a7b42d9bc8e4802e3f1445d` | PID `5924` / `127.0.0.1:55322` | Runner stdin ended after `cont`; a same-child, ownership-revalidated recovery helper completed the guest actions. QAC reported `Released`, exact child exited. |
| PacMan gameplay follow-up | `7762aecdf25c44878b0070e87a49bb88` / `s22r14r1r5-pacman-gameplay-followup-20261105-3cdc51bc98b14488abad94bb51e1380c` | PID `2004` / `127.0.0.1:63451` | Runner rejected `shutdown` until a result was recorded. QAC later reported `Released`, exact child exited. No QMP quit acknowledgment is claimed. |

Run output roots: Settings `out/s22r14r1r5-guest-interaction-20261105/s22r14r1r5-guest-interaction-20261105-389901793bdb47f198ba81102bce9f2d/output`; PacMan/Navigator `out/s22r14r1r5-app-regression-20261105/s22r14r1r5-app-regression-20261105-427b4a206a7b42d9bc8e4802e3f1445d/output`; PacMan follow-up `out/s22r14r1r5-pacman-gameplay-followup-20261105/s22r14r1r5-pacman-gameplay-followup-20261105-3cdc51bc98b14488abad94bb51e1380c/output`.

For all starts, QAC direct-child identity, process start time, executable, and endpoint ownership were checked. QMP greeting and capabilities were negotiated; the expected instance name and paused/prelaunch status were checked before continuation; reservation and child ownership were rechecked immediately before `cont`; QMP then reported running. The serial logs show actual kernel startup and desktop initialization. The QEMU profile was AMD64, 2 CPUs, 2048 MiB, standard VGA, display none, read-only candidate ISO CD, private writable ESP, OVMF, `-net none`, COM1 file serial, and QMP loopback. The launch vector had no COM2 backend.

Final QAC snapshot: coordinator `Online`, zero reservations, managed sessions, queued or blocked requests, and zero QEMU processes. The three exact reservation-inspect results and the final status/list/queue captures are preserved under the corresponding ignored `out/s22r14r1r5-*` run directories. No QAC ownership remains active.

Representative UTC event timestamps are retained in each run's `host-execution.jsonl`, `qmp-recovery-attach-events.jsonl`, and QMP transcript. The Settings run verified QMP identity at `2026-11-06T03:11:41Z`, issued `cont` at `03:11:41Z`, captured modern Display at `03:17:36Z`, returned to Display at `03:19:17Z`, desktop at `03:19:41Z`, and legacy Display at `03:25:42Z`. The app-regression run verified/continued at `03:29:21Z`/`03:29:22Z`; Navigator's manifest screenshot was `04:13:14Z`, with desktop at `04:13:47Z`. The PacMan follow-up verified/continued at `04:20:38Z`/`04:20:39Z`; the paired right key was sent at `04:41:16Z–04:41:17Z`, the score-140 screenshot was captured at `04:41:19Z`, and the responsive desktop at `04:42:11Z`.

The app-regression recovery helper quit the exact owned QEMU through QMP at `2026-11-06T04:14:29.1395656Z`; QAC subsequently reported the reservation released and the registered child exited. The follow-up runner's guarded shutdown rejection did not send a QMP quit.

## 8. Settings Display guest acceptance

**Modern Settings Display: navigation and snapshot presentation passed; backend-dependent topology values remain unavailable.** In run `389901793bdb47f198ba81102bce9f2d`, Start opened, Settings launched, and Display was selected. The screenshot shows the Display page and its read-only framebuffer snapshot (`1280 × 800`, `32 bpp`, `B8G8R8A8`, `5120 bytes / line`). The same session switched to System and returned to Display, closed Settings, and captured the responsive desktop. Evidence:

- `out/s22r14r1r5-guest-interaction-20261105/s22r14r1r5-guest-interaction-20261105-389901793bdb47f198ba81102bce9f2d/output/screenshots/settings-display-first.png`
- `out/s22r14r1r5-guest-interaction-20261105/s22r14r1r5-guest-interaction-20261105-389901793bdb47f198ba81102bce9f2d/output/screenshots/settings-system-after-display.png`
- `out/s22r14r1r5-guest-interaction-20261105/s22r14r1r5-guest-interaction-20261105-389901793bdb47f198ba81102bce9f2d/output/screenshots/settings-display-return.png`
- `out/s22r14r1r5-guest-interaction-20261105/s22r14r1r5-guest-interaction-20261105-389901793bdb47f198ba81102bce9f2d/output/screenshots/desktop-after-settings-close.png`

The serial log recorded `[settings-native]` Display-category mouse handling and `Display config command`/response activity during interaction. Queries reported failed/unavailable topology with zero outputs in this standard-VGA production profile. This correlates guest navigation with the corrected modern Settings path, but is not successful hardware-mode reconfiguration evidence and is not a dynamic stack measurement.

**Legacy Display Options: the route and query path were reached; functional result failed due to unavailable backend.** Start → Control Panel → Display opened the built-in `DisplayOptions` window, and its Displays tab was selected. Screenshot `out/s22r14r1r5-guest-interaction-20261105/s22r14r1r5-guest-interaction-20261105-389901793bdb47f198ba81102bce9f2d/output/screenshots/legacy-display-tab.png` visibly says Backend/Monitor unavailable, resolution selection unavailable, and “Display query failed.” Serial includes `LaunchDispatch ... target=DisplayOptions`, the subsequent display query command/failed response, and `[desktop] ... title=Display Options app=DisplayOptions`. This demonstrates that the supported legacy UI route and query path executed; it does not establish a valid legacy display snapshot or successful display operation. The conditional pending-topology refresh path was not presented by the guest and remains untested. No hotplug or diagnostic topology state was synthesized.

The Settings reservation later entered `RecoveryRequired` because `move -46 49` omitted the required comma. This was a runner/operator syntax failure after the captured UI work, not a guest or QMP failure. QAC independently confirmed release and exact-child exit.

## 9. PacMan and Navigator regression

**PacMan:** registration remained visible after Start-menu refresh/reopen. PacMan launched from the menu and emitted repeated `[NATIVE-ELF] frame PASS` records for a 448×553 XRGB8888 surface (1,792-byte stride, 990,976 frame bytes). The initial gameplay observation in the app regression session was already Game Over at score 70, so its subsequent input was not counted as a gameplay response. A narrow follow-up run was made rather than repeating the whole smoke: while the freshly launched game surface was active at score 70, the ownership-checked paired `right` qcode was sent; serial then logged key `0x00000103` down/up, and the subsequent screenshot displayed score 140 and the Game Over surface. This is an observable game response after paired directional input, though it does not prove the score delta was caused solely by that one key. `out/s22r14r1r5-pacman-gameplay-followup-20261105/s22r14r1r5-pacman-gameplay-followup-20261105-3cdc51bc98b14488abad94bb51e1380c/output/screenshots/pacman-after-final-qkey-left.png` (active game, score 70) and `out/s22r14r1r5-pacman-gameplay-followup-20261105/s22r14r1r5-pacman-gameplay-followup-20261105-3cdc51bc98b14488abad94bb51e1380c/output/screenshots/pacman-after-right-followup.png` (score 140) preserve the before/after observation.

Escape exited through the normal app UI. Serial recorded Escape, runtime exit with no fault, and `[NATIVE-ELF] ... lifecycle PASS window/resource cleanup complete` for runtime `3`. The final screenshot `out/s22r14r1r5-pacman-gameplay-followup-20261105/s22r14r1r5-pacman-gameplay-followup-20261105-3cdc51bc98b14488abad94bb51e1380c/output/screenshots/desktop-after-gameplay-followup.png` shows the responsive desktop. One runner shutdown command was rejected because its guard requires a result PASS first; the guest itself had already exited PacMan normally, and QAC later confirmed the registered child exited and reservation was released.

**Navigator:** launched from Start and navigated to `file:///Apps/PacMan/app.json`. Screenshot `out/s22r14r1r5-app-regression-20261105/s22r14r1r5-app-regression-20261105-427b4a206a7b42d9bc8e4802e3f1445d/output/screenshots/navigator-file-url-result-qcode.png` shows the URL and the PacMan manifest content. Serial confirms `[VFS] Opened: /Apps/PacMan/app.json`, parser begin for 956 bytes, parser complete (`blocks=1`), and load complete. Navigator closed back to the responsive desktop, captured at `out/s22r14r1r5-app-regression-20261105/s22r14r1r5-app-regression-20261105-427b4a206a7b42d9bc8e4802e3f1445d/output/screenshots/desktop-after-navigator.png`.

## 10. VFS integrity and guest kernel observations

Across boot and post-interaction serial checkpoints, the VFS table retained the expected five mounts: `/`, `/system`, `/certs`, `/config`, and `/config/navigator`. The app-regression serial log records `mountTable count=5` fifteen times, including after PacMan lifecycle events and after Navigator; generations match and the mounted devices are present. `/Apps` resolved on the root FAT32/ATA-backed filesystem; PacMan remained registered with package count 1 and menu availability true. The Navigator manifest opened completely at 956 bytes and parsed successfully. Post-PacMan cleanup returned to desktop. The retained serial logs contain no observed kernel panic, unexpected kernel exception, or VFS mount-corruption marker. This is bounded guest observation, not a proof that no latent kernel fault exists.

Evidence is in `serial.log`, `qemu-debug.log`, QMP transcripts, and screenshots under the three run directories listed in §7. QMP/UI command acknowledgments were not used as substitutes for guest-visible page, application, manifest, or cleanup evidence.

## 11. Runtime versus static stack evidence

Static path guards remain as shown in §5 and passed the 4,096-byte minimum headroom contract. Runtime evidence is narrower:

- **Settings modern:** Display page and modern Settings event/query activity observed; the specific function body has no unique serial entry marker. Source call-chain attribution plus the actual page and service activity provide supported execution evidence, not a per-function stack trace.
- **Settings legacy:** existing Display Options route, dispatch marker, Display tab, and failed query response observed. No valid backend snapshot was available, and the conditional nested pending-topology refresh did not execute.
- **COM2:** serial confirmed `[SYSBRIDGE] initialized transport=uart-com2 protocol=1`; source shows `poll()` is called from the normal main loop. No inbound request reached `serveRequest`, service dispatch, or storage snapshot provider in these production launches. Their execution is not claimed.
- **Dynamic stack high-water:** not measured for any path. Static worst-case path sums are not whole-kernel call-graph analysis and do not establish whole-kernel memory safety.

## 12. Outstanding coverage decisions and release blockers

**Legacy Settings recommendation:** retain runtime qualification as open for the legacy query result and the conditional pending-topology refresh. The supported route is reachable and ran, but this production guest reports no backend and cannot provide a successful snapshot. Do not represent modern Settings acceptance as legacy coverage. If legacy Display is within the v0.3 supported behavior contract, the release owner should require a supported display backend/fixture run before claiming it accepted; otherwise explicitly scope the unavailable backend behavior in the release coverage decision. The conditional refresh can be tested only where an existing supported operation creates pending topology state.

**COM2 recommendation:** retain request/dispatch/provider runtime as an explicit open decision. Require direct runtime qualification before claiming the external COM2 storage-service host path as supported v0.3 behavior. For the current PacMan-only production guest contract, it is reasonable for the owner to treat this as outside the completed user-facing core gate, but the static guard and hosted regression do not waive or complete runtime qualification. The smallest follow-up is a separate QAC run of the same unchanged ISO with a second COM2 serial backend and the existing read-only `GetStorageSnapshot` type `0004` client; assert request/response and no panic. Reuse `scripts/smoke-system-service-bridge.ps1` and `tests/system_service_runtime_client.cpp` only if they can target the unchanged release kernel/image; the current script's separately built proof kernel cannot prove candidate runtime. No diagnostic-only image was created. If the existing harness cannot pair a client with the unchanged kernel, the required separate harness/configuration should be approved and identified before building anything.

The independent S22R14 release blockers remain open:

- `_wcsicmp` isolated-build reproducibility
- `/Documents` functionality
- `/.startmenu_recent` persistence
- Native ELF kernel-privileged execution
- Writable application package discovery
- Insufficient runtime ABI negotiation
- Absent Native ELF process isolation

No release-owner decision or readiness-marker change is made here.

## 13. Recommended next Main Integration phase

Record this phase as Outcome B and preserve the verified ISO plus its receipt. Next, obtain a release-owner decision on whether legacy Display success and COM2 storage request runtime are mandatory for the intended v0.3 contract. If mandatory, schedule narrowly scoped qualification with an available supported display backend/operation and an unchanged-candidate COM2 backend/client pairing. If not included in the contract, record the exclusions and keep the stack guards/hosted results as limited static and hosted evidence. Keep all seven independent release blockers above open. No readiness marker, `dist`, release publication, or owner decision was changed by this phase.
