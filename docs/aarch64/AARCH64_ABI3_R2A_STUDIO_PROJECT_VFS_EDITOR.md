# AArch64 ABI3 R2A Developer Studio Project and Source Editing

- **Outcome:** `STUDIO_ARM64_PROJECT_EDITOR_VERIFIED` — three fresh QAC-authorized R2A workflow records completed the project, edit, save, reopen, cleanup, and desktop-redraw gates. Three additional authorized attempts are preserved as partial.
- **Qualification date:** 2026-11-07.
- **Repository base:** `AARCH64_SUPPORT` at `cf21942b702e3e0f57355ba6df2b6d40e7e8f261`.
- **Developer Studio:** `058c83fe1ae270a103eadc7caca961328e8abe2a`.
- **v0.5 SDK:** `f63a7710a30b49f785aac134abf06a3a6c5b5aca`.
- **R1e evidence:** preserved at `E:\CodexArtifacts\guideXOSServer_AARCH64\aarch64-abi3-r1e-studio-redraw-r12-build-20261107`; its candidate and three acceptance records were not edited.

R2A proves authentic guest project management and source editing through Developer Studio and the real guest VFS. It does not claim that the source compiled or that the project executable ran. Those gates remain R2B.

## Root cause and storage choice

Studio's pinned project service defines `guidexos.project` as its authoritative version 1 project metadata file. Project open constructs and searches for that exact logical name; the parser and JSON payload do not derive their format from the host filesystem. The project contract also creates `CMakeLists.txt`, `app/app.json`, and `src/freestanding_memory.cpp`. Debugger workspace persistence uses `guidexos.debugger.json` and `guidexos.debugger.json.bak`. The pinned Studio source and tests use those names directly; substituting a different Studio filename would break existing projects and the project's open/build contract.

guideXOS FAT creation remains limited to short 8.3 names. The R1e guest trace showed the first create failure for `/Apps/R2ATST/guidexos.project` as `FAT_FILE_WRITE_INVALID_NAME` (`VFS_ERR_INVALID`). Later authentic project attempts exposed the same constraint for `app.json` and the debugger JSON files. The VFS does not provide an integrated long filename create/read contract for these paths. R2A therefore keeps FAT validation unchanged and maps only the pinned Studio logical basenames at the ABI3 adapter boundary:

The production path is `developer_studio_projects.cpp`'s `kProjectFileName` → `CreateProject`/`writeTrackedFile` → the pinned Studio v0.5 bare-metal file callback → `aarch64_studio_v05_abi_adapter.cpp` → guideXOS VFS result mapping in `vfs.cpp` → FAT's `make_short_name` validation in `fs_fat.cpp`. `LoadProject` reconstructs `guidexos.project` for stat/read, and the compiler service independently reads the same logical metadata name. The metadata is bounded JSON; its syntax and contents do not depend on the `.project` extension, but Studio's v1 project/open contract does require the exact logical filename. No alternate native project metadata filename is defined in the pinned source. The `/Apps` workspace and project subdirectories were writable in the guest; the passing runs persisted their complete generated project through the real VFS.

| Studio logical name | Physical FAT 8.3 name |
| --- | --- |
| `guidexos.project` | `GUIDEXOS.PJT` |
| `CMakeLists.txt` | `CMAKELIS.TXT` |
| `app.json` | `APP.JSN` |
| `freestanding_memory.cpp` | `FREESTA.CPP` |
| `guidexos.debugger.json` | `GUIDEDBG.JSN` |
| `guidexos.debugger.json.bak` | `GUIDEDB.BAK` |

Metadata and source bytes are unchanged. The adapter uses the canonical logical name when it already exists, falls back to the reserved physical alias when canonical metadata is absent, and fails closed if both names exist. It maps writes, reads, stats, removes, and directory listings symmetrically. Listings expose the logical Studio names. Raw alias paths, malformed paths, traversal segments, control characters, backslashes, colons, and overlong paths are rejected. VFS errors are forwarded; the adapter does not truncate names or add FAT LFN entries.

The guest workspace was the disposable RAM-backed `/Apps/R2ATST` project directory on the candidate's FAT image. It was not a host project directory or physical disk. The alias mapping preserves ordinary pre-provisioned long-name projects and the Phase 12 fixture when the canonical names already exist.

## Native guest workflow

The R2A QAC driver uses Studio's actual **File → New Project** action, enters `/Apps` and `R2ATST`, and waits for Studio's create, open, and metadata-parse markers. Studio's project state changes to the active project; it is not a pre-provisioned editor fixture.

The passing guest log records the real VFS creates and subsequent reads:

```text
write /Apps/R2ATST/GUIDEXOS.PJT       465 bytes
write /Apps/R2ATST/CMAKELIS.TXT       4460 bytes
write /Apps/R2ATST/build.ps1          6567 bytes
write /Apps/R2ATST/README.md          1160 bytes
write /Apps/R2ATST/app/APP.JSN        756 bytes
write /Apps/R2ATST/src/main.cpp       83 bytes
write /Apps/R2ATST/src/FREESTA.CPP    966 bytes
read  /Apps/R2ATST/GUIDEXOS.PJT       465 bytes
read  /Apps/R2ATST/app/APP.JSN        756 bytes
project_create=PASS
project_open=PASS
project_metadata_parse=PASS
```

Studio's normal new-file workflow created `src/r2a.c` and activated its document. The QMP helper delivered 109 key-down and 109 key-up events in the passing guest record. The event trace contains the source text once, followed by a backspace that removed the extra final `x`. The editor screenshot `r2a-editor-buffer.png` was visually reviewed and shows the edited C source in the active `r2a.c` editor. The guest VFS write is independent evidence that the document buffer held exactly these 37 ASCII bytes:

```c
int gx_main(void* ctx) { return 42; }
```

The exact write and both later reads are recorded as:

```text
write /Apps/R2ATST/src/r2a.c bytes=37
      datahex=696e742067785f6d61696e28766f69642a2063747829207b2072657475726e2034323b207d
read  /Apps/R2ATST/src/R2A.C bytes=37 [after closing and reopening the document]
read  /Apps/R2ATST/SRC/R2A.C bytes=37 [after closing and reopening the project]
```

The driver waits for Studio's `document_save=PASS`, `document_close=PASS`, and `document_open=PASS` markers. It compares the exact source hex on each VFS readback. It then closes the project, reopens it in the same guest session, reparses the project metadata, opens the source again, and compares the second readback. I visually reviewed `r2a-project-open-prompt.png` from the R7 run; the Explorer reads “No workspace open” after the close action. The following `r2a-project-reopened.png` shows Studio's Open Project dialog with `/Apps/R2ATST`, followed by the second `project_open=PASS` and `project_metadata_parse=PASS` guest markers. The reopened editor screenshot shows `R2A.C` active with the source rendered. This is guest-RAM persistence within one boot; it is not a claim of persistence across boots.

Project close also wrote and reopened the debugger workspace state through `/Apps/R2ATST/GUIDEDB.BAK` and `/Apps/R2ATST/GUIDEDBG.JSN`. Studio then closed through its normal exit action. The passing guest records `clean_close=PASS`, `NativeElf GUI cleanup: PASS mappings=0 windows=0 widgets=0`, and `unexpected-irq=0 exceptions=0`; the QAC reservation was released and no QEMU process remained.

## QAC records

The R2A-specific workflow records below mark every R2A transition true, including the Phase 12 durability marker, GUI resource release, zero unexpected IRQs/exceptions, and desktop redraw. Each run was a fresh QAC-exclusive session. The first two runs used candidate manifest `24047be0…`; the third used `1ce0c398…`. The guest artifacts are byte-identical in all three: kernel, EFI, Studio ELF, and RAM disk hashes match. The manifest digest differs because the host-side R2A workflow driver was refined between attempts; it does not represent a guest binary change.

| Fresh boot | QAC session / reservation | R2A outcome | Source readback | QAC cleanup |
| --- | --- | --- | --- | --- |
| 1 | `abi3-guest-20261107-135228-375` / `0320ff511aa4471288c9485eb7664be5` | Complete; project/edit/save/reopen/close transitions all true | Exact 37-byte match after document and project reopen | Exclusive grant, reservation released, QEMU count 0 |
| 2 | `abi3-guest-20261107-135408-942` / `a220f3fbee4f46518e9f7a65622413e8` | Complete; all transitions true | Exact 37-byte match after document and project reopen | Exclusive grant, reservation released, QEMU count 0 |
| 3 | `abi3-guest-20261107-141219-501` / `ee79ffe60b004fd4ac1956464e2e007f` | Complete; all transitions true | Exact 37-byte match after document and project reopen | Exclusive grant, reservation released, QEMU count 0 |

The preserved run folders are below `out\aarch64-phase12-r2a-production-r6-20261107` and `out\aarch64-phase12-r2a-production-r7-20261107`. Each contains `r2a-workflow.json`, QAC and guest logs, screenshots, and the generic QAC session record. The generic R1e runner still labels these sessions `ABI3_STUDIO_GUEST_PARTIAL` and exits with code 1 because its older acceptance fields expect the R1e Open Project path text, close event, and redraw sequence. The R2A workflow record separately verifies project/source operations, `clean_close=PASS`, desktop capture, cleanup counters, QAC release, and zero remaining QEMU processes. Both labels and logs are retained; the generic runner status is not rewritten.

Three other QAC-authorized attempts are preserved and do not count toward the three complete R2A workflows:

- Session `abi3-guest-20261107-135554-871` stopped at project creation. Its guest log shows `/Apps/R2ATST/GUIDEXOS.PJT` written into parent-directory slot offset `0x40`, followed by `/Apps/R2ATST/app/APP.JSN` failing with `FAT_FILE_WRITE_NOT_FOUND` / `VFS_ERR_NOT_FOUND`. This indicates that an earlier `app` directory entry was absent or overwritten in that run. The QAC runner released the reservation and left no QEMU process. This intermittent directory-entry behavior remains a filesystem risk; the adapter does not modify FAT directory allocation.
- Session `abi3-guest-20261107-140635-554` timed out in the R2A driver waiting for the first document-close marker. The guest log later contains `document_close=PASS`, `clean_close=PASS`, zero unexpected IRQs, and zero exceptions; the run was kept partial because the driver did not observe the marker within its configured wait. QAC was released and no QEMU process remained.
- Session `abi3-guest-20261107-145531-622` used the final R7 manifest and completed the entire project/edit/save/document-reopen/project-reopen/readback/clean-close sequence. After close, the Phase12 scheduler/VFS durability gate reported `fs-failures=1` and `scheduler/VFS integration: FAIL`, then `AARCH64_PHASE12_ERROR`. GUI mappings/windows/widgets reached zero and QAC released the child, but the required Phase12 pass, zero-IRQ/exception summary, and desktop-redraw gates were absent. This run therefore remains partial. The single filesystem workload failure is not localized further by this log.

These partial attempts are evidence of incomplete attempts, not additional qualifying boots. They are included to make the repeatability record complete.

## Candidate provenance

The production candidate is `out\aarch64-phase12-r2a-production-r7-20261107`. It uses the pinned Studio source and SDK without modifying their checkouts. Its candidate manifest records the copied build-driver/compile compatibility overlay and the server worktree hashes. No ABI table, layout, or ELF validation was weakened.

| Artifact | SHA-256 |
| --- | --- |
| R7 candidate manifest | `1ce0c398828bb234636fe5e0ba9a9f09c2d068d2fe5e075ac19a8689278b936c` |
| Studio ELF | `a08fc9c4599475165874a983b7d11366dcac2f35f5dc377ad6e875a960f2e483` |
| Studio package manifest | `62af7dce96baa5605092bb561e6c4bfe5428367bff8bbb229d4f01d41691ad4c` |
| ARM64 kernel ELF | `2323a07bd292b295cfc2a6170135de5ada900b9d43634e173c3bf67482437ccf` |
| AArch64 EFI loader | `1605c55a4d7c0c7b0589bd181e28d2e32f7ef9da31a6aeb23695c237ff7bfe42` |
| RAM disk | `b2252465b2b4d72d64cbdb9b3e74e9ae485922ef30c0fe0fca5181ebf6ef0c8e` |

## Hosted and regression results

| Gate | Result | Evidence |
| --- | --- | --- |
| ABI3 adapter, bounds, mapping, collision, errors, short writes, alias/path rejection, listing translation | PASS, 175/175 | `scripts/test-aarch64-studio-v05-adapter.ps1`; isolated output `out\aarch64-studio-v05-adapter-host-r2a-final-20261107` |
| ABI1 layout and unsafe v0.5 activation rejection | PASS | `scripts/test-aarch64-phase12-sdk-alignment.ps1`; isolated output `out\phase12-sdk-alignment-r2a-20261107`; distinct 400-byte and 464-byte layouts, incompatible consumer rejected, document activation 9/9 |
| ARM64 kernel/EFI, pinned Studio native ELF, package and Phase 12 staging | PASS | R7 production manifest above |
| Phase 10 architecture/package resolver | Build/host controls PASS; guest 2/3 PASS, third boot failed final cleanup count (`cleanup=24`, expected 26) | Isolated `out\aarch64-phase10-r2a-20261107` |
| Phase 11 compiler/package regression | Build PASS; guest regression failed on first boot at `NativeElf load rejected: fixed-address-contract`, then `AARCH64_PHASE11_ERROR` | Isolated `out\aarch64-phase11-r2a-20261107` |
| Phase 12 production integration regression | Build/package PASS; first guest reached Studio build/run proof but ended at the same `fixed-address-contract` rejection and `AARCH64_PHASE12_ERROR`. A later R2A workflow guest completed Studio close but failed the Phase12 scheduler/VFS integration gate with one filesystem workload failure. | Isolated `out\aarch64-phase12-r2a-20261107`; R2A run `abi3-guest-20261107-145531-622` |
| Phase 3 scheduler guest regression | Build and host contract PASS; boot 1 reached cooperative scheduling but timed out before the complete timer-preemption proof | Isolated `out\aarch64-phase3-r2a-20261107` |
| Phase 4 allocator/VFS guest regression | PASS, 3/3 fresh boots; each recorded 10,000 timer preemptions, the repeated VFS workload, allocator/heap durability, and zero IRQs/exceptions | Isolated `out\aarch64-phase4-r2a-20261107` |
| Captured firmware DTB parser regression | PASS, 51 normal Phase 2 host assertions and 52 with the preserved 1,052,672-byte firmware source DTB; parser reached COMPLETE / FDT_END | Isolated `out\aarch64-phase2-r2a-20261107`; fixture hash `5aec32b925cb7259d651aca355106e58d77f7f1c553d80f0420c5b3e3036f311` |
| BOOTDTB2 guest regression | Parser reached `DTB: OK`, GICv2/timer setup and scheduler entry, then hit the 120-second bound before Phase4 pass; QAC released reservation `1e0b8b1024e446aa88c4973e4f073982`, child exit 0 | Preserved candidate `out\aarch64-bootdtb2-repaired-20261102`, run `bootdtb2-guest-20261107-145040-236` |
| GICv3 hosted regression | PASS, 62 assertions; the hosted test started no QEMU | Isolated `out\aarch64-gicv3-host-r2a-20261107` |

An initial attempt to run Phase10 against its default existing `out\aarch64-phase10` directory overwrote `edk2-aarch64-phase10-vars-1.fd` and `logs\phase10-boot-1.log` before it was stopped. The original contents of those two files could not be reconstructed. All later Phase10–12, Phase2, Phase3, and Phase4 regression runs used fresh output directories. The remaining contents of the default Phase10 directory and all R1e artifacts were left in place.

The first BOOTDTB2 QAC invocation mistakenly supplied the Studio production candidate to a runner that does not add `ramfb`; the guest stopped at `[A64 UEFI] ERROR: UEFI GOP not found` before DTB handoff or capture. QAC released reservation `12d757f2bc1b4bc3af05f8f78043fa83`, and no candidate files were changed. The actual BOOTDTB2 regression above used the preserved minimal Phase4 candidate intended for that runner.

The production kernel SHA-256 differs from the earlier R1e kernel because the ABI3 storage mapping is production code. The R1e candidate, its RAM disk and manifest, and the three R1e QAC records remain unmodified.

## Remaining R2B work

R2A does not qualify Studio's full Compile/Build or executable Run path. R2B must prove the project source list is compiled by the intended guest compiler, that a valid ARM64 executable is packaged with the correct identity/machine, and that Studio's normal Run action loads and executes that artifact, with failure recovery and cleanup. The Phase11 and Phase12 regression failures above also need separate triage before treating those broader baseline suites as green.
