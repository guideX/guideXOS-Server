# Server S22R9 — PacMan-Only Release Profile

Date: 2026-10-20

## Outcome

**Outcome A — PACMAN_RELEASE_PROFILE_QUALIFIED (offline composition).** The canonical AMD64 build and ISO packaging completed with the explicit `ServerV03PacManOnly` profile. The generated bootable UEFI ISO and its ESP inventory contain PacMan as the only Native ELF package. Hosted profile and package checks passed.

Guest acceptance remains pending and is reported separately below. This result does not authorize RC publication, accept native execution risk, or establish application isolation.

## 1. Starting provenance and repository state

- Repository: `D:\dev\guideXOSServer`
- Starting branch and full HEAD: `main`, `43f4725a6d7e9ae9949edccddc055c595cd93cb5`
- Starting relation to `origin/main`: 1 ahead, 0 behind.
- Required checkpoint and branch matched. Tracked worktree was clean at start; 854 paths were already untracked.
- D: had 14.04 GiB free before building. No clean build, branch change, worktree creation, history rewrite, stash, push, or publication-marker change was performed.
- No S22R9 `.phase` ownership file was found in the repository preflight. QAC exclusivity was not established; later process inspection found active QAC and QEMU processes, which were left untouched. The build used a new per-build directory and left the original `ESP` tree in place.

The builds added generated PacMan build scratch and candidate artifacts. After generation, Git reported 2,653 untracked paths, including the 854 pre-existing paths and newly generated outputs. No cleanup was run.

## 2. Profile design and explicit selection

The canonical build entry point remains `build.ps1`. Both `build.ps1` and `scripts/create-release-iso.ps1` accept `-ReleaseProfile` with two values:

- `Development` remains the default and retains the existing PacMan plus Developer Studio package selection and signed-off Studio identity gate.
- `ServerV03PacManOnly` selects PacMan alone for the Server 0.3.0 AMD64 image.

The release invocation used was:

```powershell
.\scripts\create-release-iso.ps1 -Version 0.3.0 -Arch amd64 -ReleaseProfile ServerV03PacManOnly
```

This profile uses a unique `out\release-profile\<build-id>` staging root for the ESP, PacMan build output, and candidate artifacts. It does not read the original ESP's package tree or Developer Studio source. The PacMan-only ISO path requires a fresh canonical build and rejects `-SkipBuild`. The same profile contract drives ESP staging, the wallpaper runtime packer, build receipt validation, and ISO package inventory. The build log and `build-identity.txt` record the profile and selected package names.

The package contract still checks the v0.3.0 target, AMD64, exactly one manifest entry at `bin/amd64/pacman.elf`, `gx_main`, `guidexos-c-abi-v1`, and `native-elf`. Build and ISO validation fail on a missing required package, any unselected package directory, a profile mismatch, a stale build ID, or artifact hash mismatch. No global validation was disabled.

## 3. Developer Studio preservation

The normal `Development` profile remains the default and still names PacMan and Developer Studio. Its existing exact Studio package gate is retained. The current source package remains outside that signed-off pair and declares a 0.5.0 minimum; this phase neither updates its pin nor changes its source, targets, documentation, tests, or original ESP copy.

Observed package identities, unchanged by this phase:

| Location | File | Bytes | SHA-256 |
|---|---|---:|---|
| `Apps/DeveloperStudio` | `app.json` | 2,011 | `AB1DB7C63A3DC49C06098B7A9341C33DBCF6FECBD8163460F62BCF907E7488E9` |
| `Apps/DeveloperStudio` | `bin/amd64/developerstudio.elf` | 1,104,652 | `F07270EACBD62F9AB67F1F38856A326981280E43D0A4D44F8ED1D17C777F01A6` |
| Original `ESP/Apps/DeveloperStudio` | `app.json` | 1,176 | `7AF95D35F068C44F4529420922E0F4BEC5F705568514688D160E2790022ACB09` |
| Original `ESP/Apps/DeveloperStudio` | `bin/amd64/developerstudio.elf` | 1,015,064 | `5343F5DF0EDFE423542348C0AF8C9F8690CE339D197DEB4737DD05A27348CAC7` |

The release profile's build identity has `stage.developerStudio=not_selected` and contains no Developer Studio artifact records. Developer Studio is absent from the candidate ESP and from the profile's runtime package staging. The current source package's incompatibility remains documented; preservation of the development workflow does not qualify that package for Server 0.3.

## 4. Exact Native ELF ship-set

The candidate build ID was `95cf3e2d-778b-4b31-8948-4acdb7cc6aaa`. Both the generated bootable ESP and its wallpaper runtime package staging have exactly one Native ELF package directory: `PacMan`. Settings, Navigator, and the other built-in applications remain compiled and registered as kernel-integrated applications; they are not Native ELF packages selected through `ESP/Apps`. The kernel build completed and its symbol audit passed. Guest launch and UI availability were not tested in this phase.

PacMan manifest requirements observed in the generated package: minimum Server `0.1.0`, supported architecture `amd64`, entry `bin/amd64/pacman.elf`, ABI `guidexos-c-abi-v1`, runtime `native-elf`.

The generated package has 11 files:

| Package file | Bytes | SHA-256 |
|---|---:|---|
| `app.json` | 956 | `3f3592e6c76053e323f16d48073e98c67d7f5702b8e47f0b507d6bfb3d126449` |
| `bin/amd64/pacman.elf` | 63,640 | `eeb544bbb2e2574eeaed7bb7f0bad61ea24f6945231b7757223a225b30b8e1cc` |
| `resources/audio/EatPill.wav` | 7,156 | `510a52e5776573197aef14d77e393185b6c7148d1b59dac535d45c5ca0498686` |
| `resources/audio/extralife.wav` | 21,248 | `767db1552130e676398c121ec605f90f20c89bbbab272305bc781adc8e91d1cd` |
| `resources/audio/fruiteat.wav` | 5,120 | `82c96a997ac2e837de4e418f29f4f4fa6142dd2a0c38f764b6d989d8bd98605d` |
| `resources/audio/ghosteat.wav` | 9,090 | `225168072b38671c89c1fc775d513ca86fbb899e3e82c8032445f6a6dc5e1b95` |
| `resources/audio/killed.wav` | 17,008 | `1c3e91df8e19041ea5783185c5a8a9f2321993b43481c215d8caf3df590290de` |
| `resources/audio/StartMusic.wav` | 68,318 | `062cdc7196c54033f8bf11878d6659fd5b0ba11cd6945640ec8fac4376919421` |
| `resources/audio/startmusicold.wav` | 46,592 | `a6e1c14f6cbc59cb5244c0119c728d412252370eea48bc574c1307a774c83caf` |
| `resources/level1.gximg` | 888,860 | `535b37e66d7159078df8a16ce059a6f7ab038588cc9875e152033d046ff47fe1` |
| `resources/pacpics.gximg` | 360,476 | `57a605df8f24ac9641ff6c9d39df551a17ffbcd68466a561adf5f045de259d50` |

## 5. Packaging negative tests

`scripts/test-release-build-integrity.ps1`: **PASS, 33 assertions**. Coverage includes default Development selection, explicit PacMan-only selection, missing PacMan, the current incompatible Studio package in the excluded profile, an unexpected package directory, too-new Server minimum, unsupported architecture, receipt profile mismatch, wrong package receipt paths, the explicit not-selected Studio stage, and a PacMan-only receipt with no Studio artifacts.

`scripts/test-release-iso-packaging.ps1`: **PASS** for its synthetic FAT package and read-only verification. Its dummy files are test fixtures and are not treated as release image evidence.

PowerShell parser checks passed for the modified build, ISO, integrity module, and test scripts. `git diff --check` passed. The generated candidate's build identity was revalidated against the stricter profile receipt checks after packaging. Package compatibility and unexpected-package checks remain enabled at both build and ISO packaging gates.

## 6. Canonical build and artifact results

The canonical AMD64 build completed with these stages passed: bootloader, kernel, symbol audit, ramdisk, PacMan, and App Model packages. Developer Studio was explicitly `not_selected`. PacMan's pinned source revision was `8fc2aedd603ddc57e492bdc02c99442bb165109b`. The kernel symbol audit reported 45 undefined rows, 21 distinct `.symtab` entries, and no relocations/imports.

The bootable ISO was structurally created and verified by the pinned `pyfatfs`, `fs`, and `pycdlib` tooling. Verification reported a UEFI boot record with platform ID `0xEF`, `bootable=true`, and a 90,245,120-byte ISO. The release manifest has `candidateOnly=true` and names `PacMan` as the only Native ELF package.

| Artifact | Path | Bytes | SHA-256 |
|---|---|---:|---|
| Candidate ISO | `out/release-profile/95cf3e2d-778b-4b31-8948-4acdb7cc6aaa/artifacts/guideXOS-Server-v0.3.0-amd64-pacman-only-candidate.iso` | 90,245,120 | `e98bf087efc071ddf82db4dce9d79335aacbf38310de43b032744165792d712b` |
| Candidate manifest | same directory, `guideXOS-Server-v0.3.0-amd64-pacman-only-candidate.manifest.json` | 213,724 | `1b1d31d3fa6ccbe1c5ace6044bf37ca916016a1f03d6523948a071dfc7e6cb18` |
| ISO SHA-256 receipt | same directory, `guideXOS-Server-v0.3.0-amd64-pacman-only-candidate.iso.sha256` | 100 | records the ISO SHA-256 above |
| UEFI FAT boot image inside ISO | recorded as `efiImage` in candidate manifest | 90,177,536 | `5efaf5b6a352a50cfeccff04b039be850d0e341bffdc43c41ef9fd36e73ef111` |
| Kernel in candidate ESP | `kernel.elf` | 2,771,408 | `533997c618dc6c3dcdd0d6f6603a8eebbb2db726bdc01d2ff04d8657d310e90e` |
| Bootloader in candidate ESP | `EFI/BOOT/BOOTX64.EFI` | 56,832 | `16b0e1830e7c4a13c992f31baa007b405df557eb8d9f95f56fc8586d946656ac` |
| Runtime filesystem image | `ramdisk.img` | 67,108,864 | `ead5105aa131dee575e0021fac1650a28330df422bd211ca6baccdd267179f05` |
| Kernel symbol audit | `kernel-symbol-audit.txt` | 7,972 | `9416a180b94e61ecdf94d7e3f55d2cec9211bec540d48f04dd7697fcc4853c8a` |

The checksum was re-read and matched the ISO. The candidate is under `out`, not `dist`; no RC or readiness marker was changed. Existing kernel code emitted compiler warnings, including a `desktop.cpp` fixed-buffer warning; the AMD64 kernel build and symbol audit still completed. No kernel application or runtime security code was changed in S22R9.

## 7. Guest acceptance

No guest was launched by this phase. During final inspection, QAC CLI, coordinator, GUI, and a QEMU process were active; an exclusive reservation for this task could not be established. They were left untouched. Preserve the candidate above and keep desktop, Start menu, built-in app interaction, PacMan launch/input, and Studio absence from the normal app surface as **pending guest acceptance**. No guest success is claimed.

## 8. Runtime and security limitations

S22R5 service-portability findings and S22R6's trust-boundary reassessment remain in force. S22R7 found that the v0.3 and v0.5 host tables have incompatible layouts while sharing a visible ABI label and that runtime package identity is not authenticated. S22R8 classified the unresolved release set as `RELEASE_SHIPSET_GAP` and documented that the production loader discovers packages from `/Apps` after filesystem mount.

Current production runtime behavior does not impose this release profile as a post-installation package boundary. Its Native ELF loader scans immediate `/Apps` directories through VFS, trusts self-declared manifest fields for compatibility screening, does not enforce the minimum Server version or authenticate/bind the manifest to the ELF, and invokes native code with kernel privilege. Normal boot can expose a writable ESP at `/`; a package copied there can be discovered on a later boot. Read-only ISO media does not protect a separate writable ESP or persistent volume. Kernel-privileged native code can bypass callback checks directly.

Therefore PacMan-only is a trusted first-party **build composition** statement. It does not provide a sandbox, trusted package identity, post-installation integrity, or protection from later native package additions. No cosmetic allowlist or callback check was added or represented as a security boundary.

## 9. Release-owner decisions still required

| Decision area | Owner decision required |
|---|---|
| Trusted first-party release composition | Approve or reject PacMan as the only first-party Native ELF in the Server 0.3.0 image. |
| Runtime package compatibility | Decide which package/runtime compatibility guarantees are required when the production loader ignores the manifest's minimum version and lacks ABI negotiation. |
| Post-installation native package execution | Decide whether packages added later to writable persistent storage may execute with kernel privilege, or whether release operation must prevent such additions/loading. |
| Untrusted native application isolation | State whether Server v0.3 is intended to run untrusted/third-party Native ELF apps. If yes, require a separately designed and qualified privilege/address-space boundary before release. |

No decision above is accepted on the owner's behalf by this report.

## 10. Recommended next Main Integration phase

**S22R10: PacMan Candidate Guest Acceptance and Release-Owner Disposition.** Obtain exclusive QAC access, run the focused boot/desktop/built-in-app/PacMan smoke against this exact candidate, record evidence, and obtain the four owner decisions above. If untrusted Native ELF applications are required, stop release qualification and schedule the separate isolation architecture work before any publication decision.
