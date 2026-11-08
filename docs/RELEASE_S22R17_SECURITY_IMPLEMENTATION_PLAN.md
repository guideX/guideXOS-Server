# Server v0.3 — S22R17 Security Implementation Plan

**Date:** 2026-11-08

**Linked decision:** [S22R17 Public Preview Security Gate](RELEASE_S22R17_PUBLIC_PREVIEW_SECURITY_GATE.md)

**Status:** Proposed engineering plan only. No production security policy is installed or approved by this document.

## 1. Recommendation

For the selected Model B target, use a **public, disposable VM preview of the exact authenticated ISO as read-only media**. The initial profile has no writable persistent block device, no writable `/Apps`, no network adapter, no external storage passthrough, and no host file/clipboard sharing. It runs only the first-party PacMan ELF already present in the accepted package profile. A release-profile guard must fail closed unless `/Apps` is on a read-only VFS mount.

This is the smallest candidate boundary because the kernel already supports a read-only ramdisk root when no writable persistent volume is mounted, and VFS operations already reject writes on mounts marked `readOnly`. The missing part is policy enforcement at Native ELF discovery/launch and qualification of the actual VM configuration. The whole ISO digest must be authenticated outside the guest; a hash stored only inside the image is not a trust root.

This route does not sandbox PacMan. The native executable remains kernel-privileged. The VM contains host exposure only when the hypervisor is trusted, patched, and configured without host integrations; it does not eliminate guest vulnerabilities. If owner requirements include a writable app store, physical deployment, protection from a hostile host/firmware, or untrusted third-party apps, this plan is insufficient and the alternate work in §8 is required.

## 2. Assumptions and owner decisions

The plan assumes, pending owner approval:

- Supported users boot an authenticated, read-only ISO in a disposable VM.
- The VM has no writable persistent disk, no NIC, no shared folders, no clipboard/file transfer, and no storage/device passthrough.
- The authenticated image contains only the exact approved first-party PacMan package.
- Guest data is disposable; `/Documents`, persistent recent history, and app installation are not promised.
- The host OS, hypervisor, VM configuration, UEFI firmware, and publisher signing key are trusted. Host administrator or hypervisor compromise is outside this profile.
- Native ELF bugs may compromise the guideXOS guest. The VM is a host boundary, not a guest sandbox.

Still pending: public risk acceptance; approval of a VM-only supported profile; approval of functional limitations; publisher signing-key custody; final preview contract approval; recipient scope and publication authorization. The Model B selection alone approves none of these.

## 3. Work packages

### S22R18-M1 — Enforce immutable `/Apps` for the preview profile

| Item | Plan |
|---|---|
| Exact source changes | In `kernel/core/native_elf_baremetal.cpp`, add a release-profile policy check before `discover()` caches or parses packages and again at `launch()` to prevent stale-cache bypass. Resolve `/Apps` through VFS and require an active mount with `readOnly == true`; also ensure the manifest, executable, and resource paths resolve to that intended read-only mount, with no writable nested mount shadowing `/Apps`. On missing/writable/unknown paths, publish no Native ELF apps, reject launch, and emit a clear serial diagnostic. Ensure mount changes invalidate discovery state or recheck the mount at every launch. Do not add a global Native ELF disable. |
| Dependencies | Confirm the read-only ramdisk is mounted before app discovery; use `vfs::get_mount()` and the existing `MountPoint.readOnly` flag. Keep PacMan operation and ordinary desktop boot intact. |
| Trust assumptions | The kernel itself came from an authenticated, operator-verified ISO mounted read-only. The only approved code in that image is first-party PacMan. A hostile Native ELF already running can bypass VFS because it has kernel privilege. |
| Property established | Under the approved image/VM profile, ordinary VFS operations cannot add or replace executable packages and the Native ELF loader refuses writable package roots. This establishes package-root immutability for the release path; it does not establish package signature verification or process isolation. |
| Existing code reused | Read-only ramdisk creation in `kernel/core/ramdisk.cpp`; root selection in `kernel/core/main.cpp` / `kernel/core/desktop.cpp`; VFS write rejection and `MountPoint.readOnly` in `kernel/core/vfs.cpp`; current discovery/launch path in `native_elf_baremetal.cpp`. |
| Hosted tests | Add a small pure policy helper/test seam and safe tests for read-only mount accepted, writable/missing mount rejected, writable nested mount rejected, read-only-to-writable mount change rejected at launch, and failure leaves the desktop available. No ELF fixture is executed. |
| Freestanding tests | Compile/link the AMD64 kernel with the release profile and run the existing VFS/ramdisk contract tests plus new discovery-policy tests with synthetic read-only, writable, and nested mount paths. |
| Guest qualification | Under a new QAC reservation, boot the exact profile with read-only ISO and no writable disk; assert `/Apps` resolves read-only, PacMan discovers/launches/exits normally, a safe write attempt fails, and no writable device or package-import path appears. Also use a bounded writable-mount diagnostic configuration to prove launch is refused; do not execute a test ELF. |
| Compatibility / migration | This changes release runtime behavior and kernel hash. Assign a new candidate identity and requalify affected boot, app discovery, PacMan lifecycle, VFS and failure paths. No transfer of existing guest acceptance. Development profiles may remain unchanged behind an explicit release-profile switch. |
| Residual risk | A malicious or compromised kernel-mode app can bypass this policy; a modified kernel can remove it. A host administrator can replace the VM image. The profile depends on authenticated ISO handling and trusted hypervisor configuration. |

### S22R18-M2 — Authenticate the public image and lock its package composition

| Item | Plan |
|---|---|
| Exact source/process changes | Extend release packaging to produce a detached signature over the exact ISO SHA-256 and a machine-readable signed manifest containing ISO, kernel, bootloader, ramdisk and package identities. Publish the verification key/fingerprint out-of-band. Do not put the only expected digest or public key in writable guest storage. Preserve the accepted receipt's `worktreeCleanAtPackagingStart=false` provenance; a signature authenticates those bytes but does not establish clean-source provenance. If the owner requires a clean-source candidate, that is a new artifact requiring its own targeted guest qualification. Keep `ServerV03PacManOnly` composition and content-equivalence wording; do not alter `dist` or publication markers until separately authorized. |
| Dependencies | Owner approval for publisher key custody, signing location, rotation/revocation, and independent publication of key fingerprints. Use an offline or otherwise protected signing key. |
| Trust assumptions | Release signing infrastructure and public-key distribution are trusted. The user verifies the detached signature and digest before attaching the ISO read-only. This does not stop a user from intentionally booting a different image. |
| Property established | Integrity/authenticity of the exact image and fixed first-party package contents across distribution. This is not application isolation. |
| Existing code reused | Build receipts, selected package checks, ESP inventory and SHA-256 manifest generation in `build.ps1` and release scripts. Existing accepted artifact remains a historical un-signed identity until owner-approved signing/requalification. |
| Hosted tests | Verify good signature passes; modified digest, ISO, manifest, and wrong key fail; ensure package inventory contains only PacMan; verify signing data does not claim bit-identical ISO reproducibility. Use safe synthetic artifacts. |
| Freestanding tests | Not needed for detached host-side image signature verification. M1's mount policy gets freestanding coverage. |
| Guest qualification | Verify signed digest in host handoff workflow, attach exact image read-only, and compare QAC-observed media and booted kernel identity to the signed manifest. |
| Compatibility / migration | Produces a new signed release record. It need not rebuild an unchanged ISO if the owner accepts its recorded provenance, but any kernel/image change gets a new ISO digest and fresh targeted guest qualification. The existing content-equivalence statement stays distinct from byte identity. |
| Residual risk | Compromised signing key/build pipeline can authorize harmful content. Signature validity does not prove code is bug-free or compatible unless package checks and source/release qualification also pass. |

### S22R18-M3 — Enforce the exact PacMan image scope

| Item | Plan |
|---|---|
| Exact source changes | In the release profile, restrict discovery to the one canonical `PacMan` package directory and require the existing release composition check to contain no other Native ELF package. Bind the allowed package to the signed whole-ISO identity from M2; do not treat its self-declared manifest ID as an authenticated principal. Do not change the ABI name/table for this exact-image profile. |
| Dependencies | M1 read-only `/Apps` enforcement and M2 authenticated image/signing process. Preserve the currently qualified v0.3 PacMan artifact and known 256-byte v0.3 table layout. |
| Trust assumptions | The authenticated image was produced by the trusted release pipeline and contains the already qualified first-party PacMan package. No other ABI or package compatibility is promised. |
| Property established | Only the exact app composition in the authenticated v0.3 image is launchable in the supported profile. Integrity and image-specific compatibility are bounded to that image; neither implies isolation. |
| Existing code reused | `kernel/core/native_elf_baremetal.cpp` discovery/launch path; `build.ps1` and release receipt package/version/architecture checks; `sdk/include/guidexos/abi.h`; existing ABI layout fixture. |
| Hosted tests | Test a signed synthetic inventory with PacMan only, extra package directory, changed manifest/ELF digest, and wrong release profile. Verify each unsupported inventory is rejected before guest packaging. No fixture executable is invoked. |
| Freestanding tests | Exercise canonical-directory policy with safe synthetic VFS entries; prove unexpected packages never become registered launch targets. Test the exact approved app under the runtime read-only mount policy. |
| Guest qualification | Verify only PacMan is registered and launches in the new immutable VM profile. A safe, non-executable extra package fixture must remain unavailable. Do not run malicious or intentionally incompatible native code. |
| Compatibility / migration | No ABI migration is needed for the exact v0.3 package. Any future app, writable package root, or cross-version promise requires a separate versioned ABI/schema project and new qualification. Keep v0.5's incompatible table out of the v0.3 image. |
| Residual risk | A digest-approved but buggy/malicious kernel-mode PacMan can still access all guest kernel state. The current shared ABI label remains unsafe for arbitrary cross-version applications. |

### S22R18-M4 — Package failure, recovery, and user-facing contract

| Item | Plan |
|---|---|
| Exact source/document changes | Define deterministic rejection diagnostics and ensure failed discovery does not leave a partially registered launch target. Keep desktop startup available when a package is rejected. Prepare a public VM guide covering ISO signature verification, read-only attachment, no writable storage/network/shares, unsupported app installation, no user-data persistence, known `/Documents`/recent/Display limits, recovery, and hypervisor risk. Do not mark the S22R16 draft approved. |
| Dependencies | M1–M3 behavior and owner-approved scope. Recovery must use the exact signed identity for the selected candidate. |
| Trust assumptions | Users follow the published read-only VM instructions; snapshots/recreated VMs contain no important data. |
| Property established | Rejection is visible and fail-closed; guest can be restored without trusting mutable guest package state. Public wording matches the tested profile. |
| Existing code reused | Current desktop app-registration flow, serial diagnostics, QAC workflows, accepted functional limitation records, and S22R14R2 reproducibility wording. |
| Hosted tests | Test launch failure cleanup, no stale cache after mount changes, recovery metadata generation, and contract statements against the readiness matrix. |
| Freestanding tests | Confirm rejection does not corrupt app registry, VFS mount state, or boot-to-desktop path. |
| Guest qualification | QAC verify user-visible refusal, successful normal PacMan flow, snapshot restore/recreate, no shared host path, and no persistent guest disk. |
| Compatibility / migration | Public contract becomes a new draft revision referencing S22R17 and the exact new image identity. Owner approval and publication remain separate gates. |
| Residual risk | UI/diagnostics cannot contain code already executing with kernel privilege; hypervisor and signing infrastructure remain trust dependencies. |

### S22R18-M5 — Candidate acceptance and owner gate closure

| Item | Plan |
|---|---|
| Exact source/process changes | No additional runtime changes. Freeze the validated profile, record complete image identity, verify signature, and update readiness evidence. Leave candidate promotion/publication markers unchanged until explicit authorization. |
| Dependencies | M1–M4 complete; no unrelated architecture work or QAC session is displaced. |
| Trust assumptions | Owner accepts the stated VM, host, app, persistence, and hypervisor residual risks. |
| Property established | Evidence-based qualification of one immutable public VM preview profile, not general Native ELF safety. |
| Existing code reused | S22R14 guest/COM2 workflow only where its exact candidate and path remain relevant; all claims remain identity-specific. |
| Hosted tests | Run only scoped tests required by M1–M4; retain outputs and source identity. |
| Freestanding tests | Run release kernel compile/link and applicable policy/ABI/stack checks for the changed binary. |
| Guest qualification | Use an owned QAC reservation for the new exact signed ISO and proposed no-writable-disk configuration. Record VM hardware, storage, NIC, integration settings and recovery. |
| Compatibility / migration | Any changed runtime binary requires a new build ID/hash and targeted guest requalification. No byte-identical ISO claim. |
| Residual risk | All §8 residual risks remain for approved trusted app, host, hypervisor, firmware, and distribution systems. |

## 4. Test/evidence strategy

Keep tests non-executing for untrusted content. Hosted fixtures may parse synthetic manifests and ELF headers but must never call their entry point. Share validation helpers with the production policy to avoid a test-only model. For the limited image profile, test at least: a valid signed PacMan-only inventory, modified ISO digest/signature, extra package directory, writable/missing `/Apps`, and mount state changed after initial discovery. Do not add general runtime ABI negotiation to S22R18 solely for an exact immutable image. Before any wider package support, separately test wrong product version, unsupported architecture, wrong ABI identity/table size, missing required service, malformed manifest, and `e_entry` outside a `PF_X` segment through shared production validation code.

Freestanding tests should prove the actual kernel policy path rejects before `gxos_native_call_on_stack`. The exact supported VM profile needs new QAC evidence: signed ISO verified before boot, read-only medium, no writable persistent block device, no NIC/shared resources/passthrough, `/Apps` mounted read-only, normal PacMan launch/exit, safe write failure, and recovery. No existing guest result transfers to the new configuration.

Do not rebuild/re-run QEMU solely to repeat the accepted candidate's existing qualification. A changed security binary or a new no-writable-disk profile is a new test target and must use the established QAC ownership/reservation workflow.

## 5. Artifact, source, and release implications

- No production source, ABI, app manifest, package, kernel, bootloader, ISO, `dist`, or release marker changed in S22R17.
- The accepted ISO SHA-256 remains `2fe2f5eda87c9a80164715a156af5de12c7cf3891d29a961c40012c9d032241e`; kernel SHA-256 remains `b5e029d23f9c21b5ff0838d9388322bf95b211bf3ef98145ddd344170dfe95dd`.
- S22R18-M1 changes the kernel and therefore requires a new kernel hash, ISO identity, build receipt and targeted candidate qualification.
- Detached signing may sign an unchanged existing image, but that action itself needs owner-approved key custody and must not alter the accepted artifact. A future changed ISO gets a new signature and digest.
- Preserve the documented content-equivalence contract. Repeated builds may remain non-byte-identical; the preview claim must identify the exact ISO accepted by QAC.
- No promotion or publication occurs automatically when technical gates pass.

## 6. Remaining functional owner decisions

Carry forward all S22R16 decisions except the selected target model:

- `/Documents` creation/browse/save is unavailable; decide to exclude with clear documentation/UI or fund a correction.
- Start-menu recent history is not persistent on FAT; decide whether this limitation is acceptable.
- Legacy Display configuration is unavailable; adopt a bounded unsupported-state UI correction or define another requirement. Do not build a backend without explicit scope approval.
- COM2's accepted `GetStorageSnapshot` request is read-only; decide whether its external endpoint belongs in scope. Do not advertise writes.
- Preserve content-equivalence wording and make no byte-identical ISO claim unless new evidence establishes it.
- Decide whether the exact PacMan-only immutable image is the complete supported app scope. Any writable app storage reopens package signature, key management and TOCTOU requirements; any untrusted Native ELF reopens process isolation.
- Approve or revise the complete public preview contract, security risk, supported environment, and operating/recovery controls. Publication remains unauthorized.

## 7. Do not implement during S22R17

S22R17 performs feasibility analysis only. It does not add a runtime allowlist, signature verifier, read-only mount gate, ABI version, user-mode process model, package installer, app permission system, or new release-policy enforcement. It does not disable Native ELF globally, weaken release checks, rebuild artifacts, or alter PacMan's intended operation. A universal ELF arithmetic/bounds defect would need a separate narrow correction and test rationale; no such code change is proposed in this phase.

## 8. Alternate engineering paths if scope expands

### Writable package storage / physical preview

Add signed package identity over canonical manifest plus executable/resources, verified against a key protected by a trusted boot chain. Read and verify a single immutable package snapshot and execute the verified bytes. Add version/architecture/ABI/table-size/required-callback negotiation before entry. Establish Secure Boot or another authenticated boot mechanism so external kernel replacement cannot change the verifier. Define key rotation/revocation and recovery. This is not a sandbox: approved native code still runs at kernel privilege.

### Third-party or untrusted Native ELF

Implement a real process boundary in stages: per-app user-mode address spaces and kernel-only mappings; controlled page permissions and safe loader mappings; trap/syscall ABI with bounded pointer/length validation; capability-bound VFS/device/window/audio services; process identity and handle revocation; fault termination and complete memory/resource cleanup; then versioned ABI negotiation and signed package identity. Add adversarial hosted and QAC tests before any third-party claim. Estimate this as a separate kernel architecture program, not as a small S22R18 patch.

## 9. Recommended next phase

Proceed with **S22R18-M1** only after the owner confirms that the limited Model B VM profile is an acceptable target and accepts the stated residual risks. Build and validate the concrete read-only mount guard first; then complete signed-image handling and exact-profile qualification. Keep the accepted S22R16 contract in draft status until an authorized owner approves a complete replacement contract and explicitly authorizes distribution.
