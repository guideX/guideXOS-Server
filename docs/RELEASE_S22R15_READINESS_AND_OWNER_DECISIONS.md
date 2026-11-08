# Server v0.3 — S22R15 Release Readiness and Owner Decisions

**Outcome A — `RELEASE_READY_FOR_OWNER_REVIEW`.** The accepted candidate, bounded technical qualification, reproducibility limits, and remaining product/security choices are consolidated below. The release-integrity discrepancy is explained and corrected as a Windows checkout line-ending issue; the profile-aware package rules remain enabled. This outcome means the decision package is ready for its owner. It does not authorize publication, promotion, sign-off, or RC-ready status.

## 1. Repository and candidate provenance

### Repository preflight

- Repository: `D:\dev\guideXOSServer`.
- Branch and starting HEAD: `main`, `f5646c5fdf269438d28fb075071e56db47d6c11e`, matching the requested checkpoint. Its parent is `58658c4860dfc17d1e6b2951040d81e962b2982c`; `main` is seven commits ahead of `origin/main`, with no divergence.
- Tracked worktree was clean at preflight. The one visible untracked file, `assets/Backgrounds/troncity.png`, remains untracked and unchanged (SHA-256 `444ac64211907d80f0266e2283960cefaa932803d0aee7e0ccfd414f6fae2fda`). Ignored outputs and all earlier artifacts were preserved.
- No Main Integration `.phase` file was present. The sibling QAC `.phase` was read only and still records QAC3 open / next QAC3C3.
- Live read-only QAC checks reported Online, zero reservations, zero managed sessions, zero queued or blocked requests, and zero QEMU children. The QAC coordinator and GUI were running; no guest or concurrent QAC session was interrupted.
- No branch/worktree switch, reset, clean, stash, rebase, merge, cherry-pick, push, release-channel change, readiness-marker change, or `dist` change was made. No ISO was rebuilt.

### Receipt-selected candidate

The exact accepted candidate is `out/release-profile/6ad9bb77-12ce-4b4d-8ed0-56ff3944ee5d/artifacts/guideXOS-Server-v0.3.0-amd64-pacman-only-candidate.iso`.

| Check | Direct result |
|---|---|
| ISO SHA-256 | `2fe2f5eda87c9a80164715a156af5de12c7cf3891d29a961c40012c9d032241e`; matched the ISO sidecar and release manifest. |
| ISO size and structure | 90,245,120 bytes; recorded ISO verification says bootable UEFI, platform ID `0xEF`, FAT32 boot image 90,177,536 bytes. Boot-image SHA-256: `ebaa999bf918040d12b3a7450cb68bdcc8e2ecd313155b216f3459a3656eeb41`. |
| Receipt | `Assert-ReleaseBuildIdentity` passed for build ID `6ad9bb77-12ce-4b4d-8ed0-56ff3944ee5d`, source commit `5b62216efe78158fb12707c126ec38582faecf59`, profile `ServerV03PacManOnly`, target `0.3.0`. |
| Kernel | 2,795,344 bytes; SHA-256 `b5e029d23f9c21b5ff0838d9388322bf95b211bf3ef98145ddd344170dfe95dd`, matching the receipt and release manifest. |
| Other required ESP inputs | Bootloader: 56,832 bytes, SHA-256 `70eeebc388c74ca55a0f4760e6d4a587160372787c400156b01b2e3dfed8eee2`. Ramdisk: 67,108,864 bytes, SHA-256 `27fc2310f78aa38d47a2813f07cbec829dd81cfa4211f90b28e28cb572feb578`. |
| Package selection | `candidateOnly=true`; exactly one Native ELF package, PacMan. Developer Studio is absent and its stage is recorded `not_selected`. |
| ESP comparison | All 16 paths in the release manifest were present in the staged ESP with the recorded sizes and SHA-256 values. The ESP contains 16 files total: 11 PacMan package files plus the bootloader, kernel, ramdisk, symbol audit, and build identity. |

The receipt records source branch `main` and `worktreeCleanAtPackagingStart=false`. This dirtiness is part of the candidate's provenance and is not silently treated as a clean-source build. The candidate's receipt, payload inventory, ISO hash, and full identity were re-read in this phase. The guest acceptance record for this identity is S22R14R1R5/R1R6, not the earlier S22R13R5 ISO (`b6b7…c2af`). See [S22R14R1R5 guest evidence](RELEASE_S22R14R1R5_GUEST_INTERACTION_AND_COM2_COVERAGE.md) and [S22R14R1R6 COM2 and legacy evidence](RELEASE_S22R14R1R6_COM2_AND_LEGACY_COVERAGE.md).

The receipt-selected `6ad9…` ISO remains at its original path. Build 3 and Build 4 are separate candidate-only outputs under `out/s22r14r2-build-repro/build-3/source/` and `build-4/source/`; neither replaced the accepted output nor received guest acceptance.

The v0.3 inventory is the PacMan-only Native ELF release profile. Settings, Navigator, Display Options, and the other listed built-ins are kernel-integrated applications, not additional Native ELF packages. Historical Start-menu aliases/embedded entries are not evidence that separate packages ship.

### S22R9–S22R14 evidence progression

| Phase | Finding and evidence boundary |
|---|---|
| S22R9 | Qualified offline `ServerV03PacManOnly` composition and hosted package gates; guest acceptance was explicitly pending. |
| S22R10–S22R11 | Guest initially discovered PacMan and then lost `/Apps` during Start refresh. R11 isolated the first observed break to inconsistent VFS mount state; the writer was still unknown. PacMan launch was not accepted. |
| S22R12 | Identified the `NavigatorApp` automatic-frame overflow as the VFS mount-table corruption source; moved the large objects to scoped heap allocations and passed hosted/freestanding checks. Guest qualification remained pending. |
| S22R13 | R1 had two successful PacMan/Navigator boots; R2/R3 interaction attempts were incomplete; R4 input results belonged to a diagnostic image only. R5 completed the focused guest path on the then-frozen ISO `b6b7…c2af`. These results are preserved but do not substitute for the later `2fe2…241e` candidate. |
| S22R14 initial | Release-wide stack audit found reachable Settings Display and COM2 chains above the 16 KiB boot stack. It also retained `/Documents`, recent history, and Native ELF risks as release-owner items. |
| S22R14R1–R1R6 | R1 repaired Settings and COM2 stack pressure; R1R1–R1R4 documented QAC/QMP control failures and partial guest evidence. R1R5 then accepted modern Settings navigation, PacMan, Navigator, and VFS interaction on the exact `2fe2…241e` candidate. R1R6 qualified a real read-only COM2 storage-provider request on that same hash and confirmed legacy Display remains unavailable. |
| S22R14R2 | Two independent cold builds established content equivalence under the contract in §2. Their ISO identities were not guest accepted. The isolated Developer Studio test discrepancy is resolved in §3. |

Primary reports are linked in the evidence index below; historical guest evidence is attributed to its exact ISO and phase.

## 2. S22R14R2 reproducibility and artifact relationship

S22R14R2 performed two independently initialized clean builds from source commit `58658c4860dfc17d1e6b2951040d81e962b2982c`, profile `ServerV03PacManOnly`, with separate dependency and package inputs.

| Build | Build ID | ISO SHA-256 | Guest accepted? |
|---|---|---|---|
| 3 | `d652cf81-91ed-4cf3-8338-9823ca11cd71` | `ede2f3b2bad1463ca4c6bc4fc05f847772ad703a30e2d09d40e1deed834f8d51` | No |
| 4 | `8e9efdce-bcf4-4e0e-9966-aa71eade2561` | `aab8b490c570fac9af6983ee4089c2a794204d34249dd8f6787441f1c623818f` | No |

Both new ISO hashes and manifests were read from their isolated output paths and match the recorded S22R14R2 identities. The comparison contract demonstrated **content equivalence**, not byte-for-byte ISO identity:

- The rebuilt kernel is byte-identical to the accepted kernel (`b5e029…e95dd`). PacMan's manifest, ELF, and all nine resource files are byte-identical to the accepted payload.
- The accepted ramdisk raw image is `27fc2310…feb578`; both cold builds have raw hash `ead5105aa131dee575e0021fac1650a28330df422bd211ca6baccdd267179f05`. Read-only FAT extraction found the same 96 paths and matching content hashes. The accepted-to-rebuild raw FAT layout difference was not isolated; do not claim raw ramdisk identity.
- The accepted bootloader hash is `70eeebc388c74ca55a0f4760e6d4a587160372787c400156b01b2e3dfed8eee2`; Build 3 and Build 4 hashes are `5c37894982ddaf4d50ddae72dd685bc0274e3178b6f6165f9dcde62af59cf105` and `f75921dfd02c7d1fea0fca3d87a863560cf2ea48d06e969969134357c0d5ec1c`. Build 3 and 4 have identical `.text`, `.data`, `.pdata`, and `.rsrc` sections; their PE debug metadata differs in timestamps, PDB identifiers, and source paths. Compared with the accepted bootloader, `.text`, `.data`, and `.rsrc` match while `.pdata` differs with the accepted PE's different `.rdata` layout.
- Build IDs and timestamps also differ by design, and the kernel symbol audit includes an intermediate PE hash. These metadata explain the reported whole-ISO hash differences under the documented contract; they do not make either rebuilt ISO the accepted guest image.

Do not promote either rebuilt ISO, call either byte-identical to the accepted ISO, or transfer guest acceptance to them. The full source/build details are in [S22R14R2](RELEASE_S22R14R2_BUILD_REPRODUCIBILITY.md).

## 3. Release-integrity test discrepancy

### Exact failure and cause

The failing test was `scripts/test-release-build-integrity.ps1` in the S22R14R2 Build 3 clone. Its preserved log reports:

```text
DeveloperStudio manifest is not the exact Phase 25 signed-off package manifest.
```

The failing artifact was the Development-profile test fixture, `tests/fixtures/developerstudio-phase25-app.json`. The signed-off fixture pin is 1,112 bytes, SHA-256 `d59c12267d360c4e31b70dec7f9c1b0689fb5816011d681b38ee9cdef0d2f53b`. In the Build 3 Windows checkout it was 1,157 bytes, SHA-256 `6f6f5a257ce6ce9884e6bffcd68d84f75a8217ac53a4ce83aa74c155c2aff6cc`. The JSON content was the same; `core.autocrlf=true` converted LF to CRLF because the fixture had no explicit EOL attribute. `git ls-files --eol` showed index LF / working-tree CRLF in Build 3, while the main checkout happened to retain LF.

The exact byte pin was correctly rejecting changed fixture bytes. The failure was checkout normalization in a hashed test fixture, not a package compatibility defect and not a reason to weaken an integrity assertion.

### Profile scope and correction

The Developer Studio byte-pin guard was introduced before S22R9. S22R9 added the explicit profile-aware contract: Development still selects PacMan and signed-off Developer Studio; `ServerV03PacManOnly` selects PacMan alone, requires Developer Studio to be `not_selected`, and validates the selected Native ELF ship-set. The release validator still requires PacMan and rejects missing PacMan, unsupported versions or architectures, unexpected packages, receipt/path/hash mismatches, and profile mismatches. The Developer Studio development tests continue to require the signed-off artifact and reject incompatible/current or historical unsupported manifests.

The release validator is not bypassed: `build.ps1` calls `Assert-ReleaseNativeAppShipSet`, and `scripts/create-release-iso.ps1` calls both ship-set and receipt validation with the selected profile before image composition. `Assert-ReleaseBuildIdentity` routes the PacMan-only profile through `Assert-ReleaseNativeAppShipSet`; its Development branch retains the Phase 25 Developer Studio identity/version checks.

The narrow correction adds `text eol=lf` for the hashed fixture in `.gitattributes` and an assertion that its checked-out byte length/hash match the signed-off pin. It changes no release packaging behavior and disables no validation.

Post-correction results:

- `scripts/test-release-build-integrity.ps1`: **PASS, 34 assertions**.
- `scripts/test-release-iso-packaging.ps1`: **PASS**, synthetic FAT composition and read-only verification. Its dummy files are explicitly not bootable-image evidence.
- `git diff --check`: **PASS**.
- `git check-attr eol -- tests/fixtures/developerstudio-phase25-app.json`: **lf**.

The source and test histories, failure log, and rerun are part of the S22R15 work record. No ISO rebuild was needed because this correction only fixes a test fixture's checkout bytes.

## 4. Authoritative v0.3 feature inventory and qualification matrix

“Included” describes the receipt-selected candidate and linked kernel functionality. Hosted checks, freestanding/static checks, and guest observations are kept separate. The exact candidate for guest results in this table is ISO SHA-256 `2fe2f5ed…32241e`, build `6ad9bb77-12ce-4b4d-8ed0-56ff3944ee5d`.

| Feature / subsystem | Included; expected support | Hosted evidence | Freestanding evidence | Guest evidence | Limits; disposition and owner decision |
|---|---|---|---|---|---|
| AMD64 boot and startup | Yes; UEFI boot to kernel and desktop | Build and symbol-audit records in S22R14R2 | AMD64 ELF64 kernel; symbol audit reports no relocations/imports | Exact candidate boots to `kernel_main`, framebuffer, and desktop in S22R14R1R5/R1R6 | QEMU/OVMF configuration and one candidate identity; not broad hardware coverage. **PASS_WITH_LIMITATION**. |
| Stack contracts | Yes; listed repaired paths fit within the 16 KiB boot-stack contract | R14R1 stack guard parses `.su` records, checks required frames and path sums; targeted hosted regressions pass | Settings modern 11,512 B / 4,872 B headroom; legacy 11,896 B / 4,488 B; COM2 7,768 B / 8,616 B. Minimum headroom 4,096 B | Modern Settings and legacy route reached; R1R6 completed a real COM2 storage request | Static estimates, reserves and enumerated paths only; no runtime high-water measurement. **PASS_WITH_LIMITATION** for these paths; no whole-kernel safety claim. See [stack report](RELEASE_S22R14R1_BOOT_STACK_SAFETY.md). |
| Memory integrity | Yes; repaired Navigator, Settings, and COM2 objects should not overrun their documented stacks | `navigator-stack-contract` covers seven heap-scoped fixtures; stack guard regression results in R14R1 | Navigator's original `0x52108`-byte automatic object was moved to scoped heap allocations; identified service paths now meet headroom | VFS mounts stayed intact through Settings, PacMan, Navigator and COM2 storage observation | No general stack guard/canary or whole-kernel call-chain proof. **PASS_WITH_LIMITATION** only for the repaired/guarded paths. |
| VFS mounts | Yes; `/`, `/system`, `/certs`, `/config`, `/config/navigator` | Mount-table integrity and registration/generation suites in [S22R12](RELEASE_S22R12_VFS_MOUNT_INTEGRITY.md) | Five mounts and storage initialization paths built into the kernel | Five mounts persisted through guest app interactions and R1R6 provider request | Guest qualifies observed FAT32/ATA route only. **PASS_WITH_LIMITATION**. |
| Storage initialization | Yes; initialize block storage and expose root FAT32 volume | Hosted block/VFS contract suites in S22R12 | AMD64 ATA path and FAT32 implementation present | Root FAT32/ATA mounted; COM2 request returned 3 disks and 5 volumes with no VFS regression | Other filesystem and device-driver code is not guest-qualified by this result. **PASS_WITH_LIMITATION**. |
| Desktop startup and input | Yes; desktop/taskbar, PS/2 keyboard and mouse interaction | Window/input regression evidence in R14R1 | Production input/compositor paths compiled into candidate kernel | Start opened/closed/reopened; screenshots confirmed desktop and app transitions | QEMU standard VGA and PS/2 route only; no full hardware/input survey. **PASS_WITH_LIMITATION**. |
| Start menu | Yes; open, enumerate and launch supported entries | Registry/menu lifecycle suites in S22R11/R14R1 | Menu and registry are kernel-integrated | PacMan and Settings visible; PacMan stayed registered after refresh/exit | Historical/alias menu rows are not all independently supported apps; Recent Programs persistence fails. **PASS_WITH_LIMITATION**. |
| Fonts and appearance | Yes; bundled UI fonts and theme render readable desktop/app UI | Image/font adapter and typography smoke evidence in S22R12/R14R1 | Font atlas and rendering code included in kernel/ramdisk | Guest screenshots show desktop, Settings, Navigator, and PacMan rendering | No exhaustive glyph, locale, scaling, or accessibility survey. **PASS_WITH_LIMITATION**. |
| Window lifecycle | Yes; launch, close, cleanup, return to desktop for qualified apps | Lifecycle/cleanup suites in R14R1 | Built-in and Native ELF lifecycle code included | PacMan emitted production cleanup marker; Settings and Navigator returned to desktop | Not every built-in app received a full guest open/close cycle. **PASS_WITH_LIMITATION**. |
| Modern Settings | Yes; System and read-only Display information/navigation | Settings native runtime/layout/presentation suites in R14R1 | Modern display snapshot path guarded at 11,512 B | Settings opened, System/Display navigated, closed; it reports framebuffer information | No display mode/configuration change; modern Display does not replace the legacy backend. **PASS_WITH_LIMITATION**. |
| Legacy Settings / Display Options | Yes; route exists and should report actual backend state | Legacy response and stack guard checks pass | Repaired response member and guarded route are compiled | Control Panel → Display Options ran; UI showed backend/monitor/resolution unavailable and “Display query failed” | Display configuration does not function. Correct unavailable handling is not successful feature acceptance. **OWNER_DECISION_REQUIRED**. |
| Navigator | Yes; local file URL and offline UI | Navigator URL and parser suites in S22R12/R14R1 | S22R12 stack repair; parser and VFS path compiled into kernel | Opened and parsed `file:///Apps/PacMan/app.json` (956 bytes), returned to desktop | Network browsing not qualified by this local-file result. **PASS_WITH_LIMITATION**. |
| PacMan | Yes; only Native ELF package; launch/gameplay/exit | PacMan package/build and hosted gameplay tests in S22R9/R14R2 | AMD64 ELF/package and runtime loader included; receipt validates manifest/ELF | Exact candidate discovery, launch, visible gameplay response, normal exit and cleanup | Only this packaged Native ELF received candidate guest qualification. **PASS** for the qualified path. |
| App registry and cleanup | Yes; register packages and remove runtime state on exit | Registry/lifecycle regressions in S22R11/R14R1 | Kernel registry and Native ELF lifecycle included | PacMan remained listed after refresh; cleanup marker and return to desktop observed | Does not establish isolation or safety against malicious code. **PASS_WITH_LIMITATION**. |
| COM2 service | Yes; optional external host service over UART COM2 | Device 14/14, storage 15/15, bridge 82/82; QAC runner 151/151 in R1R6 | Read-only dispatch/provider path guarded at 7,768 B | One real `GetStorageSnapshot` request (`0004`) returned `ok`, 3 disks/5 volumes, generation 1; no writes | Requires a host COM2 endpoint; no normal UI client. **PASS_WITH_LIMITATION**; owner decides if part of v0.3 contract. See [R1R6](RELEASE_S22R14R1R6_COM2_AND_LEGACY_COVERAGE.md). |
| Native ELF load and app filesystem access | Yes; PacMan package is loaded from `/Apps`; Navigator reads local files | Manifest, ELF architecture, path, and ship-set checks pass | ELF64 AMD64 loader and VFS access compiled | PacMan launched; Navigator read the PacMan manifest through VFS | Native code runs with kernel privilege; manifest is not an authenticated identity. **PASS_WITH_LIMITATION** for trusted PacMan only; security model requires owner decision. |
| Canonical release profile | Yes; `ServerV03PacManOnly` for Server 0.3 AMD64 | `test-release-build-integrity.ps1` covers profile and ship-set contracts | `build.ps1`/release ISO path consume explicit profile | Candidate receipt records the profile | Profile is build composition, not a runtime package boundary. **PASS** as composition. |
| Manifest and package checks | Yes; exactly one valid AMD64 PacMan manifest/ELF; reject extra apps | Integrity test **34/34**; tests cover unsupported version/architecture, missing PacMan, Studio/unlisted extras and receipt mismatch | Release receipt checks required artifact paths, sizes and hashes | Candidate's 16 manifest-listed ESP inputs all matched staged files | Unsigned manifests do not provide trust or a runtime security boundary. **PASS_WITH_LIMITATION**. |
| Image composition and ISO verification | Yes; bootable UEFI ISO with receipt-matched ESP | ISO packaging test passes synthetic FAT create/verify; direct candidate manifest/sidecar hashes match | Candidate ISO manifest records FAT32 EFI boot image and `0xEF` boot record | Exact candidate accepted in R14R1R5/R1R6 | Synthetic test is not boot evidence; cold outputs are separate identities. **PASS_WITH_LIMITATION**. |
| Build reproducibility | Yes; the selected content contract is reproducible | Independent build records and extracted-file comparisons in R14R2 | Kernel and PacMan payload byte-identical across clean builds | Rebuilt ISOs were not guest-tested | Not whole-ISO byte reproducible; accepted raw ramdisk delta remains unexplained. Owner must accept content-equivalence contract. **OWNER_DECISION_REQUIRED**. |

## 5. Stack-safety disposition

Capacity is 16,384 bytes; required estimated headroom is at least 4,096 bytes.

| Path | Static guarded usage | Estimated headroom | Disposition |
|---|---:|---:|---|
| Modern Settings | 11,512 B | 4,872 B | Within the documented minimum; guest path reached. |
| Legacy Settings | 11,896 B | 4,488 B | Within the documented minimum; UI query path reached, backend unavailable. |
| COM2 storage | 7,768 B | 8,616 B | Within the documented minimum; real provider request qualified. |

S22R12 found the `NavigatorApp` automatic frame at `0x52108` bytes against the 16 KiB boot stack. Its repair moved seven app objects to scoped heap allocations, and the Navigator stack contract regression passed. S22R14R1 then moved large Settings response/plan and COM2 storage snapshots/workspace allocations off the same stack. The static guard retains explicit call-path reserves and is not a runtime watermark. The three identified blockers can be considered resolved within their reported paths and checks. This does not establish safety of unenumerated kernel call chains, the IRQ/exception stack, or comprehensive kernel memory safety.

## 6. `/Documents` decision

- The applicable FAT 8.3 short-name path permits a base of at most eight characters and an optional extension of at most three; it rejects a leading period. `Documents` has a nine-character base, and the release path does not create a long filename for it. Directory creation returns `FAT_CREATE_INVALID_NAME` / `VFS_ERR_INVALID` while the block operation reports `BLOCK_OK`.
- The desktop standard-folder setup and visible Start-menu Documents entry expose the failed path. File Explorer's expected browse/save workflow cannot use `/Documents`. No app startup depends on this directory; PacMan, Settings, and Navigator were qualified without it. The shell/file workflow is the dependency.
- The reviewed [README](../README.md) and [release ISO guide](RELEASE_ISO.md) do not promise `/Documents` by name. The shipped UI does imply a working standard folder.
- The accepted guest record shows no data loss or damage to other mounts. The direct impact is inability to create/use the expected location; this is a data-availability/workflow risk, not an observed corruption event.

**Classification: `OWNER_DECISION_REQUIRED`.** Recommendation: for a narrowly controlled experimental preview, explicitly exclude `/Documents` from the supported contract and describe the failed workflow. For a general release or any contract promising standard user folders, fix FAT long-name support or change the supported folder contract and qualify the resulting candidate. Do not waive this on the owner's behalf. Evidence: [S22R13R5 §7](RELEASE_S22R13R5_FROZEN_CANDIDATE_ACCEPTANCE.md) and [S22R12](RELEASE_S22R12_VFS_MOUNT_INTEGRITY.md).

## 7. Start-menu recent-history persistence

The candidate attempts to persist to `/.startmenu_recent`. FAT's short-name rules reject the leading dot and overlength base name, returning `FAT_FILE_WRITE_INVALID_NAME` / `VFS_ERR_INVALID`. The filename is invalid on each attempted write. The observed Start-menu save produced one failure and no retry loop. In-memory recents/menu discovery still work; the defect affects persistence across refresh/restart, not application discovery. No wider mount damage was observed. The reviewed release docs do not promise persistent recents.

**Classification: `DOCUMENTED_PREVIEW_LIMITATION`.** Recommendation: document that Recent Programs may be empty/stale after restart. Fix it before release only if persistent history is part of the approved contract. Evidence: [S22R13R5 §7](RELEASE_S22R13R5_FROZEN_CANDIDATE_ACCEPTANCE.md).

## 8. Legacy Settings Display decision

Legacy Display is reachable through Control Panel → Display → Display Options and the advanced-display Settings entry. The production candidate has the VirtIO-GPU probe gate disabled; the existing route correctly returns backend unavailable and displays “Display query failed.” This is a nonfunctional configuration feature, even though the error response is correct. Modern Settings provides read-only framebuffer information and does not provide equivalent mode/topology configuration. The [S21 Settings readiness note](SETTINGS_S21_V03_RELEASE_READINESS.md) describes modern Display as informational and says topology/modes may be unavailable; it does not establish legacy configuration support. The UI route remains visible, so the visible product surface implies an available feature more strongly than the documentation does.

**Classification: `OWNER_DECISION_REQUIRED`.** Recommendation: exclude legacy display configuration explicitly from a controlled v0.3 preview and make user-facing unavailability clear in the release documentation. If the owner requires the advertised route to configure displays, the narrow follow-up is a real production backend, a new candidate identity, and guest query/apply qualification. Do not enable diagnostic probes, hide controls, or infer success from `BackendUnavailable`. Evidence: [S22R14R1R5 §§4,8](RELEASE_S22R14R1R5_GUEST_INTERACTION_AND_COM2_COVERAGE.md) and [S22R14R1R6 §5](RELEASE_S22R14R1R6_COM2_AND_LEGACY_COVERAGE.md).

## 9. Native ELF security and compatibility

Current behavior is material to every distribution decision:

- Native ELF code executes with kernel privilege. There is no enforceable per-application address-space isolation or syscall boundary.
- The loader discovers native packages from writable runtime `/Apps`. The PacMan-only ISO profile does not stop packages being added to writable storage and discovered later.
- Manifests are unsigned and not bound cryptographically to the ELF. The loader's compatibility checks do not reliably enforce minimum Server version or ABI table size; the visible ABI label does not distinguish incompatible v0.3 and v0.5 host tables. ELF entry validation does not prove the entry is inside an executable segment.
- A malicious or incompatible native package can corrupt kernel state, bypass callback-level checks, or invoke a mismatched host callback. Documentation and the build manifest do not prevent this.

### Distribution model assessment

| Model | Current status | Required operating boundary / engineering |
|---|---|---|
| **A — Controlled internal / experimental preview** | **Potentially supportable only by explicit owner risk acceptance.** The audience and installed software can be controlled outside the OS, but the OS does not enforce trust. | Limit to trusted first-party packages and trusted operators; use disposable VMs or dedicated test hardware without sensitive data; control writes to `/Apps` and updates; do not add third-party/untrusted ELF files. Documentation alone is not a control. The owner must approve the privileged-code risk. |
| **B — Public technical preview** | **Blocked under the current implementation; not recommended.** Public users can introduce native binaries into writable storage, and those binaries run with kernel privilege. A warning cannot stop this path. | Before public availability, enforce a package-source/launch policy that prevents untrusted writable `/Apps` code from running, authenticate package identity, and fail closed on ABI/version/ELF range checks. These controls reduce exposure but do not provide process isolation. |
| **C — General-purpose release for arbitrary/third-party applications** | **Blocked.** The current loader and execution model do not provide the required trust or compatibility boundary. | Requires user-mode address spaces, privilege transitions, mediated system calls, verified memory separation, trusted package identity, robust ABI negotiation, and runtime enforcement before arbitrary third-party Native ELF support. |

First-party internal use is a policy choice with explicit controls; public technical preview and general-purpose third-party use are not equivalent and are blocked by current runtime behavior. Evidence and historical design findings: [S22R6](RELEASE_S22R6_DEVELOPERSTUDIO_EDITOR_ADAPTER.md), [S22R7](RELEASE_S22R7_NATIVE_ABI_RELEASE_BOUNDARY.md), [S22R9](RELEASE_S22R9_PACMAN_ONLY_RELEASE_PROFILE.md), and [S22R13R5 §8](RELEASE_S22R13R5_FROZEN_CANDIDATE_ACCEPTANCE.md).

## 10. Release-owner decision register

Technical facts are recorded above. Every row below is a policy/product decision and remains **Pending**; no approval was found in the authorized project record.

| Decision | Recommendation | Risk if accepted | Owner approval |
|---|---|---|---|
| Intended v0.3 audience | Select Model A only: controlled first-party internal/experimental preview with operational restrictions. Do not authorize Models B/C on this implementation. | Kernel-privileged Native ELF can still corrupt the OS or data if the trusted environment boundary fails. | Pending |
| `/Documents` contract | Exclude and document for a limited preview; fix if standard folders are promised. | Users cannot use the visible Documents location; file workflows may fail. No data loss was observed. | Pending |
| Recent-history persistence | Document as a preview limitation. | Recent Programs can be empty or stale after restart. | Pending |
| Legacy Display support | Exclude from preview contract and explain unavailable status; otherwise require a real backend and new candidate. | Visible UI path cannot configure display modes/topology. | Pending |
| Native ELF execution model | Approve only controlled trusted first-party use for Model A. | Native code has kernel privilege and no enforceable application isolation. | Pending |
| Writable `/Apps` exposure | Require operators to control writable storage and package additions for Model A; block public use until runtime package loading is restricted. | Any added package can be discovered and run at kernel privilege. | Pending |
| ABI compatibility limitations | Promise no cross-version compatibility; require fail-closed version/table-size checks before broader package support. | Incompatible callbacks may be interpreted using the wrong table layout and corrupt state. | Pending |
| Content-equivalent reproducibility | Accept only the documented content-equivalence contract; do not claim bit-for-bit ISO reproducibility. | ISO/bootloader metadata and accepted raw ramdisk layout are not byte-identical across builds. | Pending |
| COM2 external service | Treat as optional unless the owner includes the host COM2 API in the v0.3 support contract. | If included, future releases need the exact guest request/provider qualification because ordinary UI boot does not exercise it. | Pending |

## 11. Release gate matrix

| Gate | Status | Evidence / remaining condition |
|---|---|---|
| Source provenance | `PASS_WITH_LIMITATION` | Exact candidate commit/profile/receipt verified; receipt records dirty worktree at packaging start. |
| Build reproducibility | `OWNER_DECISION_REQUIRED` | Two cold builds establish component/content equivalence; not whole-ISO identity; accepted raw ramdisk layout delta is unexplained. |
| Integrity and package validation | `PASS_WITH_LIMITATION` | Candidate receipt and all 16 manifest-listed ESP files match; PacMan-only ship set. Synthetic FAT test is hosted-only. |
| Kernel boot | `PASS_WITH_LIMITATION` | Exact accepted candidate booted under recorded QEMU/OVMF setup; no broader hardware claim. |
| Stack safety | `PASS_WITH_LIMITATION` | Three identified paths meet documented margin and exercised paths ran; no whole-kernel or runtime high-water proof. |
| Desktop interaction | `PASS_WITH_LIMITATION` | Start, keyboard/mouse, desktop, app launch and return tested on QEMU configuration. |
| Settings | `PASS_WITH_LIMITATION` | Modern System/Display navigation accepted; legacy display route unavailable and awaits contract decision. |
| Navigator | `PASS_WITH_LIMITATION` | Local PacMan-manifest file URL passed; network use not established by this result. |
| PacMan | `PASS` | Exact candidate package discovered, launched, visibly responded, and cleaned up normally. |
| COM2 | `PASS_WITH_LIMITATION` | One read-only real storage-provider request passed on the exact candidate; API support scope remains owner choice. |
| VFS/storage | `PASS_WITH_LIMITATION` | Five mounts and read-only provider snapshot passed; `/Documents` and recent history limitations remain. |
| User-visible filesystem limitations | `OWNER_DECISION_REQUIRED` | Decide `/Documents` contract; recent history is recommended as documented preview limitation. |
| Native ELF compatibility | `OWNER_DECISION_REQUIRED` | Trusted PacMan works; broader ABI compatibility is not established and current runtime checks are inadequate. |
| Native ELF security — controlled Model A | `OWNER_DECISION_REQUIRED` | Requires explicit acceptance of kernel privilege risk and operational controls. |
| Native ELF security — public Model B / general Model C | `BLOCKED` | Writable package discovery plus kernel privilege/no isolation; security boundary not enforceable. |
| Release documentation | `INCOMPLETE` | Final user-facing limitations and intended audience must match the owner's decisions. |
| Release-owner approvals | `OWNER_DECISION_REQUIRED` | No decisions in this register are approved. |

## 12. Remaining work and S22R16 recommendation

The bounded stack repairs and accepted guest paths do not indicate a need for another broad core-integration phase. The integrity-test fixture issue is corrected. No new kernel feature or candidate build should start automatically.

**Recommend S22R16 as a release-owner decision closure phase (option 2).** Close audience/security model, `/Documents`, recent-history, legacy Display, COM2 scope, ABI policy, and content-equivalence decisions. Then:

Remaining blockers before publication consideration are the unapproved intended audience/security model, public/general Native ELF security boundary, the `/Documents` and legacy Display contract decisions, ABI/writable `/Apps` policy, the reproducibility acceptance contract, and final user-facing release documentation. Public technical preview and general-purpose third-party Native ELF release remain blocked until their required enforcement architecture exists.

1. If the owner chooses a behavior that the current candidate does not provide (Documents, legacy Display, public package restrictions, ABI enforcement, or byte-identical ISO reproducibility), schedule the narrowest targeted remediation/qualification first.
2. If the owner accepts a controlled preview contract, update user-facing release documentation with the accepted limitations and controls.
3. After decisions and any approved changes, perform final validation against a separately identified candidate as required. Keep the current accepted candidate and its guest evidence intact until then.

The next phase is not publication. The current security model blocks public/general distribution, and the owner's approval is still pending for a controlled preview.

## 13. Final repository and QAC state

S22R15 adds this report, the owner summary, an LF checkout rule for the hashed Phase 25 fixture, and one regression assertion. No kernel, package, candidate ISO, release channel, `dist` tree, or readiness marker changed. The S22R15 documentation/fix commit is local on `main`, based on `f5646c5fdf269438d28fb075071e56db47d6c11e`; the commit SHA and final Git status are reported in the phase closeout. No push occurred. The existing untracked `assets/Backgrounds/troncity.png` remains unchanged.

At closeout, QAC was Online with zero reservations, managed sessions, queued requests, blocked requests, and QEMU children. The coordinator and GUI were running. The sibling QAC `.phase` still reads QAC3 open / next QAC3C3 and was not changed. The repository itself has no Main Integration `.phase` file.

## Evidence index

- [S22R9 PacMan-only profile](RELEASE_S22R9_PACMAN_ONLY_RELEASE_PROFILE.md)
- [S22R10 PacMan guest qualification](RELEASE_S22R10_PACMAN_GUEST_QUALIFICATION.md)
- [S22R11 registry/discovery lifecycle](RELEASE_S22R11_APPS_DISCOVERY_LIFECYCLE.md)
- [S22R12 VFS mount integrity](RELEASE_S22R12_VFS_MOUNT_INTEGRITY.md)
- [S22R13R5 frozen candidate acceptance](RELEASE_S22R13R5_FROZEN_CANDIDATE_ACCEPTANCE.md)
- [S22R14R1 stack safety](RELEASE_S22R14R1_BOOT_STACK_SAFETY.md)
- [S22R14R1R5 guest interaction and COM2 coverage](RELEASE_S22R14R1R5_GUEST_INTERACTION_AND_COM2_COVERAGE.md)
- [S22R14R1R6 COM2 and legacy coverage](RELEASE_S22R14R1R6_COM2_AND_LEGACY_COVERAGE.md)
- [S22R14R2 build reproducibility](RELEASE_S22R14R2_BUILD_REPRODUCIBILITY.md)
- Build 3 integrity log: `out/s22r14r2-build-repro/build-3/release-build-integrity-test.log` (preserved original log; rerun performed separately).
- Candidate receipt and artifacts: `out/release-profile/6ad9bb77-12ce-4b4d-8ed0-56ff3944ee5d/`.
