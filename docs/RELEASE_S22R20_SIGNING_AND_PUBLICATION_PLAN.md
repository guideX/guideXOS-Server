# Server v0.3 — S22R20 Signing and Publication Plan

**DRAFT — NOT APPROVED**

**Assessment date:** 2026-11-08
**Proposed acceptance outcome:** **Outcome B — `PROVENANCE_OR_AUTHENTICATION_GAP`**

This plan does not create keys, sign an artifact, select a public host, or authorize release. Model B — Public Technical Preview is the selected target; the remaining contract, risk, identity, signing, and publication choices still require explicit owner approval.

## Current release assessment

The S22R18 candidate has an exact local identity, boot structure, receipt, and embedded 16-file ESP inventory. The candidate ISO's own SHA-256 matches its unsigned sidecar and manifest. Its embedded ramdisk matches the build-stage receipt, and the adjacent ESP hash difference is localized to the redundant FAT32 backup boot sector. The actor that changed the adjacent copy is not identified. The recorded source and build chain is substantial but does not record exact Visual Studio/MSBuild/Windows SDK or PacMan LLVM binary versions/hashes. The current candidate has not been signed or published.

The bounded hosted SSHSIG fixture validates the proposed signature/hash/identity contract on synthetic input. It is not a production verifier and does not establish a publisher identity. No production verification key, custodian, independent fingerprint route, or official download channel exists. Legacy Display still exposes configuration controls despite the public-preview backend being unavailable. Therefore publication remains blocked.

The accepted S22R18 ISO need not be regenerated only to change the adjacent, non-authoritative staging copy. However, if the Legacy Display UI remediation is completed, the corrected kernel and ISO must receive a new release identity and exact-candidate qualification before any future signing decision.

## Readiness matrix

| Gate | S22R20 evidence | State for publication |
|---|---|---|
| Candidate identity and ISO structure | SHA-256 `4cc445e3fe33eb615c5a0434977eaa664bf9f8c0c3e46743a9e9bde578f6a92d`, 90,245,120 bytes; ISO9660/El Torito UEFI structure re-verified. | Evidence verified for this exact private candidate. |
| Candidate payload | ISO-embedded EFI image verified against all 16 manifest entries, including PacMan package and resources. | Evidence verified for this exact private candidate. |
| Ramdisk discrepancy | Embedded/build receipt hash is `ead5105a…`; adjacent image differs only in FAT32 backup sector 6. File payloads and filesystem metadata match. Post-packaging writer is not identified. | Explained at byte level; process provenance remains open for owner disposition. |
| Source/build provenance | Source commit and clean state, build ID, tool versions for kernel/ISO tools, profile, package hashes, and assembly logs recorded. MSVC/MSBuild/SDK details and PacMan LLVM versions/binary hashes absent. | Partial; no independent rebuild was performed. |
| Security and runtime qualification | Two positive QAC records and one negative changed-resource record apply to the exact S22R18 ISO/diagnostic identities. | Existing evidence retained; no new candidate is covered. |
| Signing protocol | OpenSSH SSHSIG Ed25519 plus dedicated namespace proposed; 10 synthetic cases passed (one accepted, nine rejected). | Technically demonstrated with synthetic keys only; owner approval and production verifier still required. |
| Trusted publisher identity | No production public key/fingerprint or independent trust route. | Blocked. |
| Key custody and incident response | No named key custodian, backup/recovery, rotation, revocation, or compromise plan approved. | Blocked. |
| Publication channel | No owner-approved channel, publisher account, immutable versioning policy, mirror policy, or security-notice route. | Blocked. |
| Legacy Display | Visible controls imply that topology, monitor, resolution, Apply/OK, and configuration work; backend query/apply is unavailable for this preview. | Separate UI correction and focused QAC required. |
| Public preview contract and user docs | Model B selected; environment, residual risks, unsupported persistence, functionality boundaries, and final instructions remain pending. | Blocked on owner decisions and final documentation review. |
| Publication authorization | No authorization naming final ISO digest, channel, audience, and publisher. | Not granted. |

This matrix is an S22R20 assessment and does not edit or replace the S22R19 readiness document or any release marker.

## Decisions still pending

Keep every existing pending row on [the S22R19 Owner Approval Sheet](RELEASE_S22R19_OWNER_APPROVAL_SHEET.md) pending until the named owner records a decision and date. Specifically, the release owner must decide:

1. The Model B deployment contract, exact supported disposable VM configuration, and final supported functionality.
2. Acceptance or remediation of the stated privileged Native ELF risks: PacMan has kernel privilege, no process address-space isolation exists, runtime checks do not protect a compromised kernel, the host hypervisor remains in the trust boundary, and signatures do not make software vulnerability-free.
3. `/Documents`, save/persistence behavior, recent-history behavior, Legacy Display behavior, and COM2 scope.
4. The content-equivalent or byte-identical reproducibility contract, and whether the remaining process/toolchain evidence is sufficient for provenance acceptance.
5. Signature format/tool, signer principal and namespace, named custodian, independent verification-key route, key generation/storage, authorized backup/recovery, rotation, and revocation/incident response.
6. Official channel, publisher identity/access, mirror rules, retention/versioning, correction/security notice path, and support owner.
7. The exact final manifest/ISO identity, public user documentation, and separate publication authorization.

Selecting Model B alone does not approve any of those decisions.

## Legacy Display remediation handoff

The visible Legacy context menu and Control Panel path opens `DisplayOptionsApp`. Its Displays tab presents Extend/Mirror, Primary monitor Display1/Display2, active desktop, backend and monitor information, resolution buttons, topology review/apply/refresh/keep controls, and global Apply/OK/Cancel actions. When startup query fails it can display “Display query failed,” but the controls remain visible and the global action buttons can imply that changes are supported.

`DisplayConfigurationService` delegates to the VirtIO-GPU display-configuration backend. In the public-preview build that backend is unavailable: query returns `BackendUnavailable`, and apply reports the unsupported QEMU-only backend. The build uses the security profile but does not enable the QEMU VirtIO-GPU probe or display-configuration control macro. Do not enable the probe as part of this correction.

Bounded follow-up: on the Displays tab show a clear “Not supported in this public preview” state and disable or hide the topology/resolution/backend controls and any global action that implies display configuration. Preserve Backgrounds, Desktop Icons, and Gradients. Modern Settings remains read-only for framebuffer characteristics; S22R18 QAC positively opened its System information path, but did not fully demonstrate a modern interactive display-configuration workflow.

This is a kernel UI source change. A new kernel ELF, build identity, EFI image, and ISO hash are expected; the bootloader is likely unchanged, and the ramdisk payload should be checked. Re-verify the new ISO structure, security configuration, PacMan inventory and resource hashes, and run focused QAC for the unsupported Display state, preserved non-Display tabs, modern Settings, boot, and PacMan. The S22R18 guest evidence remains tied to the original hash.

## Ordered future signing and publication procedure

The order below is the minimum controlled sequence. Each gate applies to the final identity after all candidate-changing work.

1. **Resolve owner decisions.** Approve the public-preview contract, residual risks, support scope, reproducibility standard, signature tool/format, custodian, public-key route, channel, and user-documentation owner.
2. **Complete engineering remediation.** Implement and review the bounded Display unsupported state. Resolve or explicitly disposition the adjacent ESP process provenance and toolchain-record gaps. Do not change the accepted artifact in place.
3. **Build a new candidate if source changes.** Use a clean, isolated source checkout, recorded commit, pinned dependencies, and fresh output directory. Record actual commands, environment, tool versions and binary hashes, input hashes, build receipt, and process-level artifact operations. Retain prior S22R18 evidence without substituting it.
4. **Qualify the exact candidate.** Verify source/profile configuration, kernel security boundary, package policy, all ESP contents, ISO structure, and relevant hosted/freestanding checks. Run focused guest checks for changed UI and existing preview behavior. Record the exact ISO SHA-256 and byte size in the evidence package.
5. **Freeze artifact identity.** Once approved, make no candidate-changing edits. Rehash the selected ISO and compare it with the candidate manifest and qualification record.
6. **Generate canonical manifest.** Enforce the approved schema, duplicate-key rejection, UTF-8/no-BOM JCS bytes, source/build receipt, component and package/resource hashes, ISO filename/size/hash, version/channel/architecture/profile, qualification references, and approved signing key identity.
7. **Sign with the approved authority.** Only the named custodian or owner-approved protected signing service uses the production key to create the detached signature over the exact manifest bytes. Keep private keys out of the build output and public download host. This step requires explicit owner authorization after the prior gates; S22R20 did not perform it.
8. **Independently verify.** From a clean verifier environment, verify the signature against the separately trusted public key, namespace and principal; reject malformed/noncanonical manifest bytes; check expected release identity; compare ISO bytes and SHA-256. Confirm modified manifest, modified ISO, wrong key, missing/unsupported signature, and malformed metadata all fail.
9. **Obtain explicit publication authorization.** The approval must identify exact version and ISO digest, channel, audience, publisher/uploader, and approved release files. A general Model B selection or signing approval is not publication approval.
10. **Publish and download-verify.** Upload the exact ISO, signed manifest, signature, and approved instructions to the chosen immutable/versioned channel. Download them from the public endpoint (and allowed mirrors) and repeat independent signature, identity, size, and hash verification. Record URLs, uploader, key fingerprint, time, and outputs.
11. **Retain and monitor.** Keep the signed release evidence and original versioned files. Publish correction or compromise notices via the separate owner-approved route. For key compromise, stop downloads, revoke/flag the key and affected versions, authenticate a replacement key out of band, build a new identity, requalify, and obtain new publication approval.

## Recommended S22R21 scope

Keep the next phase bounded to four deliverables:

1. Complete and QAC-qualify the Legacy Display unsupported state, creating a new candidate only after the owner approves the exact contract and residual risks.
2. Close the provenance gap using either (a) a recorded owner disposition accepting the demonstrated backup-sector and tool-version evidence, or (b) a fresh isolated, process-logged reproduction/build with the missing MSVC/MSBuild/Windows SDK and PacMan LLVM versions and binary hashes. Do not mutate S22R18 outputs.
3. Implement a reviewed cross-platform manifest verifier that enforces strict schema, RFC 8785/JCS canonical bytes, duplicate-key rejection, identity checks, and exact artifact size/hash; run the existing synthetic negative fixture plus verifier-specific tests.
4. Collect explicit owner choices for signer/custodian/trust route/channel and prepare a release approval record. Do not generate a production key or sign until the owner authorizes the specific process and exact final candidate.

S22R21 should not broaden VM support or security claims. Preserve the restricted disposable-VM boundary, PacMan-only Native ELF policy, read-only package source, no writable application package storage, no host filesystem integration, runtime identity/integrity checks, and all stated residual risks.
