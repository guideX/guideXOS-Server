# guideXOS Server release ISO

This process packages the repository's generated AMD64 UEFI `ESP` directory as
a read-only, single-boot UEFI ISO. The ISO is intended for release-candidate
testing. It does not write USB devices or create persistent storage media.

The boot payload is the existing guideXOS Server payload:

- `EFI/BOOT/BOOTX64.EFI`
- `kernel.elf`
- `ramdisk.img`

The bootloader loads `kernel.elf` and `ramdisk.img` from the root of the FAT
EFI image. Persistent user data must live on separate writable storage; the
release ISO is read-only.

## Requirements

- Windows 10 or Windows 11.
- Windows PowerShell 5.1 or later. The public entry points are PowerShell
  scripts and resolve the repository from their own locations, so the caller
  does not have to start in the repository directory.
- A working Python 3.8+ installation for the FAT-image helper. Python is used
  only through the repository-local release tooling environment.
- Microsoft `oscdimg.exe` from the Windows ADK Deployment Tools component.
- QEMU and AMD64 UEFI firmware are required only for the optional QEMU test.

Install the Windows ADK from Microsoft's [ADK installation
documentation](https://learn.microsoft.com/en-us/windows-hardware/get-started/adk-install).
In the feature selection dialog, select the Deployment Tools component only;
the release script does not download or install the ADK. `oscdimg.exe` is
normally installed below:

```text
C:\Program Files (x86)\Windows Kits\10\Assessment and Deployment Kit\Deployment Tools\amd64\Oscdimg\oscdimg.exe
```

The script checks `PATH` and common ADK locations. It does not permanently
modify `PATH`. If more than one candidate is found, pass `-OscdimgPath` with
the exact intended file.

The pinned Python dependencies are in
[`requirements-release.txt`](../requirements-release.txt): `pyfatfs==1.1.0`,
`fs==2.4.16`, `appdirs==1.4.4`, `six==1.17.0`, and `setuptools==75.8.0`.
They are MIT-licensed and are installed only under the ignored
`out\release-tools\venv` directory. The package metadata is available from
[pyfatfs on PyPI](https://pypi.org/project/pyfatfs/).

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

`-BootstrapTools` creates or updates only
`out\release-tools\venv`. Without that switch, missing or mismatched packages
are reported with instructions and nothing is installed. An explicit Python
can be selected with `-PythonPath C:\path\to\python.exe`.

## Build a release candidate from the canonical build

From any PowerShell location, run:

```powershell
.\scripts\create-release-iso.ps1 `
    -Version 0.1.0-rc1 `
    -Arch amd64 `
    -Clean
```

The script resolves the repository root from `scripts\create-release-iso.ps1`,
validates Git state, invokes the supported `build.ps1 -Arch amd64` flow, checks
for stale or incomplete ESP inputs, calculates FAT capacity from the actual
ESP contents plus a safety reserve, creates `EFI-BOOT.IMG`, creates the ISO
with `oscdimg.exe`, reopens and verifies both images, and publishes only after
all validation succeeds.

A dirty worktree is allowed for development release candidates but is warned
about and recorded in the manifest. For final packaging, require a clean
worktree:

```powershell
.\scripts\create-release-iso.ps1 `
    -Version 0.1.0-rc1 `
    -Arch amd64 `
    -Clean `
    -RequireCleanWorktree
```

Use `-Force` only when intentionally replacing all three existing artifacts.
The script otherwise fails before touching an existing final artifact.

## Package an existing ESP

`-SkipBuild` is explicit; it never becomes an implicit fallback. The same
required-file, empty-file, path, stale-input, FAT, ISO, and hash checks still
run:

```powershell
.\scripts\create-release-iso.ps1 `
    -Version 0.1.0-rc1 `
    -Arch amd64 `
    -SkipBuild
```

Use this only when `ESP\EFI\BOOT\BOOTX64.EFI`, `ESP\kernel.elf`, and
`ESP\ramdisk.img` are the intended, freshly generated files.

## Expected output

```text
dist\
  guideXOS-Server-v0.1.0-rc1-amd64.iso
  guideXOS-Server-v0.1.0-rc1-amd64.iso.sha256
  guideXOS-Server-v0.1.0-rc1-amd64.manifest.json
```

The manifest includes the product, version, architecture, artifact type, ISO
filename and byte size, SHA-256, source commit and branch, worktree status,
UTC packaging timestamp, all ESP input hashes, EFI-image capacity and hash,
ISO boot metadata, and tool versions/hashes. It contains no host-specific
secrets or unnecessary absolute paths.

The temporary FAT image, ISO staging tree, and helper reports are under the
ignored `out\release-iso` work root and are removed after a successful or
failed build. The published ISO is first validated under a temporary filename
and then moved into `dist`.

## Verify the checksum

```powershell
$iso = '.\dist\guideXOS-Server-v0.1.0-rc1-amd64.iso'
$sha = '.\dist\guideXOS-Server-v0.1.0-rc1-amd64.iso.sha256'
$expected = (Get-Content -LiteralPath $sha).Split()[0].ToLowerInvariant()
$actual = (Get-FileHash -LiteralPath $iso -Algorithm SHA256).Hash.ToLowerInvariant()
if ($expected -ne $actual) { throw "SHA-256 mismatch: $iso" }
'SHA-256 OK'
```

## Test the exact ISO in QEMU

The explicit test wrapper keeps the normal AMD64 UEFI settings used by the
repository: `pc` machine, 1024 MiB RAM, standard VGA, GTK display, VNC `:0`,
UTC RTC, user-mode networking with an E1000 device, serial capture, and
`-no-reboot`. It replaces the development-only `fat:rw:ESP` mapping with a
read-only CD-ROM ISO and does not attach a writable persistence disk.

```powershell
.\scripts\test-release-iso.ps1 `
    -IsoPath '.\dist\guideXOS-Server-v0.1.0-rc1-amd64.iso' `
    -TimeoutSeconds 60
```

QEMU and OVMF are discovered in `PATH`, the repository's `OVMF.fd`/`ovmf.fd`,
and the existing common QEMU installation locations. Split firmware variables
are copied into a unique ignored test directory; the source firmware is never
modified. Use `-QemuPath`, `-OvmfPath`, and `-OvmfVarsPath` when discovery is
not sufficient. The complete command and serial-log path are printed. A
QEMU result is virtual-machine evidence only and does not prove bare-metal
hardware compatibility.

## Promote a tested RC without rebuilding

Promotion is a byte-preserving copy. It does not rebuild the ESP or ISO. Run
this only after testing the RC and only when the final-named files do not
already exist:

```powershell
$dist = (Resolve-Path '.\dist').Path
$rcBase = Join-Path $dist 'guideXOS-Server-v0.1.0-rc1-amd64'
$finalBase = Join-Path $dist 'guideXOS-Server-v0.1.0-amd64'
$finalIso = "$finalBase.iso"
$finalSha = "$finalIso.sha256"
$finalManifest = "$finalBase.manifest.json"
foreach ($path in @($finalIso, $finalSha, $finalManifest)) {
    if (Test-Path -LiteralPath $path) { throw "Refusing to overwrite $path" }
}
Copy-Item -LiteralPath "$rcBase.iso" -Destination $finalIso
$hash = (Get-FileHash -LiteralPath $finalIso -Algorithm SHA256).Hash.ToLowerInvariant()
Set-Content -LiteralPath $finalSha -Value "$hash  $([IO.Path]::GetFileName($finalIso))" -Encoding utf8
$manifest = Get-Content -LiteralPath "$rcBase.manifest.json" -Raw | ConvertFrom-Json
$manifest.isoFilename = [IO.Path]::GetFileName($finalIso)
$manifest.isoByteSize = (Get-Item -LiteralPath $finalIso).Length
$manifest.sha256 = $hash
$manifest | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $finalManifest -Encoding utf8
```

The final manifest retains the RC's source commit, input hashes, tool records,
and verification metadata while updating the filename and checksum.

## Limitations and troubleshooting

- This release path supports AMD64 UEFI only. It intentionally creates no
  legacy BIOS El Torito entry and does not claim hybrid USB behavior.
- ISO media and the embedded EFI image are read-only. Persistent user data
  needs separate writable storage.
- Media Creator and physical USB writing are separate future work. This script
  never selects or writes a physical disk.
- `Python 3 was not found`: install Python for the current user, or pass
  `-PythonPath` to an existing Python 3 executable. No global package is
  installed by the script.
- `pyfatfs is unavailable` or an unpinned-version error: use
  `-BootstrapTools`, or install the exact pinned requirements into
  `out\release-tools\venv` yourself.
- `oscdimg.exe was not found`: install only the ADK Deployment Tools component,
  or pass `-OscdimgPath` with the full path to an existing `oscdimg.exe`. The
  script does not download the ADK.
- `required ESP input is missing/empty`: run the canonical AMD64 build and
  inspect `ESP\EFI\BOOT\BOOTX64.EFI`, `ESP\kernel.elf`, and
  `ESP\ramdisk.img`. Do not use `-SkipBuild` for an absent or stale ESP.
- An existing output collision requires `-Force`; a failed build never
  publishes a partially named final ISO.
- QEMU discovery or boot failure: pass explicit `-QemuPath` and firmware
  paths, inspect the printed command and the retained serial log under
  `out\release-iso\qemu-test-*`, and confirm that the ISO is attached as
  `media=cdrom,readonly=on`. QEMU success still requires manual bare-metal
  validation.

USB image creation is intentionally deferred. The ISO path is the current
release deliverable and no raw disk image or physical-media operation is
performed.
