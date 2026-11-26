# S22R28R3 FAT Mount Guest Differential

Date: 2026-11-26 (Pacific)

**Outcome: useful CPL3-disabled control evidence; diagnostic ISO guest run pending separate authorization.** QAC was available and the preserved CPL3-disabled control ISO was run first under a verified acceptance reservation. The control reproduces the mount failure and narrows the first observed discrepancy to the bytes visible through the guest RAMDISK path. No root cause or code correction is established.

## Repository, dependencies, and artifact preflight

- Repository: `E:\dev\guideXOSServer`, branch `main`, starting HEAD `db367cdb9ece14dd3cd5892a7e3c24c38ce6f055` (`origin/main`). The starting worktree was clean.
- Pinned dependencies verified by `scripts/verify-mbedtls-profile.ps1`: Mbed TLS `0fe989b6b514192783c469039edd325fd0989806`; TF-PSA-Crypto `29160dd877d29658279fd683b2ae57b320ddcf09`; framework submodule `dff9da04438d712f7647fd995bc90fadd0c0e2ce`; ML-DSA native submodule `5772b4f4a0105694b1203abb582273f78fa951b7`. Profile verifier result: `MBEDTLS_PROFILE_VERIFY=PASS`.
- Complete ESP and ISO verification passed for the control, CPL3-on comparison package, and new diagnostic package. Each ESP verifier checked all 17 expected files; each ISO verifier confirmed UEFI El Torito boot metadata and its embedded ESP hash. Fresh verifier reports are in `E:\CodexArtifacts\S22R28R3\guest-differential\preflight\`.
- Firmware matched the earlier S22R28R1 QEMU identities: x86_64 code SHA-256 `33090cc07675baa5190d9f1e84bf5176b33bcbfa9bacac522961150cdb6dbb2a`; i386 vars template SHA-256 `5d2ac383371b408398accee7ec27c8c09ea5b74a0de0ceea6513388b15be5d1e`.
- Preserved ISO hashes still match their recorded identities: accepted S22R23 `8d397659e0a6ec8f1be113ae4f5be14dbcaf1ef22ccda41a6e5ee5014919cf40`; S22R28R1 CPL3-on `3168d943af704a730110ab1e669ab482c3e93f282bbd7d2f73283f490b2d785c`; S22R28R2 CPL3-off `ee4ff561748ebb90f24bfe881fa225f8a373368567b1a402e390aa1eb3844907`; S22R28R3 CPL3-off `bb3ff91d7b06d5cb5105698254c9636465ab081a463c86f6bef89071bb7cbf10`; S22R28R3 CPL3-on `abe47f2d61416bc6ea1052d364b71639637fc866507216fe923547e43c030431`; S22R28R3 FAT diagnostic `e7f16b07a6886d09ab7f4114c3357d8a8d622917bbe5cc02380d5088f88d94e6`. The shared 64 MiB RAMDISK remains SHA-256 `ead5105aa131dee575e0021fac1650a28330df422bd211ca6baccdd267179f05`.

| Candidate | Guest identity | Manifest configuration | Actual guest outcome |
| --- | --- | --- | --- |
| Preserved R3 control, `E:\CodexArtifacts\S22R28R3\control-cpl3-off\guideXOS-S22R28R3-FAT-DIAGNOSTIC-CONTROL-CPL3-OFF-NOT-PUBLIC-RELEASE.iso` | ISO SHA-256 `bb3ff91d7b06d5cb5105698254c9636465ab081a463c86f6bef89071bb7cbf10` | CPL3 off; BootInfo v3; same 64 MiB RAMDISK; existing read-only FAT trace | Booted; FAT root mount failed at the first BPB check; desktop remained running |
| New R3 Recovery2 diagnostic, `E:\CodexArtifacts\S22R28R3\recovery2-fat-mount-diagnostic\guideXOS-S22R28R3-FAT-MOUNT-DIAGNOSTIC-RECOVERY2-NOT-PUBLIC-RELEASE.iso` | ISO SHA-256 `e7f16b07a6886d09ab7f4114c3357d8a8d622917bbe5cc02380d5088f88d94e6` | CPL3 off; BootInfo v3; bounded `GXOS_FAT_MOUNT_DIAGNOSTICS`; same loader and RAMDISK | Not launched. Guest result pending separate authorization. |

The CPL3-on R3 comparison ISO was hash-verified but not run in this phase. The earlier S22R28R1 guest remains the established CPL3 lifecycle evidence: three successful task cycles, with the separate root FAT mount failure still unexplained.

## Control guest evidence

QAC coordinator 2.0.0 was Online before admission with zero reservations, managed sessions, queued requests, or foreign QEMU processes. QAC granted the exclusive `guideXOSServer` / `acceptance` reservation `3ab0a9b17e6849d29d79d9fbe23e726b` to verified owner `LEON2_RDT\guideX` (owner PID 17944). QAC launched QEMU PID 17144 (`C:\Program Files\qemu\qemu-system-x86_64.exe`) as its direct child. The QMP listener on `127.0.0.1:54207` was owned by PID 17144. The ISO was run read-only with the same x86_64 UEFI code/vars pair and 1 GiB TCG configuration used by the recorded R1 run.

The kernel accepted BootInfo v3: magic `0x49425847`, version `3`, size `0x23C`, flags `0xF`, valid RAMDISK flag and checksum. It reported RAMDISK base `0x150DA000`, size `0x04000000` (67,108,864 bytes), memory map `0x3CDE8000` with 109 entries of 48 bytes, reserved task arena `0x3CE28000` / `0x01000000`, and kernel CR3 `0x3DE48000`. Memory ownership initialized with all 4,096 arena pages free. The kernel detected the boot RAMDISK, attached read-only device `wallimg` as block device 1 with 131,072 sectors of 512 bytes, and attempted `mount_type("/", wallimg, FAT32)`.

The initial FAT read at LBA 0 returned block status `0x00` (success). The guest's sector-zero FNV-1a64 was `0x7DA144B97D054B25`; the guest reported signature `0x0000` and zero for bytes per sector, sectors per cluster, reserved sectors, FAT count, total-sector fields, FAT-size fields, and root cluster. The first rejection was `fat32.bytes-per-sector-below-512`, returning mount result `0xFF`.

The host source RAMDISK is independently manifest-verified and hashes to `ead5105aa131dee575e0021fac1650a28330df422bd211ca6baccdd267179f05`. Its sector zero SHA-256 is `2adc2b485efa25f1eda23bb2ce71d3615c6eb89e6a743d571da90fc1c7be6374`, FNV-1a64 is `0x65EC0D6EC04D02C3`, the signature is `0xAA55`, and the BPB reports 512 bytes/sector, 16 sectors/cluster, 32 reserved sectors, and two FATs. Thus the first guest read reports success but the bytes observed by the guest differ from sector zero of the verified source image. This is a source-to-guest discrepancy; the observation does not identify which preceding loader, memory-mapping, RAMDISK, or block-read layer is responsible.

VFS had no root mount after the rejection. `/Apps` was absent, the user-data mount was skipped because the trusted system source was unavailable, and no normal application launch was possible. The desktop was drawn and remained visible: the QMP screenshot at `desktop-control.png` shows the desktop, Trash and File Explorer icons, and taskbar. Serial output continued through the kernel main loop with PIT heartbeats beyond tick `0x044C`. This control has CPL3 disabled, so it did not exercise CPL3 task cycles.

## Earliest failure and disposition

The earliest demonstrated failure is that the guest RAMDISK sector-zero contents already differ from the verified host image when the mount path inspects them. The block read itself returned success, then FAT rejected the returned contents at its bytes-per-sector check. The loader log confirms size and base handoff, and BootInfo validation succeeds, but no checksum of the full RAMDISK or its first sector was recorded at the loader handoff. The responsible point between firmware file read, physical memory contents/mapping, RAMDISK attachment, and the first block read remains unresolved. The failure is shared by the CPL3-disabled control; this does not by itself establish whether CPL3 contributed to the separate CPL3-on run.

No filesystem behavior was changed and no correction is justified by this evidence. The smallest next experiment is a separately authorized QAC-managed boot of the already-verified Recovery2 diagnostic ISO, preserving the same read-only RAMDISK and UEFI setup. Capture its direct initial-read status, BPB observation, first rejection stage, and BootInfo range alongside this control. Do not infer a root cause from the current control trace alone.

## QAC cleanup and evidence locations

QMP `quit` was acknowledged. QEMU exited with code 0 at `2026-11-26T20:01:24.9301808Z`; QAC reported `Managed child exited.` and reservation `3ab0a9b17e6849d29d79d9fbe23e726b` inspected as `Released`. Final QAC status was Online with zero reservations, sessions, queued requests, blocked reservations, foreign QEMU processes, or QEMU processes. The exact child PID and QMP listener were absent after release. The control ISO and source RAMDISK hashes were unchanged after the run.

The preserved guest evidence is under `E:\CodexArtifacts\S22R28R3\guest-differential\control-cpl3-off\`: `guest.serial.log` (SHA-256 `439bb243a3f61374ac1f6e9a062911734b74ec852bb10054dea6c9bcc6d793a1`), `desktop-control.png`, `desktop-control.ppm`, `qmp-transcript.jsonl`, `qac-inspect-before-cont.json`, `qac-inspect-after-release.json`, QAC status/list/queue snapshots, the disposable OVMF vars copy, and sector-zero comparison script/output. The QEMU debug log was empty. The preserved ISO, source ESP, source RAMDISK, accepted artifacts, and earlier evidence were not modified.

No push, signing, publication, release promotion, readiness-marker update, or unmanaged QEMU launch occurred.
