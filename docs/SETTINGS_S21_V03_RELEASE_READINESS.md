# guideXOS Server — S21 Native Settings v0.3 Release Readiness

**Audit outcome: A — complete.** The current native Settings implementation is suitable for a bounded Alpha distribution, with no demonstrated critical Settings defect. **Release recommendation: B — conditional readiness.** The Settings capability is qualified, but the candidate-named v0.3 artifacts currently preserved in `out/` predate native Settings and cannot be the shipping package. Rebuild and qualify the actual v0.3 package before including Settings.

This report assesses Settings only. It does not accept Settings 1.0, S9, or Server v0.3 as a whole.

## 1. Repository preflight and scope

- Repository: `D:\dev\guideXOSServer`; branch: `main`; starting HEAD: `9ca5b20d7b2a2f5801e2cf13fff36333f2ae196b` (the accepted S20 commit, as expected).
- The tracked worktree was clean. `main` and the local `origin/main` ref matched at preflight. There was no existing S21 audit. No newer commit invalidated S20.
- There were 676 pre-existing untracked paths: 575 under `docs/` and 101 under `out/`. Of these, 419 paths were in the pre-existing untracked S16R evidence directory; those local artifacts were preserved, and the 19 entries in its SHA-256 manifest match. The committed S20 evidence manifest also passed checks for all 119 listed files. No pre-existing `out/` artifact or release candidate was overwritten. No `.phase` file was introduced.
- Available `D:` space was 18,021,838,848 bytes. No production build or QEMU boot was needed: S20 already built and booted the exact starting HEAD. The one S21 test executable was written under the user temp directory, not `out/`.
- S21 changed no production source. It adds only focused decoder test cases, a short committed test transcript, and this report.

## 2. S10–S20 accepted capability history

Each row describes the accepted record, not a test rerun in S21. Acceptance commit means the commit containing the accepted phase record; S15 implementation/report content was committed together with the S16 work, as its report explains.

| Phase / acceptance commit | Production capability now present | Accepted validation and evidence | Limits recorded at acceptance |
|---|---|---|---|
| [S10 — Outcome A, `c51ace40`](SETTINGS_S10_TECHNICAL_ACCEPTANCE_20261011.md) | Shared freestanding presentation core, category/route model, capability state, and focus traversal. Current. | Shared core 2,645/2,645; hosted Settings/provider set 464/464; production-flag compile. No native guest was claimed at S10. | Presentation architecture only; no native Settings app, provider, launch route, or persistence. |
| [S11 — Outcome A after S11R, `ac40af27`](SETTINGS_S11_NATIVE_RUNTIME_ACCEPTANCE.md) | Native `KernelApp` runtime, System/About value snapshots, lifecycle, and AppManager-owned identity. Current. | S11R native harness 554/554; production freestanding build and QEMU launch/lifecycle. The 443/443 hosted group is historical, not an S11R rerun. | Read-only System/About slice; Start still followed the old Control Panel route until S12. |
| [S12 — Outcome A, `de63af80`](SETTINGS_S12_DEFAULT_LAUNCH_AND_SYSTEM_ABOUT_ACCEPTANCE.md) | Start → Settings typed launch to `gxos.builtin.settings`; Control Panel remains separate; versioned System/About. Current. | Native runtime 564/564, shared core 2,645/2,645, hosted/model 359/359; clean production build and QEMU route/reopen. | No authoritative OS release field; About correctly reports it as undefined. Other categories deferred. |
| [S13 — Outcome A, `db05e3b6`](SETTINGS_S13_NATIVE_DISPLAY_ACCEPTANCE.md) | Read-only active framebuffer snapshot with validated optional topology fields. Current. | Native provider/runtime 796/796, shared core and hosted regressions; freestanding AMD64 build and real QEMU Display/legacy Control Panel review. | Ordinary guest showed framebuffer 1280×800, 32 bpp, pitch 5120, B8G8R8A8; logical desktop, topology, layout, and primary mode unavailable. No display writes. |
| [S14/S14R — Outcome A, `6b89e42e`](SETTINGS_S14_DISPLAY_REFRESH_AND_TOPOLOGY_ACCEPTANCE.md) | Validated display-generation refresh and cache/focus preservation. Current. | Runtime/provider 828/828, core 2,647/2,647, hosted regressions, freestanding build, and production QEMU refresh/lifecycle evidence. | The ordinary AMD64 build does not enable the gated live VirtIO topology backend; no output-mode or multi-display control. |
| [S15 — Outcome A, committed with S16 as `655702c8`](SETTINGS_S15_NATIVE_PERSONALIZATION_ACCEPTANCE.md) | Read-only Personalization snapshot for current background, scale, gradient colors, and supported icon visibility. Current. | Runtime/provider 842/842; freestanding validation and two production QEMU guests. | At this phase it was read-only; mutation arrived in S16. No custom paths or arbitrary colors. |
| [S16R — Outcome A, `30bf2c9a`](SETTINGS_S16_GRADIENT_MUTATION_ACCEPTANCE.md) | Built-in gradient selection with explicit Apply/Cancel, revision checks, canonical config update/readback, and runtime rollback reporting. Current. | Runtime/persistence 1,068/1,068; core 2,649/2,649; freestanding build; raw-FAT Apply, same-ESP reboot, and legacy Display Options review. | In-place FAT write is not crash-atomic. S16R closes the earlier guest-evidence gap; the early S16 checkpoint was Outcome B. |
| [S17 — Outcome A, `26340ec1`](SETTINGS_S17_WALLPAPER_MUTATION_ACCEPTANCE.md) | Built-in wallpaper catalog and Apply/Cancel through the same desktop-owned persistence path. Current. | Runtime/persistence 1,289/1,289; production raw-FAT Apply, byte readback, cold relaunch, and legacy recognition. | QEMU `fat:rw` host-directory readback anomaly remains separate from accepted raw-FAT evidence. No power-loss guarantee. |
| [S18 — Gates A and B pass, `5099c8a8`](SETTINGS_S18_ICON_CONFIGURATION_AND_INTEGRITY_ACCEPTANCE.md) | Trash, File Explorer, and Settings icon visibility Apply/Cancel; bounded `/desktop.cfg` targeted update. Current. | Controlled VFS malformed/partial-write/readback/rollback cases; isolated raw-FAT guest Apply, close/reopen, cold relaunch, legacy check, and restoration. | Historical malformed config cause and an unrelated whole-media hash delta remain unlocalized. Auto-arrange is session-only. Malformed config is rejected, not silently repaired. |
| [S19/S19R3 — Outcome A, `492c1054`](SETTINGS_S19_NATIVE_UI_POLISH_ACCEPTANCE.md), [S19R3 guest evidence](evidence/settings-s19r3-native-ui-20261017/README.md) | UI hierarchy/hover/focus, bounded layout, and interactive resize: minimum 660×440, preferred 760×560, maximum 1180×820. Current. | Runtime 1,417/1,417; layout 1,037/1,037; core 2,650/2,650; hosted 89/89; actual production resize to minimum and back, page review, close/reopen, and legacy lifecycle. | Existing bitmap font and compact text remain. Initial failed resize attempts were invalid QMP gestures, not a reproduced compositor defect. |
| [S20 — Outcome A, `9ca5b20d`](SETTINGS_S20_MODIFIER_AWARE_FOCUS_ACCEPTANCE.md) | Left/right Shift tracking, transition-time modifier snapshots, and forward Tab / reverse Shift+Tab focus in Personalization. Current. | Decoder 31/31; core 3,659/3,659; runtime 1,421/1,421; layout 1,037/1,037; hosted 89/89; identity 25/25; file activation 60/60; freestanding build and real QEMU lifecycle/focus/resize/legacy checks. | No timeout after a missing modifier break; input-driver initialization resets state, but only at startup. Key repeat has no distinct event flag. |

S21 did not regenerate these phase counts. The linked reports and their QEMU/build artifacts remain the evidence for those historical acceptances.

The S16R evidence directory remains untracked local evidence from before S21; the accepted S16R report is tracked. The manifest check verifies its listed files, not every file in that directory. S20 evidence is committed and its complete listed-file manifest verified above.

## 3. Current native Settings capability inventory

The shared/hosted Settings Center has twelve category descriptors. The production native adapter intentionally enables four. It renders the other eight dim, labels them “Dim pages are deferred,” and rejects their selection before page/provider operations. A click on a deferred row leaves the current page selected; it does not expose mock controls or privileged operations.

| Category | Native production classification | Supported behavior / boundary |
|---|---|---|
| System | **Limited native implementation** | Product name, kernel version, and architecture. Read-only. |
| Display | **Limited native implementation** | Active framebuffer properties and validated optional configuration snapshot. Read-only; ordinary production topology/modes are unavailable. |
| Personalization | **Limited native implementation** | Eight built-in gradients, bundled built-in wallpapers, and supported desktop icon visibility; explicit Apply/Cancel and revision-checked persistence. No custom images, themes, fonts, or arbitrary configuration editing. |
| About | **Limited native implementation** | Product, kernel version, architecture, and OS release availability. The OS release is explicitly undefined in this build. |
| Network & Internet | **Hosted-only; deferred natively** | Hosted model/service exists; the native Settings page is unavailable. Existing network snapshot service is not a native Settings management page or write API. |
| Devices | **Hosted-only; deferred natively** | Hosted inventory/model exists; no native Settings device controls. |
| Storage | **Hosted-only; deferred natively** | Hosted inventory/model exists. Disk Manager remains a separate legacy app. |
| Apps | **Hosted-only; deferred natively** | Hosted App Model/default-app surface; no native Settings app-management page. |
| Users | **Hosted-only; deferred natively** | Hosted page/model is present but does not provide a human-account management system. |
| Date & Time | **Hosted-only; deferred natively** | Hosted clock/time-zone surface; no native Settings controls. |
| Accessibility | **Hosted-only; deferred natively** | Hosted preference/tooling; no native Settings controls. |
| Developer | **Hosted-only; deferred natively** | Hosted diagnostics/tools; no native Settings developer page. |

The hosted pages/models are implemented separately in `settings_center.cpp` and related service/model code. They are not evidence of native production functionality. S9 hosted review exercised twelve pages, but S9 / Settings 1.0 remains unaccepted. Native Settings uses `SettingsNativeApp`, the shared presentation core, and kernel-owned providers; it does not require the hosted process, IPC, or STL-heavy hosted page implementation.

## 4. Authorization and configuration safety

### Trusted caller and provider boundary

- `AppManager` registers the native Settings factory from built-in metadata and copies the registered app ID into a private `KernelApp` field before `init()`/`initWithParam()`. App parameters cannot set that field; duplicate factories cannot claim the same non-empty app ID.
- Each native Settings read and mutation provider checks the caller object's AppManager-assigned runtime ID. Requests contain no identity string. S11–S18 tests reject unrelated app objects and identity-like launch arguments.
- Snapshots are fixed-size/versioned, bounded, copied into caller-owned buffers, and contain no raw framebuffer pointer or kernel-owned pointer. Mutators require exact structure size and supported version; validate preset IDs/targets and nonzero expected appearance revision; and return explicit failure statuses. Unknown IDs and stale revisions are rejected. Result mapping defaults unknown desktop statuses to failure.
- There is no generic Settings file-write operation. The S10 operation callback rejects mutation. Native Personalization has only three dedicated in-kernel C++ calls: built-in gradient apply, built-in wallpaper apply, and supported icon visibility apply. They update the shared desktop-owned configuration rather than accepting paths, bytes, or arbitrary colors.
- These provider functions are declared as kernel C++ APIs, but S21 found no Native ELF import, syscall, hosted app, or COM2/system-service dispatch to native Settings mutations. This is not process isolation against malicious kernel-resident code: all `KernelApp` code runs in the trusted kernel domain, and `AppManager::getRunningApp()` exposes app object pointers. An in-kernel component could misuse a real Settings object pointer when directly calling a provider. Do not claim the ID check sandboxes mutually untrusted kernel apps; no such untrusted app extension boundary is supported in this release. If that trust model changes, use dispatcher-supplied caller context or a capability handle.

Source review found no supported external-app dispatch path to the native Settings mutators and no arbitrary-file-write API. Existing provider-negative tests cover invalid caller objects, spoofed launch data, malformed/unsupported requests, invalid/unknown IDs, stale revisions, and storage failures. That is sufficient for the current trusted built-in-app model, not a general kernel-app sandbox claim.

### Persistence and configuration integrity

Current code retains the accepted S16–S18 contract: `/desktop.cfg` is the canonical shared file; the desktop appearance owner is shared with legacy Display Options; supported writes target only the relevant background/icon fields; valid unrelated bytes, unknown fields, ordering, and line endings are preserved; runtime readback verifies the serialized result; and ordinary write/readback failure attempts runtime and byte rollback where supported. Appearance revisions reject stale writes. A rollback failure is surfaced as failure rather than success.

The actual durability guarantee is **runtime readback and rollback within tested limits**. FAT updates are in-place and are not crash-atomic or journaled. No power-loss recovery or transactional filesystem guarantee is claimed.

| Historical observation | S21 classification | Reason / release treatment |
|---|---|---|
| In-place FAT write is not crash-atomic. | **Acceptable documented limitation.** | Confirmed by implementation; ordinary failures have readback/rollback paths. Document the power-loss limit. |
| Certain malformed/truncated historical config images have no conclusively known cause. | **Insufficient evidence to attribute to current production code.** | S18 reproduced malformed historical bytes, not a current writer defect. Current targeted updates reject malformed input byte-preservingly. Do not silently repair it. |
| Supported targeted operations preserve unrelated bytes. | **Accepted within tested bounds.** | S16–S18 exact-file comparisons and injected VFS failure cases; raw-FAT guest paths verified. Not a universal guarantee for all filesystems/writers. |
| A whole-media hash changed while `/desktop.cfg` was byte-identical and no config write was logged. | **Insufficient evidence to localize; not a demonstrated config defect.** | The pre-close block snapshot was not retained. The canonical config readback remained stable. Keep separate from config correctness claims. |
| QEMU `fat:rw` host-directory readback differed from raw FAT behavior. | **Acceptable backend-specific limitation for this audit.** | S17 accepted production persistence on isolated raw FAT. The host-directory discrepancy is not qualified as a production raw-device failure. |
| Historical malformed config file. | **Requires no release-time migration.** | S18's safe behavior is rejection without mutation; the original bytes and cause remain historical evidence, not a current repro. |

No reproducible current configuration corruption defect was found in the accepted current-source evidence. A future reproducible mismatch on a supported raw-FAT path must be treated as a new correctness investigation, not waived because earlier suites passed.

## 5. Missing-key-up recovery assessment

The PS/2 set-2 decoder in `kernel/core/ps2keyboard.cpp` owns modifier state. It tracks left and right Shift independently; Control and Alt use decoder-owned state as well. Key events snapshot modifiers at the transition. Focus, application close/reopen, and Settings state do not own or clear this global driver state. Repeated make is idempotent. A lost Shift break can therefore leave Shift active for subsequent events.

S20 exercised a real guest Shift+Tab, ordinary Tab after Shift release, and ordinary Tab after Settings reopened. It did not inject a missing physical break. S21 added a deterministic missing-break sequence to the [production decoder test](../tests/ps2keyboard_modifier_test.cpp): omit left Shift's break, observe modified Tab on later events, deliver a fresh same-side make/break pair, then verify unmodified Tab and ordinary `a` input. It also verifies that driver `init()` clears stale right Shift. The test passed **39/39**; transcript and test-binary hash are recorded in [S21 decoder evidence](evidence/settings-s21-20261018/ps2keyboard-missing-break-test.log).

`ps2keyboard::init()` is called at kernel startup and resets modifier/parser/event state. There is no supported in-session timeout, controller-loss resynchronization, or user-facing driver reset; `clear()` drains event queues but does not reset modifier state. A normal press/release of the affected physical Shift key supplies a fresh break and restores neutral input while the stream is functioning. This makes the behavior bounded and recoverable without kernel instability. **Classification: Low.** Document the tap/release recovery for Alpha; no input-hardening code gate is warranted by current evidence.

The new test did not combine missing-break injection with a live focus transfer and app destruction/reopen in QEMU. S20's real guest separately proves normal focus/lifecycle/reopen behavior, and source inspection shows those paths do not reset PS/2 state; persistence of a missed modifier across those boundaries is therefore a source-based inference. The deterministic S21 test injects Shift specifically; Ctrl/Alt have analogous decoder-owned latches but were not separately fault-injected in S21.

## 6. Mouse, focus, resize, lifecycle, and legacy coexistence

S19R3 and S20 show no remaining user-visible mouse/focus/window failure in the supported sequence. The production guest resized Settings from preferred 760×560 to the enforced 660×440 minimum and back to preferred size in a separate qualified run; the minimum-size pages and Personalization controls were reviewed. S20 then qualified forward/reverse focus at minimum, pending-state Cancel, close/reopen, ordinary Tab after reopen, Start, legacy Control Panel close, and return to a stable desktop.

S19R3 identified earlier failed QMP drags as incomplete or miscalibrated input sequences. Those attempts are not compositor defect evidence. The later held-button movement at the actual grip worked. Pointer input after legacy teardown, resize gestures, window close/reopen, and legacy Control Panel coexistence were visibly exercised; no stale pointer capture was observed through user-visible behavior (private capture state was not instrumented). No new input framework or production fix is indicated. The configured maximum 1180×820 is covered by bounded layout geometry tests; S19R3's guest proof centers on the minimum and resize-back rather than a guest maximum-size run.

## 7. Production build, package, and artifact provenance

### Current accepted production build

The exact starting HEAD's S20 final ordinary AMD64 build is `kernel/build/s20-modifier-focus-20261018-final-recheck1/bin/`:

| Artifact | Bytes | SHA-256 | S21 verification |
|---|---:|---|---|
| `kernel.elf` | 2,771,240 | `F5AA964DA871DEAD7259D766DBAA4F67BE5485AF9DF3AFCD7812FE559ECA9AEF` | Rehashed; matches S20 report and QEMU launch manifest. |
| `kernel.pe` | 2,888,634 | `159D82DADE3B2BF3EEE607FA6C286B561BC9128D4048F1A4096C6DAE17552589` | Rehashed; S20 build summary reports zero PE imports. |
| S20 final private raw media, post-run | 134,249,984 | `4A51FF256BB6E0E463365B66CA1137227553829E11B866FF10C7C0FF95AAC5B2` | Rehashed; accepted pre-run identity was deterministically reconstructed and matched an unrun sibling clone. |
| S20 read-only OVMF | 3,653,632 | `2DF617A6FD1BAE41EC7C0E3BCECA96C928A5A88D8DC198468D34733393AB2792` | Rehashed; matches the launch record. |

S20's build summary records 257 compile commands, zero compiler errors, the established 224 normalized warning signatures (no additions), 21 undefined ELF symbols (unchanged from S19), freestanding flags, and zero PE import directory. The real guest was task-owned, used a private QMP endpoint and writable raw-FAT image, and was shut down through that endpoint. These are accepted historical production results; S21 did not repeat the build or guest.

### Candidate-named artifacts currently in `out/`

The preserved `out/v03-amd64-qemu-candidate/` and `out/v03-amd64-scifi-final-candidate/` artifacts are from October 4–5, before S10/S11 native Settings. Their kernel artifacts are identical and therefore cannot prove or package the accepted S11–S20 implementation.

| Preserved artifact | Bytes | SHA-256 / provenance |
|---|---:|---|
| `out/v03-amd64-qemu-candidate/kernel.elf` | 2,677,184 | `1C0FA4325B1DA9D8220F08E16E404B70C6D8F6CA414ACD1E053D53747388D086` |
| `out/v03-amd64-qemu-candidate/ramdisk.img` | 67,108,864 | `A497B234C41EE936AEF402555EC067F2775DB3765FA612E41B23EB3D80B6F02F` |
| `out/v03-amd64-scifi-final-candidate/kernel.elf` and `kernel-build2.elf` | 2,677,184 each | Same `1C0FA432…388D086` kernel. The `acceptance-esp` copy matches. |
| `out/v03-amd64-scifi-final-candidate/acceptance-esp/ramdisk.img` | 67,108,864 | Same `A497B234…0B6F02F` ramdisk. |
| `out/v03-amd64-scifi-final-candidate/acceptance-esp/EFI/BOOT/BOOTX64.EFI` | 56,832 | Rehashed SHA-256 `E2FDB6AD78E0416AF250BEBBC6541640E3CCDC7DD9A14453C08143B7F4DA48AA`. |

The candidate build identity records `builtAtUtc=2026-10-05T04:14:40Z`; both candidate folder timestamps are October 4–5. They were read and hashed, not modified. No current post-S20 assembled v0.3 release package or package manifest was found. Thus native Settings is present and QEMU-qualified in the current S20 production build, but **not demonstrated in the existing candidate-named package artifacts**.

### Production integration checks

- `kernel/Makefile` compiles `core/*.cpp`, explicitly adds the shared presentation core, and uses freestanding kernel flags. The native provider/layout/runtime are in kernel core; root hosted `settings_center.cpp` is not linked into the kernel. No hosted-only dependency is required.
- Built-in metadata maps the native Settings factory. Start → Settings launches `gxos.builtin.settings`; Start → Control Panel remains a distinct legacy action. S12's failure path reports launch failure rather than silently redirecting to Control Panel. S20 repeated both routes in a production guest.
- Required bundled wallpaper assets are consumed through the existing desktop/VFS image pack and desktop owner. S17 raw-FAT guest evidence applied and rebooted a bundled wallpaper. The actual v0.3 candidate ramdisk still needs qualification because it is older than native Settings.
- System/About currently source kernel version `0.1.0` from `kernel/core/include/kernel/version.h`. There is no authoritative OS release metadata source. `build_identity.h` is a temporary file-paste verification marker, not release metadata. Do not hard-code Server v0.3 into About.

## 8. Evidence coverage and S21 execution

| Readiness requirement | Evidence coverage at starting HEAD | S21 action / remaining evidence |
|---|---|---|
| Native launch, category rendering, providers, lifecycle | S11–S20 reports; final S20 QEMU guest on exact HEAD. | No rerun; adequate for the current production build. |
| Provider authorization and invalid requests | S11/S13/S15–S18 negative tests and version/size/revision/ID validation. | Source-reviewed; no new security suite because source and artifact were unchanged. Note trusted-kernel-app boundary above. |
| Appearance persistence and legacy config compatibility | S16–S18 raw-FAT guest readback/reboot plus injected VFS failures; S20 config equality after focus/cancel flow. | No new write test; no current raw-FAT corruption repro. |
| Modifier-aware Settings navigation | S20 31 decoder tests, shared/runtime suites, serial QEMU Shift+Tab and Tab after release/reopen. | Added missing-break/re-tap/reinit decoder cases; 39/39 passed. |
| Missing-break through focus and app close/reopen | No S20 lost-break injection. Source shows modifier ownership remains in the driver across focus/app lifecycle. | Cross-boundary guest fault sequence remains inferred, not claimed as newly tested. Recovery is demonstrated at decoder level. |
| Resize, focus, pointer, legacy coexistence | S19R3 actual minimum/resize-back and lifecycle; S20 minimum-size focus and legacy Control Panel QEMU sequence. | No QEMU rerun; sufficient for current S20 artifact. |
| Current production binary identity | S20 build log, summary, launch manifests, media staging and SHA list. | ELF, PE, post-run raw image, and OVMF rehashed; identities match accepted records. |
| Actual v0.3 release package contains Settings/assets | Current candidate-named artifacts are October 4–5, before S10/S11; no post-S20 package found. | **Open release gate.** Build and boot the actual package once; do not rerun all historical suites. |

**New S21 validation only:** `scripts/run-ps2keyboard-modifier-test.ps1` compiled production `kernel/core/ps2keyboard.cpp` with the deterministic port stub and ran **39/39** checks. It reported the same existing always-true `uint8_t scancode < 256` warning recorded in S20. No production kernel build, full suite rerun, or QEMU guest was executed in S21.

## 9. Risk register

| Risk | Severity / likelihood evidence | Existing mitigation | Release action / blocks Settings? | Owner/workstream |
|---|---|---|---|---|
| Existing v0.3 candidate artifacts do not contain the S10–S20 native Settings implementation. | **High / certain for those artifacts:** October 4–5 kernels predate S10/S11 and hash differently from S20. | Current S20 production ELF has accepted build/QEMU evidence. | Rebuild and smoke-qualify the actual release package. **Blocks shipping Settings in those existing artifacts; does not block the feature after the package gate.** | v0.3 release integration |
| In-place FAT persistence is not crash-atomic. | **Moderate / inherent:** interruption during an update can leave on-disk content incomplete. | Bounded writes, readback, rollback attempts, explicit rollback-failure statuses. | State the durability limit; no S21 code gate. **Does not block Limited/Alpha.** | Desktop/config owner |
| Lost modifier break can leave Shift active. | **Low / reproducible but recoverable:** new decoder test reproduced it; a fresh same-key break clears it, and keyboard input remains usable. | Tap/release the affected Shift; startup driver initialization resets state. No in-session timeout/reset. | Include the short recovery note; focused input hardening is optional follow-up, not a v0.3 gate. | PS/2 input stack |
| Historical malformed config cause and whole-media hash delta are unresolved. | **Unclassified historical evidence; no current raw-FAT defect reproduced.** | Reject malformed config without mutation; verify canonical file bytes separately from whole-media hash. | Preserve as an evidence limitation; reopen only on a current supported-path repro. **No current block.** | Desktop/VFS investigation if reproduced |
| Ordinary production Display topology and OS release metadata are unavailable. | **Known capability limits:** topology backend is gated off; no OS release source exists. | UI reports unavailable/undefined and does not infer topology or label kernel `0.1.0` as OS release. | Document. **No block.** | Display/release metadata workstreams |
| Eight of twelve categories are hosted-only/deferred; icon auto-arrangement is session-only. | **Known and visible:** native rows are dim and explicitly marked deferred; auto-arrange is not persisted. | No fake controls or incomplete privileged operations are exposed. | State clearly in release notes; do not promise future versions. **No block.** | Settings roadmap |
| Caller-ID checks do not sandbox malicious kernel-resident code. | **Known architecture boundary:** provider API is direct C++; kernel apps share privilege and can enumerate running app pointers. No Native ELF/COM2 Settings mutation dispatch was found. | Current built-ins are trusted kernel code; no untrusted third-party KernelApp extension surface is supported. | Do not claim kernel-app isolation. Introduce capability-supplied caller context before changing that trust model. **No v0.3 block under current model.** | Kernel app security boundary |

## 10. Release decision and required gates

### Primary recommendation: B — Conditional readiness

The implemented native capabilities are stable and bounded enough for inclusion as **Limited/Alpha**. No demonstrated Settings crash, raw-FAT corruption, external authorization bypass, or unrecoverable keyboard failure blocks the feature. The condition is packaging: the only candidate-named v0.3 artifacts found are stale and predate native Settings.

Before Settings is included in a v0.3 package, satisfy these bounded gates:

1. **Build/package provenance:** Produce a new candidate in a unique output directory from the release-frozen source that includes S20 acceptance and this S21 record. Preserve the October 4–5 candidates. Record source commit, AMD64 ELF/PE, bootloader, ramdisk/assets, and final package hashes. Confirm the ordinary freestanding build includes native providers and no hosted runtime dependency.
2. **One exact-package smoke boot:** Boot the produced candidate on isolated writable media. Verify Start → Settings opens `gxos.builtin.settings`, System/Display/Personalization/About render, the eight deferred rows remain visibly unavailable, a bundled appearance asset is present, and Start → Control Panel still launches independently. Capture the launch log/screenshot and package/media identities. If a disposable package image is used, verify one supported appearance Apply/readback or retain a clear asset/catalog readback proving the packaged path.

The existing S10–S20 acceptance suites do not need to be rerun for this packaging gate unless the candidate source or artifact differs materially from the accepted S20 build. No new feature flag or disabled capability is required.

## 11. Alpha limits and deferred roadmap

Document these current limits: only System, Display, Personalization, and About are native; Display is informational and ordinary topology/modes may be unavailable; About has no authoritative OS release version; appearance controls are limited to built-in presets and supported icon visibility; desktop auto-arrangement is session-only; writes have readback/rollback behavior but no crash-atomic or power-loss guarantee; a stuck Shift can be cleared by tapping and releasing the affected physical Shift key; hosted Settings categories are not native production features.

Possible later work includes additional native Network, Devices, Storage, Apps, Users, Date & Time, Accessibility, and Developer pages; broader provider management; desktop themes; font selection/TrueType integration; configurable bitmap fallback; display topology; richer icon arrangement; and persistence hardening. These are roadmap candidates, not v0.3.1 promises. Do not delay v0.3 for them absent a new demonstrated critical requirement.

### Suggested release-note wording

> **Native Settings (Alpha)** provides read-only System/About information, active framebuffer details where available, and Personalization controls for bundled gradients, bundled wallpapers, and supported desktop icon visibility. Network & Internet, Devices, Storage, Apps, Users, Date & Time, Accessibility, and Developer are currently unavailable in native Settings. Display topology and the OS release version may be reported as unavailable. Appearance changes are checked by runtime readback and have bounded rollback behavior; they are not crash-atomic. Desktop icon auto-arrangement is not persisted. If Shift remains active after an input loss, tap and release the same physical Shift key to restore ordinary Tab behavior.

## 12. Acceptance boundary and recommended next task

- S21 is **Outcome A — Audit Accepted**: accepted evidence is inventoried; current implementation, trust boundary, persistence, key-up risk, input lifecycle, and packaging are classified; one focused risk test is newly recorded; no production feature was added.
- S9 remains Outcome B, S9R's hosted/native integration assessment remains historical Outcome C, and Settings 1.0 remains unaccepted. S21 does not override those records.
- This report does not accept Server v0.3 overall. It recommends conditional inclusion of native Settings after the two package gates above.
- **Recommended S22 scope:** “v0.3 candidate package integration and native Settings smoke qualification.” Build the exact release candidate from frozen current source into a new output path; verify hashes and bundled appearance assets; boot that exact package once on isolated media; verify the two Start routes, four supported pages, deferred-page behavior, and Settings identity; save the manifest/screenshots/log; report package-gate outcome. Do not expand native Settings categories or claim overall v0.3 acceptance in S22.

The S21 report and focused decoder test are committed locally on `main`; no push or release publication is part of this audit.
