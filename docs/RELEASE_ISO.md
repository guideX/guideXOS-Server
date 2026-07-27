# guideXOS Server release ISO

This process packages the repository's generated AMD64 UEFI `ESP` directory as
a read-only, single-boot UEFI ISO. It does not write USB devices or create
persistent storage media.

The boot payload is unchanged and remains inside the complete FAT EFI image:

- `EFI/BOOT/BOOTX64.EFI`
- `kernel.elf`
- `ramdisk.img`
- `build-identity.txt`

The ISO9660 tree contains that image as `UEFI_BOOT.IMG` and a small
`README.TXT`. The El Torito catalog references the exact `UEFI_BOOT.IMG` extent.

## Requirements

- Windows 10 or Windows 11.
- Windows PowerShell 5.1 or later.
- Python 3.10 or later. The release dependencies are installed only in the
  ignored `out\release-tools\venv` environment.
- QEMU and AMD64 OVMF are required only for the optional bounded smoke test.

The pinned release dependencies are in
[`requirements-release.txt`](../requirements-release.txt): `pyfatfs==1.1.0`,
`fs==2.4.16`, `pycdlib==1.16.0`, `appdirs==1.4.4`, `six==1.17.0`, and
`setuptools==75.8.0`. `pycdlib` 1.16.0 is a pure-Python ISO9660/El Torito
library, requires Python 3.10 or later, and is licensed LGPL-2.1-only. The
other pinned packages retain their existing licenses. No package is installed
globally.

## Bootstrap the local release tools

If the repository-local environment does not exist, use a real Python 3
executable and opt in to the isolated bootstrap:

```powershell
.\scripts\create-release-iso.ps1 `
    -Version 0.1.0-rc1 `
    -Arch amd64 `
    -Clean `
    -BootstrapTools
```

`-BootstrapTools` creates or updates only `out\release-tools\venv`.
Without that switch, missing or mismatched packages fail with instructions.
Use `-PythonPath C:\path\to\python.exe` to select a real Python executable.

## Build a release candidate

The default backend is `PyCdlib`; the normal flow does not discover or require
the Windows ADK:

```powershell
.\scripts\create-release-iso.ps1 `
    -Version 0.1.0-rc1 `
    -Arch amd64 `
    -Clean
```

To package the existing ESP without rebuilding it:

```powershell
.\scripts\create-release-iso.ps1 `
    -Version 0.1.0-rc1 `
    -Arch amd64 `
    -SkipBuild
```

`-SkipBuild` is explicit and still performs stale-input, required-file, FAT,
ISO, structural, and hash checks. Do not use `-RequireCleanWorktree` while the
release tooling is under review. Use `-Force` only when intentionally replacing
all three existing files in `dist`.

## Oversized UEFI El Torito behavior

The El Torito sector-count field is a count of 512-byte virtual sectors and
can represent at most `65535` sectors (`32 MiB - 512 bytes`). The current FAT
EFI image is larger than that limit, so `oscdimg` rejects it.

The release helper uses `pycdlib.PyCdlib.add_eltorito` directly with:

```python
iso.add_eltorito(
    "/UEFI_BOOT.IMG;1",
    boot_load_size=0,
    platform_id=0xEF,
    efi=True,
    media_name="noemul",
    bootable=True,
)
```

The chosen oversized sentinel is `0`. UEFI 2.10 section 13.3.2.1 specifies
that sector count `0` or `1` makes an EFI no-emulation system partition consume
the image from its start through the end of the CD-ROM; see the
[UEFI media-access specification](https://uefi.org/specs/UEFI/2.10/13_Protocols_Media_Access.html).
Established `xorriso` tooling documents the same `0`/`1` behavior and records
`0` for an oversized EFI image; see the
[xorriso El Torito documentation](https://manpages.debian.org/unstable/xorriso/xorriso.1.en.html).
The helper verifies the complete FAT file extent independently and permits no
unexpected data after it; `pycdlib` may place the expected human-readable
`README.TXT` after the boot-linked extent, followed only by zero alignment
padding. A false value such as `65535` is never used.

The catalog is independently checked for:

- a valid Primary Volume Descriptor and El Torito boot record;
- a valid catalog pointer and validation-entry checksum;
- platform ID `0xEF`;
- bootable `0x88`, no-emulation media type, and nonzero in-range boot-image LBA;
- the selected oversized sentinel;
- the complete FAT image extent and source/embedded SHA-256 equality; and
- plausible FAT32 boot-sector data and an untruncated ISO.

The helper also reopens the temporary ISO with `pycdlib` before it is promoted.

`oscdimg` is retained only as an explicitly selected compatibility backend:

```powershell
.\scripts\create-release-iso.ps1 `
    -Version 0.1.0-rc1 `
    -Arch amd64 `
    -SkipBuild `
    -IsoBackend Oscdimg `
    -OscdimgPath 'C:\path\to\oscdimg.exe'
```

It is not the default and cannot package the current oversized EFI image. The
normal release path must use `PyCdlib`.

## Expected output

```text
dist\
  guideXOS-Server-v0.1.0-rc1-amd64.iso
  guideXOS-Server-v0.1.0-rc1-amd64.iso.sha256
  guideXOS-Server-v0.1.0-rc1-amd64.manifest.json
```

The manifest records the backend, source commit/branch, worktree state, ESP
input hashes, complete FAT-image size/hash, raw El Torito verification report,
and pinned tool versions. Temporary ISO files, reports, and QEMU logs are
under ignored `out\release-iso`; the final ISO is moved into `dist` only after
validation succeeds.

## Test the exact ISO in QEMU

The bounded test attaches only the generated ISO as read-only CD-ROM media. It
does not map `fat:rw:ESP`, expose the source ESP directory, or attach a writable
persistence disk. It preserves the repository's normal `pc`, 1024 MiB, standard
VGA, GTK/VNC, UTC RTC, user-mode networking, E1000, serial, and `-no-reboot`
settings. Split OVMF variables are copied to a unique ignored test directory.

```powershell
.\scripts\test-release-iso.ps1 `
    -IsoPath '.\dist\guideXOS-Server-v0.1.0-rc1-amd64.iso' `
    -TimeoutSeconds 60
```

Review the retained `out\release-iso\qemu-test-*\serial.log` and QEMU log.
The wrapper reports virtual-machine evidence only; it does not claim
bare-metal compatibility.

## Limitations and troubleshooting

- AMD64 UEFI only; no legacy BIOS El Torito entry or hybrid USB image is
  generated.
- The ISO and embedded EFI image are read-only. Persistent user data requires
  separate writable storage.
- `Python 3 was not found` or a dependency-version error: pass `-PythonPath`
  to a real Python 3.10+ executable and use `-BootstrapTools`.
- Missing or stale ESP inputs: run the canonical AMD64 build and inspect
  `ESP\EFI\BOOT\BOOTX64.EFI`, `ESP\kernel.elf`, `ESP\ramdisk.img`, and
  `ESP\build-identity.txt`.
- Existing output collisions require `-Force`; a failed build does not publish
  a partially named final artifact.
- QEMU discovery or boot failure: pass explicit `-QemuPath`, `-OvmfPath`, and
  `-OvmfVarsPath`, then inspect the printed command and retained serial log.
  QEMU success still requires manual bare-metal validation.

Physical USB and bare-metal testing remain intentionally outside this tooling
change.
