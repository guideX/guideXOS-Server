# S22R28R2 — CPL3, VFS Integration, and Guest Acceptance

**Outcome D — `QAC_UNAVAILABLE`.** Hosted checks confirm that the exact S22R28R1 system ramdisk is readable through the production RAMDISK, FAT, and VFS code, including `/Apps/PacMan`. The prior guest still failed while mounting that image at `/`. The first failure is narrowed to the FAT mount of the attached `wallimg` device, but the existing serial log does not show the block-read result or boot-sector fields needed to identify why the guest rejected it. An active, owner-verified QAC acceptance reservation prevented the control and experiment guest comparison. It was left untouched.

The S22R28R1 CPL3 experiment remains unchanged. A separately packaged CPL3-off control ISO uses the same BootInfo v3 loader and the same filesystem payload. It is **not guest-qualified**. No product kernel or loader correction is claimed in this report.

## 1. Starting repository and preservation

- Repository: `D:\dev\guideXOSServer`
- Starting branch and HEAD: `main`, `7c3c6a0135d7e1eceb50b714a27fef8eb0c6ac10`
- Starting branch was three commits ahead of `origin/main`; no branch operation or push was performed.
- The only pre-existing untracked file was `assets/Backgrounds/troncity.png`, SHA-256 `444ac64211907d80f0266e2283960cefaa932803d0aee7e0ccfd414f6fae2fda`. It remains untracked and unchanged.
- The accepted public-preview ISO was independently hashed at its existing path and remains SHA-256 `8d397659e0a6ec8f1be113ae4f5be14dbcaf1ef22ccda41a6e5ee5014919cf40`.
- The S22R28R1 diagnostic ISO was independently hashed and remains SHA-256 `3168d943af704a730110ab1e669ab482c3e93f282bbd7d2f73283f490b2d785c`.
- No `dist`, signing, publication, or readiness files were changed.

The only repository changes in this phase are this report and a hosted release-ramdisk mount test and runner. The kernel and bootloader source are unchanged. `git diff --name-only 14be308b7282a2131b0f124eeccd662a5378014d..7c3c6a0135d7e1eceb50b714a27fef8eb0c6ac10` lists documentation only.

## 2. S22R28R1 artifact provenance

The original experiment artifact and its evidence remain at `E:\CodexArtifacts\S22R28R1\candidate-v2`. Its repository evidence index is [the S22R28R1 evidence README](evidence/s22r28r1-cpl3-guest/README.md), and the prior runtime result is in [the first CPL3 guest acceptance report](RELEASE_S22R28R1_FIRST_CPL3_GUEST_ACCEPTANCE.md).

| Item | Identity |
|---|---|
| Experiment ISO | 90,245,120 bytes; SHA-256 `3168d943af704a730110ab1e669ab482c3e93f282bbd7d2f73283f490b2d785c` |
| Embedded ESP | 90,177,536 bytes; SHA-256 `fb1eb4975b823a790d70fa2be4682a2e3a4ba1ec017ff95d324336b15ce0d049`; 17 files |
| Bootloader | 57,856 bytes; SHA-256 `81258f7ad179494a3cdf289b87170370d3a281900e1e386caf41041e00fb00d4`; BootInfo v3 |
| Diagnostic kernel | 2,846,392 bytes; SHA-256 `47d4409e9d37e6e593b2f6068f45c970ad666ecb3831706b6b4b52ce15336c3b` |
| System ramdisk | 67,108,864 bytes; SHA-256 `ead5105aa131dee575e0021fac1650a28330df422bd211ca6baccdd267179f05` |
| PacMan manifest / ELF | `3f3592e6c76053e323f16d48073e98c67d7f5702b8e47f0b507d6bfb3d126449` / `eeb544bbb2e2574eeaed7bb7f0bad61ea24f6945231b7757223a225b30b8e1cc` |

The R1 artifact manifest, ESP receipt, embedded-image verification report, ISO verification report, and QAC/serial evidence remain in place. The public-preview ISO was not used as a scratch image or rewritten.

## 3. First S22R28R1 filesystem failure

In the exact R1 guest serial transcript, kernel startup initializes VFS with an empty mount table. The app diagnostic runs at that point and reports `mountTable count=0` and `/Apps state=absent`. This check occurs before the boot ramdisk is attached, so the missing `/Apps` result is downstream of the root mount not yet existing; it is not a separate initial failure.

The first failing filesystem operation is later:

1. The kernel reads the BootInfo v3 `RamdiskBase` and `RamdiskSize` and logs the 64 MiB image at `0x150DC000`.
2. The RAMDISK layer attaches `wallimg` as a read-only image with `0x20000` sectors.
3. `desktop::set_wallpaper_image_pack()` calls `vfs::mount_type("/", wallimg, FS_TYPE_FAT32)` because no writable root mount exists.
4. `fs_fat::mount()` returns `0xFF`; VFS logs `Filesystem mount failed` and the desktop reports `failed to mount boot runtime image at /`.
5. `/Apps` resolution and the user-data mount then fail because `/` was not mounted.

The preserved transcript is [`guest-com1-v3c.serial.log`](evidence/s22r28r1-cpl3-guest/candidate-v2/artifacts/guest-com1-v3c.serial.log). It contains no FAT boot-sector probe or block-read status. It shows no exception or panic at the mount site.

## 4. Root-cause investigation

The diagnostic ISO has the expected filesystem payload. The ramdisk receipt records the exact 64 MiB file and hash above. Its FAT BPB contains 512-byte sectors, 16 sectors per cluster, 32 reserved sectors, two FATs, 131,072 total sectors, a 256-sector FAT, root cluster 2, and the `55 AA` boot signature. Its root directory contains `Apps`.

The hosted test added in this phase invokes the production release-image RAMDISK provider, FAT driver, and VFS against that exact file. It passed 9/9 checks: the image registers as a read-only owner-verified release device; `/` mounts; `/Apps` resolves and enumerates; `PacMan` is present; and the complete 956-byte manifest reads with package identity `com.guidexos.pacman`. This excludes a missing `/Apps` tree and malformed on-disk image as the explanation for the R1 guest result. It does not prove the guest’s physical view of the ramdisk matches the file on the host.

The current evidence therefore locates the guest failure at the `fs_fat::mount()` boundary but does not distinguish a failed sector read, unexpected first-sector contents, filesystem-driver state, or an execution interaction. No root cause or product defect is claimed without the control/experiment guest comparison or more specific guest instrumentation.

## 5. BootInfo v3 and boot-memory disposition

The loader and kernel share `guideXOSBootLoader/guidexOSBootInfo.h`. It defines the v3 magic/version, exact `BootInfo` size, checksum, memory-map fields, ramdisk base/size, kernel physical range, boot stack, framebuffer descriptors, and task-frame arena. The loader allocates BootInfo, the ramdisk, and the 4,096-page task-frame arena as `EfiLoaderData`; it reads the full ramdisk before setting its base, length, and valid flag. It fills the memory map after `ExitBootServices`, computes the checksum, and hands off through the v3 kernel entry.

The kernel validates the v3 magic, version, exact structure size, checksum, memory-map metadata, and task-arena flag before protected-task memory is enabled. The frame-owner initializer rejects an arena that overlaps the memory map, kernel image, ramdisk, BootInfo page, boot stack, or framebuffer, and it checks that the kernel page-table tree does not overlap that arena. It allocates protected-task frames only from the task arena; the active BootInfo, ramdisk, and loader-owned regions are not reclaimed by task teardown.

The R1 runtime log reports the task arena at `0x3CE28000` (4,096 pages), the ramdisk at `0x150DC000` through `0x190DC000`, and the kernel physical image beginning at `0x190DC000` and ending at `0x3BB6BA60`. The arena begins after the kernel image. Frame ownership initialized successfully, and all three task cycles restored the kernel CR3 and task-frame accounting baseline. The loader’s page-table log records a mapping for the full ramdisk range. These checks support the intended BootInfo v3 and reservation contract; they do not prove that every guest read of the ramdisk returned the expected bytes.

## 6. Control and experiment packaging

To isolate CPL3 execution from shared boot and filesystem inputs, a CPL3-off control was assembled under `E:\CodexArtifacts\S22R28R2\control-cpl3-off` using the previously built public-preview kernel with `CPL3_DIAGNOSTIC=0`. It uses the same BootInfo v3 loader, system ramdisk, Apps package, and ESP file set as the R1 CPL3-on experiment.

The control has 17 logical ESP files, like the experiment. A file-hash comparison found only four logical differences: `kernel.elf`, `kernel-symbol-audit.txt`, `build-identity.txt`, and the diagnostic classification marker. The loader and ramdisk hashes are identical. The experiment is the unchanged R1 ISO listed above; its guest evidence applies only to that exact ISO.

The control ISO is marked **DIAGNOSTIC — NOT PUBLIC RELEASE CANDIDATE** and has no guest result. The separate ESP creation and verification passed, and the independent ISO verifier confirmed UEFI El Torito boot metadata and the exact embedded ESP hash.

| CPL3-off control item | Identity |
|---|---|
| ISO | `E:\CodexArtifacts\S22R28R2\control-cpl3-off\guideXOS-S22R28R2-CPL3-VFS-CONTROL-BOOTINFO-V3-NOT-PUBLIC-RELEASE.iso`; 90,245,120 bytes; SHA-256 `ee4ff561748ebb90f24bfe881fa225f8a373368567b1a402e390aa1eb3844907` |
| Embedded ESP | 90,177,536 bytes; SHA-256 `7cd6723af1fd2f97dce9ce367ff36522877f2bf925a7716f4ff95796073fae54` |
| Bootloader | SHA-256 `81258f7ad179494a3cdf289b87170370d3a281900e1e386caf41041e00fb00d4`; BootInfo v3 |
| CPL3-off kernel | 2,839,856 bytes; SHA-256 `3b1c383b4f141c43653054b58998492e55077df95c92b75cbaaf84a0ff9630eb` |
| System ramdisk | SHA-256 `ead5105aa131dee575e0021fac1650a28330df422bd211ca6baccdd267179f05` |

The external control artifact manifest at `E:\CodexArtifacts\S22R28R2\control-cpl3-off\artifact-manifest.json`, ESP receipts, ISO receipts, and file-difference report record the provenance and composition. The control kernel symbol audit passed with 53 undefined `.symtab` rows / 22 unique names and no relocations or imports. The matching R1 experiment kernel audit passed with 55 rows / 22 names and no relocations or imports.

## 7. CPL3 and kernel-memory coexistence review

The R1 experiment completed three real CPL3 cycles with CS `0x23`, result `42`, vector `0x80` return, kernel CR3 restoration, off-stack teardown, and task-frame accounting returned to baseline. The kernel then entered its event loop and continued PIT heartbeats. The frame-owner source restricts task allocation to the separately reserved arena; its overlap checks exclude the kernel and ramdisk ranges, and the protected-task tests preserve supervisor-only shared mappings.

These facts make a direct ramdisk/image packaging defect less likely and show no reported task-frame leak. They do not rule out a kernel lifecycle or page-table interaction. The R1 serial transcript provides only the CPL3-enabled observation; no CPL3-off guest control has been run. The failure is not attributed to CPL3 or to another cause at this point.

## 8. Hosted and freestanding validation

All new test outputs and the CPL3-off control artifacts are outside the repository under `E:\CodexArtifacts\S22R28R2`. The added reusable test is [release_ramdisk_mount_test.cpp](../tests/release_ramdisk_mount_test.cpp), run by [run-release-ramdisk-mount-test.ps1](../scripts/run-release-ramdisk-mount-test.ps1).

| Area | Result |
|---|---|
| Release ramdisk, FAT root mount, `/Apps`, PacMan manifest | PASS, 9/9 |
| VFS mount-table integrity | PASS, 19/19 |
| Hosted VFS reads | PASS, 4/4 |
| S22R27 memory foundation | PASS; hosted only, no guest fault claim |
| Protected-task lifecycle contract | PASS |
| AMD64 voluntary context-switch ABI | PASS; no preemption claim |
| Native ELF package security policy | PASS, 29 checks, zero failures |
| Native ABI layout | PASS |
| UserDataVolume policy | PASS, 9 cases |
| FAT persistence | PASS, 44/44 on the runner’s disposable copy of the accepted data image |
| Settings native layout/runtime/presentation | PASS, 1,037/1,037; 908/908; 3,659/3,659 |
| Settings Center and inventory models | PASS, 89/89; device 21/21 and storage 24/24 |
| Default Apps model/interactions | PASS, 39/39 and 9/9; 100 repeated lifecycles |
| PacMan source registration and window UI regression | PASS |
| Stack guard | PASS; 16 KiB contract, 4,520 bytes minimum modeled headroom |
| AMD64 symbol audit | PASS for the exact CPL3-on and CPL3-off ELF/PE pairs; no relocations/imports |

The accepted 128 MiB user-data source image was SHA-256 `3ee67553ab9bece4130e4aa84ec4ed0d341cb29d7fd78798884c5ae3b0b7b148` before and after the FAT persistence test. The modified test copy is separately preserved under the S22R28R2 hosted evidence directory.

The S22R28R1 diagnostic-on and public-profile diagnostic-off freestanding kernel outputs were reused because no kernel or loader source changed in this phase. No new freestanding kernel rebuild was justified by the hosted test/tooling changes. The stack report is static `.su` modeling, not a complete call-chain or runtime stack proof. The independent R1 build receipts are in [preflight-results.json](evidence/s22r28r1-cpl3-guest/preflight/preflight-results.json).

## 9. Persistence and application behavior

The accepted document and settings architecture remains unchanged: trusted boot `/Apps` remains read-only, `/Documents` and `/AppData` remain on the existing user-data volume when configured, and the existing Native ELF allowlist remains PacMan-only. The hosted persistence test exercised a disposable copy and confirmed existing Notepad document contents were preserved through settings writes. No accepted source image was modified.

This phase did not attach a data volume to a guest. It did not verify `/Documents`, `/AppData`, Settings open/close, PacMan launch/input/exit, desktop input, or guest-requested shutdown after CPL3. Prior S22R23 behavior remains historical evidence for its own qualified image and is not transferred to this control ISO. The S22R28R1 headless guest did not exercise input or PacMan and was stopped by host QMP quit.

## 10. QAC and guest gates

The read-only closeout record and current process inspection identify active acceptance reservation `9602b018c23b4ce08f7b2c77c4aba931`, owner process PID `29120`, and managed QEMU child PID `30872`. The QAC CLI owns that child. The session was left untouched; this task made no guest request and launched no QEMU process.

| Gate | S22R28R2 status |
|---|---|
| A — UEFI, BootInfo v3, root and `/Apps` | Not rerun; R1 previously reached the root FAT mount and failed there |
| B — three CPL3 cycles | R1 exact experiment passed; no result is transferred to the new control ISO |
| C — input, Settings, PacMan | Not run |
| D — user data and normal guest shutdown | Not run |

QAC unavailability is an environment disposition, not a product defect. The exact active session is documented in [the R1 QAC closeout snapshot](evidence/s22r28r1-cpl3-guest/candidate-v2/artifacts/qac-closeout-after-task.txt).

## 11. Remaining limitations and next milestone

The first failing guest operation is known, but its low-level cause is unresolved. The next guest admission should run the CPL3-off control and the exact R1 CPL3-on experiment under exclusive QAC reservations, with serial capture of the `wallimg` device index, sector-zero read status, boot-sector BPB fields, and FAT rejection reason. If both variants fail identically, continue with the shared BootInfo/memory view and mount path. If only the CPL3-on experiment fails, investigate CR3/TLB and task teardown interactions.

No user-origin exception recovery, arbitrary forced termination, timer preemption, scheduler, or PacMan CPL3 migration was added. S22R29 should first qualify safe recovery from a bounded user-origin exception: classify the fault, return with known kernel CR3 and stack, record the task failure, reap it off-stack, and verify the kernel and desktop continue. Timer entry and independent scheduling should follow as separate work. A successful voluntary CPL3 task remains insufficient evidence for recovery or preemptive survivability.

## 12. Commit and final disposition

The validated hosted test/tooling and this report are committed locally on `main`. No push, signing, publication, promotion, or readiness-marker change was performed. Final commit, branch/worktree state, wallpaper preservation, R1 experiment identity, CPL3-off control identity, and the active QAC reservation are reported in the task closeout.
