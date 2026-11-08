# Server v0.3 — S22R16 Release-Owner Decision Closure

**Outcome B — `OWNER_DECISIONS_PENDING`.** The decision register and preview contract are prepared for owner review. No release-owner authorization was supplied or recorded in the reviewed project evidence. This phase does not approve a distribution model, accept residual risk, authorize recipients, or permit publication.

## 1. Scope and repository preflight

S22R16 closes the release contract questions carried from S22R15. It does not change the runtime, build a new image, repeat guest acceptance, or promote the candidate.

| Check | Observed state |
|---|---|
| Repository | `D:\dev\guideXOSServer` |
| Branch | `main` |
| Starting HEAD | `9486b696b7cfd1f02e522e0b7d83385b8c5a9701`, matching the required checkpoint |
| Tracked worktree at preflight | Clean |
| Existing untracked work | `assets/Backgrounds/troncity.png` (2,537,256 bytes); preserved and left untracked |
| Candidate ISO | `out/release-profile/6ad9bb77-12ce-4b4d-8ed0-56ff3944ee5d/artifacts/guideXOS-Server-v0.3.0-amd64-pacman-only-candidate.iso`; SHA-256 `2fe2f5eda87c9a80164715a156af5de12c7cf3891d29a961c40012c9d032241e` |
| Candidate kernel | `out/release-profile/6ad9bb77-12ce-4b4d-8ed0-56ff3944ee5d/ESP/kernel.elf`; SHA-256 `b5e029d23f9c21b5ff0838d9388322bf95b211bf3ef98145ddd344170dfe95dd` |

The hashes were read directly from the existing artifacts and match the accepted values. Both artifacts remain frozen at their original paths. No ISO or kernel was rebuilt or substituted. No QEMU guest was started. No branch switch, reset, clean, stash, rebase, merge, push, `dist` change, manifest change, readiness-marker change, or publication-state change was made.

The S22R15 receipt identifies build `6ad9bb77-12ce-4b4d-8ed0-56ff3944ee5d`, source commit `5b62216efe78158fb12707c126ec38582faecf59`, profile `ServerV03PacManOnly`, and PacMan as the only Native ELF package. The receipt records that the packaging-start worktree was not clean, so the accepted ISO is not claimed to be reproducible from its Git commit alone. Exact-candidate guest acceptance is documented in S22R14R1R5 and S22R14R1R6.

## 2. Current technical evidence

S22R15 consolidated the acceptance evidence for this exact candidate. The technical claims below remain bounded by the cited evidence; they are not blanket safety or hardware-compatibility claims.

| Area | Evidence and boundary |
|---|---|
| Boot and desktop | The accepted ISO booted under the recorded QEMU/OVMF setup. Desktop, Start menu, and PS/2 interaction were observed on that setup; this is not a broad hardware survey. |
| PacMan | The sole packaged Native ELF app was discovered, launched, visibly interacted with, and exited with cleanup on the exact accepted ISO. This does not establish safety of other Native ELF files. |
| Settings and Navigator | Modern Settings System/read-only Display navigation and a local PacMan-manifest URL in Navigator were exercised. Network browsing and all built-in app workflows were not established. |
| Storage and VFS | Five mounts persisted through the observed interactions. The accepted guest exercised the FAT32/ATA route. Other filesystems and devices are not established by that result. |
| Stack contracts | The enumerated modern Settings, legacy Settings, and COM2 paths have compiler-derived guarded estimates of 11,512 B, 11,896 B, and 7,768 B against a 16,384 B boot stack, with at least 4,096 B required margin. There is no runtime high-water mark or whole-kernel stack proof. |
| Package integrity | The accepted receipt and 16 manifest-listed ESP inputs were checked. The PacMan-only profile is a build-composition rule; runtime discovery can still see additional packages on writable `/Apps`. |
| Reproducibility | S22R14R2 completed two independent cold builds. Kernel and PacMan payloads matched the accepted candidate, and extracted ramdisk file contents matched. Whole-ISO byte identity is not established; the accepted raw ramdisk layout delta was not isolated. Build IDs and bootloader debug metadata vary. The rebuilt ISOs have different identities and were not guest-accepted. |

The technical package can support owner review of a **controlled experimental preview**. It does not resolve the owner’s supported-scope choices, prove environmental controls are in place, or make public/general distribution safe.

## 3. Release models

| Choice | Intended use | Current assessment |
|---|---|---|
| **A — Controlled experimental preview** | Named, trusted evaluators using trusted first-party software in controlled test systems. | The only model that could be considered with the present implementation, and only after explicit owner risk acceptance and evidence that the external operating controls in §6 are arranged. The OS itself does not enforce those controls. Native applications execute with kernel privilege and are not isolated from the kernel. |
| **B — Public technical preview** | General public downloads and use, including user-modifiable application storage. | **Blocked under the current security model.** Writable runtime package discovery can expose kernel-privileged execution to packages added by users. Warnings and release documentation do not create a runtime restriction. |
| **C — General-purpose release** | General use, including arbitrary or third-party Native ELF applications. | **Blocked.** It requires enforceable application isolation, trusted application identity, reliable runtime ABI compatibility checks, and execution restrictions before arbitrary native code can be supported. |

**Recommendation:** consider Model A first, subject to explicit owner approval, accepted scope decisions, and satisfactory evidence for the actual test environment. No model is selected in this closure.

## 4. Functional and package-scope decisions

### `/Documents`

The FAT32 short-name creation path accepts a base name of at most eight characters and does not create a long filename here. `Documents` has nine characters; creation returns `FAT_CREATE_INVALID_NAME` / `VFS_ERR_INVALID` while block I/O reports `BLOCK_OK`. Desktop standard-folder initialization and the Start-menu Documents entry expose the failed workflow. File Explorer cannot use the expected `/Documents` location for ordinary browse/save work. S22R15 found no app-startup dependency and no observed loss or damage to other mounts; the impact is an unavailable user-data workflow, not an observed corruption event.

**Recommendation:** for a narrowly scoped experimental preview, exclude `/Documents` from supported behavior and disclose the broken visible shortcut/workflow. Fix it before any release contract that promises Documents or standard user folders. If the owner rejects the proposed exclusion, engineering must add/qualify long-name support or define and qualify another working standard-folder contract; any changed kernel requires a new candidate identity and requalification.

### Start-menu Recent Programs persistence

FAT rejects `/.startmenu_recent` because of its leading dot and overlength base name, returning `FAT_FILE_WRITE_INVALID_NAME` / `VFS_ERR_INVALID`. The accepted guest evidence shows one failed save and no retry loop or wider mount damage. In-memory recents and application discovery still work; persisted recents may be empty or stale after refresh/restart. The reviewed release docs do not promise persistent recents.

**Recommendation:** propose deferring the persistence fix and documenting this narrow user-visible limitation. If persistent recents are required, change the persistence filename/storage contract and qualify the new candidate. Deferral is only a recommendation until the owner accepts it.

### Legacy Display Options

The visible Control Panel → Display → Display Options route and advanced-display Settings entry run, but the release kernel has the VirtIO-GPU probe gate disabled. The route reports backend unavailable and displays `Display query failed`; modern Settings only reports read-only framebuffer information. Display mode/topology configuration does not work. The error handling is explicit, but the visible configuration route can still imply functionality.

**Recommendation:** exclude functional display configuration from a controlled preview and clearly label the route unavailable. Before distribution, the smallest UI correction is an explicit “not supported in this build” state at the entry/page and disabled configuration/apply actions; this is a proposed UI correction, not something implemented by S22R16. If the owner requires working configuration, implement and initialize the production backend, create a separately identified candidate, and qualify query/apply behavior.

### Developer Studio

Developer Studio is excluded from the `ServerV03PacManOnly` profile and is recorded as `not_selected`; the accepted package set is PacMan-only. Do not reintroduce its incompatible 0.5.0 package into this release profile. Developer Studio development can continue independently. A change to its own development package does not change the accepted Server v0.3 profile unless a future owner-approved release scope explicitly says otherwise.

### Native application loading and trust

The ISO package profile selects PacMan, but runtime package discovery reads writable `/Apps`; package files can be added or replaced through storage workflows or direct storage modification and rediscovered. The loader performs format, architecture, manifest-field, path, and ELF checks. Manifests are unsigned and not cryptographically bound to executable contents, so they are not trusted identities. ABI/version/table-size enforcement is insufficient for cross-version compatibility, and the entry check does not establish that the entry lies in an executable segment.

Native ELF code runs with kernel privilege and no enforceable address-space or syscall isolation. The PacMan-only package profile does not block runtime discovery/execution of other privileged binaries. A malicious native binary can corrupt kernel state; an incompatible but non-malicious package can also invoke mismatched callbacks or corrupt state. No malicious ELF was run in the cited qualification.

**Recommendation:** Model A may only be considered for trusted first-party software and operators with explicit owner acceptance of this residual risk and external controls. Do not describe unsigned manifests as trust, call PacMan-only a runtime allowlist, or claim that malicious native binaries are prevented from executing.

## 5. Owner decision register

The status column describes the current closure disposition. Owner authorization is tracked separately. `PROPOSED` is an unapproved recommendation; `REQUIRES_ENGINEERING` marks a model/path that the present candidate cannot satisfy. `APPROVED`, `REJECTED`, or `DEFERRED` will be recorded only if an explicit release-owner instruction or authorized approval record supplies that decision. None was present in the reviewed S22R15 records, and this prompt explicitly says not to treat the prompt as approval.

| ID | Decision and current technical evidence | Recommended disposition | Alternatives and consequences | Engineering dependencies if recommendation is rejected | Status / owner approval / source |
|---|---|---|---|---|---|
| **R16-D01** | Select distribution model. Current runtime exposes writable Native ELF discovery to kernel-privileged execution without isolation. | Model A, controlled experimental preview, only with explicit acceptance of the privileged-code risk. | Choose B or C: the current candidate remains blocked; a warning alone is insufficient. | Model B needs runtime restriction of untrusted writable package loading, authenticated package identity, and fail-closed ABI/range validation. Model C additionally needs user-mode address spaces, privilege transitions, mediated calls, and verified memory separation. | `PROPOSED`; owner approval pending; source: none. |
| **R16-D02** | Decide whether `/Documents` and standard folders are part of supported v0.3 workflows. The visible path fails as detailed in §4. | Exclude and disclose for Model A. | Require a working folder: current candidate fails the contract; no data loss was observed, but the visible file workflow is unavailable. | FAT long-name support or an alternate standard-folder contract and candidate qualification. | `PROPOSED`; owner approval pending; source: none. |
| **R16-D03** | Decide whether Recent Programs must persist across restart. Session recents/discovery work; FAT rejects `/.startmenu_recent`. | Defer persistence repair and document that recents may be empty/stale after restart. | Require persistence: current candidate does not meet the contract. | Choose a valid persistent filename/storage contract, implement, and qualify the changed candidate. | `PROPOSED`; owner approval pending; source: none. |
| **R16-D04** | Decide whether legacy display configuration is supported. The UI route is reachable but the backend is unavailable. | Exclude configuration and disclose unavailable state; require the small UI clarity correction described in §4 before distribution. | Require actual display configuration: current candidate does not meet the contract. | Implement production backend and initialization; qualify query/apply on a new candidate. UI correction also requires a code change if the route currently implies support. | `PROPOSED`; owner approval pending; source: none. |
| **R16-D05** | Accept or reject Model A's Native ELF trust boundary and operational `/Apps` controls. PacMan is unsigned; runtime discovery is writable and privileged. | Consider only trusted first-party use; restrict recipients and package/storage changes externally; do not claim OS enforcement. | Reject residual risk: do not distribute this candidate. Require third-party/public use: use R16-D06 prerequisites. | Demonstrate actual host/test-system restrictions and recovery controls; runtime trust/authentication and package policy require separate engineering for public use. | `PROPOSED`; owner approval pending; source: none. |
| **R16-D06** | Security requirements for public Model B or general Model C. | Keep B/C blocked. | Selecting B/C as a required audience means this candidate is ineligible until its requirements are implemented and validated. | Model B: enforced package source/launch policy, identity, and fail-closed ABI/ELF validation. Model C: full enforceable isolation architecture in addition to trust/compatibility controls. | `REQUIRES_ENGINEERING`; owner approval pending; source: none. |
| **R16-D07** | Set cross-version Native ELF compatibility promise. Runtime checks do not reliably reject v0.3/v0.5 host-table mismatch. | Make no cross-version compatibility promise for the proposed preview; allow only the qualified PacMan binary. | Promise broader compatibility: current candidate cannot substantiate it. | Fail-closed version/table-size/callback negotiation, executable-range validation, and compatibility qualification; these checks still do not isolate privileged code. | `PROPOSED`; owner approval pending; source: none. |
| **R16-D08** | Accept a content-equivalence reproducibility contract or require bit-identical ISO output. Two cold builds matched kernel/PacMan and extracted ramdisk content; whole-ISO bytes and accepted raw ramdisk layout did not match. | Accept only the documented component/content-equivalence scope; make no byte-identical ISO claim. | Require byte identity or resolution of the raw ramdisk layout delta: current evidence does not satisfy that requirement. | Further isolate the accepted/rebuild raw layout delta and remove/normalize varying build metadata where possible, then reproduce and compare under the specified contract. Do not substitute a rebuild ISO for the accepted guest-qualified candidate without new identity/acceptance. | `PROPOSED`; owner approval pending; source: none. |
| **R16-D09** | Decide whether external read-only COM2 host service belongs in the supported v0.3 contract. One real `GetStorageSnapshot` request passed with a host endpoint; ordinary UI startup does not exercise it. | Treat it as optional/out of preview scope unless explicitly requested. | Include it: document host endpoint/API requirements and retain exact-candidate evidence as an acceptance criterion. | No new runtime change is indicated by existing evidence; any changed implementation or claimed wider behavior needs targeted qualification. | `PROPOSED`; owner approval pending; source: none. |
| **R16-D10** | Authorize exact preview recipients and approve the release contract. No recipient list, approval record, or risk acceptance was supplied. | Keep the contract as a draft and distribute to no preview recipients until an authorized owner records the audience, residual-risk acceptance, operational-control evidence, and recipient scope. | Defer or reject preview: keep candidate undistributed. Approval of a draft alone would not authorize publication unless the approval explicitly includes that action. | If contract changes imply runtime work, follow the corresponding dependency above. | `PROPOSED`; owner approval pending; source: none. |

No entry is `APPROVED`, `REJECTED`, or `DEFERRED` because the owner has not supplied those decisions. Recommendations to exclude or defer a function remain proposals. The `REQUIRES_ENGINEERING` status for D06 describes the current technical gate, not an owner selection of B or C.

## 6. Controlled-preview acceptance conditions

### Technical requirements

- Preserve the exact accepted ISO and kernel identities above; verify their hashes when preparing any controlled handoff.
- Use the exact-candidate guest evidence for boot, desktop, PacMan, Settings, Navigator, VFS, and COM2 claims. Do not transfer that acceptance to either content-equivalent rebuild ISO.
- State the memory-safety evidence narrowly: three enumerated guarded paths meet their reported static budget; no runtime high-water or whole-kernel safety proof exists.
- State the observed FAT32/ATA scope and the two file-workflow limitations. Do not imply that `/Documents` or persisted recents work.
- Keep package integrity, build composition, guest behavior, and reproducibility as separate claims. PacMan-only is composition evidence, not runtime confinement.
- If any binary or runtime code changes, assign a new identity and perform the requalification required for the changed paths. This phase made no such change.

### Operational requirements for Model A

These are requirements to arrange and verify outside the OS before any controlled handoff. S22R16 found no evidence that they are currently implemented or configured:

1. Name the trusted operators and limit the preview image to those authorized recipients.
2. Use disposable VMs or dedicated test hardware. Keep production credentials, sensitive data, and important writable host volumes out of the guest.
3. Isolate the test system from untrusted networks and disable unnecessary host integration such as shared folders, device passthrough, and clipboard/file exchange.
4. Verify the ISO hash before use; retain a known-good image/snapshot and a tested restore/reimage procedure.
5. Control who can modify package-bearing storage and updates. Use only verified first-party packages; do not add third-party or untrusted Native ELF files. If the operator intends to prevent guest modification of package-bearing storage, configure that restriction at the VM/media layer and validate that it actually applies to this image. Do not assume the OS enforces it.
6. Record the actual environment, storage policy, recovery method, and recipient list as part of the handoff decision.

Snapshots, network isolation, write controls, and operator instructions reduce exposure and recovery cost; they do not provide kernel/application isolation, prevent malicious native execution, or guarantee containment of guest faults. Do not claim a proposed control is implemented until the target test setup has evidence for it.

### Owner decisions

Before Model A distribution, an authorized owner must explicitly select Model A, accept its residual kernel-privileged Native ELF risk, decide each supported/excluded behavior in R16-D02 through D09, approve the release contract, and name the recipients. If any selected requirement exceeds the current candidate, record the required engineering path and keep distribution blocked.

## 7. Smallest post-decision path

There is no owner-selected next phase yet, so no engineering work is started by S22R16.

| Owner outcome | Smallest defensible follow-up |
|---|---|
| Model A and the proposed exclusions/content-equivalence boundary are approved, and the owner adopts the draft's Legacy Display UI clarity requirement | **S22R17 — Targeted preview remediation.** Clearly label the Legacy Display route unsupported and disable configuration/apply actions, then assign a new candidate identity and requalify the affected UI and release construction. Complete final preview qualification against that new identity before any handoff. |
| Model A is approved and the owner explicitly determines the current unavailable-state UI already meets the approved contract, so no runtime change is required; actual operating controls are evidenced | **S22R17 — Final preview qualification.** Recheck frozen artifact identity and approved handoff controls, finalize the owner-approved contract, and close any expressly required preview checks. Do not rebuild the ISO by default. |
| Owner requires Documents, functional legacy Display, persistent recents, or another concrete runtime behavior | **S22R17 — Targeted preview remediation.** Implement only the selected correction, issue a new candidate identity, and requalify affected behavior plus release construction. |
| Owner chooses public Model B or general Model C | **S22R17 — Security prerequisite implementation.** Define and implement the minimum enforceable policy/identity/ABI controls for B; Model C also requires user-mode isolation and mediated execution. The existing candidate remains blocked while this work is open. |
| Owner requires bit-identical accepted-to-rebuild ISO output | First scope a reproducibility remediation for the unresolved raw ramdisk layout and varying metadata; preserve the currently accepted candidate. |

The actual next phase can be selected only after the owner answers the unresolved decisions. Any new binary must have its own identity and appropriate requalification. S22R16 does not authorize publication, promotion, or release-channel changes.

## 8. Evidence index

- [S22R15 readiness and decision evidence](RELEASE_S22R15_READINESS_AND_OWNER_DECISIONS.md)
- [S22R15 concise owner recommendations](RELEASE_S22R15_OWNER_DECISION_SUMMARY.md)
- [S22R14R2 build reproducibility](RELEASE_S22R14R2_BUILD_REPRODUCIBILITY.md)
- [S22R14R1 stack-safety qualification](RELEASE_S22R14R1_BOOT_STACK_SAFETY.md)
- [S22R14R1R6 COM2 and legacy Display coverage](RELEASE_S22R14R1R6_COM2_AND_LEGACY_COVERAGE.md)
- [S22R14R1R5 guest interaction and COM2 coverage](RELEASE_S22R14R1R5_GUEST_INTERACTION_AND_COM2_COVERAGE.md)

S22R16 adds only release-decision documentation. The accepted candidate, kernel, `dist`, manifests, readiness markers, and publication state are unchanged. The initial tracked worktree was clean; the pre-existing untracked background asset was preserved. No push occurred.
