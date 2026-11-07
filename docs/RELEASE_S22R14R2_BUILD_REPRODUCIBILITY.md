# S22R14R2 Release Build Reproducibility and _wcsicmp Investigation

**Disposition: RELEASE_BUILD_REPRODUCIBILITY_VERIFIED**, under the content-equivalence contract below. This does not qualify either newly built ISO for guest acceptance or authorize publication.

The isolated _wcsicmp failure came from building an older detached source revision that still included the Windows header in freestanding mode. The required header guard was already present on main at the S22R14R2 starting commit. The corrected release inputs and scripts now produce complete, structurally verified PacMan-only AMD64 ISOs in two independent clean clones. The kernel ELF, PacMan package, and repeated-build ramdisk are byte-identical. Full ISO hashes differ because each build has a unique identity and the bootloader embeds build-specific PE debug metadata and source paths.

## 1. Starting state and preservation

- Repository: D:\dev\guideXOSServer
- Starting branch and HEAD: main, 7eb50340de4d63834b2e96c7a3cd7b9cf63e160f
- The tracked worktree was clean; the branch was already two commits ahead of origin/main.
- The only pre-existing untracked root file was assets/Backgrounds/troncity.png. It remains untracked and unchanged (SHA-256 444ac64211907d80f0266e2283960cefaa932803d0aee7e0ccfd414f6fae2fda).
- The accepted ISO remains at out/release-profile/6ad9bb77-12ce-4b4d-8ed0-56ff3944ee5d/artifacts/guideXOS-Server-v0.3.0-amd64-pacman-only-candidate.iso, SHA-256 2fe2f5eda87c9a80164715a156af5de12c7cf3891d29a961c40012c9d032241e. It was read and compared but not modified.
- No branch switch, worktree creation, history rewrite, reset, clean, stash, cherry-pick, or push was performed. The repository dist directory, release channels, readiness markers, and publication state were not changed.
- Build and diagnostic output was kept under out/s22r14r2-build-repro/. Earlier phase output was preserved.
- QAC was not accessed and no guest was started. An unrelated QEMU process from another checkout was left untouched.

The accepted manifest identifies source commit 5b62216efe78158fb12707c126ec38582faecf59, marks the worktree as not clean at packaging start, and records the accepted profile as ServerV03PacManOnly, candidate-only, with PacMan as the only Native ELF package and Developer Studio not selected. Its required kernel, bootloader, ramdisk, and PacMan hashes were checked against the accepted ESP.

The accepted stack-guard record remains applicable because this phase changed no kernel source, kernel compiler option, or relevant call graph, and both cold builds produced the accepted kernel ELF SHA-256 b5e029d23f9c21b5ff0838d9388322bf95b211bf3ef98145ddd344170dfe95dd.

| Path | Static usage | Estimated headroom |
|---|---:|---:|
| Settings modern | 11,512 B | 4,872 B |
| Settings legacy | 11,896 B | 4,488 B |
| COM2 | 7,768 B | 8,616 B |

Stack capacity is 16,384 bytes and the required minimum headroom is 4,096 bytes. These are static estimates, not measured runtime high-water marks.

## 2. _wcsicmp failure and cause

The preserved S22R14 failing log is out/s22r14-repro-isolated/canonical-rebuild-with-dependencies.log. Its source clone was detached at ea786e2da4da01a57b1ab00429c8453edc9446ad, not at the required main HEAD. The failure is recorded around log lines 1225–1232 and 1415–1422:

- Translation units including the problematic header include core/native_elf_baremetal.cpp and core/system_font.cpp.
- The compiler reports C:/mingw64/x86_64-w64-mingw32/include/stralign.h:155:14: error: '_wcsicmp' was not declared in this scope.
- The diagnostic points to MinGW's inline ua_wcsicmp implementation, whose body calls _wcsicmp. It is not a product call site and is not a macro-expanded product expression.
- The old root bitmap_font.h used #ifdef _WIN32 around windows.h and the hosted HDC drawing overload. MinGW defines _WIN32; the old condition therefore imported Windows headers into the GXOS_BARE_METAL kernel compile. The error occurred while compiling object files, before linking.
- The bare-metal translation unit does not require a hosted Windows wide-string API. No kernel product source calls _wcsicmp; no replacement string routine or runtime dependency is appropriate.

The failure is a source-revision mismatch, not evidence of a production-versus-isolated compiler-option mismatch. The failing clone predates commit 0bdb3741d78b8c7cf9fe90fd413e19d23dfcfe9d, which changed both Windows-only branches to #if defined(_WIN32) && !defined(GXOS_BARE_METAL). That correction is present in the required starting main revision and remains present now. It preserves the hosted Windows behavior while excluding Windows headers and APIs from bare-metal builds.

The archived failure log records the mingw32-make wrapper and failing include stack but does not record the expanded kernel compiler command or MinGW version. The current host reports MinGW GCC 15.2.0; that current observation is not attributed retroactively to the October failure. On the current source, a focused compile of core/native_elf_baremetal.cpp using the production MinGW bare-metal flags passes. No kernel source-level workaround was added in this phase.

## 3. Build-system corrections committed locally

The four source/build-system commits made on main before this report are:

| Commit | Change |
|---|---|
| a90f622cb77bfc4a118c2749b715f1a2a37bcb41 | Added pinned host-only release dependencies and removed stale absolute EDK include paths from both bootloader project configurations. |
| 6bd6e50df72d9fa1a852b281e1bd58e6e077e61f | Made the PacMan builder explicitly load the hash utility module in its child process. |
| 2153c0bd171d3f2f7e9db66a58f69703f0b61309 | Loaded the PowerShell utility module by its absolute path under $PSHOME, fixing Get-FileHash discovery in the nested Windows PowerShell process. |
| 58658c4860dfc17d1e6b2951040d81e962b2982c | Pinned setuptools to a version that provides pkg_resources, required by the pinned fs/pyfatfs dependency chain. |

The final requirements-release.txt pins appdirs==1.4.4, fs==2.4.16, pycdlib==1.16.0, pyfatfs==1.1.0, setuptools==75.8.0, and six==1.17.0. A prior bootstrap attempt with setuptools 84.2.1 failed when pyfatfs imported pkg_resources; the final pin was installed and used in both complete cold builds.

These changes are host build configuration and artifact tooling. They add no kernel CRT dependency, do not alter the kernel ABI, and do not change kernel compile options.

## 4. Build inputs and provenance

Builds 3 and 4 used separate source clones at the same clean source commit and separate fresh package/dependency inputs. Before each build, the kernel build output and bootloader Release output were absent in that clone. Neither build consumed the previous build's generated artifacts.

| Input class | Recorded input |
|---|---|
| Server source | Clean main at 58658c4860dfc17d1e6b2951040d81e962b2982c in each clone |
| Release profile | ServerV03PacManOnly; canonical entry point build.ps1 -Arch amd64 -ReleaseProfile ServerV03PacManOnly -BootstrapTools -RequireCleanWorktree |
| PacMan source | Pinned detached repository revision 8fc2aedd603ddc57e492bdc02c99442bb165109b |
| Mbed TLS | 4.1.0, commit 0fe989b6b514192783c469039edd325fd0989806, reconstructed from the tracked lock/profile |
| TF-PSA-Crypto | 1.1.0, commit 29160dd877d29658279fd683b2ae57b320ddcf09 |
| Mbed TLS local patches | Three tracked patches; profile verification passed in each clone |
| Python release packages | Exact pins in requirements-release.txt above |
| C/C++ tools | MinGW GCC 15.2.0; GNU Make 4.4.1; GNU objcopy / Binutils 2.46.0.20260210; Clang 22.1.8; LLD 22.1.8 |
| Bootloader toolchain | Visual Studio Community 2026 version 18.11.12328.437; MSBuild 18.11.1.51303; PE linker reports version 14.44 |
| Host runtimes | Outer PowerShell 7.6.9; nested Windows PowerShell 5.1.26100.9818; Git 2.54.0.windows.1; Python 3.12.10 |
| Build environment | EXTRA_CFLAGS empty; no Developer Studio selection; PacMan source root points to the physical isolated clone |

A temporary X: drive alias was used only to keep deep Mbed TLS submodule paths within Windows path limits. It mapped into the phase output directory and was not a source input. The bootloader embeds the resolved source/PDB path in PE debug metadata; the alias and per-build clone path therefore appear in the generated bootloader metadata and are recorded as a source of non-identical bytes below.

The canonical wallpaper generator uses a fixed 15-name list and the tracked large-image fixture under assets/Images/BlueVelvet/16/image.png. The root assets/Backgrounds/troncity.png is not in that list, is not present in either clean clone, and does not appear in the accepted or rebuilt ramdisk file inventory. It was preserved without copying it into the build inputs.

The manifests record Git, PowerShell, Python, Python package versions, ISO backend, and canonical build command. Compiler/SDK versions above were captured separately during this phase because the existing release manifest does not record all of them. The old October failing log also lacks its expanded kernel command and MinGW version; this is a historical provenance limit, not a missing input in builds 3 and 4.

The accepted manifest's inputFiles records PacMan's complete selected payload. The same paths and hashes were present in both cold builds:

| Input path | Bytes | SHA-256 |
|---|---:|---|
| Apps/PacMan/app.json | 956 | 3f3592e6c76053e323f16d48073e98c67d7f5702b8e47f0b507d6bfb3d126449 |
| Apps/PacMan/bin/amd64/pacman.elf | 63,640 | eeb544bbb2e2574eeaed7bb7f0bad61ea24f6945231b7757223a225b30b8e1cc |
| Apps/PacMan/resources/audio/EatPill.wav | 7,156 | 510a52e5776573197aef14d77e393185b6c7148d1b59dac535d45c5ca0498686 |
| Apps/PacMan/resources/audio/extralife.wav | 21,248 | 767db1552130e676398c121ec605f90f20c89bbbab272305bc781adc8e91d1cd |
| Apps/PacMan/resources/audio/fruiteat.wav | 5,120 | 82c96a997ac2e837de4e418f29f4f4fa6142dd2a0c38f764b6d989d8bd98605d |
| Apps/PacMan/resources/audio/ghosteat.wav | 9,090 | 225168072b38671c89c1fc775d513ca86fbb899e3e82c8032445f6a6dc5e1b95 |
| Apps/PacMan/resources/audio/killed.wav | 17,008 | 1c3e91df8e19041ea5783185c5a8a9f2321993b43481c215d8caf3df590290de |
| Apps/PacMan/resources/audio/StartMusic.wav | 68,318 | 062cdc7196c54033f8bf11878d6659fd5b0ba11cd6945640ec8fac4376919421 |
| Apps/PacMan/resources/audio/startmusicold.wav | 46,592 | a6e1c14f6cbc59cb5244c0119c728d412252370eea48bc574c1307a774c83caf |
| Apps/PacMan/resources/level1.gximg | 888,860 | 535b37e66d7159078df8a16ce059a6f7ab038588cc9875e152033d046ff47fe1 |
| Apps/PacMan/resources/pacpics.gximg | 360,476 | 57a605df8f24ac9641ff6c9d39df551a17ffbcd68466a561adf5f045de259d50 |

The kernel, bootloader, and ramdisk are recorded separately as required ESP artifacts in each release manifest.

## 5. Kernel, symbol, stack, package, and ISO validation

- The previously failing kernel translation unit compiled successfully first on the current source.
- The complete freestanding AMD64 kernel build and symbol audit passed in both cold builds.
- Both shipped kernel ELFs are 2,795,344 bytes with SHA-256 b5e029d23f9c21b5ff0838d9388322bf95b211bf3ef98145ddd344170dfe95dd, matching the accepted candidate.
- The audit reports no PE imports and no COFF symbol relocations. Its 45 ELF undefined rows / 21 distinct names are classified as non-required or linker-resolved; there is no unexplained required unresolved dependency.
- PacMan-only package composition passed in the canonical build and ISO gates. Each release manifest names exactly one Native ELF package, PacMan; Developer Studio is not_selected. The ESP has 16 files and no Developer Studio package.
- Both release manifests report candidateOnly=true. Each ISO sidecar matches the ISO SHA-256.
- The ISO backend's own verification reports an EFI boot record with platform ID 0xEF, no-emulation media, a 90,177,536-byte FAT32 boot image, and bootable=true.
- scripts/test-release-iso-packaging.ps1 passed against the pinned Python tool environment. It creates and verifies a synthetic FAT32 image; the script explicitly does not claim that its dummy image is bootable.
- scripts/test-release-build-integrity.ps1 was also run in the build-3 clone and failed at its Developer Studio assertion: “DeveloperStudio manifest is not the exact Phase 25 signed-off package manifest.” The current Developer Studio manifest is 2,011 bytes with SHA-256 AB1DB7C63A3DC49C06098B7A9341C33DBCF6FECBD8163460F62BCF907E7488E9; the fixture expects the historical Phase 25 manifest of 1,112 bytes with SHA-256 D59C12267D360C4E31B70DEC7F9C1B0689FB5816011D681B38EE9CDEF0D2F53B. Developer Studio was not selected, and this phase changed no Developer Studio files. The canonical PacMan-only build's selected-package checks passed; the unrelated stale Developer Studio test expectation remains an independent cleanup item.

## 6. Independent release artifacts

Both builds came from clean source commit 58658c4860dfc17d1e6b2951040d81e962b2982c, produced distinct build IDs, and completed every selected stage.

| Build | Build ID | ISO SHA-256 | Bytes | EFI boot image SHA-256 |
|---|---|---|---:|---|
| 3 | d652cf81-91ed-4cf3-8338-9823ca11cd71 | ede2f3b2bad1463ca4c6bc4fc05f847772ad703a30e2d09d40e1deed834f8d51 | 90,245,120 | f229a5c37ff7719e37aa805f1e962a1b3c93d66031e5366bfe968e96fdae9225 |
| 4 | 8e9efdce-bcf4-4e0e-9966-aa71eade2561 | aab8b490c570fac9af6983ee4089c2a794204d34249dd8f6787441f1c623818f | 90,245,120 | 3212f4db32aead69f3a699110cd4f78bfb800359c88b0b253a37f4e944872542 |

Artifacts and manifests are under each clone's out/release-profile/<build-id>/artifacts/. The accepted ISO hash remains 2fe2f5eda87c9a80164715a156af5de12c7cf3891d29a961c40012c9d032241e; neither new ISO replaces it.

## 7. Reproducibility comparison and limits

### Build 3 versus build 4

The kernel ELF, PacMan ELF and manifest, every PacMan resource, and the raw 64 MiB ramdisk are byte-identical between the two independently initialized builds. The ESP inventory differs in exactly three files:

1. build-identity.txt, which contains a distinct build ID and start time by design.
2. EFI/BOOT/BOOTX64.EFI.
3. kernel-symbol-audit.txt, which includes the hash of a timestamped intermediate kernel.pe; the final shipped kernel ELF is identical.

PE inspection shows build 3 and build 4 have identical .text, .data, .pdata, and .rsrc sections. Their .rdata difference is PE/CodeView debug metadata: COFF/debug timestamps, checksum, PDB GUID, and embedded absolute PDB path (X:\build-3\source\... versus X:\build-4\source\...). The bootloader hashes therefore differ even though the executable code section matches. This identifies the ISO hash change between these builds as per-build identity and debug/build-path metadata, rather than unexplained kernel or PacMan output variation.

### Accepted ISO versus the cold builds

- Kernel ELF: byte-identical to the accepted candidate.
- PacMan manifest, ELF, audio, and GXIMG files: byte-identical to the accepted candidate.
- Ramdisk: the accepted raw image hash is 27fc2310f78aa38d47a2813f07cbec829dd81cfa4211f90b28e28cb572feb578; builds 3 and 4 both produce ead5105aa131dee575e0021fac1650a28330df422bd211ca6baccdd267179f05. Read-only FAT extraction found the same 96 file paths and matching size/content SHA-256 for every file. The raw FAT images are not byte-identical; the exact raw layout delta from the accepted image was not isolated further. The two cold images do reproduce byte-for-byte.
- Bootloader: the accepted hash is 70eeebc388c74ca55a0f4760e6d4a587160372787c400156b01b2e3dfed8eee2; the cold builds have different PE hashes. The accepted PE linker version is also 14.44. The accepted PE contains the original checkout PDB path; the new builds contain their isolated clone paths. The new bootloader .text hash is a4f753921ce1ac1b336112af620593a08c4306d5cd0f6224e84fb45b7cfeb32c; its .data and .rsrc hashes match the accepted PE. The accepted .pdata differs because its unwind-info RVAs reflect the accepted PE's different .rdata layout.
- The accepted manifest records a dirty worktree and earlier source commit. Therefore it is not a byte-for-byte equivalent clean input set to builds 3 and 4. Its matching extracted ramdisk contents support content equivalence only.

The reproducibility contract demonstrated here is: for a clean source revision, pinned external source revisions, pinned Python artifact dependencies, recorded host toolchain, and the PacMan-only profile, two separately initialized release builds complete; their kernel and PacMan binaries and payload are identical; their ramdisk filesystem payload is identical; their bootable ISO structure and package composition are valid. Whole-ISO byte identity is not claimed because build IDs, timestamps, PE debug identifiers, embedded source paths, and the kernel audit's intermediate-PE hash vary. The accepted-to-cold ramdisk raw byte delta is also explicitly not claimed as explained, although its extracted content matches exactly.

## 8. Guest qualification and remaining release boundaries

No new ISO was booted. The kernel ELF, PacMan payload, and accepted stack-guard identity remain unchanged, so the existing S22R14R1R5/R1R6 evidence remains applicable to those exact binaries. The bootloader PE hash is new, so the accepted guest evidence does not qualify either full new ISO. If a new ISO is selected for release review, the smallest new runtime check is an owned, focused UEFI boot through kernel and ramdisk loading to the desktop, followed by the existing PacMan launch/input smoke. The unchanged kernel means this phase does not require a fresh broad COM2 or Settings campaign solely for the build-system changes.

QAC status: not accessed; no QAC run, guest launch, or QAC reservation was performed.

The previously documented release limitations remain unresolved and outside this phase:

- /Documents functionality limitation and /.startmenu_recent persistence failure.
- Legacy Settings Display backend unavailable; user-visible functionality is absent, and separately unqualified display configuration paths remain unqualified.
- Native ELF applications run with kernel privilege and can be discovered from writable runtime storage.
- The v0.3/v0.5 host ABI tables are incompatible despite a shared visible ABI label.
- Package identity is unsigned and Native ELF process isolation is not enforceable.

No VirtIO-GPU gate or display backend was changed. PacMan-only describes build composition, not a runtime security boundary.

## 9. Recommended S22R15 scope

Keep the accepted ISO and its guest evidence as the current accepted identity. Use the two cold outputs as build-process evidence. If release review chooses one new ISO identity, first perform the focused UEFI boot and PacMan check above. Separately, update the Developer Studio integrity test fixture/contract so the general integrity test reflects the currently supported manifest, and consider a follow-on change that records compiler, linker, SDK, and source-root metadata directly in release manifests. Neither item changes the result that the canonical PacMan-only profile built and structurally verified twice.

No candidate was promoted, no release was published, and nothing was pushed.
