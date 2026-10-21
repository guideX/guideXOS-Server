# S22R8 Server v0.3 Native Application Ship-Set Qualification

Date: 2026-10-20

## Outcome

**Outcome B — RELEASE_SHIPSET_GAP.** PacMan is the only currently staged Native ELF package whose manifest is compatible with the Server 0.3.0 target. The canonical build and ISO workflow still require Developer Studio, while the checked-in source package is a different, newer package and declares a 0.5.0 minimum. The existing ESP copy also fails the signed-off manifest pin. Therefore the current workflow cannot produce a qualified v0.3 release image.

S22R8 adds packaging checks for target version, architecture, and unexpected ESP package directories. These checks close the stale-package copy path described below; they do not remove Developer Studio from the current required ship-set and do not change release readiness or publication state. S22R7's runtime ABI and isolation findings remain in force.

## 1. Preflight and repository safety

| Check | Result |
|---|---|
| Repository | `D:\dev\guideXOSServer` |
| Branch | `main` |
| Starting HEAD | `d7b7673d6b3346faf48cf44ffab9bbcb876a8eb2` |
| Prior report | `docs/RELEASE_S22R7_NATIVE_ABI_RELEASE_BOUNDARY.md`, present at the expected checkpoint |
| Tracked/staged changes at start | None |
| Pre-existing untracked paths | 854; still 854 after validation |

The required checkpoint matched exactly. No existing untracked paths were edited, removed, or staged. No release output, readiness marker, or publication state was changed. No branch operation, push, kernel/runtime change, or guest run occurred.

## 2. Actual Native ELF package selection

The canonical release build's `Stage-AppModelPackages` package list is explicit: `Apps/PacMan` and `Apps/DeveloperStudio`. Both are required build stages. `Build-WallpaperRuntimeImage` copies those two ESP package trees into the boot runtime image, and `create-release-iso.ps1` requires Developer Studio's manifest and ELF in the ESP inventory. That is the configured ship-set; repository presence by itself does not add an app to the release.

| Application | Release disposition | Provenance, package requirements, architecture, and ABI |
|---|---|---|
| **Nexgen PacMan** | Configured for staging; eligible for v0.3. This is the only selected package whose current manifest passes the v0.3 version and AMD64 package contract. | `scripts/build-pacman-package.ps1` exports the pinned upstream source revision `8fc2aedd603ddc57e492bdc02c99442bb165109b`, compiles the registered sources with Clang for `x86_64-unknown-elf` as freestanding C++11 with exceptions, RTTI, and stack protector disabled, then links a static ELF with LLD and entry `gx_main`. The script checks the ELF header, entry symbol, and absence of unresolved symbols. Its package contains `app.json`, an AMD64 ELF, level/sprite resources, and seven audio files. Manifest: minimum `0.1.0`, `amd64`, `NativeElf`, `native-elf`, `gx_main`, and `guidexos-c-abi-v1`. The source uses host callbacks for logging, window creation, event polling, ticks, frame presentation, resource reads, and audio; `request_window_ex` and `exit` are optional. The manifest's `file.read` permission is unsigned package metadata, not a security boundary. |
| **guideXOS Developer Studio** | Configured by the current build scripts but incompatible and not qualified for v0.3. The official build rejects it. | `Apps/DeveloperStudio/app.json` declares minimum Server `0.5.0`, AMD64, `NativeElf`, `gx_main`, and the shared `guidexos-c-abi-v1` label. The current source manifest is 2,011 bytes and its ELF is 1,104,652 bytes; neither matches the Phase 25 source pin (1,112-byte manifest and 1,015,064-byte ELF). The ESP copy's ELF matches the Phase 25 ELF hash and size, but its 1,176-byte manifest does not match the signed-off manifest hash/size and also declares `0.5.0`. Thus the current source pair and existing ESP pair are not the pinned pair the release receipt expects. Even if the old binary is used, S22R7 measured a 456-byte v0.5 host table against the 256-byte v0.3 table, with incompatible callback offsets. Its minimum and ABI make it unsuitable for v0.3. The Server checkout contains package artifacts but not a reproducible source build for the current Studio binary. |
| **PacMan PGM1 ready-input validation** | Intentionally excluded; validation fixture, not a release app. | `Apps/PacManPGM1ReadyInputValidation`; AMD64 Native ELF metadata, but its ID and binary are test-specific. It has no release package spec. |
| **AudioBeep, HelloWorld, Missile Command, Resource Viewer** | Metadata-aligned candidates, not selected or release-qualified. They are SDK samples and have no entries in the canonical release package list. | Their manifests declare minimum `0.1.0`, AMD64, `guidexos-c-abi-v1`, and `gx_main`. Their sources are under `sdk/samples/{audiobeep,helloworld,missilecommand,resourceviewer}`. AudioBeep and Missile Command use the audio host call; Resource Viewer uses file-read access. These are not treated as included merely because their manifests and ELF files are in `Apps/`. No expansion of the release set is recommended in S22R8. |
| **Settings and Navigator** | Built-in kernel applications, not Native ELF packages. | No standalone `Apps/<name>/app.json` or ELF exists for either. Their code and launch registration are part of the kernel (`kernel/core/kernel_apps.cpp` and `kernel/core/desktop.cpp`); they are delivered through the kernel image and are outside the Native ELF package allowlist. |

### Effective inclusion status

The configured build attempts to include PacMan and Developer Studio, but the canonical build checks the Developer Studio source identity before output cleanup and fails when the source pair differs from the Phase 25 pin. If it got past that comparison, its declared 0.5.0 minimum would still fail the 0.3.0 target check. The existing ESP copy is likewise rejected by the signed-off manifest receipt check. There is consequently no current successful canonical v0.3 artifact in which Developer Studio is a qualified included app.

## 3. Packaging enforcement and alternate paths

Before S22R8, `Stage-AppModelPackages` recreated the two selected package directories but did not reject other pre-existing directories under `ESP/Apps`. The ISO builder recursively inventoried the entire ESP and generated its expected file list from that inventory; the FAT helper then copied all inventoried files. Therefore a stale or manually copied third package under `ESP/Apps` could enter the release image without being one of the two package specs. The wallpaper runtime image copied only the two named package roots, but the ESP-root boot path exposed the full ESP `/Apps` tree to the kernel, so that did not close the alternate path.

S22R8 adds these checks in `scripts/release-build-integrity.psm1` and invokes them from `build.ps1` and `scripts/create-release-iso.ps1`:

- Each selected package must declare a parseable minimum Server version no newer than 0.3.0.
- The manifest must declare only the target architecture (`amd64`) and exactly one matching Native ELF entry with the selected executable path, `gx_main`, `guidexos-c-abi-v1`, and `native-elf` runtime.
- The selected ELF must have an ELF64, little-endian AMD64 header.
- `ESP/Apps` must contain only the currently configured package directories. This check also runs on the final ISO path, including `-SkipBuild`, so stale extra package directories fail before packaging.

The checks use the manifest as compatibility input and directory names as build selectors only. They do not authenticate an app, turn an ID or permission into a security principal, or bind an unsigned manifest to the executed bytes at runtime. The separate receipt/hash checks provide build artifact integrity for the files they cover, not hostile-app containment.

The current Developer Studio source package is rejected first by the existing package hash/size pin; an independent regression also passes the unmodified current `Apps/DeveloperStudio/app.json` through the compatibility check and verifies its 0.5.0 minimum is rejected. The architecture regression uses an arm64-only declaration. The new ship-set regression accepts a synthetic PacMan-only selection and rejects an extra unlisted directory. This tests the policy helper; it does not change the official current selection, which still requires Developer Studio.

## 4. Runtime discovery, execution, and exposure

The production loader is `kernel/core/native_elf_baremetal.cpp`, not the separate hosted executor. After persistent storage is mounted, `kernel/core/main.cpp` calls `native_elf::discover()`. The loader scans immediate directories under `/Apps`, reads `app.json` through VFS, and accepts packages whose extracted fields say `NativeElf`, `native-elf`, AMD64, `gx_main`, and `guidexos-c-abi-v1`, with a regular executable at the manifest-relative path. Discovered packages are added to the Start Menu and resolved by display name, manifest ID, or directory label. Discovery is cached for that boot; a package copied after discovery is not registered until a later boot.

The loader does **not** read `minGuideXOSVersion`, validate the top-level `supportedArchitectures` array, authenticate the manifest, bind manifest data to an ELF digest, or negotiate an app-required ABI size/service set. Its string extraction is not structural JSON parsing. It validates an AMD64 ELF subset, allocates an image and a separate app stack, and calls the entry point directly from kernel code. It does not switch to user privilege or a per-app address space; the separate stack is not isolation. S22R7 also documented that the loader's entry validation does not prove the entry lies in an executable segment.

The v0.3 runtime advertises `GX_API_VERSION == 0` and a 256-byte host table. `guidexos-c-abi-v1` is shared with the newer Developer Studio binary even though S22R7 measured its table at 456 bytes and found incompatible callback placement. A v0.5 app can therefore pass the runtime's visible name/architecture checks if staged under `/Apps`, ignore its higher minimum, and call through the wrong slot or read beyond the v0.3 table. This is a compatibility hazard with possible kernel failure, not an enforcement boundary.

Normal bare-metal boot mounts a writable ESP at `/`; the built-in Files workflow and VFS support file writes/copy operations. The release ISO's boot path instead uses a read-only runtime image when there is no writable persistent volume. There is no dedicated package installer identified, but an operator can add a package directory to a writable ESP after the release image was created. On the next boot, the directory is scanned, can appear in the Start Menu, and can launch if its self-declared fields and ELF satisfy the loader's checks. Because Native ELF code executes in kernel privilege, code already running as a Native ELF can also bypass host callbacks and directly affect kernel memory and services. This is not an endorsed untrusted-app workflow; it is evidence that release-time package selection does not constrain later runtime additions.

### Consequence classification

- **Official release artifact safety:** Package allowlisting and receipt checks can define which files the official build composes. S22R8 now rejects extra `ESP/Apps` package directories and incompatible declared requirements at those build gates.
- **Runtime compatibility:** The loader ignores minimum Server version and has no negotiated ABI-layout contract. A compatible-looking manifest can still describe a binary that is not compatible.
- **Trusted first-party operation:** A v0.3 image using only a qualified first-party PacMan package is a possible trusted-code configuration after the release composition is changed and the release owner accepts that Native ELF code shares kernel privilege.
- **Untrusted native application security:** Not provided. Package allowlisting does not sandbox Native ELF code, and writable/offline ESP additions can be discovered on a later boot. Any untrusted package can corrupt or compromise the kernel.

S22R7's architectural concern remains unchanged. The untrusted-native-code exposure is an Outcome C blocker if Server v0.3 is intended to execute untrusted native apps. It is not silently reclassified as safe here. S22R8's overall result is Outcome B because the present canonical release composition itself is not buildable for the 0.3.0 target; a bounded release-set change could make a trusted-first-party artifact reviewable without claiming native isolation.

## 5. Qualification decision and required next step

**Do not qualify the current composition for RC.** The build currently requires the incompatible Developer Studio package in the staging list, required stages, boot runtime image, and ISO receipt. Removing it is a bounded packaging/configuration change, but it changes the required-stage and receipt contracts and therefore needs its own reviewed release-set update. This phase intentionally does not change those readiness criteria or fabricate a successful candidate.

Before RC qualification, the next Main Integration step should:

1. Define PacMan as the only selected Native ELF package for the trusted-first-party v0.3 profile, or provide a separately qualified v0.3-compatible Studio package. Remove deferred Studio from every build, ramdisk, and receipt requirement only through a reviewed ship-set change.
2. Build from a clean, reviewed source state and verify the exact ESP and ISO package contents against the allowlist and hashes. The current checkout's 854 pre-existing untracked paths prevent treating this worktree as a clean release source.
3. Obtain explicit release-owner approval for the trust model: every Native ELF is trusted with kernel privilege, and user-added Native ELF packages are not isolated. If hostile or third-party apps must be supported, complete and qualify the separate user-mode/address-space/syscall architecture before release.
4. Run the required guest acceptance only under the exclusive QAC reservation protocol. S22R8 produced hosted evidence only.

An independent small follow-up could add a runtime minimum-version check after the product version source and manifest parser are made canonical. That alone would not fix the shared ABI identity/layout ambiguity, authenticate packages, or contain malicious code, so it is not a substitute for the S22R7 architecture work.

## 6. Validation

### Hosted checks

- `scripts/test-release-build-integrity.ps1`: **PASS**, 23 assertions, including the unchanged modern Developer Studio manifest, an arm64-only manifest, a qualified PacMan manifest, and an unlisted ESP package directory.
- `scripts/test-release-iso-packaging.ps1`: **PASS**, synthetic FAT packaging and read-only verification. Its dummy files do not represent a bootable release image.
- Existing `build/s22r6_native_abi_layout_test.exe`: **PASS**, `Native ABI layout test PASS`. This pre-existing executable was run without rebuilding or overwriting untracked build outputs.
- PowerShell parser checks for `build.ps1` and `scripts/create-release-iso.ps1`: **PASS**.
- `git diff --check`: **PASS**.

### Not run

- Canonical release build or ISO creation: not run. The inspected preflight deterministically rejects the current Developer Studio source pair before cleanup, and running a full build would not qualify the requested Studio-free composition.
- Kernel build, QEMU, or guest acceptance: not run. No kernel/runtime code changed; no guest claim is made.

## Recommended outcome record

Record **Outcome B — RELEASE_SHIPSET_GAP** for S22R8. The exact remaining qualification gap is that the current v0.3 packaging contract still requires a Developer Studio pair whose current source and ESP manifest do not match the signed-off pin and whose declared minimum is Server 0.5.0. The extra-package bypass found in the ESP copy path is now rejected by packaging-level assertions and regression coverage. This outcome does not authorize publication or qualify untrusted Native ELF execution.
