# S22R14 Release-Wide Regression and Stability Qualification

**Assessment:** `RELEASE_BLOCKER_CONFIRMED` (Outcome C)
**Candidate:** Server v0.3.0 AMD64 PacMan-only, frozen S22R12 image
**Decision boundary:** This report qualifies evidence for release-owner review. It does not assign RC-ready status, publish v0.3, or accept security risks for the release owner.

## 1. Repository provenance and evidence checkpoint

Preflight began on branch `main` at expected HEAD `454c8f14dc2048a7174cc517c3499ef62c521a5f`. The expected commit was an ancestor of HEAD; the tracked worktree was clean. The S22R13R5 report and its evidence were initially uncommitted. `.phase` was absent in this repository. The separate QAC repository phase file was read and left unchanged (`QAC1`, `QAC2`, `QAC3A/B/C1/C2` accepted; `QAC3` open; next `QAC3C3`).

The R5 report was checked against the actual serial log, acceptance extract, QAC session record, candidate receipt, and evidence manifest. Those sources agree on the candidate hash, guest actions, and outcome. The accepted run was QAC-managed from 2026-10-24 14:45:42.663 to 15:00:09.676 UTC (14 minutes 27 seconds); QEMU exited 0. It covered cold boot, desktop and Start menu interaction, Settings Display navigation and display requests, PacMan discovery/launch/gameplay/exit cleanup, Navigator package-manifest lookup, TrueType typography smoke, and VFS mount integrity. No candidate ISO bytes changed.

The narrow evidence checkpoint is commit **`530e2144cc63156d239925ff8a81f7d969e60dae`** (`docs: checkpoint S22R13R5 frozen guest evidence`), on top of the expected HEAD. It contains 43 files: the validated R5 report and appropriately sized/hash-identified supporting evidence. The R5 bundle contained 132 raw files. Its `retention-manifest.tsv` identifies 41 repo-checkpointed files (about 4.38 MiB) and 91 files retained in place only (about 208.59 MiB), with hashes for both groups. The local-only group is not repository-persisted evidence; it is mainly screenshots/PPM captures and a 64 MiB guest ramdisk. The raw evidence remains at its existing paths.

The pre-checkpoint inventory was approximately 10,009 untracked paths. No pre-existing path was cleaned or moved. Qualification generated additional ignored/untracked build and test output; the latest full untracked-file listing was 10,683 paths. The final commit in this report is the only subsequent tracked change. No reset, clean, stash, rebase, merge, cherry-pick, branch switch, worktree, or push was performed.

## 2. Frozen candidate identity

| Item | Verified value |
|---|---|
| ISO | `out/release-profile/0eed0ed2-df00-403a-9646-0dffb22be3bb/artifacts/guideXOS-Server-v0.3.0-amd64-pacman-only-candidate.iso` |
| SHA-256 | `b6b7b0faeef779a4ac06abb96ad749d2ce906df2e7751c1b9c329dd0a23fc2af` |
| Build ID / profile | `0eed0ed2-df00-403a-9646-0dffb22be3bb` / `ServerV03PacManOnly` |
| Candidate source commit | `ea786e2da4da01a57b1ab00429c8453edc9446ad` |
| Native ELF package set | `Apps/PacMan` only; Developer Studio absent |
| Older S22R9 ISO SHA-256 | `e98bf087efc071ddf82db4dce9d79335aacbf38310de43b032744165792d712b` |

The full ISO SHA-256 matches the release receipt and R5 evidence. The receipt records kernel and package hashes; the R5 private ESP listing/hash records the candidate guest files. The original S22R9 ISO and its recorded hash were preserved. Neither ISO was modified. R5 acceptance belongs to this hash only and is not transferred to any rebuild.

## 3. Actual Server v0.3 ship-set inventory

The canonical release contract is `ServerV03PacManOnly`; the release build invocation must select it explicitly. It requires PacMan, excludes Developer Studio, and the packaging gate rejects unexpected Native ELF packages and unsupported manifests. The `build.ps1` default is Development, so an omitted profile is not a release build.

| Feature | Ships in this image | Required behavior and evidence | Remaining checks / known limits | Release relevance |
|---|---|---|---|---|
| Kernel-integrated built-ins | Yes: Notepad, Calculator, Clock, File Explorer, Trash, TaskManager, ImageViewer, DiskManager, Settings, DisplayOptions, guideXOS Navigator | Built-in factories/registry exist; hosted lifecycle suites pass. R5 used Settings and Navigator. | Not every built-in received an individual guest launch and close in R5. | Core OS functionality; broad per-app guest coverage remains incomplete. |
| Native ELF packages | Yes: PacMan only | Release receipt and guest discovery, registration, launch, gameplay, exit cleanup pass for the frozen hash. | Privileged execution and compatibility/security limits in §9. | Core for this PacMan-only candidate. |
| Start menu and desktop | Yes: desktop, taskbar, compositor, window/input dispatch and Start menu | R5 desktop/Start interactions; `window-ui-regression` hosted pass. The menu has 16 historical rows, including embedded paths and aliases; it is not a list of 16 independent shipped packages. | Individual guest lifecycle of every row not demonstrated. Paint and HDInstaller have no bare-metal factory; Console, ControlPanel and AppModel have embedded paths; ImgViewer is a legacy alias. | Core shell; feature inventory must not imply absent applications ship. |
| Display and input | Yes: UEFI framebuffer display (1280x800 in the accepted guest), PS/2 keyboard/mouse | R5 pointer/keyboard interaction and Settings Display; hosted pointer and PS/2 modifier suites pass. | USB input paths exist in source but were not established as active/qualified in this QEMU session. | Core interaction; stack blocker is reachable through Settings Display. |
| Settings and Navigator | Yes | R5 Settings navigation and Navigator `/Apps/PacMan/app.json` lookup; hosted Settings and Navigator suites pass. | `/Documents` and stack concerns are described below. | Shipped user-facing workflows. |
| Storage and filesystems | Yes: block/VFS path and FAT32 guest filesystem; accepted mounts `/`, `/system`, `/certs`, `/config`, `/config/navigator` | R5 mount integrity; hosted mount registration, generation/ownership and FAT/VFS contract checks pass. | R5 guest qualifies the observed FAT32/ATA path only. Ext4/NTFS/XFS and additional block drivers present in source are not thereby guest-qualified. `/Documents` creation fails. | Core boot, package discovery and file access. |
| Fonts and theme | Yes: Roboto TrueType atlas and integrated UI theme | R5 typography smoke and screenshots; image/font-adapter hosted checks pass. | No exhaustive glyph, locale, scaling, or accessibility survey. | Readability and presentation quality. |
| App lifecycle services | Yes: built-in app registry/window management and Native ELF lifecycle | Hosted registry, close/cleanup and service suites pass; R5 PacMan cleanup and return to desktop pass. | General Native ELF remains kernel-privileged, and only the accepted PacMan binary was exercised. | Core stability and security boundary. |
| Network, audio, other source subsystems | Some code is present; the v0.3 profile does not establish all of it as qualified release behavior | Hosted audio mixer/stream checks pass; PacMan audio resources are in the accepted package. | No release-wide guest qualification of network, DHCP, all audio devices, or source-only drivers was performed. | Do not describe as qualified merely because code exists. |

The Start-menu inventory includes legacy/historical rows for Console, ControlPanel, HDInstaller, AppModel, Paint and ImgViewer. These should not be counted as separate Native ELF packages. The only Native ELF package in the candidate is PacMan.

## 4. Boot-stack and memory-safety audit

The AMD64 freestanding kernel was built into isolated `out/s22r14-stack-audit/kernel` outputs with compiler stack-usage and call-graph records (`-fstack-usage`, `-fcallgraph-info=su`, `-Wframe-larger-than=4096`, `-Wstack-usage=8192`) and a linker map. The kernel symbol audit passed: ELF64 AMD64, 45 undefined-symbol rows (21 distinct `.symtab` entries), no relocations/imports. The build produced 3,522 `.su` function records: 38 static frames exceed 4 KiB, 16 exceed 8 KiB, and none were reported dynamic. These are per-function static frame estimates, not a proven whole-program maximum call-chain measurement.

S22R12 had previously identified a `0x52108`-byte `NavigatorApp` frame on the 16 KiB boot stack, which overwrote BSS VFS mount metadata during typography smoke. That specific root cause was repaired by moving seven `NavigatorApp` objects to scoped heap allocations; the `navigator-stack-contract` hosted suite passes seven fixture checks. This S22R14 audit checks other reachable paths and does not reopen the repaired typography defect.

### Confirmed release blocker: Settings Display exceeds the boot stack

The AMD64 boot stack is 16,384 bytes (`kernel/arch/amd64/boot.asm`, `stack_bottom` to `stack_top`). `_start` assigns it and calls `kernel_main`; built-in application initialization and Settings callbacks do not switch stacks. No stack guard, canary, overflow check, or runtime watermark was found. Compiler flags include `-fno-stack-protector`, `-fno-stack-check`, and `-mno-stack-arg-probe`.

The measured reachable chain is:

| Function on the same stack | Static frame |
|---|---:|
| `settings_native::readDisplaySnapshot` | 9,296 bytes |
| `DisplayConfigurationService::submit` | 11,120 bytes |
| **Nested subtotal** | **20,416 bytes** |
| Available boot stack | 16,384 bytes |

Settings initialization calls display snapshot refresh, and Display navigation refreshes it as well. The direct synchronous path has no stack switch. The nested subtotal is 4,032 bytes beyond the entire boot stack, before caller frames such as Settings/app-manager/desktop/`kernel_main`. The accepted R5 session did navigate to Display and submitted display requests without a reproduced panic; a successful interaction does not bound stack high-water or disprove the compiled frame estimates. This is a serious potential stack/BSS corruption path and is a release blocker. The underlying relevant source files match candidate source commit `ea786e2...`; the differences between current `main` and that source do not alter these frames.

### Second reachable over-budget service chain

The COM2 read-only system service polling path is called from `kernel_main` on the same boot stack. A valid storage request traverses `system_service_bridge::serveRequest` (4,944 bytes), `system_service::dispatch` (9,856 bytes), and `settings_inventory_provider::readStorageSnapshot` (9,136 bytes): a 23,936-byte nested subtotal, before `poll` (96 bytes) and `kernel_main` (896 bytes). The device snapshot route is also at least 20,144 bytes. These callbacks do not switch stacks. This is a second serious potential stack-overflow/corruption path; it is reachable via the service request interface. It was not reproduced as runtime corruption in this phase.

Other notable frames include `load_persisted_app_shortcuts` 8,672 bytes, `persist_app_shortcuts` 8,304, `load_icon_positions` 4,624, recent-history load 4,304, `stbi__parse_png_file` 7,952, and `NavigatorApp::submitFormForBlock` 8,928. The separate 16 KiB exception IST stack has no guard; Native ELF app stacks are 128 KiB but do not provide privilege or address-space isolation. Startup-only `desktop::init` and `kernel_main` frames are 336 and 896 bytes respectively, but those numbers do not prove all startup call chains fit.

**Disposition:** do not advance the candidate for publication. The first demonstrated failure is the static nested Settings Display path exceeding the known boot stack. S22R15 should narrowly move large provider snapshots/temporary records off the boot stack or otherwise reduce these frames; add path-aware stack-budget checks and runtime high-water/guard evidence; then rerun the affected Settings, COM2 storage, VFS mount-integrity and QAC paths on a separately identified candidate. Do not treat this bounded audit as comprehensive memory-safety proof.

## 5. Hosted and freestanding regression results

All listed hosted suites passed. Counts below are the suites' emitted assertions/cases; entries marked “PASS, no count emitted” are compile-time or script assertions without a numeric case count. Repeated lifecycle counts are stated separately where the runner reported them. No product test failure was observed. This is not an aggregate release-wide test claim; the scope is the named suites below.

| Suite | Result |
|---|---|
| block-device-generation | 5/5 |
| vfs-mount-table-integrity | 16/16 |
| vfs-hosted-read | 4/4 |
| native-filesystem-contract | PASS, no count emitted |
| app-model-identity | 25/25 |
| app-model-file-activation | 60/60 |
| appmodel-folder-activation | 26/26; 100 valid and 100 invalid lifecycle cycles |
| appmodel-application-actions | 55/55; 100 lifecycle cycles |
| appmodel-default-handlers | 54/54; 100 reload cycles and 100 dispatches per handler |
| built-in-document-dispatcher | 21/21; 100 PNG/Navigator/JPEG lifecycle cycles |
| file-explorer-open-with | 17/17; 100 lifecycle cycles |
| appmodel-uri-protocol | 41/41; 100 persistence/activation/no-handler cycles |
| native-abi-layout | PASS, compile-time layout assertions; no count emitted |
| native-present-validation | PASS, no count emitted |
| compositor-pointer-buttons | PASS, no count emitted |
| ps2keyboard-modifier | 39/39 |
| settings-center-model | 89/89 |
| settings-inventory-model | device 21/21; storage 24/24 |
| settings-native-layout | 1,037/1,037; 1,000 resize-geometry transitions |
| settings-native-runtime | 1,426/1,426; 100 lifecycle, 250 refresh, 1,000 notification transitions |
| settings-presentation-core | 3,659/3,659 |
| settings-default-apps | 39 + 9 cases; 100 lifecycle cycles |
| settings-s6-model | 42/42 |
| settings-s7-model | accessibility 21; developer 23; navigation/search 14 |
| settings-users-model | 11 + 8 + 13 cases |
| image-adapter-bounds | 11/11 |
| navigator-app-action-delivery | 20/20 |
| navigator-local-document | 19/19 |
| navigator-stack-contract | PASS, 7 fixture checks for heap-scoped app instances |
| PacMan source registration | 4 fixture cases |
| Navigator URL resolution | 11 cases |
| system-service-bridge | device 13 + storage 12; combined 78/78 |
| window-ui-regression | PASS, no count emitted |
| file-operations regression | PASS, no count emitted |
| PNG codec | PASS, no count emitted |
| JPEG codec | PASS, no count emitted |
| audio mixer | PASS, no count emitted |
| audio stream | PASS, no count emitted |
| release-build-integrity | PASS, 33 assertions |
| release-ISO packaging synthetic FAT32 package/read-back | PASS; synthetic image is not bootable |

The scope includes VFS mount lifecycle, block generation and ownership, filesystem contracts, native ABI and package validation, PacMan registration, app lifecycle, desktop/window behavior, Settings, Navigator, image/font adapters, input dispatch, package/build checks and representative file operations. It does not establish guest behavior for every built-in app, every filesystem/driver, USB input, network, or every Native ELF ABI combination. Some fixture suites include Developer Studio data; those fixtures do not mean Developer Studio ships.

The freestanding AMD64 stack-audit build and symbol audit passed as described in §4. Benign compiler warnings remain (including existing initializer/indentation/unused warnings and codec warnings); no warning was treated as a test failure. The canonical isolated release build failure is recorded separately in §6. One initial hosted-runner invocation supplied an unsupported output option for `settings-native-runtime`; it fell back to an ignored existing `out/validation/.../settings_native_runtime_test.exe` path. The suite was rerun with a unique output and passed. That ignored executable may have been replaced. No tracked source was changed by test execution; no generated output has been staged.

## 6. Release build, packaging, and reproducibility

The release profile contract and `release-build-integrity` checks confirm explicit selection, required PacMan, Developer Studio exclusion, rejection of unexpected Native ELF packages and unsupported manifests, unique build-ID staging, and no reuse of an existing release staging directory. The canonical PacMan-only ISO script disallows `SkipBuild`, requires version `0.3.0`, and records build inputs/outputs through receipts and hashes. The accepted candidate is a structurally bootable AMD64 image, proven by its receipt and QAC cold boot. Synthetic FAT32 packaging/read-back checks passed but are not a bootability test.

An isolated source clone at candidate commit `ea786e2da4da01a57b1ab00429c8453edc9446ad` was used for one canonical build attempt with a new build ID; it did not touch the accepted candidate or `dist`. The first attempt identified a missing local Mbed TLS dependency. After copying the locally pinned and patched dependency profile, its verification passed (Mbed TLS 4.1.0 commit `0fe989b6b514192783c469039edd325fd0989806`; TF-PSA-Crypto 1.1.0 commit `29160dd877d29658279fd683b2ae57b320ddcf09`; three patches). Missing untracked PacMan audio source inputs were copied into the clone and their hashes matched the frozen package resources. The PacMan ELF and manifest then reproduced exactly (SHA-256 `eeb544bbb2e2574eeaed7bb7f0bad61ea24f6945231b7757223a225b30b8e1cc` and `3f3592e6c76053e323f16d48073e98c67d7f5702b8e47f0b507d6bfb3d126449`).

Available disk space and unique output destinations were checked before creating the isolated build. No freestanding source modification was made during S22R14; no STL/`std::`, exceptions, RTTI, or unsupported runtime dependency was introduced.

The kernel stage failed before producing a second ISO. MinGW's `stralign.h` references undeclared `_wcsicmp` while compiling `core/native_elf_baremetal.cpp`; `make` exited 2. The PowerShell wrapper returned process exit 0 but its integrity check correctly detected the stage error and failed the build. Therefore there is no second ISO, no whole-image/ESP/kernel comparison, and no byte-identical reproducibility claim. This could be a source/toolchain/configuration compatibility issue; the exact cause was not resolved within this bounded phase. Candidate provenance is traceable to its receipt, source commit, package hashes, and recorded external/untracked inputs, but not reproducible end-to-end from the isolated attempt. Release build status remains incomplete pending a clean successful isolated build and content comparison.

## 7. QAC guest and stability results

The accepted R5 managed guest session is reused rather than repeating the same focus checks. Its 14m27s run is the actual recorded duration, not a newly performed S22R14 soak. It established one cold boot, successful desktop startup, Settings Display navigation, PacMan launch/gameplay/cleanup, Navigator PacMan manifest lookup, TrueType typography smoke, five VFS mounts, and no fatal boot/guest error in that session. The candidate SHA was checked and unchanged.

QAC ownership/reservation state was inspected: QAC was online with no active session, reservation, or queued request. No new admission was requested after the stack audit identified the potential memory corruption path. Thus this report makes no new claim about soak duration, reboot reliability, all built-in apps, persistent-storage behavior under stress, or absence of corruption. The R5 session had no reproduced panic or mount corruption, but is not a stack high-water measurement. The existing QAC phase guard was preserved.

## 8. `/Documents` limitation

The shipped Start menu exposes Documents and desktop standard-folder initialization attempts to create `/Documents`. On the accepted candidate, FAT32 reports `FAT_CREATE_INVALID_NAME`, while block I/O reports `BLOCK_OK`; the VFS returns `VFS_ERR_INVALID`. The FAT writer's short-name path rejects the nine-character base name `Documents`; this release path does not create/use a long filename for it. As a result, the Documents shortcut/folder workflow does not work and standard folder creation is incomplete. The failure is recorded in the R5 serial log and `acceptance-extract.txt`.

No evidence showed user data loss or damage to other mounts. The reviewed release documentation did not promise `/Documents` by name, but the shipped Start/desktop UI implies the workflow. **Disposition: acceptable only for a clearly scoped preview with an explicit `/Documents` limitation.** If the intended v0.3 contract includes working standard user folders or the Documents menu workflow, this becomes a must-fix publication blocker. The release owner must make that contract decision; this report does not waive the limitation.

## 9. `/.startmenu_recent` persistence limitation

FAT32 rejects the dotted root filename for recent-history persistence with `FAT_FILE_WRITE_INVALID_NAME`; VFS returns `VFS_ERR_INVALID`. In-memory session recents still work, but recent history does not survive restart. The accepted serial evidence records one failed create/write and no retry loop. No mount or broader FAT corruption was observed. **Disposition: a documented preview limitation unless the v0.3 contract requires persisted recent history.** It is user-visible after restart but does not make session history unusable. No speculative filename workaround was applied. Owner decision is required before claiming the corresponding persistence behavior.

## 10. Native ELF trust and compatibility boundary

The PacMan-only profile constrains the package set embedded in the official ISO; it does not constrain every package later discovered in writable `/Apps`. The release root/ESP is writable storage. Ordinary file-copy workflows through shipped file-management functionality and direct physical modification can add or replace files under `/Apps`; there is no dedicated installer boundary. Package discovery runs from that storage at boot (and registry refresh paths can rediscover packages).

The runtime checks package-manifest parseability and selected fields (`kind=NativeElf`, AMD64 architecture, `entryPoint=gx_main`, `abi=guidexos-c-abi-v1`) and validates that an executable path can be loaded as ELF64 AMD64. These checks are not signatures, a trusted identity, or a hash binding between manifest and executable. Runtime compatibility negotiation is insufficient: the v0.3 host table is 256 bytes and a later v0.5 layout is 456 bytes; a v0.5 callback at offset 240 can be interpreted as the v0.3 `play_pcm` slot. The loader does not enforce manifest minimum-version metadata or an adequate host-table size/callback contract. ELF entry validation also does not establish that the entry lies in an executable segment.

**Compatibility risk:** an incompatible but otherwise non-malicious package may call the wrong host callback or corrupt state.
**Malicious-code risk:** accepted Native ELF executes with kernel privilege and no enforceable address-space isolation; it can corrupt kernel memory. No untrusted ELF was executed in this audit. A manifest, package allowlist in build scripts, or user documentation cannot create a runtime security boundary the loader does not enforce.

Feasible bounded follow-up mitigations include fail-closed ABI version/table-size negotiation, validate entry and executable ranges, deny runtime package discovery/launch from writable storage by default or require an explicit trusted boot policy, and establish authenticated package identity before load. These mitigate compatibility and loading exposure but do not contain code once launched. Future user-mode page tables, syscall mediation, privilege transitions, and verified per-process memory separation remain necessary for enforceable application isolation.

### Release-owner decision record required

| Deployment model | Current assessment |
|---|---|
| Trusted first-party experimental/preview | Could be considered only if the owner explicitly scopes the audience and update/storage trust assumptions, approves the known privileged Native ELF risk, and publishes clear limitations. This report does not accept that risk. The stack blocker still prevents publication progression. |
| General release permitting third-party Native ELF | **Blocked.** Writable package discovery plus kernel-privileged execution and inadequate compatibility negotiation are incompatible with a general third-party trust boundary. |
| General release with enforceable application isolation | **Blocked / not implemented.** This requires future architecture; current process stacks do not provide isolation. |

The owner must select the intended model and explicitly decide whether first-party preview risk is acceptable. No model is silently selected here, and no RC-ready or publication status is assigned.

## 11. Release-readiness matrix

| Category | Status | Evidence and remaining gap |
|---|---|---|
| Boot reliability | PASS WITH DOCUMENTED LIMITATION | Frozen-hash QAC cold boot and 14m27 accepted guest session passed. No new S22R14 boot/soak; one run does not establish broad hardware coverage or stack safety. |
| Kernel stack safety | **BLOCKED** | Settings Display static nested frames 20,416 > 16,384-byte boot stack; COM2 storage route 23,936 > 16,384. No guard/high-water check. Serious potential memory corruption; targeted remediation and requalification required. |
| Desktop and UI | PASS WITH DOCUMENTED LIMITATION | R5 desktop, Start, Settings Display, typography and close/return behavior passed; hosted window/input checks passed. `/Documents` UI path is broken; not every built-in/row received guest acceptance. |
| Built-in apps | INCOMPLETE | Eleven kernel-integrated factories are present; hosted lifecycle/dispatch checks pass. R5 covered Settings and Navigator, not individual guest acceptance of every built-in. Several historical Start rows are aliases/embedded/unavailable. |
| PacMan | PASS | Frozen ISO hash verified; exactly one Native ELF package; QAC discovery, launch, gameplay and cleanup accepted for this hash. |
| VFS/storage | PASS WITH DOCUMENTED LIMITATION | Five R5 mounts and hosted registration/generation/FAT/VFS checks passed. `/Documents` creation fails; `/.startmenu_recent` persistence fails; only observed FAT32 guest path is qualified. |
| Native ABI | BLOCKED | Layout checks pass for the tested v0.3 layout, but v0.3/v0.5 runtime negotiation is inadequate and Native ELF is privileged. Third-party package model cannot be supported responsibly by current checks. |
| Release build | INCOMPLETE | Profile and packaging guardrails plus receipt checks pass; isolated build reproduced PacMan package files but failed compiling kernel at `_wcsicmp`. No complete reproduction or ISO content comparison. |
| Security | **BLOCKED** for general third-party/isolation models; owner decision pending for trusted preview | Writable `/Apps`, unsigned/unbound manifests, privileged execution and missing isolation remain. No risk accepted by this report. |
| Documentation | INCOMPLETE | Must state the owner-selected audience/security model, `/Documents` and recent-history limitations where relevant, supported storage scope, and remaining Native ELF boundary before any preview decision. |

## 12. Specific blockers and remaining work

1. **Memory integrity:** reduce the proven over-budget reachable stack chains or provide a separately justified, guarded stack design; add stack-budget/high-water evidence; rerun affected functionality. This is the first demonstrated publication blocker.
2. **Release model/security decision:** release owner must select trusted first-party preview, general third-party, or isolated general release. General third-party and isolation models are blocked by current Native ELF behavior. Preview use requires explicit owner acceptance and a documented scope; no publication is authorized here.
3. **Build reproducibility:** resolve the isolated MinGW `_wcsicmp` failure and complete one fresh canonical release build, compare kernel, packages, ESP/filesystem contents and explain nondeterministic metadata. Never substitute the new build for this accepted candidate without separate identity and qualification.
4. **Contract decisions:** owner must decide whether Documents and persisted recent history are mandatory for v0.3. If so, each is a publication blocker; if preview-scoped, publish explicit limitations.
5. **Coverage/documents:** qualify remaining built-in guest workflows and supported hardware/filesystem claims as required by the chosen release contract; finalize documentation only after the owner decision.

## 13. Recommended S22R15 scope

Keep the phase bounded to the demonstrated blockers: (1) move or reduce large Settings display/storage inventory stack objects and other reachable >8 KiB stack objects, add targeted compile-time frame/call-path budget checks plus runtime guard/high-water evidence, and rerun Settings Display, COM2 storage, and VFS integrity; (2) repair or explicitly exclude `/Documents` and recent-history persistence according to the release owner’s contract; (3) make Native ELF ABI/version/range validation fail closed and document the remaining privilege risk without claiming isolation; (4) fix the isolated build toolchain compatibility and complete meaningful content comparison. Any changed binary must receive a new candidate identity and independent acceptance. Do not add Developer Studio, implement full process isolation, alter `dist`, or publish v0.3 as part of this qualification.

## 14. Outcome

**Outcome C — `RELEASE_BLOCKER_CONFIRMED`.** The static Settings Display path exceeds the known AMD64 boot stack by at least 4,032 bytes before its caller frames; the COM2 storage path is also over budget. This is a serious potential memory-integrity defect on a shipped, reachable path. Accepted R5 guest results remain valid for the frozen hash, but do not disprove the stack finding. Preserve the candidate and accepted evidence for diagnosis; do not proceed to release-owner publication approval until the stack blocker is resolved and a separately identified candidate is requalified.

## Evidence index

- S22R13R5 report: `docs/RELEASE_S22R13R5_FROZEN_CANDIDATE_ACCEPTANCE.md`
- R5 checkpointed evidence and retention manifest: `docs/evidence/s22r13r5-frozen-candidate-acceptance-20261024/`
- R5 serial and explicit limitation extracts: `boot1/serial.log`, `boot1/acceptance-extract.txt`
- Stack usage, call graph, map, and symbol audit: `out/s22r14-stack-audit/kernel/` (local generated evidence, not committed)
- Hosted suite outputs: `out/s22r14-hosted-tests/` and named validation output directories (local generated evidence, not committed)
- Isolated build logs/artifacts: `out/s22r14-repro-isolated/` (local generated evidence, not committed)

Generated audit, hosted test, and isolated build output is retained locally but is not repository-persisted evidence. The tracked report records the findings and relevant identifiers; the local directories remain outside the evidence checkpoint.
