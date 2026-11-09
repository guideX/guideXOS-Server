# Server v0.3 — S22R19 Public Preview Readiness

**DRAFT — NOT APPROVED**
**Assessment date:** 2026-11-08
**Outcome C — PUBLIC_PREVIEW_TECHNICAL_BLOCKER**
**Candidate:** S22R18 AMD64 public technical preview ISO, SHA-256 4cc445e3fe33eb615c5a0434977eaa664bf9f8c0c3e46743a9e9bde578f6a92d

## Assessment

S22R18 established and QAC-qualified a restricted PacMan-only runtime profile for one exact read-only UEFI ISO. Direct inspection in S22R19 confirms that ISO's recorded hash, boot structure, EFI image and all 16 listed payload identities. Two positive QAC runs used that exact ISO; a separate QAC diagnostic ISO demonstrated rejection of one modified PacMan resource before native entry.

The candidate is not ready for public distribution. No publisher signing workflow, signing key, independently trusted verification key, or approved distribution channel is present. The adjacent generated ESP directory also has a ramdisk hash that differs from the build manifest and the ISO's embedded ramdisk. This does not change the verified ISO bytes, but the provenance discrepancy must be resolved or explicitly accepted before release. Owner approval is also pending for residual privileged execution risk and the exact preview contract. The visible Display route needs clearer unsupported-state behavior before publication.

This assessment does not call guideXOS a sandbox or a general-purpose secure application platform. PacMan executes with kernel privilege. The user and host hypervisor remain part of the trust boundary.

## Repository preflight and preservation

- Repository: D:\dev\guideXOSServer.
- Starting branch and HEAD: main / 80abd047a5cf665284eb0480d4d23baa384a1893, matching the requested checkpoint.
- Candidate source commit 2e1c2493355bd005399977bcd82a4142c94ca2e9 is an ancestor of the starting HEAD. Its separate source clone is at that commit and reports a clean tracked worktree.
- The main checkout has 11 commits ahead of origin/main and zero behind. It had no tracked changes at preflight.
- The existing untracked assets/Backgrounds/troncity.png is unchanged; SHA-256 444ac64211907d80f0266e2283960cefaa932803d0aee7e0ccfd414f6fae2fda. It is absent from the candidate wallpaper inventory and ISO payload.
- No Main Integration .phase marker was found. The sibling QAC .phase marker was read without changes: QAC1–QAC3C2 accepted, QAC3 open, next QAC3C3. No QEMU process was visible during the read-only preflight. The S22R18 QAC runs are complete records; no guest was started by S22R19.
- S22R19 changed no ISO, dist content, release marker, publication channel, source code, or QAC state. No reset, clean, stash, rebase, merge, cherry-pick, branch switch, worktree creation, or push was performed.

## Candidate identity and direct verification

Candidate path:

E:\CodexArtifacts\S22R18\source-2e1c2493\out\release-profile\7f9173c1-550b-4dfb-84e4-b44e1e1c260a\preview-artifacts\guideXOS-Server-v0.3.0-amd64-public-preview-pacman-only-candidate.iso

| Item | Directly verified identity |
|---|---|
| ISO | 90,245,120 bytes; SHA-256 4cc445e3fe33eb615c5a0434977eaa664bf9f8c0c3e46743a9e9bde578f6a92d; matches the required value, sidecar, and manifest |
| Build receipt | Build ID 7f9173c1-550b-4dfb-84e4-b44e1e1c260a; success; ServerV03PublicPreviewPacManOnly; source main at 2e1c2493355bd005399977bcd82a4142c94ca2e9; clean at packaging and build start |
| Boot structure | ISO9660 PVD and El Torito catalog valid; UEFI platform 0xEF; no-emulation boot entry points to LBA 32 |
| EFI image | 90,177,536 bytes; FAT32; SHA-256 ba9ad43ca8c1b8d5f675b2e20aee46f07ea7e7a0a093f9953b4c648dbe5b3fc2 |
| Bootloader | EFI/BOOT/BOOTX64.EFI, 56,832 bytes; SHA-256 1ebb48ff3ff7534e10fc0877f90c764be57df7f7a1b1b9da30f910da8e1afe79 |
| Kernel | kernel.elf, 2,805,584 bytes; SHA-256 4d416905e36e77ae17314cb39d6805c4dcef3d01834a0aab00a6985f3d49369a |
| RAM disk | ramdisk.img, 67,108,864 bytes; SHA-256 ead5105aa131dee575e0021fac1650a28330df422bd211ca6baccdd267179f05 |
| PacMan | Manifest 956 bytes, SHA-256 3f3592e6c76053e323f16d48073e98c67d7f5702b8e47f0b507d6bfb3d126449; ELF 63,640 bytes, SHA-256 eeb544bbb2e2574eeaed7bb7f0bad61ea24f6945231b7757223a225b30b8e1cc |
| Inventory | All 16 manifest-listed files in the ISO's embedded FAT image match their recorded sizes and SHA-256 values. PacMan is the only Native ELF package; Developer Studio is absent. |

The ISO was parsed read-only. The full 90 MB embedded UEFI image was hashed and its FAT directory traversed in memory; no candidate file was extracted to or rewritten on disk. The candidate identity and QAC evidence are detailed in [S22R18 Disposable VM Qualification](RELEASE_S22R18_DISPOSABLE_VM_QUALIFICATION.md).

The ISO9660 root contains BOOT.CAT, README.TXT (340 bytes; SHA-256 3f86308c442d014749d46e4e4d312b36a54724b82d0dd042f3bd9f2cd9fb0f43), and UEFI_BOOT.IMG. The directly read SHA-256 sidecar matches the ISO.

### Staging-directory discrepancy

The adjacent generated build folder's ESP/ramdisk.img is 67,108,864 bytes but hashes to 24e2a7aba0ab7e37e2f0c92033d04da2b6766ebfa7bbb6e41eb2d60773f19e40. The receipt and candidate ISO's embedded FAT both identify ead5105aa131dee575e0021fac1650a28330df422bd211ca6baccdd267179f05. The other 15 staged ESP files match the receipt. The candidate ISO itself is internally consistent and QAC-tested; the neighboring ESP directory is not a reliable reconstruction input for that ISO. Preserve both outputs and reconcile how that copy diverged before making a full build-reproducibility claim. If it cannot be reconciled, create a separately identified clean build and qualify its own ISO before substituting it.

The build identity includes the tracked build probe GXOS-LARGE-FILE-PASTE-TRACE-V1. The marker is present in tracked build source at the recorded commit; it is not evidence that the untracked troncity.png entered the build.

## QAC qualification and evidence boundaries

The exact candidate received two completed positive QAC runs:

- s22r18-preview-positive-qualified-e6152cfa191a4b94b0a1d0071eebcc1a: PacMan was allowed, produced frames and key input, returned normally, and cleaned up.
- s22r18-preview-settings-navigator-qualified-5f5f7baf13a84ade9f9bf0220ea004e3: Settings and Navigator smoke plus PacMan launch/input/close.

Both QAC manifests and execution records identify the candidate SHA-256 above. They used QEMU 11.0.0 on AMD64 PC with WHPX, two vCPUs, 2048 MiB, standard VGA, read-only OVMF code firmware, the read-only candidate CD-ROM, no writable disk, no NIC, no host share, no guest-accessible COM2, no HMP monitor, and a QAC-managed QMP listener on 127.0.0.1.

The completed negative guest test used a different diagnostic ISO, SHA-256 b2350439ee3b9b92a02a7baf20dfba106f344c418dea406ac0a28f46ab24d19f. It changed one byte in PacMan's EatPill.wav resource. The guest emitted resource-integrity rejection before native entry; there was no allow, PacMan entry, or smoke-pass marker. This is meaningful actual guest evidence for that tamper case.

The 27-case Native ELF policy suite, release-integrity checks, and QAC launch-profile cases are hosted regressions. They exercise synthetic package identities, ABI/version/architecture rejection, writable mount/device state, aliases, overlays, unresolved ownership, and a source-generation change. They are not QAC guest attack tests. The guest negative test did not inject an unauthorized ELF, attempt an incompatible ABI, mount a writable package source, or force a verification-time race. Do not claim those bypasses were tested in the guest.

## Security boundary

The exact candidate embeds fixed PacMan manifest and executable hashes and nine resource hashes. The runtime checks package directory and manifest identity, architecture, ABI metadata, executable bytes, and read-only source ownership. It requires /Apps to use the supported root FAT32 mount backed by the release RAM disk, rejects aliases and nested Apps mounts, and checks source generation/ownership across reads and launch. It snapshots the verified executable before entry. The exact code paths are in kernel/core/native_elf_baremetal.cpp and kernel/core/include/kernel/native_elf_security_policy.h.

| Boundary question | Evidence and limit |
|---|---|
| PacMan-only identity | Candidate ISO inventory has exactly PacMan. Runtime and hosted policy enforce one approved directory. QAC positively observed that exact package. No malicious ELF was run. |
| Resource and executable identity | Manifest, ELF, and all nine resources are pinned. QAC rejected one altered resource before entry. No QAC executable-tamper test was recorded. |
| Immutable source and backing ownership | Runtime checks VFS read-only state, block-device read-only state, release-RAM-disk identity, mount/device/volume identity, and generation. Hosted cases cover simulated failures. QAC's read-only-only launch demonstrates the supported configuration, not a guest attack against writable media. |
| Unauthorized package, ABI, architecture, version | Hosted tests reject synthetic identity mismatches. The QAC negative changed a resource only. Guest rejection of injected third-party or ABI-incompatible ELFs is not demonstrated. |
| Alternate paths and overlays | Runtime checks alias, nested /Apps mounts, device ownership, and generation. These failure states are hosted cases; no guest adversarial mount test is claimed. |
| Immediate verification and TOCTOU | Runtime rechecks source state, validates each resource, reads and hashes the actual ELF snapshot, and verifies source identity before entry. Hosted generation-change cases and one QAC resource-tamper case exist. A guest race under concurrent storage mutation was not tested; supported media is read-only. |
| Failure and registry consistency | QAC confirms the modified resource did not reach native entry. Registry behavior is a synthetic hosted regression; QAC did not execute an unauthorized package and inspect registry recovery after that attack. |

Residual limitations remain explicit:

- PacMan executes with kernel privilege. There is no native process address-space or syscall isolation.
- A vulnerable or compromised approved PacMan can corrupt the guest. Runtime checks cannot defend against a compromised kernel.
- The guest does not authenticate its own bootloader, kernel, or ISO signature. A modified boot chain can replace the embedded trust data.
- Hypervisor containment depends on QEMU, WHPX, host firmware, and host configuration. guideXOS provides no host isolation.
- No arbitrary third-party Native ELF support, guest network, host filesystem share, or guest-accessible COM2 is included in the tested profile.
- External ISO authenticity is not established by an unsigned sidecar SHA-256.

## Deployment contract finding

The only qualified deployment is the exact S22R18 disposable AMD64 QEMU/OVMF profile. The proposed contract in [S22R19 Public Preview Deployment Contract](RELEASE_S22R19_PUBLIC_PREVIEW_DEPLOYMENT_CONTRACT.md) records the full hardware and integration restrictions.

The QAC profile uses display none and QMP for controlled input/capture. A public interactive example will need a host-visible display frontend and ordinary keyboard/mouse delivery. Those host UI choices were not part of the exact QAC command line and do not inherit acceptance automatically. Keep the guest adapter at standard VGA and qualify any materially different virtual hardware. QMP must be omitted for ordinary use or confined to protected loopback host management; never expose it to a network or guest.

## Functionality dispositions

### /Documents and user data

S22R16 records the exact filesystem defect: creating the nine-character Documents directory returns FAT_CREATE_INVALID_NAME / VFS_ERR_INVALID, so File Explorer cannot use the expected location for ordinary browse/save work. That creation fails before block I/O reports a write; no data loss or damage to other mounts was observed. No app-startup dependency was found. S22R18 QAC did not directly test Documents listing, reading, or save dialogs on the final ISO. Read-only access to other shipped paths is demonstrated, including the PacMan package/local Navigator smoke; that is not proof that /Documents works. The final VM has no writable guest disk and its release media is read-only, so no user-created file can be saved durably and no guest data survives shutdown. PacMan uses shipped resources; the accepted Settings and Navigator smoke did not need writable documents. Other editor/save workflows were not exhaustively qualified. Since document-editing normally implies persistence, warn prominently that no data is saved and unsaved in-memory work is lost when the VM closes. Recommend explicitly excluding /Documents and all save workflows; owner approval remains pending.

### Start-menu recent history

Both positive S22R18 serial logs show one boot-time “No mount point for path: /.startmenu_recent” diagnostic followed by “none persisted.” The Start menu and app launch still worked. No repeated retry or unrelated VFS failure was observed in those runs. This affects persistence only in the observed path; in-session menu operation remains available. Recommend exclude persistent recent history and state that recent items may be empty after restart. A future narrow filename/storage repair needs its own qualification if persistence is required.

### Legacy Display

The candidate build identity records the public-preview kernel flag, while the production VirtIO-GPU probe gate is absent from the release build configuration. The exact S22R18 Settings/Navigator serial includes a display configuration request followed by backend-unavailable/query-failed results. No working display configuration was demonstrated. Earlier S22R14R1R6 guest evidence on the previous ISO opened the visible Legacy Display route and showed “Display query failed”; the relevant route source was not changed for S22R18, but that page was not reopened in the S22R18 QAC run. The wording can be mistaken for a transient error. Recommend exclude display configuration, label it “not supported in this preview,” and disable apply/configuration controls before publication. This is a small separate UI correction requiring a new candidate identity and focused QAC check.

### COM2

S22R14R1R6 qualified one real read-only GetStorageSnapshot request on the earlier ISO and runtime. The bridge/provider implementation files are unchanged between the recorded S22R15 candidate source and S22R18 source, but S22R18 has a different kernel binary and its qualified launch expressly rejects a guest-accessible COM2 host channel. The older guest result is not exact-S22R18 runtime acceptance. Recommend COM2 remain outside this preview. If the owner wants it, update the launch contract and run a focused read-only request on the selected candidate/configuration before making a support claim. Do not advertise writable storage operations.

## Provenance and reproducibility

S22R18's separate source clone addresses the earlier dirty-main-checkout concern: source revision and branch are recorded, status at build and packaging start is clean, and the main checkout's untracked background image is outside the fixed wallpaper inventory and ISO. The receipt directly inventories kernel, bootloader, RAM disk, PacMan manifest/ELF/resources, wallpaper files, profile, ABI header hash, kernel tool versions, and Python ISO dependencies. Developer Studio is marked not_selected.

This is a clean tracked Git state and a useful exact-artifact receipt, not a complete independent reproducibility qualification. The receipt does not provide a full independent cold build for S22R18; its recorded tools do not enumerate every bootloader SDK/linker version; and the neighboring ESP ramdisk mismatch above remains unexplained. S22R14R2 found that its two cold-build ISOs were not byte-identical: kernel and PacMan payloads matched byte-for-byte, extracted ramdisk file contents matched, the accepted raw ramdisk differed, and bootloader/ISO metadata varied. That comparison used different source/profile identities and cannot be transferred to S22R18.

Proposed standard: independently initialize two builds from the same exact clean source commit and pinned external dependency inputs. Compare kernel ELF and PacMan payload identities byte-for-byte; compare extracted ramdisk file paths and content hashes, recording raw-image differences; compare bootloader executable sections and explain debug/build-path metadata; record every toolchain version. Do not claim whole-ISO byte identity unless demonstrated. Do not substitute a rebuilt ISO for the QAC-tested candidate without identity-specific qualification.

### Distribution authentication

No release signing key, detached-signature workflow, signed release manifest, or trusted official download channel was found in the reviewed release records or scripts. The manifest and SHA-256 sidecar are unsigned. A digest next to an ISO on an untrusted site does not authenticate origin. Public release is blocked until the owner approves and implements a channel and verifiable publisher-authentication procedure. The minimum proposed procedure is in [S22R19 Publication Prerequisites](RELEASE_S22R19_PUBLICATION_PREREQUISITES.md). S22R19 generated no keys, signatures, digest publication, or upload.

## Owner decision state

S22R17 records Model B as the selected target only. Risk acceptance, exact VM contract, PacMan-only policy, /Documents, recent history, Display, COM2, reproducibility standard, distribution authentication, user-facing documentation, and publication authorization remain pending. Recommendations in this report are not approvals. See [S22R19 Owner Approval Sheet](RELEASE_S22R19_OWNER_APPROVAL_SHEET.md).

## Readiness matrix

| Area | State | Evidence and limit |
|---|---|---|
| Boot and desktop | PASS_WITH_LIMITATION | Two QAC positive runs on exact ISO; QEMU/OVMF profile only |
| Settings | PASS_WITH_LIMITATION | System view opened and closed; Display configuration unavailable |
| Navigator | PASS_WITH_LIMITATION | Local/offline smoke only; network/TLS and persistence excluded |
| PacMan | PASS | Two exact-ISO positive runs; approved package, input, normal return, cleanup |
| VFS and read-only storage | PASS_WITH_LIMITATION | Hosted mount/read checks; exact QAC read-only root; no guest writable-source attack |
| Native ELF integrity policy | PASS_WITH_LIMITATION | Hosted synthetic cases plus exact guest modified-resource rejection |
| Unauthorized Native ELF rejection | PASS_WITH_LIMITATION | Hosted identity rejection only; no QAC injected-ELF test |
| Runtime ABI compatibility | PASS_WITH_LIMITATION | Hosted ABI mismatch rejection; only exact PacMan runtime qualified |
| Stack safety | PASS_WITH_LIMITATION | Static 16 KiB stack guard for named Settings, Display, and COM2 paths; not whole-kernel runtime telemetry |
| COM2 | OWNER_DECISION_REQUIRED | Older-candidate GetStorageSnapshot evidence; absent from S22R18 profile |
| Reproducibility | OWNER_DECISION_REQUIRED | No S22R18 independent cold-build comparison; stage-copy anomaly unresolved |
| Source provenance | PASS_WITH_LIMITATION | Clean exact source and candidate payload inventory; incomplete tool inventory and adjacent ESP mismatch |
| Disposable VM configuration | PASS_WITH_LIMITATION | Exact QEMU 11 / WHPX / OVMF profile; public display frontend differs |
| Host-integration restrictions | PASS_WITH_LIMITATION | No NIC, writable disk, share, or guest COM2; QAC QMP was host-loopback management |
| Public distribution authentication | BLOCKED | No signature, trusted key/fingerprint, or approved official channel |
| Public documentation | ENGINEERING_REQUIRED | Four S22R19 drafts exist; final user guide and Display UI clarity need completion/approval |
| Owner approvals | OWNER_DECISION_REQUIRED | Model B target only is recorded as approved |
| Publication authorization | OWNER_DECISION_REQUIRED | No explicit authorization or channel record |

## Outcome and next phase

**Outcome C — PUBLIC_PREVIEW_TECHNICAL_BLOCKER.** The minimum blocker is publisher authentication for the exact ISO and an owner-approved trust/channel procedure. The build-stage discrepancy also prevents a full reproducibility claim until reconciled or superseded by a new, separately qualified candidate. Neither issue means that the directly verified ISO hash or its exact QAC results are false.

Recommend exactly one next major direction: **Path C — Security or provenance remediation.** Keep S22R20 bounded to the owner-approved signature/manifest/key-custody workflow and reconciliation of the S22R18 receipt/staging discrepancy. Do not expand into general Native ELF isolation. Keep the small Legacy Display UI correction as a separately scoped, identity-specific prerequisite if the preview contract excludes the feature.

## Evidence references

- [S22R15 Readiness and Owner Decisions](RELEASE_S22R15_READINESS_AND_OWNER_DECISIONS.md)
- [S22R16 Owner Decision Closure](RELEASE_S22R16_OWNER_DECISION_CLOSURE.md)
- [S22R16 Preview Release Contract](RELEASE_S22R16_PREVIEW_RELEASE_CONTRACT.md)
- [S22R17 Public Preview Security Gate](RELEASE_S22R17_PUBLIC_PREVIEW_SECURITY_GATE.md)
- [S22R17 Security Implementation Plan](RELEASE_S22R17_SECURITY_IMPLEMENTATION_PLAN.md)
- [S22R18 Public Preview Security Enforcement](RELEASE_S22R18_PUBLIC_PREVIEW_SECURITY_ENFORCEMENT.md)
- [S22R18 Disposable VM Qualification](RELEASE_S22R18_DISPOSABLE_VM_QUALIFICATION.md)
- [S22R14R2 Build Reproducibility](RELEASE_S22R14R2_BUILD_REPRODUCIBILITY.md)
- [S22R14R1R6 COM2 and Legacy Coverage](RELEASE_S22R14R1R6_COM2_AND_LEGACY_COVERAGE.md)
