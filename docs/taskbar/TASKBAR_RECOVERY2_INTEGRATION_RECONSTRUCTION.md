# TASKBAR-RECOVERY2 — Integration Reconstruction and Requalification

- **Recorded:** 2026-11-26
- **Outcome:** `TASKBAR_RECOVERY2_CANDIDATE_REBUILT_GUEST_PENDING`
- **Candidate branch:** integration/taskbar-recovery2
- **Candidate worktree:** E:\dev\guideXOSServer_TASKBAR_INTEGRATION_RECOVERY2

## 1. Result and provenance

The original qualified integration candidate was not recovered. The conflicting recorded candidate hashes remain unresolved, and none of the candidate objects or their historical ancestry was found in the inspected repositories or the Recovery1 bundle. This branch is a new reconstruction with new provenance; it does not claim byte-for-byte or history equivalence with integration/taskbar-prep.

The reconstructed candidate combines the current committed mainline snapshot with the surviving TASKBAR_IMPROVEMENTS branch, then restores the missing canonical Native ELF AppID lifecycle and FAT32 directory growth behavior. The branch passes focused hosted regressions, the full hosted desktop build, the supported ServerV03PacManOnly AMD64 build, and offline release-ramdisk mount checks. QAC-managed desktop interaction, persistent writable-disk restart, and native shutdown acceptance remain pending.

This report records:

- **Source recovered:** No.
- **Source reconstructed:** Yes.
- **Functional behavior qualified:** Partially, by source checks, hosted regressions, and builds listed below.
- **Native behavior qualified:** No; no QAC session was available.
- **Integration ready:** No; native acceptance is pending, and the actual main checkout currently has uncommitted work that must be reconciled before an INTEGRATION3 proposal.

## 2. Source recovery and repository baselines

### 2.1 Repositories and pinned commits

| Source | Branch | HEAD / state |
|---|---|---|
| Surviving taskbar checkout | TASKBAR_IMPROVEMENTS | 63034c507093a9f2d34c503312893245db98839b; tracked and untracked worktree clean |
| Actual main checkout | main | aad1bce8a8e42890e9bfd49a2f397f04e19f3eef |
| Fetched upstream main reference | origin/main in actual main checkout | db367cdb9ece14dd3cd5892a7e3c24c38ce6f055 |
| Recovery2 candidate base | main commit cloned into independent repository | aad1bce8a8e42890e9bfd49a2f397f04e19f3eef |
| Recovery2 candidate HEAD | integration/taskbar-recovery2 | 4cc16acf69cdbf4756fe96510fe7504f9d8fba6e |

Actual main is three local commits ahead of its fetched origin/main and zero behind it. The local-only commits are aad1bce8 (Document S22R28R3 staged FAT guest evidence), d22debb0 (Add bounded ramdisk stage fingerprints), and 44a62832 (Document S22R28R3 FAT mount guest differential). A live remote update could not be verified; the fetched reference is the available upstream snapshot.

The actual main worktree was not clean when inspected. It contained seven modified tracked files and three untracked files, including edits to kernel/Makefile and kernel/core/desktop.cpp that overlap this candidate, plus ramdisk-integrity diagnostic work. These files were not incorporated into Recovery2 and the main checkout was not changed by this work. To preserve that work, an external snapshot was created at E:\dev\TASKBAR_RECOVERY2_MAIN_WORKTREE_SNAPSHOT_20261126. It includes a binary tracked-worktree patch (SHA-256 760E6211DAFE8C0CD6D1B6A8D8083D22ABC79270756C7730AF46D9AF14203A8B) and direct copies of all ten changed or untracked files; all ten copy hashes were verified against the source. The exact list is in SNAPSHOT.txt.

Ignored entries were inventoried separately and left in place. Actual main contains out/mbedtls-bootstrap, out/recovery2-stage-fingerprint-diagnostic, the two stage-fingerprint verification JSON files, out/release-tools, out/validation, and third_party/mbedtls. The taskbar source checkout had no ignored entries reported. The Recovery2 candidate’s generated ignored output includes its bootloader/kernel products, hosted executable, staged pinned PacMan source and build artifacts, profile/ramdisk outputs, test results, and third_party/mbedtls; none were cleaned or committed.

The source taskbar checkout remains at its original HEAD and clean. Its object database and available refs passed integrity inspection. No source checkout or actual main commit was reset, cleaned, pruned, or force-updated.

### 2.2 Bounded missing-object search

Read-only checks covered the two E: repositories, the independent candidate object store, relevant local CPU and DotNet repositories, refs, tags, remote-tracking refs, reflogs, worktrees, alternates, loose and packed objects, fsck dangling-object reports, configured remote information, the Recovery1 bundle, and the connected GitHub repository/commit records. The missing integration branch was not present remotely. The configured SSH remote was not usable from the shell; GitHub commit lookups for the candidate and ISO source returned no commit.

| Historical record | Result in inspected sources |
|---|---|
| Taskbar milestone 0afe07d57353f4ce16a452c00aff1e612b4bc0c0 | Absent from inspected Git object stores |
| Candidate transcription A d3ef13caefcdfc2738b71d568badfb833b3f1f4b | Absent; transcription conflicts with the other records |
| Candidate transcription B d3ef13caefcdfc2738b71d568badfb833b3b1f4b | Absent; transcription conflicts with the other records |
| Candidate hash in the Recovery1 report d3ef13caefcdfc2738b71d568badfb833b3f1b4b | Absent; its transcription conflicts with both prompt versions |
| Earlier integration merge 457fc283651cc1195ab7878b03b075098cda578b | Absent |
| Initial candidate 72b40c9f92dd944da8046bc222ca4eda337bf5c1 | Absent |
| Later ISO source 91eba69246cb9c0b7a7be3f0ee9898e46ee014f7 | Absent |
| Historical mainline checkpoint d1b2b2ce9bf7df71e21abbfcead411ce5b12d4c9 | Object found and verified, but superseded by the current pinned main HEAD |

An archived report mentioning a hash was not treated as evidence that its object exists. The candidate hash discrepancy remains unresolved; no one version was selected as canonical.

The preserved Recovery1 bundle is E:\dev\TASKBAR_RECOVERY1_BACKUP_20261126\repository-all-refs.bundle. Its 28 available refs and complete bundle contents were verified from the recovered taskbar repository. Its SHA-256 is DF359C179A142465FFCDC0441F050EA8586BB829CC69418256E73AD46D1A5DAA. The bundle does not contain the missing integration history and remains untouched.

### 2.3 Reconstruction history

The source taskbar history and actual main share merge base f5cb9f4c50aabec24e5c3b42b1a0247bba3605c4. Recovery2 was created as an independent local clone of actual main, without Git alternates, at E:\dev\guideXOSServer_TASKBAR_INTEGRATION_RECOVERY2. The surviving taskbar source was transferred as a verified local reference and merged as the second parent; the merge had no textual conflicts.

| Commit | Purpose |
|---|---|
| 25239b9d756a603f0b7328d655b767ca63e5b436 | Reconstruction baseline merge; first parent aad1bce8…, second parent 63034c50… |
| 4cc16acf69cdbf4756fe96510fe7504f9d8fba6e | Reconstructed Native ELF identity lifecycle, FAT32 directory extension/rollback, Makefile shell portability, and focused tests |

The baseline merge reuses the complete surviving taskbar branch history. It does not substitute the recovered TASKBAR_IMPROVEMENTS tip for the missing qualified candidate.

## 3. Surviving functionality and implementation inventory

This inventory describes source presence and compatibility evidence in this candidate. It does not promote historical test totals to Recovery2 acceptance.

### 3.1 Taskbar presentation

**Present and compatible at the source/model level:** canonical application grouping; stable running-window entries; independent persistent taskbar pins; independent Start Menu pins; grouped-window chooser and per-window context actions; popup ownership and pointer hit testing; hover preview model and popup placement; horizontal and vertical overflow; docking; supported size and label modes; auto-hide/reveal; running, attention, and operation-progress state.

The existing preview implementation has an explicit title/icon fallback where a safe per-window frame surface is unavailable. This reconstruction does not claim live native thumbnail rendering was demonstrated. Hosted presentation, layout, pointer, popup, and activity models passed the current tests below; native display and pointer behavior remain for QAC.

### 3.2 App Model and application lifecycle

The surviving App Model registry, trusted launch path, taskbar pin persistence, Start Menu history, file activation, and File Explorer progress ownership are retained. The missing Native ELF canonical-ID path was reconstructed:

1. Registered package manifests reject unsafe or over-capacity AppIDs.
2. Exact canonical-ID lookup and launch functions were added; display-name/alias launch remains a separate route.
3. The runtime copies the registered ID into trusted launch context.
4. A private KernelApp binding method is callable only by AppManager and NativeWindowOwner.
5. The native window owner checks the trusted runtime ID against the registered package ID before binding it to the application/window lifecycle.
6. Taskbar launch by pin uses the exact AppID; unknown IDs remain nonlaunchable and the existing fallback remains available.

Focused model tests reject delimiter injection, truncation, unknown IDs, and case-mismatched IDs. Production AMD64 compilation passed. Runtime association, minimize/restore continuity, cleanup of stale associations, unknown-app guest fallback, and protection against a malicious native package still require genuine QAC-managed validation. Native ELF multi-window grouping remains separately unqualified.

### 3.3 Persistence, storage, and shutdown

Taskbar settings, independent pin storage, Start Menu history, and existing migration behavior survived in the branch. The missing FAT32 directory-growth behavior was reimplemented in kernel/core/fs_fat.cpp:

- A full cluster-backed FAT32 directory extends only after a valid chain reaches EOC.
- The new cluster is allocated, zeroed, and linked at the prior tail.
- FAT mirror values are snapshotted before mutation; FAT32 reserved high bits are preserved.
- A failed FAT mirror write rolls the attempted mirrors back, including the mirror whose write reported failure, and retains the original error.
- A failed new-cluster/link operation releases the new allocation and preserves the failure status.
- Corrupt chains, I/O errors, invalid clusters, and traversal limits fail closed.

Recovery2 added a focused successful growth case and a failure-injection rollback case. The current FAT persistence suite passes 82/82 checks. ATA cache-flush behavior, device selection, per-channel serialization, capability-aware flush choice, bounded completion, diagnostics, and timeout/error propagation were already present in surviving source and passed the focused 80-check hosted test. VFS read/mount and user-data-volume policy checks passed.

The existing writable-handle/mount synchronization and shutdown error propagation source was reused rather than rewritten. No managed guest was available to prove actual ATA persistence across cold restart, required-sync failure behavior, or ACPI S5. Those remain pending; hosted storage tests are not native shutdown acceptance.

### 3.4 Boot and compatibility

The current committed mainline AMD64/UEFI and ramdisk implementation was retained. No historical address constants were copied. The new release ramdisk passed a 9-check production-source mount test: nonempty/aligned image, registered read-only block owner, VFS root mount, /Apps enumeration, PacMan package presence, and readable manifest identity. The full AMD64 profile build passed its kernel symbol audit.

This is not a UEFI boot test. BootInfo ownership/allocation, actual virtual-alias behavior, writable ATA root, read-only wallimg root, /system mount, wallpaper display, and normal ACPI shutdown still need native guest qualification. The actual main checkout contains separate uncommitted ramdisk-integrity diagnostics; those changes were preserved externally and excluded from this candidate.

The inspected current main asset directory assets/Backgrounds/NEW contains nine image files. No wallpaper was overwritten or replaced.

### 3.5 NDB exclusion

No unfinished NATIVE_DB_STORAGE work was merged or reconstructed. NDB shrink/reclamation and native database-file bridge compatibility remain separate convergence work.

## 4. Build environment and release artifacts

### 4.1 Windows Makefile and dependencies

The Windows Makefile failure was reproduced from the existing recipe pattern: Windows if-exist commands could be interpreted by Git’s POSIX sh. Platform commands now live in kernel/makefile-platform.mk. Windows uses PowerShell New-Item and Remove-Item commands compatible with Git sh and cmd; POSIX retains mkdir -p and rm -rf. scripts/test-kernel-make-shells.ps1 exercised create/remove paths containing spaces under both Git sh.exe and cmd.exe and passed.

The checkout originally lacked third_party/mbedtls. The pinned dependency bootstrap was used; its first attempt encountered Windows MAX_PATH while checking out mldsa-native. scripts/bootstrap-mbedtls.ps1 now applies core.longpaths=true only to that Git invocation. The retry installed the pinned dependency without global Git configuration changes or placeholder files. Verification reports Mbed TLS 4.1.0, TF-PSA Crypto 1.1.0, and three expected patches. The hosted and AMD64 builds verified the dependency profile. The required stb image header passed its reproducibility check.

The PacMan build script’s default local source path was unavailable. The exact pinned upstream source revision 8fc2aedd603ddc57e492bdc02c99442bb165109b was verified and staged in the ignored Recovery2 out directory; no package placeholder was created and no frozen app package was changed.

### 4.2 Builds

| Build | Result |
|---|---|
| Full hosted desktop build via cmd.exe /d /c build.bat | PASS; guideXOSServer.exe SHA-256 17414060665E05345844A4D73D2B3B571233523927BBE1F2049C8CE6BE11BC19 |
| Production AMD64 with ServerV03PacManOnly | PASS; build ID 2444a1a0-25c2-41b7-a699-c9c2e3337bba; source HEAD 4cc16acf…; clean source tree at build start |
| Kernel symbol audit | PASS; 52 UND rows, 22 distinct .symtab entries, no relocations/imports |
| Separate diagnostic AMD64 profile | No separate build profile is exposed by the current top-level build script. Its -Debug switch adds QEMU runtime diagnostics only. The kernel Makefile includes CPL3 and FAT-mount diagnostic compile flags in the normal AMD64 build. No unmanaged guest was launched. |
| Release ramdisk mount test | PASS, 9/9, using the clean-revision profile output |

The release profile is an ESP directory image, not an ISO. It is ServerV03PacManOnly, does not include Developer Studio, and records publicPreviewSecurityPolicy=not_enabled. It must not be represented as an authorized public preview.

Generated profile artifacts are preserved under out/release-profile/2444a1a0-25c2-41b7-a699-c9c2e3337bba/ESP:

| Artifact | Bytes | SHA-256 |
|---|---:|---|
| EFI/BOOT/BOOTX64.EFI | 57,856 | f6126b6cb4d7c999e0620cc95367203e673b13d168e3ed486f30e422a4f97a48 |
| kernel.elf | 2,893,696 | 19dd9dfca5e1dc88ebffde4f8f406c3c8a8f503ffff228d5f7ab418ec97063aa |
| ramdisk.img | 67,108,864 | ead5105aa131dee575e0021fac1650a28330df422bd211ca6baccdd267179f05 |
| Apps/PacMan/bin/amd64/pacman.elf | 63,640 | eeb544bbb2e2574eeaed7bb7f0bad61ea24f6945231b7757223a225b30b8e1cc |
| Apps/PacMan/app.json | 956 | 3f3592e6c76053e323f16d48073e98c67d7f5702b8e47f0b507d6bfb3d126449 |

The profile receipt is ESP/build-identity.txt. A first pre-commit profile build is also preserved under out/release-profile/74187d23-1ee1-4e10-9408-bdec9cac6c18; it was superseded as qualification evidence by the clean-revision build above. The hosted executable and other generated tests remain in ignored output locations; they were not staged or committed.

## 5. Recovery2 verification

All results below were produced from this reconstruction or its current source snapshot. Historical taskbar qualification results were used as test guidance only.

| Area | Recovery2 result |
|---|---|
| Taskbar application model and identity | 118 PASS, 0 FAIL |
| App Model identity | 25/25 |
| App Model actions | 55/55 |
| App Model file activation | 60/60 |
| Default-handler registry/dispatch | 54/54; 100 persistence/reload cycles, 100 ordinary dispatches, 100 open-with dispatches |
| Folder activation | 26/26 |
| URI/protocol activation | 41/41; 100 persistence, activation, and no-handler cycles |
| FAT/VFS persistence and FAT32 growth/rollback | 82/82 |
| ATA PIO flush | 80/80 |
| Native filesystem contract | PASS |
| Hosted VFS read | 4/4 |
| VFS mount table integrity | 19/19 |
| User-data volume policy | PASS |
| Compositor pointer buttons | PASS |
| Window UI regression | PASS |
| Compositor resize notification | PASS |
| Settings native runtime | 908/908; 100 lifecycle repetitions, 250 provider refresh cycles, 1,000 notification/state transitions |
| AMD64 memory foundation hosted regression | PASS; hosted assertions only, not CPL3 or hardware page-fault acceptance |
| AMD64 protected-task contract | PASS |
| Native ABI layout | PASS |
| Windows Makefile shell regression | PASS under Git sh.exe and cmd.exe |
| Release ramdisk/VFS mount | 9/9 |

The taskbar model output is preserved at out/validation/recovery2-taskbar-model.log. The clean-revision release ramdisk check used out/build-staging/2444a1a0-25c2-41b7-a699-c9c2e3337bba/ramdisk.img.

No test fixture was altered to imitate historical totals. The FAT32 test operates on a disposable copy under out/validation; the tracked assets/Disks/test-fat32.img retained its pre-test SHA-256 0D6F82E808F7F83ED8B7F5F04A8CDDECC8F65A81ACF724F59D14D1E148DE9A32.

## 6. QAC and preserved review ISO

QAC was not available through the configured tools or command path during this run. No QAC admission/session was obtained, no unmanaged VM was launched, and no QAC resources were created. Therefore the following remain unqualified:

- ISO-only UEFI desktop boot and taskbar interaction.
- Real hover preview, auto-hide/reveal, Start Menu and File Explorer interaction.
- AppID runtime/window registration and Native ELF grouping.
- Writable ATA/FAT32 settings or pin persistence across cold restart.
- Required-sync failure propagation and successful ACPI S5 after synchronization.

The preserved internal review ISO at E:\CodexArtifacts\TaskbarReview\guideXOS-Taskbar-Review-AMD64.iso remains unchanged. Its final SHA-256 is B5A638F134C949DDEDE9658596009DF4C716112F4D200FDE0CA3F4C86ADA2906, matching the archived value. No replacement ISO was generated in Recovery2; the new review build is the ServerV03PacManOnly ESP directory described above.

## 7. Mainline compatibility and INTEGRATION3 risks

The pinned committed integration target is aad1bce8a8e42890e9bfd49a2f397f04e19f3eef, not the historical d1b2b2ce… checkpoint. Recovery2 includes the three local commits then present at that target. A live upstream update was not available, and actual main has uncommitted work. Its dirty kernel/Makefile and kernel/core/desktop.cpp overlap candidate files; its bootloader, AMD64 boot, kernel entry, and ramdisk-integrity diagnostics are additional work absent from this branch. The complete main worktree snapshot is retained at E:\dev\TASKBAR_RECOVERY2_MAIN_WORKTREE_SNAPSHOT_20261126. These changes must be reconciled before proposing an integration target.

There were no textual conflicts in the automatic merge of the committed taskbar source. Semantic review remains necessary for the shared App Model, compositor, desktop/window registration, native ELF launch, ATA, FAT, VFS, shutdown, bootloader, and ramdisk ownership paths. Storage correctness is a dependency of taskbar persistence and shutdown. No merge into main or push occurred.

### 7.1 Candidate file inventory relative to pinned main

The Recovery2 branch changes these paths relative to aad1bce8:

- Hosted taskbar/UI: compositor.cpp, compositor.h, desktop_config.h, right_click_menu.cpp, right_click_menu.h.
- Historical taskbar records: docs/taskbar/TASKBAR1_CORE_APPLICATION_MANAGEMENT.md, TASKBAR2_NATIVE_TASKBAR_INTEGRATION.md, TASKBAR3_PREVIEWS_AND_VISUAL_ENHANCEMENTS.md, TASKBAR5_CUSTOMIZATION_AND_AUTOHIDE.md, TASKBAR6_NATIVE_QUALIFICATION_AND_POLISH.md, TASKBAR6R1_NATIVE_INTERACTION_AND_PERSISTENCE_ACCEPTANCE.md, TASKBAR6R2_NATIVE_ACCEPTANCE_AND_INTEGRATION_READINESS.md, TASKBAR6R3_ATA_FLUSH_RELIABILITY.md, TASKBAR6R4_ATA_DIAGNOSTIC_AND_PERSISTENCE_ACCEPTANCE.md, TASKBAR6R5_NATIVE_ATA_PERSISTENCE_QUALIFICATION.md, TASKBAR6R6_FAT_FILE_OPERATIONS_AND_CLEAN_SHUTDOWN.md, TASKBAR_RECOVERY1_STORAGE_RELOCATION.md.
- Recovery2 completion report: docs/taskbar/TASKBAR_RECOVERY2_INTEGRATION_RECONSTRUCTION.md.
- Project/build: guideXOSServer.vcxproj, guideXOSServer.vcxproj.filters, kernel/Makefile, kernel/makefile-platform.mk, scripts/bootstrap-mbedtls.ps1.
- Kernel storage and lifecycle: kernel/core/ata.cpp, kernel/core/desktop.cpp, kernel/core/fs_fat.cpp, kernel/core/kernel_app.cpp, kernel/core/kernel_apps.cpp, kernel/core/kernel_compositor.cpp, kernel/core/native_elf_baremetal.cpp, kernel/core/shell.cpp, kernel/core/vfs.cpp.
- Kernel interfaces: kernel/core/include/kernel/ata.h, kernel/core/include/kernel/ata_pio_flush.h, kernel/core/include/kernel/kernel_app.h, kernel/core/include/kernel/kernel_compositor.h, kernel/core/include/kernel/native_elf_baremetal.h, kernel/core/include/kernel/vfs.h.
- Taskbar models: taskbar_activity_core.h, taskbar_application_model.h, taskbar_application_model_core.h, taskbar_customization_core.h.
- Regression tools and tests: scripts/run-ata-pio-flush-test.ps1, scripts/run-fs-fat-persistence-test.ps1, scripts/run-taskbar-application-model-test.ps1, scripts/test-kernel-make-shells.ps1, tests/ata_pio_flush_test.cpp, tests/fs_fat_persistence_test.cpp, tests/taskbar_application_model_test.cpp.

NATIVE_DB_STORAGE is excluded. The current actual-main worktree modifications are not included in this inventory because they are uncommitted and were intentionally not copied into the candidate.

## 8. Acceptance gate matrix

| Gate | Status | Evidence / limitation |
|---|---|---|
| REC2-G1 — Repository baselines and source integrity verified | PASS | Exact HEADs, refs, dirty state, common base, source clone status, and bundle integrity recorded. Actual main dirty work was snapshotted. |
| REC2-G2 — Final bounded missing-object recovery completed | PASS | Local repos, bundle, refs, reflogs, object databases, and connected GitHub records checked; original candidate history remains unavailable. |
| REC2-G3 — Surviving source and qualified functionality inventoried | PASS | Section 3 and the new implementation/test record distinguish source presence from native evidence. |
| REC2-G4 — Isolated E: integration workspace established | PASS | Independent clone at E:\dev\guideXOSServer_TASKBAR_INTEGRATION_RECOVERY2, branch integration/taskbar-recovery2. |
| REC2-G5 — Required taskbar functionality restored | PARTIAL | Surviving presentation and persistence were retained; model/build checks pass. Native previews and desktop interaction need QAC. |
| REC2-G6 — Native AppID and application lifecycle reconciled | PARTIAL | Trusted exact-ID launch and binding were reconstructed, focused tests/build pass; native lifecycle and multi-window behavior are not guest-qualified. |
| REC2-G7 — FAT/ATA/VFS/shutdown correctness preserved | PARTIAL | FAT growth/rollback 82/82 and ATA flush 80/80; no writable guest restart or native shutdown proof. |
| REC2-G8 — Ramdisk and boot compatibility addressed | PARTIAL | Production ramdisk/root mount 9/9 and AMD64 build pass; UEFI boot, BootInfo alias, writable root, wallimg and desktop remain for QAC. |
| REC2-G9 — Windows build environment repaired or precisely characterized | PASS | Git sh/cmd regression passes; dependency bootstrap handles Windows long paths; pinned Mbed TLS installed and verified. |
| REC2-G10 — Hosted/native builds and relevant regressions | PASS | Full hosted build, production AMD64 profile, symbol audit, and focused regressions pass. No separate debug profile exists; normal kernel Makefile compiles diagnostic flags. |
| REC2-G11 — QAC native desktop acceptance | BLOCKED | QAC unavailable; no guest was launched outside QAC. |
| REC2-G12 — Writable persistence and clean shutdown | BLOCKED | Requires QAC-managed disposable writable FAT32 guest and cold-restart/shutdown evidence. |
| REC2-G13 — Existing review ISO preserved | PASS | SHA-256 matches archived value; no overwrite or replacement. |
| REC2-G14 — Mainline integration compatibility assessed | PARTIAL | Committed target and clean textual merge recorded; uncommitted main overlap and semantic/native review remain. |
| REC2-G15 — QAC/resource cleanup and Git delivery verified | PARTIAL | Local candidate commits are present and the branch worktree is clean; no QAC resources were created, so native cleanup evidence is pending with QAC. |

## 9. Delivery and next action

- Final candidate branch: integration/taskbar-recovery2
- Reconstructed source implementation commit: 4cc16acf69cdbf4756fe96510fe7504f9d8fba6e
- This report is committed after the source implementation commit. The exact branch HEAD is the report delivery commit containing this file; it is recorded in the final delivery response. Candidate worktree is clean after that commit; no push was performed.

The main checkout and TASKBAR_IMPROVEMENTS checkout remain untouched by candidate edits. Recovery1’s bundle, the preserved review ISO, all source and test images, and generated profile artifacts remain available. The only new external copy is the dated snapshot of actual main’s pre-existing dirty worktree.

**Next action:** obtain a QAC-managed admission and owner, then run bounded ISO-only desktop checks and a disposable writable-FAT32 persistence/cold-restart/shutdown profile against this exact candidate HEAD. Before INTEGRATION3, reconcile the ten snapshotted uncommitted main files and re-pin the intended mainline target; rerun compatibility review against that target. Do not merge or push as part of Recovery2.
