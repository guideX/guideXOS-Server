# Server v0.3 — S22R19 Publication Prerequisites

**DRAFT — NOT APPROVED**
**Purpose:** define a separate future publication procedure. Nothing here signs, uploads, promotes, tags, or authorizes the candidate.

## Current release identity

The only candidate covered by this review is:

- Version: guideXOS Server 0.3.0, AMD64.
- Profile/build: ServerV03PublicPreviewPacManOnly / 7f9173c1-550b-4dfb-84e4-b44e1e1c260a.
- Source: main at 2e1c2493355bd005399977bcd82a4142c94ca2e9.
- ISO size: 90,245,120 bytes.
- ISO SHA-256: 4cc445e3fe33eb615c5a0434977eaa664bf9f8c0c3e46743a9e9bde578f6a92d.

The artifact has a local SHA-256 sidecar and manifest but no detached publisher signature. Those files prove local consistency only. They do not establish origin when obtained from an untrusted channel.

## Authentication gap and proposed minimum

No project-owned release signing key, signed release manifest, detached-signature workflow, key fingerprint distribution process, or official public channel was found in the S22R19 review. Public distribution is blocked until the release owner selects and approves them.

Before a future publication:

1. Name the accountable release owner, signing-key custodian, and channel maintainer.
2. Select a maintained, standard asymmetric detached-signature format and verifier. Sign a machine-readable manifest that binds the product, version, architecture, profile, build ID, exact ISO filename, byte size and SHA-256, source commit, build receipt, kernel, bootloader, RAM disk, PacMan manifest/ELF/resources, qualification report, and supported VM contract. The signature must cover the complete manifest; the manifest must identify the exact ISO bytes.
3. Protect the private signing key offline or in an owner-approved protected signing service. Define least-privilege access, backup, recovery, audit logging, rotation, revocation, and incident response before importing any production key. Do not store the only private key with the build workspace or public download server.
4. Publish the verification key fingerprint through a separately trusted, owner-controlled channel. The ISO and its manifest must not be the only source of the trust key. Record how users can confirm the fingerprint independently.
5. Host the ISO, signed manifest, detached signature, and readable verification instructions on an owner-approved official channel. Record the exact stable release URL and whether mirrors are allowed. Mirrors must preserve the signed files exactly.
6. Tell users to verify the fingerprint, verify the signature, compare manifest identity and ISO SHA-256, and stop without booting if any check fails. A SHA-256 file downloaded from the same untrusted source is not a substitute for signature verification.
7. Define revocation and recovery: stop distribution of a suspect artifact, publish a signed revocation/incident notice through independent trusted channels, revoke a compromised key, authenticate the replacement key out of band, issue a new versioned manifest and candidate, and repeat the required qualification. Never silently replace an ISO while retaining the old version identity.

The project must choose the concrete tool and signature format during the owner-approved implementation phase. S22R19 creates no production keys and signs no files.

## Mandatory prerequisites before a publication request

### 1. Freeze the exact approved ISO

- Record the exact SHA-256, byte size, filename, product version, architecture, profile, and build ID in the approval record.
- Rehash the frozen ISO immediately before signing and immediately before upload.
- If any byte changes, assign a new candidate identity and repeat the applicable qualification. Do not substitute the earlier accepted ISO or the separate diagnostic ISO.
- Preserve the candidate, signed manifest, signature, QAC reports, and build receipt as immutable release evidence.

### 2. Approve and reconcile provenance

- Preserve the clean S22R18 source identity: commit 2e1c2493355bd005399977bcd82a4142c94ca2e9, clean source status, selected package inventory, tool record, and exact component hashes.
- Resolve or explicitly disposition the adjacent generated ESP ramdisk mismatch: the staged ESP copy hashes to 24e2a7aba0ab7e37e2f0c92033d04da2b6766ebfa7bbb6e41eb2d60773f19e40, while the candidate ISO's embedded ramdisk and manifest hash to ead5105aa131dee575e0021fac1650a28330df422bd211ca6baccdd267179f05.
- Record the bootloader toolchain/SDK versions and all external dependency revisions needed for an independent build.
- If the discrepancy cannot be reconciled or the owner requires stronger source provenance, create a separately identified clean build and qualify its exact ISO. Never overwrite this S22R18 artifact.
- Do not claim S22R14R2 byte or content reproducibility for S22R18. If a reproducibility claim is required, complete two independently initialized builds under the approved comparison standard.

### 3. Approve the supported preview contract

The release owner must approve the exact disposable VM configuration, PacMan-only scope, privileged Native ELF risk, no user-data persistence, no /Documents, recent-history and Display exclusions, no guest network or host shares, no COM2 host service, and the known UI limitations. The public display launcher must be checked against the tested standard-VGA guest profile. Do not call this a sandbox or general-purpose secure app platform.

### 4. Approve the final user documentation

Complete and approve user-facing installation/launch, artifact verification, troubleshooting, recovery, support scope, and known-issues instructions. State prominently:

- Use the supported disposable AMD64 VM profile only.
- Verify the detached signature and exact ISO identity before boot.
- Attach the ISO and OVMF read-only; do not add writable disks, network, host shares, passthrough, remote QMP, or COM2.
- Nothing created or changed in the guest is saved after shutdown.
- /Documents and save workflows, recent-history persistence, display configuration, user-installed Native ELF apps, Navigator network/TLS, and COM2 are unsupported.
- PacMan has kernel privilege and the VM depends on the host and hypervisor.

Provide recovery instructions for signature failure, checksum failure, boot failure, VM reset, artifact revocation, and replacement releases.

### 5. Obtain explicit approvals

The signed approval record must identify:

- The exact ISO digest and manifest version.
- Accepted residual security risks and out-of-scope attacks.
- Approved disposable VM contract and exact exclusions.
- Accepted source-provenance and reproducibility standard.
- Signing-key custodian, verifier, key fingerprint publication route, rotation and revocation ownership.
- Official distribution channel, permitted mirrors, intended audience, and support owner.
- Approval of the final user guide.
- A separate explicit authorization to publish.

Model B target selection alone does not satisfy these approvals.

## Controlled publication sequence

1. Confirm all engineering and owner gates are closed for the exact candidate.
2. Freeze the approved ISO, manifest, build receipt, component inventory, and QAC evidence.
3. Independently rehash and inspect the ISO; verify the profile, PacMan-only inventory, UEFI boot structure, and read-only VM contract.
4. Produce the final manifest and detached signature using the owner-approved key process.
5. Verify the signature from a clean verifier environment using the independently published key fingerprint. Confirm altered-manifest and altered-ISO checks fail.
6. Upload only the approved ISO, manifest, detached signature, and instructions to the authorized official channel.
7. Download the published bytes from that channel and an allowed mirror, if any. Verify signature, manifest identity, exact ISO hash, and byte size after transfer.
8. Record publication time, URLs, uploader, approval record, public key fingerprint, and final verification outputs. Retain the signed evidence.
9. If a post-publication mismatch occurs, stop promotion/downloads, retain evidence, notify the release owner, revoke or correct as appropriate, and publish a new versioned identity only after reauthorization.

## S22R19 boundary

This document is a proposed checklist only. S22R19 did not generate or import signing keys, create a signature, publish a digest, upload the ISO, create a release tag, push a branch, modify dist, change readiness markers, or authorize publication. Publication remains a separate owner decision.
