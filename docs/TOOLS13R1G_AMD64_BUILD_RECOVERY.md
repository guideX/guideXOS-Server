# TOOLS13R1G — AMD64 Build Recovery and Private ESP

**Outcome A — TOOLS13R1G_IMAGE_READY**

**Date:** 2026-11-11 (Pacific)

**Classification:** Experimental compatibility-test candidate; guest execution pending authorization.

The isolated AMD64 build now completes with the pinned guideXOS Mbed TLS profile. The accepted TOOLS12 Developer Studio package and TOOLS13R1 fixture are staged in a private, directory-backed FAT ESP. Hosted checks pass. No guest was launched, and this image is not certified for release.

## Repository and worktree identities

| Checkout | Identity and use |
|---|---|
| Tools evidence repository, D:\dev\guideXOS_DeveloperStudio_Tools | Branch DEVELOPER_STUDIO_TOOLS; starting HEAD 2e00734c16b0729e06501f8203eb42e687e11141; clean before this record. This file is the only tracked change for TOOLS13R1G. |
| Developer Studio source, D:\dev\guideXOS_DeveloperStudio_Tools_Source | Branch DEVELOPER_STUDIO_TOOLS; HEAD 48f2da543517bd7238378c5a18769be148b10863; clean. Accepted application and fixture inputs were read from this checkout's existing out directories. |
| Server integration candidate, D:\dev\guideXOSServerV0.5_TOOLS13R1E_INTEGRATION | Branch codex/tools13r1e-server-integration; HEAD e712106509473247eba959a494b65595769372a6; clean. No Server source changes were made. |
| Preserved TOOLS13R1F failure worktree, D:\dev\guideXOS_DeveloperStudio_Tools_Source\out\TOOLS13R1F-guest-image | Detached at e712106509473247eba959a494b65595769372a6. Its partial ESP and untracked build-failure.txt remain unchanged. The failure-log SHA-256 is D2C11F34D5E142426A396AC5EE26D7204D47B94731C265917567AF17F0A08B2D. |
| TOOLS13R1G preflight worktree, D:\dev\guideXOS_DeveloperStudio_Tools_Source\out\TOOLS13R1G-guest-image | Detached at e712106509473247eba959a494b65595769372a6. Preserved separately; a prior make dry-run created task-local object/dependency outputs here, so it was not used for the final build. |
| TOOLS13R1G final image worktree, D:\dev\guideXOS_DeveloperStudio_Tools_Source\out\TOOLS13R1G-final-guest-image | Detached at e712106509473247eba959a494b65595769372a6. Contains the private dependency/profile overlays, full build outputs, staged ESP, and test logs. Tracked Server files remain unchanged. |
| TOOLS13R1G hosted-test worktree, D:\dev\guideXOS_DeveloperStudio_Tools_Source\out\TOOLS13R1G-hosted-tests | Detached at e712106509473247eba959a494b65595769372a6. Native ELF hosted regressions ran here. The development-app-model runner rewrote its tracked temporary test executable; that generated test output is preserved in this disposable checkout. |

No existing worktree was switched, reset, stashed, cleaned, or removed. The TOOLS13R1F evidence and all generated G outputs remain available.

## Original failure and verified root cause

The preserved TOOLS13R1F command was:

    powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\build.ps1 -Arch amd64

It ran in D:\dev\guideXOS_DeveloperStudio_Tools_Source\out\TOOLS13R1F-guest-image at Server commit e712106509473247eba959a494b65595769372a6. The UEFI bootloader completed; kernel compilation stopped with:

    mingw32-make: *** No rule to make target '../third_party/mbedtls', needed by 'build/amd64/obj/core/gxos_tls_foundation.o'. Stop.
    ERROR: Kernel build failed with process exit code 2

In kernel/Makefile, the target kernel/build/amd64/obj/core/gxos_tls_foundation.o depends on MBEDTLS_GUIDEXOS_IMPORT_STATE_DEPS. That variable lists ../third_party/mbedtls and its source/include directories. The relative path is evaluated from the kernel directory, so it resolves to the Server checkout's top-level third_party/mbedtls source tree. It is a source tree containing Mbed TLS, TF-PSA-Crypto, profile headers, and source files—not a generated make target or prebuilt library.

The e712 candidate has no .gitmodules entry or dependency bootstrap step for Mbed TLS; its third_party tree contains stb only. Its build script does not initialize or locate Mbed TLS. The missing source tree was therefore caused by incomplete construction of the disposable TOOLS13R1F worktree. The Makefile path is consistent with the source manifest, and the failure was not caused by WSL path translation or a missing dependency-generation step.

## Dependency provenance and repair

The candidate's established Server build metadata was recovered read-only from D:\dev\guideXOSServer_MODERN_INPUT at HEAD a4c5d5ad58b9b0848992802438e5a58f186c110b. Its tracked lock, profile, patches, bootstrap, and verifier identify:

| Component | Pinned source |
|---|---|
| Mbed TLS | https://github.com/Mbed-TLS/mbedtls.git, tag mbedtls-4.1.0, commit 0fe989b6b514192783c469039edd325fd0989806 |
| TF-PSA-Crypto | https://github.com/Mbed-TLS/TF-PSA-Crypto.git, tag v1.1.0, commit 29160dd877d29658279fd683b2ae57b320ddcf09 |
| Mbed TLS framework submodule | commit dff9da04438d712f7647fd995bc90fadd0c0e2ce |
| TF-PSA-Crypto mldsa-native gitlink | commit 5772b4f4a0105694b1203abb582273f78fa951b7 |

The lock/profile and official scripts were introduced in Server metadata commit 8bc29f59ac9984110efe45e390ac736910dc1699. The final dependency was reconstructed with that bootstrap script, using its pinned revisions and three tracked guideXOS profile patches. It was not replaced with an arbitrary current release or another crypto library.

From the final image worktree, the install command used process-scoped Git long-path configuration:

    $env:GIT_CONFIG_COUNT = '1'
    $env:GIT_CONFIG_KEY_0 = 'core.longpaths'
    $env:GIT_CONFIG_VALUE_0 = 'true'
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File D:\dev\guideXOSServer_MODERN_INPUT\scripts\bootstrap-mbedtls.ps1 -Install -Destination D:\dev\guideXOS_DeveloperStudio_Tools_Source\out\TOOLS13R1G-final-guest-image\third_party\mbedtls -StagingRoot D:\dev\guideXOS_DeveloperStudio_Tools_Source\out\TOOLS13R1G-final-dependency-bootstrap -PythonPath python

The bootstrap completed with MBEDTLS_BOOTSTRAP_INSTALL=PASS. The installed profile verifier passed for Mbed TLS 4.1.0, TF-PSA-Crypto 1.1.0, and all three patches. The tracked crypto profile retains TLS client, SNI, X.509 certificate parsing, PSA P-256, ECDSA and RSA-PSS configuration. The kernel contains linked TLS handshake, certificate-parse, certificate-verification callback, PSA initialization, zeroization, and external random-generation symbols.

The bootstrap log records Windows permission failures while first creating nested Git metadata, then records successful checkout of the pinned mldsa-native and framework gitlinks. Their source files are present and the nested mldsa-native gitdir reports the expected pinned commit. Because the bootstrap moves the cloned root into its destination, the nested mldsa-native .git pointer still names the former staging path; consequently a later recursive submodule-status query cannot follow that pointer. The selected guideXOS source manifest and crypto configuration do not include or enable ML-DSA, and the required Mbed TLS/TF-PSA profile hashes and revisions verify. This is a nested Git-metadata relocation limitation in the private dependency copy, not a missing source used by this AMD64 build.

The final Server worktree also contains the exact guideXOS profile overlay from MODERN_INPUT. The older e712 Makefile expects the profile files under third_party/mbedtls/guidexos, so the private build copy includes the manifest and both config headers there. No production Makefile or Server source was changed.

## Full AMD64 build

The final image worktree was freshly checked out at e712106509473247eba959a494b65595769372a6 without existing kernel or bootloader build products. The wallpaper filesystem image was regenerated by the established script. GNU make was forced to rebuild all targets; no Clean or RunQemu option was used.

From D:\dev\guideXOS_DeveloperStudio_Tools_Source\out\TOOLS13R1G-final-guest-image, the build command and process environment were:

    $env:MBEDTLS_GUIDEXOS_REPO_ROOT = '..'
    $env:MAKEFLAGS = '-B'
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\build.ps1 -Arch amd64

The complete output is preserved at D:\dev\guideXOS_DeveloperStudio_Tools_Source\out\TOOLS13R1G-forced-full-amd64-build.log. The build exited 0, reported Kernel built successfully and ESP directory ready, and emitted 507 compiler warning lines with zero error lines. Warnings include existing unused parameters/functions, type-limit and packed-member diagnostics, and a Mbed TLS test-macro redefinition. No build flags were weakened. The build script found the local QEMU executable while checking prerequisites; it did not launch it.

| Output | Bytes | SHA-256 |
|---|---:|---|
| kernel/build/amd64/bin/kernel.elf and matching ESP/kernel.elf | 2,896,280 | F1DA8EB275886FC5AA8126DE1CBFB38154FBA8C3509D971A93A4F2D89FACC6B5 |
| ESP/EFI/BOOT/BOOTX64.EFI | 46,592 | D515E66BD4F59EB46171B88CCF8BF1DD27EACD62C2A9C8ACAECB0CE0D0E43998 |
| ESP/ramdisk.img | 67,108,864 | 7E93E34732526DBFFEE097912F776467B0DD03C989CBDA06F4AA24B0945A969B |

The kernel is ELF64, little-endian, ET_EXEC for AMD64. The bootloader is PE/COFF x86-64. The ramdisk is the wallpaper runtime filesystem mounted at /system/wall; the build regenerated it and its resulting bytes match the tracked candidate input.

## Developer Studio package and fixture

The accepted package was copied from the existing TOOLS12 output without rebuilding or modifying it. Its staged guest directory is ESP/Apps/DeveloperStudio (guest path /Apps/DeveloperStudio). The package audit passed with exactly three files:

| File | Bytes | SHA-256 |
|---|---:|---|
| app.json | 2,217 | 47A9C855AE32A372CC7674350A9E7255BB58927FF943798CC929F6EDFCBCF1BE |
| bin/amd64/developerstudio.elf | 1,262,228 | 9CBC32163417A553B38A1F01EF69CDBAF49B99C58B9D63D0F06E2E9A31C1AC16 |
| bin/arm64/developerstudio.elf | 1,448,964 | 9CEB966F4AD74EE70E138F7A2BA1797FCE9B50A007F3E57E74BD3C5EAD365AB7 |

The manifest audit confirmed both architecture entries, ELF64 little-endian ET_EXEC files, no section/debug metadata, and matching paths and ABI. Metadata remains application version 0.1.0, minimum guideXOS version 0.5.0, and ABI guidexos-c-abi-v1.

The four TOOLS13R1 fixture inputs were copied byte-for-byte to ESP/TOOLS13R1C/workspace (guest path /TOOLS13R1C/workspace): helpers.cpp, helpers.h, main.cpp, and README.md. Their staged SHA-256 values are respectively 3B87E2777DA8E9A4C5E1A544F6C0FA20ABA563449045109805C043BD29F91AF6, BFF05C4417363F656C5DBBCC495364138CE7CBF879FE3F51DE10B8A0E583AAC8, 1D2872A76269D7A342A9041F4C9E269D65A9200953361AF6A4C3A40783888D11, and DD6A8DA8B1A3DED0F7CB0A6E20A9EC03136773BF0EBEA208266B85DD248A7538.

Static inspection confirms the persistent boot storage is mounted at /, FAT path lookup supports nested directories, and Native ELF bare-metal stat/list/read/write host calls forward absolute paths to VFS. The staged workspace path is within the VFS path bound. This establishes structural path support and Developer Studio's available file API; actual guest listing/opening remains untested until an authorized guest phase.

## Hosted validation

| Check | Result |
|---|---|
| Pinned Mbed TLS profile verifier | PASS; exact versions, commits, overlays, and three patches verified. |
| TLS/PSA linkage | PASS; TLS handshake, X.509 parse and verify, PSA random, initialization, and zeroization symbols are present in the kernel ELF. |
| Native ABI layout | PASS; 191 static layout assertions plus four append invariants. |
| Accepted TOOLS12 AMD64 ELF compatibility | PASS, 10/10 checks; accepted hash and 1,262,228-byte size match. |
| Native ELF window bridge Debug | PASS, 42/42 checks. |
| Native ELF window bridge Release | PASS, 42/42 checks. |
| Native ELF validator regression | PASS, 13/13 checks. |
| Native ELF runtime regression | PASS, 18/18 checks. |
| Native ELF trampoline regression | PASS; seven return/status/register/stack invariants. |
| Native ELF development app-model regression | PASS, 13/13 checks. |
| Native filesystem contract | PASS. The tracked helper script omits current profile include roots and lists elf_writer.cpp and native_elf_debug_watches.cpp twice. Its unmodified invocation failed for those harness issues. The equivalent hosted build used the tracked Server source list with duplicate entries removed and the profile include roots added; 87 unique source files built with zero errors, then the contract executable passed. Its warning-heavy build emitted 6,498 warning lines. No Server source or runner was edited. |
| Staged Developer Studio manifest/package audit | PASS; exactly three runtime files, both architectures and manifest entries verified. |
| Application ELF identity | PASS; the staged AMD64 ELF matches the accepted TOOLS12 SHA-256 exactly. |
| ESP structure and inventory | PASS; bootloader, kernel, ramdisk, application package, existing tracked project fixtures, and TOOLS13R1 workspace fixture are present. |

The established TOOLS13R1E report counts 340 explicit ABI, accepted-ELF, window-bridge, and four Native ELF regression assertions; all corresponding hosted checks passed again here. The filesystem-contract test reports one aggregate PASS and does not emit an internal assertion count. These are hosted checks only; they do not establish guest boot, loader acceptance, or application runtime behavior.

The principal hosted commands were:

    powershell.exe -NoProfile -ExecutionPolicy Bypass -File D:\dev\guideXOS_DeveloperStudio_Tools\scripts\run-tools13r1c-runtime-compatibility-test.ps1 -ServerRoot D:\dev\guideXOS_DeveloperStudio_Tools_Source\out\TOOLS13R1G-hosted-tests -AcceptedElf D:\dev\guideXOS_DeveloperStudio_Tools_Source\out\TOOLS12\dual-architecture-package\Apps\DeveloperStudio\bin\amd64\developerstudio.elf

    powershell.exe -NoProfile -ExecutionPolicy Bypass -File D:\dev\guideXOS_DeveloperStudio_Tools_Source\out\TOOLS13R1G-hosted-tests\scripts\run-native-elf-host-test.ps1
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File D:\dev\guideXOS_DeveloperStudio_Tools_Source\out\TOOLS13R1G-hosted-tests\scripts\run-native-elf-runtime-host-test.ps1
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File D:\dev\guideXOS_DeveloperStudio_Tools_Source\out\TOOLS13R1G-hosted-tests\scripts\run-native-elf-trampoline-host-test.ps1
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File D:\dev\guideXOS_DeveloperStudio_Tools_Source\out\TOOLS13R1G-hosted-tests\scripts\run-native-elf-development-app-model-host-test.ps1

    powershell.exe -NoProfile -ExecutionPolicy Bypass -File D:\dev\guideXOS_DeveloperStudio_Tools_Source\tests\validate-developer-studio-package.ps1 -ServerRoot D:\dev\guideXOS_DeveloperStudio_Tools_Source\out\TOOLS13R1G-final-guest-image -PackageRoot D:\dev\guideXOS_DeveloperStudio_Tools_Source\out\TOOLS13R1G-final-guest-image\ESP\Apps\DeveloperStudio

The pinned profile was independently checked with:

    powershell.exe -NoProfile -ExecutionPolicy Bypass -File D:\dev\guideXOSServer_MODERN_INPUT\scripts\verify-mbedtls-profile.ps1 -DependencyRoot D:\dev\guideXOS_DeveloperStudio_Tools_Source\out\TOOLS13R1G-final-guest-image\third_party\mbedtls -RepoRoot D:\dev\guideXOSServer_MODERN_INPUT

The Native filesystem contract build and run logs are retained under the final worktree's out/validation directory.

## Private ESP identity

The final image input is the directory-backed FAT ESP at:

    D:\dev\guideXOS_DeveloperStudio_Tools_Source\out\TOOLS13R1G-final-guest-image\ESP

This Server build process consumes the ESP directory directly as a FAT-backed guest volume; this is not a raw disk image or ISO. The tree has 33 files and 72,774,398 bytes of file payload. Its canonical ESP tree SHA-256 is DDB26BB2D986A331963971492C87EF2F5F9643AC07AB791E513DA966D2F59CDD. The digest is reproducible by sorting slash-normalized relative paths by ordinal order and hashing, for each file, an eight-byte little-endian path-byte length, UTF-8 path bytes, an eight-byte little-endian file size, and the raw file bytes. Key artifact hashes are listed above; the full per-file inventory was audited before closeout.

## Version contract and QAC baton

The application manifest's 0.5.0 minimum-platform field still has no authoritative comparison rule. The owner clarified that the v0.5_DEVELOPER_STUDIO branch name was a development guess and the application version is independent of Server releases. Preserve the current manifest and kernel metadata unchanged. This record does not claim that the runtime satisfies the declared minimum or bypass any loader rejection. Treat the image only as an experimental compatibility-test candidate.

No QAC reservation was requested, no QEMU process was started, and no guest was launched. The latest inspected QAC coordination records leave first QEMU priority with .NET Support and require explicit handoff or independent Tools-lane authorization; an idle coordinator is not authorization. The QAC baton remains with .NET Support pending that handoff.

## TOOLS13R1H prerequisites and procedure

Before guest acceptance:

1. The Server platform owner must define the authoritative meaning/value of the platform version, and the Developer Studio owner must confirm the intended meaning of the existing 0.5.0 minimum. Keep all existing metadata unchanged while that decision is pending.
2. Obtain explicit .NET Support baton release or independent Tools-lane authorization. Then create a TOOLS13R1-specific QAC runner and request a managed reservation; do not reuse the Phase29Y runner pinned to another staged ELF.
3. Re-audit this ESP tree hash and the accepted ELF/package hashes immediately before launch.
4. Have QAC spawn QEMU as its direct child, with the existing Server AMD64 boot arguments, a directory-backed FAT drive using this ESP, loopback QMP, serial capture, and a run-specific evidence directory.
5. Require the bounded guest acceptance cases, visible first frame, keyboard/mouse interaction, clean application/window close, QMP shutdown, QEMU exit, and verified QAC release. Preserve serial, screenshot, and QAC lifecycle evidence with the image hash and reservation ID.

The documented command boundary is:

    D:\dev\guideXOS_QAC\src\GuideXOS.QAC.Cli\bin\Debug\net10.0-windows\GuideXOS.QAC.Cli.exe run --project DeveloperStudio --mode acceptance -- <qemu-system-x86_64.exe> <existing AMD64 Server arguments using the ESP path above>

This is a future authorized procedure, not an instruction executed in TOOLS13R1G. Do not start TOOLS13R1H automatically.

## Local changes

Only this recovery record is committed to the Tools evidence repository. No Server source, dependency manifest, Developer Studio source, application binary, release branch, or remote repository was changed. The private dependency, build outputs, validation logs, staged package, fixture, and ESP remain in the task-created output worktrees.
