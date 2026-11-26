# S22R28R3R2 Kernel Entry and RAMDISK Integrity

**Outcome: `RAMDISK_ROOT_CAUSE_IDENTIFIED`**

**Classification: DIAGNOSTIC — NOT PUBLIC RELEASE**

The sector-zero change is caused by an address-translation collision, not by a demonstrated write into the RAMDISK allocation. The loader's low kernel virtual mapping covers the numeric address stored in `BootInfo::RamdiskBase`. After CR3 changes, using that physical address as a pointer reads zero-filled kernel BSS backing instead of the RAMDISK frames. Its FNV-1a64 is exactly the observed `0x7DA144B97D054B25`.

The fix preserves the BootInfo v3 physical-address contract and maps the complete RAMDISK at a separate kernel virtual alias, `0x0000400000000000`. The corrected R3 image passed offline verification but was **not guest-executed**. The one permitted QAC experiment was consumed by an earlier candidate built without the loader diagnostic properties. Therefore this report does not claim that the corrected image mounts FAT32 in a guest.

## Provenance and preservation

- Repository: `E:\dev\guideXOSServer`; branch `main`; pre-task HEAD `aad1bce8a8e42890e9bfd49a2f397f04e19f3eef`; worktree was clean at preflight.
- Instrumentation commit `d22debb09ca448e6f5e6145bab6dd1e07f0dad1d` is the direct parent of report commit `aad1bce8a8e42890e9bfd49a2f397f04e19f3eef` (`aad1bce8^ == d22debb0`).
- Corrected R3 source fingerprint (sorted changed-source paths and SHA-256 records): `c32b2d98b34b08d56183634c9f768a0693f68c49147b8694478227b0c13ca7ff`.
- Pinned dependencies were not restored or changed: Mbed TLS 4.1.0, revision `0fe989b6b514192783c469039edd325fd0989806`; TF-PSA Crypto 1.1.0, revision `29160dd877d29658279fd683b2ae57b320ddcf09`.
- Accepted images and their recorded SHA-256 identities were preserved:

| Preserved artifact | SHA-256 |
| --- | --- |
| S22R23 | `8d397659e0a6ec8f1be113ae4f5be14dbcaf1ef22ccda41a6e5ee5014919cf40` |
| S22R28R1 CPL3 on | `3168d943af704a730110ab1e669ab482c3e93f282bbd7d2f73283f490b2d785c` |
| S22R28R2 CPL3 off | `ee4ff561748ebb90f24bfe881fa225f8a373368567b1a402e390aa1eb3844907` |
| S22R28R3 CPL3 off control | `bb3ff91d7b06d5cb5105698254c9636465ab081a463c86f6bef89071bb7cbf10` |
| S22R28R3 CPL3 on | `abe47f2d61416bc6ea1052d364b71639637fc866507216fe923547e43c030431` |
| S22R28R3 FAT diagnostic | `e7f16b07a6886d09ab7f4114c3357d8a8d622917bbe5cc02380d5088f88d94e6` |
| S22R28R3 R1 stage-fingerprint diagnostic | `88635152c149bccb5495b3e691beb2fe6100fec25c1e4ea6f310f87c60103de9` |
| Shared accepted RAMDISK | `ead5105aa131dee575e0021fac1650a28330df422bd211ca6baccdd267179f05` |

The nine files under `assets/Backgrounds/NEW` were left in place. No accepted ISO, evidence package, pinned dependency, or current background was replaced. The ignored workspace ESP RAMDISK has a different identity from the accepted RAMDISK and was not used or modified.

## Fingerprint contract and address fields

Loader and kernel fingerprint the first 512 bytes of sector zero using the same shared helper and FNV-1a64 (offset basis `14695981039346656037`, prime `1099511628211`). The attached block device's LBA 0 reads that same sector at RAMDISK offset zero. BPB fields are read at their standard offsets in the same sector.

BootInfo v3 `RamdiskBase` is the EFI-allocated **physical** base; `RamdiskSize` is the exact file length in bytes. The loader rounds the allocation up to EFI pages for `AllocatePages(EfiLoaderData)`, reads the file into that allocation, and stores the physical base and exact length in BootInfo. The captured RAMDISK length is `0x04000000` (64 MiB). R1 and R2 runtime physical bases were respectively `0x150DB000` and `0x150D9000`. The historical source/loader handoff fingerprint `0x65EC0D6EC04D02C3` matches the accepted RAMDISK's sector-zero FNV.

The bug was a mismatch in address *meaning*: the loader wrote a physical address into BootInfo, while kernel consumers cast it directly to a virtual pointer after paging. The fingerprints alone did not establish physical overwrite; the mapping walk and ELF layout establish that the two stages read different backing frames.

## Initialization order and first disagreement

The relevant order is:

1. UEFI loader allocates and loads the kernel ELF. `LoadElf` clears the kernel's full `PT_LOAD` memory span, including BSS.
2. The loader allocates the RAMDISK and task-frame arena, reads the RAMDISK, and fills BootInfo v3 with physical base/length. The RAMDISK allocation is adjacent to, but does not overlap, the captured kernel physical allocation.
3. The loader builds identity mappings, then maps the low kernel virtual image with `paging::MapRange(kernelMinVaddr, kernelPhysBase, kernelSpanBytes)`. This mapping replaces identity translations wherever the low kernel VA range numerically overlaps physical-address values.
4. Page-table pages are identity-mapped. The loader switches from firmware CR3 `0x3F801000` to target CR3 `0x3DE48000` through the trampoline. R1/R2 CR4 was `0x668` (PCIDE clear); these mappings are not global.
5. `_start` selects the kernel BSS entry stack and calls the first C checkpoint. `kernel_main` validates BootInfo, initializes the bounded task frame owner, then proceeds through framebuffer/desktop setup and RAMDISK consumers. The C++ heap is lazy and its storage is kernel BSS.

The ELF memory layout proves the collision:

| Run | Kernel low VA range | RAMDISK physical base | Translation of that numeric VA after loader mapping | Backing |
| --- | --- | --- | --- | --- |
| R1 | `[0x00100000, 0x22B90A60)` | `0x150DB000` | `0x150DB000 -> 0x2E0B6000` | Kernel `PT_LOAD` BSS, cleared by `LoadElf` |
| R2 | `[0x00100000, 0x22B92A60)` | `0x150D9000` | `0x150D9000 -> 0x2E0B2000` | Kernel `PT_LOAD` BSS, cleared by `LoadElf` |

The R1 ELF's second `PT_LOAD` has VA `0x337000`, file size `0x12050`, memory size `0x22859A60`; the R2 kernel has the same BSS shape with a slightly larger linked end. In both runs the RAMDISK base lies in the mapped memory tail, beyond file-backed bytes. FNV-1a64 over 512 zero bytes is `0x7DA144B97D054B25`, exactly the R1 attach and successful LBA 0 block-read fingerprint. This establishes the loader's kernel mapping as the earliest responsible operation and the post-CR3 physical-as-virtual dereference as the first wrong read.

### Physical ownership and overwrite audit

- The kernel image was loaded and its BSS zeroed before the RAMDISK allocation. Captured physical ranges are adjacent, with the kernel starting at the end of the 64 MiB RAMDISK; no overlap was observed.
- The full physical RAMDISK range is reserved in the loader memory-map setup. Loader page tables use separate `EfiLoaderData` pages, zeroed before use. No post-load RAMDISK copy, clear, free, or reuse was found.
- The task frame arena observed in R2 was `0x3CE28000/0x01000000`, separate from `[0x150D9000,0x190D9000)`. The frame owner manages only this bounded task arena and its initialization rejects overlapping reserved ranges. CPL3 and task CR3 activation remain disabled.
- The R2 diagnostic's firmware reservation map could not be verified because its expected-CR3 input was absent; that result is recorded as map-unverified, not as proof of a reservation failure. Source review confirms the loader allocation type and reservation path.
- Thus no write to the original RAMDISK physical frames is demonstrated. The failure is the virtual translation at the physical-looking numeric address.

## Checkpoints and guest evidence

Instrumentation covers loader state before page-table construction, immediately before transfer, trampoline before CR3, earliest kernel entry after CR3, post-frame-owner initialization, and immediately before attachment. It records stage, physical base and exact length, VA, CR3, translated backing and page size/permissions where verifiable, full-range mapping status, sector fingerprint/BPB, firmware reservation, and task-arena relation. It skips reads when the CR3 root or mapping cannot be verified. It handles 4 KiB, 2 MiB, and 1 GiB page translations and checks complete range backing.

The single QAC run used ISO SHA-256 `58c544b1db592e691d6e3e43444ba94477cd6abc8e95ab4ad150071c6acf4451`. Its loader build omitted both required diagnostic properties, so expected loader checkpoints were absent. The guest nevertheless provided useful earlier evidence: actual CR3 `0x3DE48000`, RAMDISK `0x150D9000/0x04000000`, attach and LBA 0 FNV both `0x7DA144B97D054B25`, invalid zero bytes-per-sector, and FAT mount rejection. Kernel checkpoints safely reported `unverified-root-mismatch` and skipped mapping walks/fingerprints because the expected target CR3 was zero. Desktop and main loop ran.

QAC was healthy and unoccupied before the run. Reservation `ab23ceb58afd4e4ea2045c7e6c582b71` was held by owner PID `21848`; QEMU child PID `22972` ran with TCG and 1024 MiB and exited `0`. The reservation was released; final reservation, queue, QEMU, and foreign-QEMU counts were zero. Serial, QMP, execution JSON, and before/after QAC snapshots are preserved under `E:\CodexArtifacts\S22R28R3R2\kernel-entry-integrity-diagnostic\artifacts\guest-run-r2`. No second guest run was made. The corrected R3 candidate is offline-only.

## Correction

The loader now maps `[BootInfo::RamdiskBase, RamdiskBase + RamdiskSize)` at kernel VA `0x0000400000000000` after mapping the kernel image and before CR3 transition. The full alias is supervisor-only under the existing `MapRange` behavior, writable and executable; permissions were not changed. Kernel RAMDISK consumers use this alias, while BootInfo validation, physical reservations, ownership, and diagnostics retain the physical base. The alias is canonical and does not overlap the kernel's low image. No BootInfo v3 layout, FAT validation, filesystem behavior, CPL3 policy, or native ELF security policy was changed.

Regression cases reproduce the exact R1 and R2 collision formula and zero-sector fingerprint, then verify that the new alias resolves the full range to the intended physical backing. No speculative filesystem or memory-manager correction was added.

## Offline validation and diagnostic artifact

| Validation | Result |
| --- | --- |
| RAMDISK translation/fingerprint helper | PASS, 17 checks |
| AMD64 memory foundation | PASS: ownership, rollback, effective permissions, shared mappings, teardown |
| FAT mount diagnostics | PASS: 15 on, 7 off |
| Protected task contract | PASS, exit 0 |
| Native ELF security | PASS, zero failures |
| VFS mount table / hosted read | PASS, 19/19 and 4/4 |
| Accepted RAMDISK FAT/VFS | PASS, 9 checks on read-only accepted image |
| AMD64 freestanding diagnostic kernel | PASS, CPL3 disabled |
| Kernel symbol audit | PASS: 52 UND rows, 22 distinct `.symtab` entries, no imports or relocations |
| Stack guard | PASS, minimum guarded-path headroom 4,488 bytes |
| Diagnostic and default loader builds | PASS; diagnostic properties verified in loader, alias also compiles without instrumentation |
| ESP and ISO structure | PASS: 17 files; UEFI El Torito no-emulation, embedded ESP at LBA 32 |

Corrected candidate: `E:\CodexArtifacts\S22R28R3R2\kernel-entry-integrity-root-cause-fix-r3\guideXOS-S22R28R3R2-KERNEL-ENTRY-RAMDISK-INTEGRITY-ROOT-CAUSE-FIX-R3-NOT-PUBLIC-RELEASE.iso`.

| Component | SHA-256 |
| --- | --- |
| ISO, 90,245,120 bytes | `cc8370414cd422e3b459378961324ccd1a50e4b2dbb09ef456444cc77fd9860e` |
| Embedded ESP, 90,177,536 bytes | `98f5f1d3aa1eb0a34929ba5a60b31f4ebc5e0cf4bc406e283a4b65b3eb080806` |
| Loader | `2e7e9c9495b515a9852b688c04a53e6575c6bc9add85ab452c3c157f4744e9fa` |
| Kernel ELF | `c3459f4bb0cf2538e8725e3650b3d4d455831843f78cb6573f0dec8ffd6e5e9f` |
| Kernel PE | `b96946cdfb6c9ffd1ec31ebbe5a71a24a6be6a4e85d485ea7f5eeecb88c11388` |
| RAMDISK | `ead5105aa131dee575e0021fac1650a28330df422bd211ca6baccdd267179f05` |
| Artifact manifest | `1419dd2acf1bc5bd404fa61be87d29e7c4e29524a62c66d972e1031e4a4b5b04` |

The image is labeled **DIAGNOSTIC — NOT PUBLIC RELEASE**. It is not signed, published, or presented as a release. Accepted artifacts, release-readiness markers, and backgrounds remain untouched.

## Next step

At the next authorized QAC window, run the corrected R3 ISO once and verify that the loader and kernel checkpoints agree on RAMDISK physical base, alias backing, and the original sector-zero fingerprint through the CR3 transition and attachment. Then confirm successful FAT32 mount in the guest. Until that evidence exists, the root cause is identified and corrected offline, while guest confirmation of the correction and filesystem mount remains outstanding. Do not start S22R29 fault-recovery work in this phase.
