# Server v0.3 — S22R19 Public Preview Deployment Contract

**DRAFT — NOT APPROVED**
**Scope:** exact S22R18 public-preview candidate only.

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
- Legacy Display mode or topology configuration.
- Third-party or user-installed Native ELF applications.
- General Native ELF compatibility or isolation.
- Network browsing, TLS, remote downloads, and bookmarks persistence.
- COM2 host storage service or writable storage management.
- Physical hardware, other hypervisors, or alternate QEMU virtual devices.

The /Documents creation path has a recorded FAT short-name failure. Read-only listing/reading of /Documents was not demonstrated on the S22R18 candidate, and no user-created file can be saved on its read-only root. The Start-menu history path reports no persisted state at boot. The Display backend is unavailable, and its visible UI needs a clear unsupported label and disabled configuration controls before public use. These are proposed contract exclusions; the release owner has not approved them.

## Security expectations

Before boot, the user must verify the exact ISO using an owner-approved signed release manifest and trusted public-key fingerprint. The SHA-256 above can identify the bits after verification; by itself it does not prove who published them.

The runtime allows the exact first-party PacMan package from the release read-only RAM disk. This is a narrow integrity/load policy, not process isolation. PacMan retains kernel privilege. A vulnerable PacMan, compromised kernel or boot chain, compromised host, hypervisor, firmware, or untrusted signing key can invalidate the expected protection. The profile does not support arbitrary Native ELF software.

## Evidence and approval

The exact ISO received two positive QAC runs and one separate diagnostic resource-tamper rejection. The QAC-tested host display frontend differs from the public interactive launcher proposed above. No public user launch configuration has been authorized. See [S22R18 Disposable VM Qualification](RELEASE_S22R18_DISPOSABLE_VM_QUALIFICATION.md), [S22R19 Readiness](RELEASE_S22R19_PUBLIC_PREVIEW_READINESS.md), and [S22R19 Owner Approval Sheet](RELEASE_S22R19_OWNER_APPROVAL_SHEET.md).
