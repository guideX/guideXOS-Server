# Server v0.3 Controlled Preview — Draft Release Contract

**Status: DRAFT — owner approval required.** This document proposes a controlled experimental preview contract for review. It does not select a release model, accept risk, authorize recipients, or permit distribution. The candidate is not approved for publication.

## Proposed audience

Named, trusted first-party evaluators and operators using disposable virtual machines or dedicated test hardware. Do not use the system for production workloads or sensitive data. Do not distribute it publicly under this contract.

This audience is a proposal only. Model A is not approved until an authorized release owner explicitly accepts the Native ELF risk, confirms the actual operating controls, and names the recipients.

## Candidate identity

This draft refers only to the already accepted Server v0.3 PacMan-only candidate:

- ISO SHA-256: `2fe2f5eda87c9a80164715a156af5de12c7cf3891d29a961c40012c9d032241e`
- Kernel SHA-256: `b5e029d23f9c21b5ff0838d9388322bf95b211bf3ef98145ddd344170dfe95dd`
- Build profile: `ServerV03PacManOnly`; PacMan is the only packaged Native ELF app. Developer Studio is excluded.

The evidence covers the exact ISO above. The separate content-equivalent rebuild ISOs have different identities and have not inherited guest acceptance. Verify the hashes before any future owner-authorized handoff.

## Proposed supported scope

The following behavior has evidence on the accepted candidate, within the stated limits:

- UEFI AMD64 boot to desktop on the recorded QEMU/OVMF setup.
- Desktop, Start menu, and keyboard/mouse interaction on the recorded setup.
- PacMan discovery, launch, visible gameplay response, normal exit, and cleanup.
- Modern Settings System and read-only framebuffer/display information.
- Navigator local-file URL use for reading the packaged PacMan manifest.
- Root FAT32/ATA storage initialization and the five observed VFS mounts.
- The optional external COM2 read-only storage-snapshot service only if the owner explicitly includes it; it requires a host endpoint and is not exercised by ordinary UI startup.

These are bounded qualification claims, not support for all hardware, devices, filesystems, built-in apps, network browsing, or Native ELF packages.

## Explicitly excluded or limited behavior

- **`/Documents`:** The visible Documents workflow cannot create/use the expected folder on this candidate. Do not rely on it for browse/save workflows. No data loss was observed in the qualification.
- **Recent Programs persistence:** In-session recents and app discovery work, but `/.startmenu_recent` is rejected by FAT; items may be empty or stale after restart.
- **Legacy Display configuration:** Display mode/topology configuration is unavailable. The production VirtIO-GPU probe gate is disabled; the route returns backend unavailable and shows `Display query failed`. Modern Settings is informational only. Before distribution, the visible entry/page must clearly say it is unsupported in this build and any configuration/apply controls must be disabled. This UI correction has not been made in S22R16.
- **Developer Studio:** Not included in the Server v0.3 PacMan-only profile. Its independent development is unaffected.
- **Network browsing and broad device/filesystem support:** Not established by the cited candidate qualification.
- **Arbitrary, third-party, or cross-version Native ELF compatibility:** Not supported by this contract. PacMan is the only packaged Native ELF app that received candidate guest qualification.

## Security and known risks

Native applications execute with kernel privilege. There is no enforceable per-application address-space isolation or syscall boundary. Runtime discovery can load packages from writable `/Apps`; the PacMan-only ISO profile does not prevent additional binaries being added to writable storage and discovered later. Manifests are unsigned and not cryptographically bound to executables, so they are not trusted identities. Runtime ABI/version checks are insufficient to promise cross-version compatibility.

An incompatible binary can call the wrong host callback or corrupt state. A malicious native binary can corrupt kernel memory. This release does **not** claim to prevent execution of malicious native binaries. A warning document and an ISO package profile do not create technical isolation or a runtime package restriction.

The recorded stack checks cover named paths with static compiler-derived estimates; there is no runtime high-water telemetry or whole-kernel memory-safety proof. Storage qualification is limited to the observed FAT32/ATA path. The reproducibility evidence supports a component/content-equivalence contract, not byte-identical whole-ISO output; the accepted raw ramdisk layout difference was not isolated.

## Required operating controls before any Model A handoff

The following controls must be established and evidenced by the preview operator. This draft does not claim any of them are already configured:

1. Limit access to named, trusted recipients and operators.
2. Run only in a disposable VM or dedicated test system, with no sensitive data, production credentials, or important host storage exposed to the guest.
3. Isolate networking and disable unnecessary host integration (shared folders, clipboard exchange, and device passthrough).
4. Verify the ISO hash before use; keep a known-good snapshot/image and a tested recovery or reimage procedure.
5. Restrict package-bearing storage and update changes to authorized operators. Use only verified first-party packages and do not add third-party or untrusted Native ELF files. If preventing guest writes to package-bearing storage is required, configure and validate that restriction at the media/VM layer; the OS does not enforce it.
6. Record the tested environment, storage controls, recovery procedure, and recipient list with the owner decision.

These controls reduce exposure and recovery cost. They do not isolate native code from the kernel or guarantee containment of guest faults.

## Approval required

Before distribution, the authorized release owner must:

- explicitly select the controlled experimental preview model;
- accept the residual risk from kernel-privileged Native ELF and writable package discovery;
- approve or revise each excluded/limited behavior above, including `/Documents`, recent history, Legacy Display, ABI scope, reproducibility wording, and optional COM2 scope;
- approve the actual operating controls and named recipient list; and
- explicitly authorize any distribution action separately.

Until those approvals exist, this contract remains a draft and the owner decision status is `OWNER_DECISIONS_PENDING`. Public technical preview and general-purpose release remain blocked under the current security model.

## Evidence

See [S22R16 Owner Decision Closure](RELEASE_S22R16_OWNER_DECISION_CLOSURE.md) and its S22R15/S22R14 evidence index for detailed qualification reports. The S22R16 phase did not alter or rebuild the candidate.
