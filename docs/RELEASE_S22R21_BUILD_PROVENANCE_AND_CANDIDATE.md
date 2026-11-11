# Server v0.3 — S22R21 Build Provenance and Candidate

**Assessment date:** 2026-11-10 (local); build and capture timestamps are UTC 2026-11-11

**Phase outcome:** `DISPLAY_CORRECTED_CANDIDATE_QUALIFIED`
**Release status:** candidate eligible for subsequent owner review; not approved, signed, tagged, or published

## Source and repository provenance

- Main Integration started on `main` at expected HEAD `59cfc323ae23bb159959965cab811798c0bb4490`, following the existing S22R20 documentation commit. No reset, clean, stash, branch switch, rebase, merge, cherry-pick, worktree creation, push, or release-marker change was performed.
- The implementation was committed locally as `b71147271d046a547885c47f333561dd66425785` (`fix: mark public preview display configuration unsupported`). The isolated build source is a detached clone at that exact commit; `git status --porcelain=v1 --untracked-files=all` was empty at build and packaging start.
- The detached clone used Git object alternates pointing to `D:\dev\guideXOSServer\.git\objects`; it was clean but not an independently self-contained source archive. The pinned, ignored Mbed TLS tree was copied into the clone. Its profile records Mbed TLS 4.1.0 commit `0fe989b6b514192783c469039edd325fd0989806`, TF-PSA Crypto 1.1.0 commit `29160dd877d29658279fd683b2ae57b320ddcf09`, and three patch series; profile SHA-256 `960888bc747745d97fe104e0552c2602eeb57ef32b4f79f2d7bfa6121a945055`.
- The pre-existing untracked `assets/Backgrounds/troncity.png` remains untouched in the integration checkout (SHA-256 `444ac64211907d80f0266e2283960cefaa932803d0aee7e0ccfd414f6fae2fda`). It was not copied into the clean source clone and is absent from the receipt's wallpaper input inventory and candidate.
- Source correction and test additions are listed in [Legacy Display UI Remediation](RELEASE_S22R21_LEGACY_DISPLAY_UI_REMEDIATION.md). The S22R18 candidate remains unchanged at SHA-256 `4cc445e3fe33eb615c5a0434977eaa664bf9f8c0c3e46743a9e9bde578f6a92d`.

## Canonical build

Build ID: `861456a4-85fd-4aee-9f95-9b34b763e83d`. The successful command was:

```powershell
scripts/create-release-iso.ps1 -Version 0.3.0 -Arch amd64 -ReleaseProfile ServerV03PublicPreviewPacManOnly -BootstrapTools
```

The release script invoked `build.ps1 -Arch amd64 -ReleaseProfile ServerV03PublicPreviewPacManOnly`. The successful run used a validated PowerShell 7.6.9 shim; it completed with exit code 0. The wrapper selected `-BootstrapTools` to install the pinned release-tool versions into the clone-local ignored venv. An earlier invocation failed before producing a candidate and was superseded; its transcript and the successful transcript are retained under `provenance\` in the run root.

At build and packaging start, the build identity records a clean worktree, source commit `b71147271d046a547885c47f333561dd66425785`, public-preview security profile `S22R18-PacMan-ReadOnly-Boot-Image-v1`, and `-DGXOS_PUBLIC_PREVIEW_SECURITY_PROFILE`. `EXTRA_CFLAGS` was empty; `GXOS_QEMU_VIRTIO_GPU_PROBE_ACTIVE` was unset. Bootloader, kernel, symbol audit, ramdisk, PacMan, and app-package stages passed; DeveloperStudio was explicitly `not_selected`.

### Toolchain inventory

The complete corrected point-in-time inventory, including executable paths, versions, SHA-256 values, source-script hashes, environment, and release-tool package versions, is retained at:

`E:\CodexArtifacts\S22R21\run-20261110-3d8d7075754749d7beb64590d4e2f162\provenance\tool-inventory.txt`

Key positively identified tools and input generations:

| Stage | Tool identity and role |
|---|---|
| Bootloader | MSBuild 18.11.1.51303; persisted project state selects MSVC v143 toolset 14.44.35207 and Windows SDK 10.0.26100.0. MSVC `cl.exe` 19.44.35229.0 and `link.exe` 14.44.35229.0 executable hashes are in the inventory. `guideXOSBootLoader.vcxproj` uses the project-local `tools\nasm.exe` 2.15.05 for `handoff_trampoline.asm`; this differs from the kernel assembler. |
| Kernel | MinGW-W64 GCC/G++ 15.2.0, GNU ld/Binutils 2.46.0.20260210, GNU Make 4.4.1, NASM 2.16.03, and Binutils `objcopy`, `objdump`, and `nm`; exact paths and SHA-256 are in the inventory. The AMD64 Makefile links PE and converts the result to ELF with `objcopy`. |
| PacMan | LLVM clang++ and LLD 22.1.8 plus `llvm-nm`; exact binary hashes and compile/link options are in the inventory and command log. Source pin `8fc2aedd603ddc57e492bdc02c99442bb165109b`; sibling repository HEAD `5d6133bfd206ab44d3d369a95b98ee004523936d`. The `git archive --format=zip` source export is 209,046,307 bytes, SHA-256 `cf400c32709881f7f782ccd0ac9881dc852933e4518c30ee718aa31a833ff314`. No sibling repository changes were made. |
| Runtime ramdisk | `scripts/generate-wallpaper-pack.ps1` builds the FAT32 filesystem in PowerShell/.NET. Its exact source hash is in the inventory. The build receipt inventories each included wallpaper by path, size, and digest. No external `mkfs` program was invoked. |
| ESP / ISO | Python 3.12.10 in the clone-local venv; `pyfatfs` 1.1.0, `fs` 2.4.16, and `pycdlib` 1.16.0 (plus `appdirs` 1.4.4, `six` 1.17.0, `setuptools` 75.8.0, and pip 25.0.1). `tools/release_esp.py` creates/verifies the FAT ESP; `tools/release_iso.py` creates/verifies ISO9660/El Torito UEFI media. Exact executable and script hashes are in the inventory. |
| Source/archive runner | Git 2.54.0.windows.1 and PowerShell 7.6.9. Git's `archive` creates the pinned PacMan ZIP; PowerShell `Expand-Archive` expands it. A local `requirements-release.txt` defines the Python package versions. |

The preliminary inventory's NASM and Visual Studio entries were corrected. There are two different NASM executables: repository-local NASM 2.15.05 for the bootloader project and MinGW NASM 2.16.03 recorded by the kernel receipt. `vswhere -latest` without a product filter selects the Visual Studio Community installation used by `build.ps1`; the prior inventory's SSMS 22 result was unrelated.

### Provenance limits

The exact host executables, tool versions, and binary hashes are recorded for this build, but the toolchains and Windows SDK are host-installed, not pinned or vendored by the repository. The Python requirements pin package versions, not downloaded wheel hashes; SDK libraries and every system header/library were not separately hashed. The source clone's Git object alternate also depends on the original local object store. The inventory is therefore a traceable point-in-time input record, not proof of an independently reproducible build.

The S22R14R2 content-equivalence reproducibility contract remains in force. ISO timestamps and generated UUID metadata vary, so byte-identical ISO output is not claimed. No second independent rebuild was performed.

## Candidate identity and direct verification

Candidate path:

`E:\CodexArtifacts\S22R21\run-20261110-3d8d7075754749d7beb64590d4e2f162\source\out\release-profile\861456a4-85fd-4aee-9f95-9b34b763e83d\preview-artifacts\guideXOS-Server-v0.3.0-amd64-public-preview-pacman-only-candidate.iso`

| Item | Identity |
|---|---|
| ISO | 90,245,120 bytes; SHA-256 `86a3296adf0558fa509a044232b0086e57edd6c0bd4236677fba90d8a294d5fe` |
| Manifest | SHA-256 `a6ad54184bf9f8b4e918302b0680d9f6b4e55f64eb898c6ca1715ac3c04b655d`; `candidateOnly=true`; no signature |
| Profile and source | `ServerV03PublicPreviewPacManOnly`, amd64; source commit `b71147271d046a547885c47f333561dd66425785`; clean build and packaging start |
| Embedded ESP | 90,177,536 bytes; SHA-256 `351f112f0c2b489d581445ce95f2004a6cca01befca8efbf03687d19ef966064`; extracted from the ISO to `provenance\embedded-UEFI-ESP.img` and verified, not inferred from an adjacent staging copy |
| Bootloader | `EFI/BOOT/BOOTX64.EFI`, 56,832 bytes; SHA-256 `bd0f631093f47bbeaabe2a2bc2a5f82312b3d2ca667f7d6c247884cad5241d30` |
| Kernel | 2,805,768 bytes; SHA-256 `2f00af98b86f27b28966c9d7b53fa27c8e652e528f50046114ed65cbdf3dbb31` |
| Ramdisk | 67,108,864 bytes; SHA-256 `f5ae73027e5c4a092bc68428a1f4097b6da066b7bda247580960a9a4317d7257` |
| PacMan | The sole Native ELF package. Manifest `Apps/PacMan/app.json`: 956 bytes, SHA-256 `3f3592e6c76053e323f16d48073e98c67d7f5702b8e47f0b507d6bfb3d126449`. ELF: 63,640 bytes, SHA-256 `eeb544bbb2e2574eeaed7bb7f0bad61ea24f6945231b7757223a225b30b8e1cc`. |
| Kernel symbol audit | SHA-256 `faf4972d31ea4c6234c33e250bf3ff6d5821b04211a7898e7a95c6e6da09bef6` |

`release_iso.py verify` confirmed the ISO9660 primary volume descriptor (LBA 16), El Torito boot record (LBA 17), catalog (LBA 31), UEFI platform ID `0xEF`, bootable no-emulation entry, and embedded image at LBA 32. `release_esp.py verify` checked all 16 receipt-listed payloads from the ESP extracted from the ISO; all sizes and hashes matched. DeveloperStudio is absent. The receipt and manifest are candidate integrity evidence, not publisher authentication.

## S22R20 discrepancy disposition

The S22R20 adjacent ESP discrepancy remains limited to FAT32 backup sector 6; its actor and cause are unknown. S22R21 did not patch that intermediate artifact, copy sectors, alter receipts, or claim the S22R20 authoritative ISO was changed. The new clean-source S22R21 ISO is a separate candidate identity and a separately extracted/verified ESP; it does not retroactively attribute or repair the S22R20 copy.

## Security and stack disposition

The runtime still allows only the approved PacMan Native ELF package, validates package and resource identity, enforces supported ABI/platform compatibility, and requires the read-only release `/Apps` source. The public-preview profile carries no new capability or privilege. DeveloperStudio remains excluded. Hosted policy tests and an isolated negative resource-tamper guest test are summarized in [Guest Qualification](RELEASE_S22R21_PUBLIC_PREVIEW_GUEST_QUALIFICATION.md).

The stack-budget guards passed for Settings Display (11,512/16,384 bytes; 4,872 headroom), Legacy Display Options (11,896/16,384; 4,488 headroom), and COM2 (7,768/16,384; 8,616 headroom). Each exceeds the required 4,096-byte headroom. These are static `.su` estimates, not measured runtime consumption.

## Owner boundary and next phase

Model B is the selected target; selection does not approve its contract. Keep pending the `/Documents` FAT short-name creation failure, nonpersistent recent history, COM2 scope, reproducibility standard, user-facing support boundaries, and acceptance of kernel-privileged PacMan with no process isolation. Also pending are signer identity/format, key custody and recovery, trusted verification-key route, distribution host/channel, and explicit publication authorization naming the final ISO digest. The synthetic SSHSIG proposal remains unapproved. No production key, signature, Git tag, upload, publication, release marker, or readiness promotion was created.

Recommended next phase: owner review of the exact S22R21 ISO and draft contract, followed by explicit closure of the remaining scope/security/reproducibility/signing/channel decisions. If approved, prepare a separate signed-manifest and independent-verification phase; do not change this candidate after approval, and require a new identity and requalification for any candidate-changing edit.
