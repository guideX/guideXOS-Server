# Server v0.3 — S22R19 Public Preview Deployment Contract

**DRAFT — NOT APPROVED**
**Historical base scope:** exact S22R18 public-preview candidate. The original contract below remains its identity-specific record; see the proposed S22R21 addendum at the end for the separately qualified candidate and revised Display behavior.

This contract describes the narrow disposable virtual machine profile that received S22R18 QAC evidence. It is not approval to distribute the ISO. It does not describe physical installation, a general-purpose OS security model, or a sandbox for Native ELF applications.

## Exact image

- Product: guideXOS Server 0.3.0, AMD64.
- Candidate build ID: 7f9173c1-550b-4dfb-84e4-b44e1e1c260a.
- Release profile: ServerV03PublicPreviewPacManOnly.
- ISO size: 90,245,120 bytes.
- ISO SHA-256: 4cc445e3fe33eb615c5a0434977eaa664bf9f8c0c3e46743a9e9bde578f6a92d.
- Boot: UEFI AMD64, El Torito platform 0xEF, no-emulation. No legacy BIOS entry is present.
- Contents: first-party PacMan is the only Native ELF package; Developer Studio is absent.
- The ISO hash is a local artifact identity. Users need an owner-approved signature and trusted distribution channel before it can serve as publisher authentication.

## Tested virtual hardware

| Setting | S22R18 QAC observation | Contract boundary |
|---|---|---|
| Emulator | QEMU 11.0.0 (v11.0.0-12122-ga4bb4b10c9), qemu-system-x86_64 | The exact qualified QEMU family/version only. Other versions or hypervisors need separate qualification. |
| Machine and acceleration | AMD64 PC machine, WHPX | No ARM, legacy BIOS, physical installation, or alternate virtual chipset claim. |
| CPU and memory | 2 virtual CPUs; 2048 MiB | These are the qualified values. |
| Firmware | OVMF code at C:\Program Files\qemu\share\edk2-x86_64-code.fd; SHA-256 33090cc07675baa5190d9f1e84bf5176b33bcbfa9bacac522961150cdb6dbb2a | Attach firmware code read-only. No persistent writable UEFI variable file or variables drive is permitted. |
| Video | Standard VGA | Do not add VirtIO-GPU or other unqualified display hardware. |
| Keyboard and mouse | QEMU PC machine defaults; QAC delivered bounded input through QMP | PS/2-style standard PC input is the target. USB input and passthrough are not qualified. |
| Guest disks | Read-only release CD-ROM only; no writable virtual disk | Do not attach writable disks, USB storage, host directories, or package volumes. |
| Network | QEMU network disabled | No NIC, user/NAT network, tap, bridge, forwarding, or guest network access. |
| Serial/COM2 | COM1 debug log only; no guest-accessible host COM2 channel | Do not attach a host service endpoint or advertise COM2 support. |
| Monitor/QMP | HMP monitor disabled. QAC used QMP on 127.0.0.1 at a run-specific port for host control and capture. | QMP was host management, not guest access. Ordinary public launch should omit QMP; if required, bind only to protected local loopback or a private local socket. Never expose it externally. |
| Host integrations | No host filesystem share, clipboard share, writable disk, or device passthrough in the tested launch | Do not enable 9p, virtiofs, SMB, host forwarding, USB passthrough, or similar host integrations. |
| Display frontend | QAC used display none and QMP screenshots | An interactive public launcher needs a visible host display frontend and ordinary user input. That frontend differs from the QAC command and was not itself qualified. Preserve standard VGA; qualify materially different guest hardware separately. |

The QAC command used a read-only OVMF pflash image and a read-only ISO CD-ROM. It had no writable guest disk or persistent UEFI variables file. QEMU was initially paused; QAC verified the exact process, reservation, and loopback QMP endpoint before continuing the guest. The tested QMP port was assigned per run and was not reachable from the guest because networking was disabled.

## Proposed launch shape

The public launcher should use qemu-system-x86_64 with the exact QEMU release, AMD64 PC/WHPX machine, 2 vCPUs, 2048 MiB, standard VGA, read-only OVMF code, the hash-verified ISO attached as a read-only CD-ROM, and UEFI boot from that CD-ROM. It must disable all network interfaces and omit every writable disk, host share, USB passthrough, COM2 endpoint, and remote monitor. It must not create a writable UEFI variables disk.

The S22R18 QAC invocation additionally used display none and private QMP for automation. A human-facing launcher must change the host display/input frontend to make the desktop usable. That host-side change is documented here but does not inherit QAC acceptance automatically. The owner must approve the resulting supported launch instructions, and the selected public launcher needs a focused guest check before it is advertised as qualified.

## Persistence and reset

This is a live, disposable VM profile. The ISO and guest root source are read-only. There is no writable guest disk, writable package directory, persistent user-data volume, or persistent UEFI variable store in the tested configuration. Treat all guest state as temporary. Do not enter or create information that must survive shutdown. Close or discard the VM to reset it; boot a newly verified copy of the exact release ISO for a clean session.

There is no supported installation, update, import, backup, or restore workflow. Do not copy a personal disk into the VM or add writable storage.

## Supported user-facing scope

The exact S22R18 evidence supports UEFI boot to desktop, Start-menu operation, Settings System information, Navigator's local/offline smoke path, and first-party PacMan launch/input/normal close. The candidate's stack guards cover the named Settings, Display, and COM2 paths statically; they are not a general memory-safety guarantee.

The following are outside the proposed preview contract:

- Persistent user files, /Documents, and save workflows.
- Persistent settings and Start-menu recent history.
- Legacy Display mode or topology configuration (S22R21 now presents this as an explicit read-only unsupported state on its candidate).
- Third-party or user-installed Native ELF applications.
- General Native ELF compatibility or isolation.
- Network browsing, TLS, remote downloads, and bookmarks persistence.
- COM2 host storage service or writable storage management.
- Physical hardware, other hypervisors, or alternate QEMU virtual devices.

The /Documents creation path has a recorded FAT short-name failure. Read-only listing/reading of /Documents was not demonstrated on the S22R18 candidate, and no user-created file can be saved on its read-only root. The Start-menu history path reports no persisted state at boot. The Display backend is unavailable. The S22R19 assessment required a clear unsupported label; the S22R21 addendum records that correction and its exact-candidate QAC evidence. These are proposed contract exclusions; the release owner has not approved them.

## Security expectations

Before boot, the user must verify the exact ISO using an owner-approved signed release manifest and trusted public-key fingerprint. The SHA-256 above can identify the bits after verification; by itself it does not prove who published them.

The runtime allows the exact first-party PacMan package from the release read-only RAM disk. This is a narrow integrity/load policy, not process isolation. PacMan retains kernel privilege. A vulnerable PacMan, compromised kernel or boot chain, compromised host, hypervisor, firmware, or untrusted signing key can invalidate the expected protection. The profile does not support arbitrary Native ELF software.

## Evidence and approval

The exact ISO received two positive QAC runs and one separate diagnostic resource-tamper rejection. The QAC-tested host display frontend differs from the public interactive launcher proposed above. No public user launch configuration has been authorized. See [S22R18 Disposable VM Qualification](RELEASE_S22R18_DISPOSABLE_VM_QUALIFICATION.md), [S22R19 Readiness](RELEASE_S22R19_PUBLIC_PREVIEW_READINESS.md), and [S22R19 Owner Approval Sheet](RELEASE_S22R19_OWNER_APPROVAL_SHEET.md).

## Proposed S22R21 addendum — 2026-11-10

**DRAFT — NOT APPROVED.** This addendum records engineering evidence and a proposed identity update only. It does not amend approval status or authorize release-owner acceptance, signing, or distribution. The S22R18 values above remain the historical candidate identity; they are not transferred to the new ISO.

### Proposed candidate identity

- Product: guideXOS Server 0.3.0, AMD64.
- Build ID: `861456a4-85fd-4aee-9f95-9b34b763e83d`.
- Source: `b71147271d046a547885c47f333561dd66425785`.
- Release profile: `ServerV03PublicPreviewPacManOnly`.
- Candidate ISO: `guideXOS-Server-v0.3.0-amd64-public-preview-pacman-only-candidate.iso`.
- Size: 90,245,120 bytes.
- SHA-256: `86a3296adf0558fa509a044232b0086e57edd6c0bd4236677fba90d8a294d5fe`.
- Candidate is unsigned and unpublished; SHA-256 is a local identity, not publisher authentication.

The image is AMD64 UEFI with no legacy BIOS boot entry. PacMan is the only Native ELF package; DeveloperStudio is absent. It is intended for the restricted, disposable QEMU profile described above, subject to owner approval and final user-facing launch instructions. Keep the read-only ISO and OVMF code, no writable guest storage or UEFI variables, no NIC, no host share, no guest COM2 service, and no exposed monitor. QAC's loopback QMP was host control for qualification, not guest access.

### Proposed Display behavior and supported scope

Legacy Display Options remains visible so users can inspect its page. Its Displays tab states: “Display configuration is not supported in the v0.3 public technical preview.” The page also explains it is read-only until a supported backend is qualified. Mode/topology, monitor, resolution, Apply, and OK controls are hidden and blocked; Close remains available. Reopening the window and re-navigation were observed in QAC. Modern Settings navigation remains active, and its Display page remains a read-only framebuffer snapshot. No display backend or GPU probe is enabled. The S22R21 UI result does not qualify other hardware or a public interactive host launcher.

The exact new candidate received direct ISO/embedded-ESP verification and focused QAC evidence for the Display state and Modern Settings navigation. A separate QAC run passed PacMan gameplay input and Escape cleanup. A separately hashed, one-byte resource-tamper diagnostic was rejected before native entry; that diagnostic media is not a release candidate. See [S22R21 guest qualification](RELEASE_S22R21_PUBLIC_PREVIEW_GUEST_QUALIFICATION.md).

### Remaining proposed exclusions and owner decisions

The following remain outside the proposed supported scope until the owner decides otherwise: `/Documents` creation and persistence, recent-history persistence, COM2 host storage/service scope, arbitrary or third-party Native ELF execution, general app isolation, network use, writable disks/package sources, physical installation, other hypervisors, and other QEMU hardware profiles. PacMan continues to execute with kernel privilege and the kernel has no process address-space isolation. Those are explicit residual risks, not claims of a sandbox.

S22R21 records exact point-in-time host tool identities and a clean source/build state. Host toolchain/SDK inputs are not repository-pinned, a second independent build was not performed, and nondeterministic ISO timestamps/UUIDs mean byte-identical ISO output is not promised. Retain S22R14R2 content-equivalence as the reproducibility contract. The S22R20 backup-sector-6 discrepancy remains unexplained and unmodified; this new candidate does not repair or attribute the older adjacent ESP.

Owner decisions still required include acceptance of the candidate and exact VM scope, `/Documents` and recent-history behavior, COM2 scope, content-equivalence and provenance sufficiency, privileged PacMan/no-process-isolation risk, final user documentation/support ownership, production signer and signature format, key custody/recovery/rotation, independently trusted verification-key delivery, distribution host/channel, and explicit publication authorization naming the ISO digest. The synthetic SSHSIG proposal remains unapproved. No production signing key, signature, Git tag, upload, publication, or release-readiness marker has been created.
