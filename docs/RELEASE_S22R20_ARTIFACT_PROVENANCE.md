# Server v0.3 — S22R20 Artifact Provenance

**DRAFT — INVESTIGATION ONLY**

**Assessment date:** 2026-11-08

**Proposed outcome:** `PROVENANCE_OR_AUTHENTICATION_GAP`
**Candidate under review:** S22R18 AMD64 PacMan-only candidate; no artifact was modified.

## Disposition

The ramdisk difference is now localized to a redundant FAT32 backup boot sector in the adjacent generated ESP. The accepted ISO contains the ramdisk whose SHA-256 is recorded in its build identity and candidate manifest. The two ramdisks contain the same 96 filesystem paths with identical file bytes, file timestamps, and directory metadata. The adjacent ESP's backup boot sector was populated after the candidate and manifest were written; the retained logs do not identify the process that wrote it.

This supports classification **A — metadata/image-construction-only** and **D — post-packaging modification** for the adjacent ESP copy. It does not indicate that different ramdisk inputs reached the accepted ISO. The accepted ISO can be linked to its source commit, clean source state, build identity, package inventory, and ISO assembly record. Full independent-build provenance is still incomplete: the recorded evidence omits the exact MSVC/MSBuild and Windows SDK versions, the PacMan LLVM compiler/linker versions and binary hashes, and the writer of the later backup-sector change. S22R20 therefore does not claim complete reproducible-build provenance.

No new ISO is required solely to replace or repair that non-authoritative adjacent staging file. Do not copy the accepted ramdisk over it. The public candidate still needs a separate Legacy Display correction before publication; any candidate produced after that correction will have a new identity and must be requalified.

## Repository preflight and preservation

- Integration checkout: `D:\dev\guideXOSServer`, branch `main`, HEAD `a4528794e4aab3ff28f0a6a95bb51ac057672428`.
- At preflight, `origin/main` and `main` both pointed at that expected commit (0 ahead / 0 behind). The tracked worktree was clean.
- `git worktree list` also showed existing `guideXOSServer-gfx1`, `guideXOSServer-vfs1`, and `guideXOSServer_INPUT_RELIABILITY` worktrees; none was modified. A later concurrent-work scan at `2026-11-09T04:46:51.7690100Z` found QEMU PID 26568 using a separate `D:\dev\guideXOSServer_FONT_SUPPORT` clone and its FONT15R4R2 candidate, not this S22R18 ISO. It was left running and untouched.
- No S22R20 phase marker or repository `AGENTS.md` was found in the integration checkout during the read-only discovery.
- The pre-existing untracked `assets/Backgrounds/troncity.png` remains untouched; SHA-256 `444ac64211907d80f0266e2283960cefaa932803d0aee7e0ccfd414f6fae2fda`. It is absent from the candidate input inventory.
- The source export is the separate path `E:\CodexArtifacts\S22R18\source-2e1c2493`, at commit `2e1c2493355bd005399977bcd82a4142c94ca2e9`, branch `main`, with clean tracked and untracked status at that commit.
- No submodule dependency was listed in that source export; the recorded external package/source pins are described below.
- This phase did not switch branches, create worktrees, modify `dist`, alter release/readiness markers, reset/clean/stash, or change the candidate ISO and source export.

## Exact candidate identity and verification

| Item | Evidence |
|---|---|
| ISO | `E:\CodexArtifacts\S22R18\source-2e1c2493\out\release-profile\7f9173c1-550b-4dfb-84e4-b44e1e1c260a\preview-artifacts\guideXOS-Server-v0.3.0-amd64-public-preview-pacman-only-candidate.iso` |
| Size | 90,245,120 bytes |
| SHA-256 | `4cc445e3fe33eb615c5a0434977eaa664bf9f8c0c3e46743a9e9bde578f6a92d` |
| Candidate manifest | 15,447 bytes; SHA-256 `3949f9d3ff14123a532416df8d98b78ba5c3949c1a55036a3449dc326ad87d83` |
| SHA sidecar | `guideXOS-Server-v0.3.0-amd64-public-preview-pacman-only-candidate.iso.sha256`; contains the measured ISO SHA-256 and exact filename. The size is independently measured from the ISO. It is unsigned and does not authenticate the publisher. |
| Build identity / receipt in ESP | `build-identity.txt`, 5,554 bytes; SHA-256 `ce74eb013dfd57e5717570e84435b75d256efbafae111b87680a9e23f69cde21` |
| Build ID / profile | `7f9173c1-550b-4dfb-84e4-b44e1e1c260a` / `ServerV03PublicPreviewPacManOnly` |
| Source | `2e1c2493355bd005399977bcd82a4142c94ca2e9` on `main`; canonical build and packaging both recorded a clean source worktree. |
| Candidate state | `candidateOnly=true`; `buildMode=canonical-build`; not signed or publicly released. |

The repository `tools/release_iso.py verify` independently parsed the ISO and matched its expected inventory. It found an ISO9660 primary volume descriptor at LBA 16, El Torito boot record at LBA 17, catalog at LBA 31, UEFI platform ID `0xEF`, bootable no-emulation media, and the 90,177,536-byte EFI image at LBA 32. The EFI image SHA-256 is `ba9ad43ca8c1b8d5f675b2e20aee46f07ea7e7a0a093f9953b4c648dbe5b3fc2`.

The EFI image was extracted from the ISO at the recorded LBA and verified with the source export's pinned release-tools virtual environment and `tools/release_esp.py verify`. All 16 candidate-manifest entries passed. The resulting identities include:

| ESP payload | Size | SHA-256 |
|---|---:|---|
| `EFI/BOOT/BOOTX64.EFI` | 56,832 | `1ebb48ff3ff7534e10fc0877f90c764be57df7f7a1b1b9da30f910da8e1afe79` |
| `kernel.elf` | 2,805,584 | `4d416905e36e77ae17314cb39d6805c4dcef3d01834a0aab00a6985f3d49369a` |
| `ramdisk.img` (embedded) | 67,108,864 | `ead5105aa131dee575e0021fac1650a28330df422bd211ca6baccdd267179f05` |
| `build-identity.txt` | 5,554 | `ce74eb013dfd57e5717570e84435b75d256efbafae111b87680a9e23f69cde21` |
| `Apps/PacMan/app.json` | 956 | `3f3592e6c76053e323f16d48073e98c67d7f5702b8e47f0b507d6bfb3d126449` |
| `Apps/PacMan/bin/amd64/pacman.elf` | 63,640 | `eeb544bbb2e2574eeaed7bb7f0bad61ea24f6945231b7757223a225b30b8e1cc` |

The PacMan manifest plus executable and nine resource files (11 entries total) matched the signed-input inventory listed below. The accepted release profile is PacMan-only and records `-DGXOS_PUBLIC_PREVIEW_SECURITY_PROFILE`; neither `GXOS_QEMU_VIRTIO_GPU_PROBE_ACTIVE` nor `GXOS_DISPLAY_CONFIGURATION_CONTROL_ACTIVE` is recorded as enabled.

### Complete 16-file ESP inventory

These are the exact path, size, and SHA-256 values in the candidate manifest's `inputFiles` array, independently confirmed from the ISO's embedded ESP.

| Path | Bytes | SHA-256 |
|---|---:|---|
| `Apps/PacMan/app.json` | 956 | `3f3592e6c76053e323f16d48073e98c67d7f5702b8e47f0b507d6bfb3d126449` |
| `Apps/PacMan/bin/amd64/pacman.elf` | 63,640 | `eeb544bbb2e2574eeaed7bb7f0bad61ea24f6945231b7757223a225b30b8e1cc` |
| `Apps/PacMan/resources/audio/EatPill.wav` | 7,156 | `510a52e5776573197aef14d77e393185b6c7148d1b59dac535d45c5ca0498686` |
| `Apps/PacMan/resources/audio/extralife.wav` | 21,248 | `767db1552130e676398c121ec605f90f20c89bbbab272305bc781adc8e91d1cd` |
| `Apps/PacMan/resources/audio/fruiteat.wav` | 5,120 | `82c96a997ac2e837de4e418f29f4f4fa6142dd2a0c38f764b6d989d8bd98605d` |
| `Apps/PacMan/resources/audio/ghosteat.wav` | 9,090 | `225168072b38671c89c1fc775d513ca86fbb899e3e82c8032445f6a6dc5e1b95` |
| `Apps/PacMan/resources/audio/killed.wav` | 17,008 | `1c3e91df8e19041ea5783185c5a8a9f2321993b43481c215d8caf3df590290de` |
| `Apps/PacMan/resources/audio/StartMusic.wav` | 68,318 | `062cdc7196c54033f8bf11878d6659fd5b0ba11cd6945640ec8fac4376919421` |
| `Apps/PacMan/resources/audio/startmusicold.wav` | 46,592 | `a6e1c14f6cbc59cb5244c0119c728d412252370eea48bc574c1307a774c83caf` |
| `Apps/PacMan/resources/level1.gximg` | 888,860 | `535b37e66d7159078df8a16ce059a6f7ab038588cc9875e152033d046ff47fe1` |
| `Apps/PacMan/resources/pacpics.gximg` | 360,476 | `57a605df8f24ac9641ff6c9d39df551a17ffbcd68466a561adf5f045de259d50` |
| `build-identity.txt` | 5,554 | `ce74eb013dfd57e5717570e84435b75d256efbafae111b87680a9e23f69cde21` |
| `EFI/BOOT/BOOTX64.EFI` | 56,832 | `1ebb48ff3ff7534e10fc0877f90c764be57df7f7a1b1b9da30f910da8e1afe79` |
| `kernel.elf` | 2,805,584 | `4d416905e36e77ae17314cb39d6805c4dcef3d01834a0aab00a6985f3d49369a` |
| `kernel-symbol-audit.txt` | 7,972 | `e88a8e036ab34806885df786fae40123804c284a2eb30ce483b68bc1be4c83a0` |
| `ramdisk.img` | 67,108,864 | `ead5105aa131dee575e0021fac1650a28330df422bd211ca6baccdd267179f05` |

## Provenance chain and recorded inputs

The candidate manifest reports `build.ps1 -Arch amd64 -ReleaseProfile ServerV03PublicPreviewPacManOnly` as the canonical build. `scripts/create-release-iso.ps1` sets the release build ID and invokes that build, then assembles the ESP with `tools/release_esp.py` and the ISO with `tools/release_iso.py`; it verifies the ISO before emitting the manifest and SHA sidecar. Retained command logs record the stages, package compilation commands, ESP/ISO tool output, and source paths. The ISO assembly tools and pinned runtime versions recorded in the candidate are Git 2.54.0, Windows PowerShell 5.1.26100.9818, Python 3.12.10, pyfatfs 1.1.0, fs 2.4.16, and pycdlib 1.16.0.

The build identity records kernel MinGW GCC 15.2.0, GNU ld 2.46.0.20260210, NASM 2.16.03, the kernel security-profile flag, Mbed TLS profile hash, SDK ABI header hash, and wallpaper inventory. It records the embedded ramdisk as 67,108,864 bytes with SHA-256 `ead5105aa131dee575e0021fac1650a28330df422bd211ca6baccdd267179f05`. The source profile pins Mbed TLS 4.1.0 commit `0fe989b6b514192783c469039edd325fd0989806` and TF-PSA Crypto 1.1.0 commit `29160dd877d29658279fd683b2ae57b320ddcf09`; the tracked patch series and selected source/overlay hashes are in `guidexos/mbedtls_profile.json`.

PacMan build evidence records the exported pinned source revision `8fc2aedd603ddc57e492bdc02c99442bb165109b`, source archive SHA-256 `cf400c32709881f7f782ccd0ac9881dc852933e4518c30ee718aa31a833ff314`, seven source hashes, object hashes, compiler flags, and the resulting ELF hash. It also records repository HEAD `5d6133bfd206ab44d3d369a95b98ee004523936d`; the build log says the pinned revision was exported, so the current repository HEAD was not the revision compiled.

### Provenance limitations

- The build identity has a compiler path and version for the MinGW kernel toolchain, but it does not hash the compiler/linker executables.
- The retained bootloader log says it built with Visual Studio and names `cl.exe` warnings, but the exact Visual Studio/MSVC, MSBuild, and Windows SDK versions and binary identities are not captured in the build receipt.
- PacMan evidence records `clang++.exe` and `ld.lld.exe` paths, source/object hashes, and flags, but not their version strings or executable hashes.
- The candidate ties the source commit, clean-worktree assertion, release profile, generated ramdisk, package hashes, ESP image, and ISO bytes together. It is not an independently reproduced build, and host-local tool installations/environment are not fully attested.
- Filesystem creation and modification timestamps are useful chronology clues, not authenticated process logs. The timestamps below are the observed NTFS UTC values and may not prove actor identity.

If stronger provenance is a release gate, the smallest meaningful follow-up is a fresh, isolated checkout of the recorded commit with pinned/archived dependencies and a new output directory. Capture exact compiler/linker/SDK versions and binary hashes, actual commands, environment inputs, generated-source/resource hashes, and process-level staging operations. Compare component hashes and extracted ramdisk contents under the owner-approved reproducibility standard. Do not rebuild the accepted ISO just to normalize the unrelated backup sector. If any changed output is selected as a release candidate, assign it a new identity and qualify that exact ISO.

## Ramdisk discrepancy: byte-level result

| Artifact | Size | SHA-256 | Observed UTC creation / modification | Role |
|---|---:|---|---|---|
| `out/build-staging/7f9173c1-550b-4dfb-84e4-b44e1e1c260a/ramdisk.img` | 67,108,864 | `ead5105aa131dee575e0021fac1650a28330df422bd211ca6baccdd267179f05` | created `2026-11-08T22:46:46.3275211Z`; modified `2026-11-08T22:46:52.3489560Z` | Build intermediate; matches receipt and ISO-embedded file. |
| `out/release-profile/7f9173c1-550b-4dfb-84e4-b44e1e1c260a/ESP/ramdisk.img` | 67,108,864 | `24e2a7aba0ab7e37e2f0c92033d04da2b6766ebfa7bbb6e41eb2d60773f19e40` | created `2026-11-08T22:46:52.4495105Z`; modified `2026-11-08T22:47:31.6481568Z` | Adjacent ESP copy; differs from the accepted candidate after its packaging time; non-authoritative. |
| `out/release-profile/7f9173c1-550b-4dfb-84e4-b44e1e1c260a/ESP/build-identity.txt` | 5,554 | `ce74eb013dfd57e5717570e84435b75d256efbafae111b87680a9e23f69cde21` | created and modified `2026-11-08T22:46:52.7110733Z` | Build receipt embedded in the candidate; records the accepted ramdisk hash. |
| Ramdisk extracted from candidate ISO EFI image | 67,108,864 | `ead5105aa131dee575e0021fac1650a28330df422bd211ca6baccdd267179f05` | containing ISO created `2026-11-08T22:46:57.4034674Z`; modified `2026-11-08T22:47:00.0888139Z` | Authoritative embedded release content; hash matches build identity. |
| Candidate manifest | 15,447 | `3949f9d3ff14123a532416df8d98b78ba5c3949c1a55036a3449dc326ad87d83` | created `2026-11-08T22:47:02.6702250Z`; modified `2026-11-08T22:47:02.6712245Z` | Records ISO identity and exact 16 ESP file identities. |

The raw ramdisk comparison found 48 changed bytes in 12 contiguous ranges, all within byte offsets 3072–3583 (FAT32 reserved sector 6). The primary VBR in sector 0 is identical. Sector 6 is the FAT32 backup boot sector: it is zero in build staging and in the ISO-embedded copy, while the adjacent ESP contains a redundant copy of the primary boot sector. The 96 paths inside the three filesystems have the same file sizes, content hashes, file timestamps, and directory metadata. The difference is therefore in raw FAT32 image structure, not generated application/resource payloads or ordinary filesystem directory entries.

The candidate and build receipt were written before the adjacent ESP's recorded modification time. The accepted ISO and its S22R18 QAC evidence are read-only and contain the receipt-matching ramdisk. No retained log identifies which process populated the adjacent backup sector. The precise byte-level effect is demonstrated; the modifying actor or operation remains unknown.

## Qualification boundary and next step

The two positive S22R18 QAC results and negative changed-resource rejection result remain evidence for the exact candidate/diagnostic hashes recorded in [S22R18 Disposable VM Qualification](RELEASE_S22R18_DISPOSABLE_VM_QUALIFICATION.md). They are not evidence for a future ISO. The established public-preview threat limits remain: PacMan has kernel privilege, there is no process address-space isolation, the guest kernel/hypervisor remains trusted, and package signature verification does not establish that software is vulnerability-free.

The adjacent backup-sector operation is sufficiently explained to show that the ISO payload is not inconsistent, but its process-level origin and portions of the compiler environment remain unrecorded. S22R20 therefore recommends **Outcome B**, a bounded provenance/authentication gap. The owner may decide whether the current evidence is enough for that component of the provenance gate. S22R21 should either document that disposition or run the isolated, logged reproduction described above; it should not overwrite or repair any S22R18 artifact.
