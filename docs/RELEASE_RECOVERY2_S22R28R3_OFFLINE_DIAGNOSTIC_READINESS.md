# RECOVERY2 / S22R28R3 Offline Diagnostic Readiness

**Outcome: `RECOVERY2_OFFLINE_READY`.** The pinned build dependency was restored and verified; diagnostic-only FAT mount tracing was added; focused hosted and AMD64 freestanding checks passed. Guest qualification remains pending and was not attempted in this offline phase. The unresolved R1 FAT mount failure remains unexplained until the new trace is observed in an authorized guest run.

## 1. Active checkout provenance

- Recovered checkout: `E:\dev\guideXOSServer`, branch `main`.
- Recovery baseline at entry: `d1b2b2ce9bf7df71e21abbfcead411ce5b12d4c9`. The entry worktree and index were clean; `git fsck --full --strict` passed.
- Local `origin/main` was the synchronized recovery snapshot at entry. No fetch was performed, so this does not establish live remote synchronization.
- Validated source commit: `ee8393d24f99f26406c5fb1283cbdf7ac96ddabe` (`Add diagnostic-only FAT mount tracing`). The recovery readiness document is committed separately after artifact creation. No push was performed.
- The verified backup `C:\dev\guideXOSServer-E-20261124-recovery` was not accessed or modified.
- `assets/Backgrounds/troncity.png`, previously reported as untracked, is absent after recovery. It was not reconstructed or replaced.

## 2. Restored pinned dependencies

The authoritative records are `guidexos/mbedtls.lock` and `guidexos/mbedtls_profile.json`:

| Component | Version/tag | Verified revision |
| --- | --- | --- |
| Mbed TLS | 4.1.0 / `mbedtls-4.1.0` | `0fe989b6b514192783c469039edd325fd0989806` |
| TF-PSA-Crypto | 1.1.0 / `v1.1.0` | `29160dd877d29658279fd683b2ae57b320ddcf09` |
| TF-PSA framework submodule | pinned by TF-PSA tree | `dff9da04438d712f7647fd995bc90fadd0c0e2ce` |
| ML-DSA native submodule | pinned by TF-PSA tree | `5772b4f4a0105694b1203abb582273f78fa951b7` |

Restoration used `scripts/bootstrap-mbedtls.ps1 -Install` to retrieve the pinned upstream commits into the active ignored dependency directory. The exact commits and dereferenced tags matched the lock records; recursive submodule status matched; strict object checks passed for the restored repositories; and `scripts/verify-mbedtls-profile.ps1` reported `MBEDTLS_PROFILE_VERIFY=PASS`, versions 4.1.0 and 1.1.0, and three patch series. The profile verifier checked its 17 normalized source-file hashes and the required profile/configuration files.

The S22R23 candidate source tree recorded the same revisions and profile hashes, but its Git alternates point to the failed `D:` drive. Its metadata was not used as authority and no alternates were copied. No newer dependency was substituted. No required Mbed TLS dependency remains missing or unverifiable in this build.

## 3. FAT mount diagnostics

`kernel::fs_fat::mount()` now has compile-time-gated diagnostics under `GXOS_FAT_MOUNT_DIAGNOSTICS`, enabled through `FAT_MOUNT_DIAGNOSTICS=1` in `kernel/Makefile`. The disabled configuration remains the default.

For each bounded mount attempt, the trace records an attempt ID; selected block device and sector size; initial and candidate LBA; saturated read count; the direct first and last block-layer status values; sector-zero availability; signature readability/value/validity; BPB readability and bytes-per-sector, sectors-per-cluster, reserved sectors, FAT count, FAT16/FAT32 sizes, and total-sector fields; first and last BPB rejection identifiers; and the final mount stage. It reports one fixed-format line per attempted mount and uses static diagnostic state rather than a large stack buffer.

The result of the actual initial `block::read_sectors()` call is captured immediately. It is not inferred from `last_io_status()`. Failed reads preserve their returned block status. Sector and BPB data are only inspected within the existing 4096-byte sector buffer and when the fields are safely readable. Boot signature validity is recorded as an observation; it does not add or relax a FAT signature check. Existing validation decisions and successful mount behavior are retained. No storage write path or runtime gate was added.

The stage IDs distinguish FAT16/FAT32 field and geometry rejections, partition-sector read failures, candidate exhaustion, exFAT probe outcomes, mount-capacity rejection, and success. When both FAT and exFAT probes fail, the final stage describes the last failed probe while `firstReadStatus` continues to preserve the actual first FAT read result.

## 4. Hosted regression evidence

The regressions use the production `fs_fat`, block-device, RAMDISK, and VFS implementations. The exact previously qualified R1 `wallimg` RAMDISK was used read-only; its 67,108,864-byte source image remains SHA-256 `ead5105aa131dee575e0021fac1650a28330df422bd211ca6baccdd267179f05`.

| Check | Result |
| --- | --- |
| New FAT mount diagnostic tests, diagnostics on | 15 checks, 0 failures |
| New FAT mount diagnostic tests, diagnostics off | 7 checks, 0 failures |
| Production RAMDISK/FAT/VFS mount against exact `wallimg`, diagnostics off | 9 checks, 0 failures |
| Same production mount against exact `wallimg`, diagnostics on | 12 checks, 0 failures; direct read and BPB observations matched |
| FAT persistence regression on a disposable writable copy | 44/44 passed; source template was not modified |
| VFS hosted read | 4/4 passed |
| VFS mount-table integrity | 19/19 passed |
| User-data volume policy | 9/9 passed |
| Native ELF security policy | passed; 0 failures, 4 native-entry calls |
| Native ABI layout | passed |
| AMD64 memory foundation (S22R27) | passed |
| AMD64 protected-task/CPL3 contract | exited 0 |
| Microsoft x64 context-switch regression | passed |

The diagnostic tests cover a valid FAT32 mount, the direct initial read failure, invalid boot-signature observation, malformed BPB fields, unsupported empty-data geometry, distinct FAT16/FAT32 rejection identifiers, and diagnostics-disabled behavior. The invalid-signature fixture confirms the existing mount decision remains unchanged. No hosted pass is treated as guest evidence or as proof that the R1 mount failure is fixed.

## 5. Freestanding AMD64 evidence

Both builds used the public-preview security profile, `CPL3_DIAGNOSTIC=0`, and separate clean output roots. The diagnostics-on build set `FAT_MOUNT_DIAGNOSTICS=1`; the diagnostics-off build used the default `0`. `SHELL=cmd.exe` was required by the Windows makefile recipes.

| Configuration | Kernel ELF SHA-256 | Kernel PE SHA-256 | Symbol audit | 16 KiB stack guard |
| --- | --- | --- | --- | --- |
| Diagnostics on | `e6f478e4a272912d3ea8561a36d62d37f34e6538079f7d9f41230287d697b2ba` | `91b40ef602e1f453555a1c1259f9526dc2aadb6868b30ad25707c4afcf244f7d` | passed, 53 UND rows / 22 distinct; no relocations/imports | passed |
| Diagnostics off | `3b1c383b4f141c43653054b58998492e55077df95c92b75cbaaf84a0ff9630eb` | `c597425f8d2787aa74c692d18b8d162232ab10fd62f262ec33baf0b1e255065f` | passed, 53 UND rows / 22 distinct; no relocations/imports | passed |

The stack guard's largest named path was Legacy Display Options at 11,864/16,384 bytes, leaving 4,520 bytes of estimated headroom; the minimum required is 4,096 bytes. These are compiler-derived estimates for enumerated paths, not runtime measurements or a whole-kernel stack proof.

Build tool identities: GCC `g++` 15.2.0 (MinGW-W64 x86_64-msvcrt-posix-seh); GNU Make 4.4.1; GNU Binutils `ld`, `objcopy`, `readelf`, `objdump`, and `nm` 2.46.0.20260210; NASM 2.16.03. The freestanding builds, symbol audits, and stack guard completed without QEMU.

## 6. Separate diagnostic artifact

A new artifact was created under `E:\CodexArtifacts\S22R28R3\recovery2-fat-mount-diagnostic`. It was assembled from a copy of the existing CPL3-disabled control ESP; only the copied kernel and its identity/audit receipts were replaced. The control and CPL3-on experiment directories were read-only inputs.

- **Classification:** `DIAGNOSTIC — NOT PUBLIC RELEASE`
- Source commit: `ee8393d24f99f26406c5fb1283cbdf7ac96ddabe`
- Configuration: AMD64, public-preview security profile on, CPL3 diagnostic off, `GXOS_FAT_MOUNT_DIAGNOSTICS` on, BootInfo v3.
- Kernel ELF: 2,848,304 bytes; SHA-256 `e6f478e4a272912d3ea8561a36d62d37f34e6538079f7d9f41230287d697b2ba`.
- RAMDISK: 67,108,864 bytes; SHA-256 `ead5105aa131dee575e0021fac1650a28330df422bd211ca6baccdd267179f05`.
- Bootloader: 57,856 bytes; SHA-256 `81258f7ad179494a3cdf289b87170370d3a281900e1e386caf41041e00fb00d4`.
- ESP: 90,177,536 bytes; SHA-256 `ce2422fa961f8d8485c32c6998d4f294a7932aad35d6839d5f229d91de13f4ba`.
- ISO: 90,245,120 bytes; SHA-256 `e7f16b07a6886d09ab7f4114c3357d8a8d622917bbe5cc02380d5088f88d94e6`.

The pinned release packager recreated the FAT32 ESP from a fresh 17-file expected manifest. `release_esp.py verify` passed. `release_iso.py create` and its independent raw ISO verifier passed: the ISO is UEFI El Torito bootable, and the embedded boot image at LBA 32 hashes to the verified ESP SHA-256. Root artifact manifest, expected manifests, create/verify receipts, build identity, marker, and symbol audit are in the artifact directory. The build used the pinned release Python packages in `out/release-tools/venv`; no release `dist` output or readiness marker was touched.

## 7. Preserved artifacts

Read-only SHA-256 checks after packaging matched the recorded identities:

| Preserved artifact | SHA-256 |
| --- | --- |
| Accepted S22R23 public-preview ISO | `8d397659e0a6ec8f1be113ae4f5be14dbcaf1ef22ccda41a6e5ee5014919cf40` |
| S22R28R1 CPL3 diagnostic ISO | `3168d943af704a730110ab1e669ab482c3e93f282bbd7d2f73283f490b2d785c` |
| S22R28R2 CPL3-disabled control ISO | `ee4ff561748ebb90f24bfe881fa225f8a373368567b1a402e390aa1eb3844907` |
| Existing S22R28R3 CPL3-disabled control ISO | `bb3ff91d7b06d5cb5105698254c9636465ab081a463c86f6bef89071bb7cbf10` |
| Existing S22R28R3 CPL3-on experiment ISO | `abe47f2d61416bc6ea1052d364b71639637fc866507216fe923547e43c030431` |

No accepted ISO, receipt, or backup was overwritten. The new candidate is independently named and does not replace the control.

## 8. Relocated build and QAC status

The active environment has Git 2.56.1, CMake 4.3.1, Ninja 1.13.2, GCC 15.2.0, GNU Make 4.4.1, NASM 2.16.03, Python 3.12.10, and the required pinned ISO/ESP Python packages. The kernel's Windows make recipes work with `SHELL=cmd.exe`. The existing BootInfo v3 loader was retained from the CPL3-disabled control rather than rebuilt. Release ESP/ISO scripts and their manifests were reviewed and used in the isolated candidate directory. No unrelated old `D:` paths or other repositories were rewritten; generated CMake/Ninja outputs were not regenerated because their projects were not needed for this build.

QAC could not be queried through its established read-only CLI: `GUIDEXOS_QAC_CLI` was blank, the expected CLI executable and `qac.exe` were not on PATH, and the documented local coordinator configuration was absent. Live QAC health and reservation status are therefore unavailable. No coordinator restart, reservation clearing, QEMU process termination, unmanaged launch, or QEMU execution occurred. Guest acceptance gates remain pending.

## 9. Remaining qualification and next action

S22R28R1's three successful real CPL3 cycles remain established. S22R28R2's FAT mount rejection cause remains unknown. This work instruments the observed mount path but does not change its validation policy and does not establish a guest fix.

When an authorized QAC reservation is available, run the already-prepared **CPL3-disabled control ISO first**. Keep that control's outcome separate. Then, if useful for the same investigation, run this distinct diagnostic ISO to record whether the initial block read failed or which BPB validation stage rejected the volume. Preserve exact reservation, ISO hash, serial log, and returned block status with the resulting guest evidence. Do not begin timer-driven preemption, PacMan CPL3 migration, arbitrary Native ELF termination, or unqualified CPL3 fault recovery before filesystem integration is qualified.