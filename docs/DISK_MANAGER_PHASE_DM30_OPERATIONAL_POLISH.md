# DM30 — Disk Manager Operational Polish and GPT Repair Qualification

**Outcome B.** Current-source runtime repair passed in both authority directions on three-partition 512-byte AHCI media and three-partition 4Kn USB media. Data canaries, partition metadata, gaps, PMBR, authoritative copy, bounded metadata writes, independent image diffs, and cold restarts all passed. Fresh ATA/AHCI/USB, read-only mount, stress, hotplug, FAT32, and storage-suite regressions passed. Outcome B remains because the graphical Disk Manager confirmation/cancel/confirm flow was not clicked through; headless QEMU exercised the production repair service and action model, but cannot establish visual dialog behavior.

## Phase gate and starting state

- Repository: `D:\dev\guideXOSServer_DiskManagerImprovements`
- Project: `guideXOS_SERVER_DISK_MANAGER_IMPROVEMENTS`
- Branch: `DISK_MANAGER_IMPROVEMENTS`
- Starting HEAD: `386540df88b69c50dda0f3c430128546a6685f07`
- Starting `.phase`: `last_completed=DM29`, `next_expected=DM30`
- No prior DM30 report or completion was present in local history or the implementation status.
- The pre-existing `.gitignore` modification and untracked `kernel/out/` were preserved and excluded from the DM30 commit. Existing ignored/untracked `out/` evidence was preserved; DM30 evidence was added under `out/`.
- Cached upstream divergence was `0 / 0`. `git fetch` failed with `git@github.com: Permission denied (publickey). Fatal: Could not read from remote repository.` Remote freshness is unknown; no remote or credentials were changed.
- Existing full-build failure was reproduced before source edits and again after them. Both runs stopped at the same unrelated PacMan undefined symbols: `pacman_audio_load_resources(gx_app_context*)` and `pacman_audio_submit(void*, PacManSoundId)`. PacMan code and project settings were not changed.

## Runtime GPT repair

`scripts/create-dm30-gpt-fixture.py` creates raw, unformatted partition data with three active GPT entries and three SHA-256 canaries per entry. The corruption helper flips one name byte in the first active entry; at 512 bytes, that array sector also contains the other two active entries, and at 4Kn it contains all three. Each fixture also has two explicit 2,048-sector gaps between partitions. Entry-array layout is 128 entries × 128 bytes: 32 logical sectors at 512 bytes and four logical sectors at 4Kn.

| Entry | Type GUID | Unique GUID | 512-byte LBA range | 4Kn LBA range | Attributes |
|---|---|---|---:|---:|---:|
| 1 — DM30 Partition A | `ebd0a0a2-b9e5-4433-87c0-68b6b72699c7` | `d0300001-0000-4000-8000-000000000001` | 2048–131071 | 256–16639 | `0x1` |
| 2 — DM30 Partition B | same | `d0300002-0000-4000-8000-000000000002` | 133120–262143 | 16896–33279 | `0x5` |
| 3 — DM30 Partition C | same | `d0300003-0000-4000-8000-000000000003` | 264192–393215 | 33536–49919 | `0x1000000000000002` |

Each canary is at the first, middle, and last LBA of its partition. The fixtures are raw-canary data rather than formatted filesystems; the separate DM24 regression qualifies 4Kn FAT32. The host verifier records type GUID, unique GUID, start/end, attributes, name, disk GUID, entry-array CRC, and PMBR hash.

### Corruption and firmware handling

The runner independently creates the damaged reference image and validates its single degraded GPT copy. In initial trials, OVMF rewrote the pre-damaged copy before the kernel parser ran. To exercise the actual guest service, the final runner pauses QEMU at the kernel's pre-controller-registration corruption window, injects the already-verified array damage into the disposable live image, and resumes before AHCI/USB registration. Thus the GPT repair itself is performed by guideXOS against the live QEMU block device. The backing image is repository-owned; no host physical disk or USB device is passed through.

### Direction and write results

Both directions passed for both sector sizes. The production service wrote the complete damaged entry-array sectors first, verified the array, then wrote the damaged header last and flushed. It revalidated the exact target/fingerprint, checked both copies, checked the protective MBR, and reparsed a healthy GPT before reporting success.

| Runtime proof | Authoritative copy | Damaged copy repaired | Array + header writes | Bytes | Flushes | Restart |
|---|---|---|---:|---:|---:|---|
| AHCI, 512-byte sectors | Primary | Backup | 32 + 1 sectors; 33 total | 16,896 | 2 | Healthy |
| AHCI, 512-byte sectors | Backup | Primary | 32 + 1 sectors; 33 total | 16,896 | 2 | Healthy |
| USB Mass Storage, 4Kn | Primary | Backup | 4 + 1 sectors; 5 total | 20,480 | 2 | Healthy |
| USB Mass Storage, 4Kn | Backup | Primary | 4 + 1 sectors; 5 total | 20,480 | 2 | Healthy |

The USB device reported 4096-byte logical blocks. Its repair log confirmed `zero512ByteWrites=yes`; write calls were four complete 4 KiB array sectors plus one complete 4 KiB header sector. The AHCI trace likewise recorded only the full 32-sector array and single-sector header writes per repair. Elapsed repair time was not instrumented.

For every direction, the independent before/after verifier found changes only in the deliberately damaged entry-array logical sector; the reconstructed header bytes matched the valid counterpart. It confirmed normalized GPT copies match, all three entries and GUIDs match, the authoritative copy and full logical-sector LBA0/PMBR remain unchanged, all nine canaries remain unchanged, and the gap map is unchanged. The 512-byte fixture PMBR SHA-256 is `e6134c448cf2064b5ca342c3164452aaaa17d846ff3e9275704c2e14eb4c889b`; the complete 4Kn LBA0 SHA-256 is `7abec1d7a5d36b502af3946635dd8ca8c52f20f6774bc54160937d45f7a45369`.

The report runner captured three canaries per partition before repair and re-read them afterward. Cold restart after each repair parsed `GPT Healthy` with three entries and all canaries intact. No filesystem was mounted or written during the whole-image differential capture.

Evidence:

- `out/dm30-qemu-ahci512-final/manifest.json`
- `out/dm30-qemu-ahci512-final/backup-repair-verification.json` and `primary-repair-verification.json`
- `out/dm30-qemu-ahci512-final/backup-repair-serial-capture.txt` and `primary-repair-serial-capture.txt`
- `out/dm30-qemu-usb4kn-final/manifest.json`
- `out/dm30-qemu-usb4kn-final/backup-repair-verification.json` and `primary-repair-verification.json`
- `out/dm30-qemu-usb4kn-final/backup-repair-serial-capture.txt` and `primary-repair-serial-capture.txt`
- Fixture descriptions and all canary hashes: `out/dm30-qemu-ahci512-final/fixture.json` and `out/dm30-qemu-usb4kn-final/fixture.json`

DM30 did not perform a real interruption during GPT repair. The existing deterministic storage suite retains interruption/retry coverage for the forward-only array-then-header model. DM14 did exercise removable-media data-write and Sync Cache interruption; that is not claimed as a GPT-repair interruption.

## Disk Manager action and policy audit

The Disk Manager now uses `disk_manager_gpt_repair_action_visible` and `disk_manager_gpt_repair_action_enabled` for the contextual action. The model tests cover both degraded authority directions, hide Repair for healthy/conflict/unrecoverable states, and keep the action disabled when read-only or mounted preflight is unavailable. A closed dialog and selected disk are required. Diagnostics retain the individual primary/backup states and actionable blocker.

The confirmation entry path prepares an identity-bound repair plan and displays the confirmation only after preflight. Cancel calls `cancel_gpt_repair`; a new fake-device test verifies the lease is released, no write attempt or Flush occurs, and parser state stays degraded. QEMU invokes the same `prepare_gpt_repair` and `execute_gpt_repair` production service for all four runtime repairs and confirms the post-repair parser state is Healthy.

| Operation on degraded GPT | Policy found in current source |
|---|---|
| Initialize Disk | Raw/Not Initialized only. A degraded GPT is not reinitialized implicitly. |
| Create Partition | Rejected with `CREATE_PARTITION_GPT_DEGRADED`; both copies must be valid and agree. |
| Delete Partition | Rejected with `DELETE_PARTITION_GPT_DEGRADED`; both copies must be valid and agree. |
| Format FAT32 | Rejected; formatting requires a healthy agreeing GPT pair and exact partition identity. |
| Quick Reformat | Same healthy-pair and identity gate as FAT32 formatting. |
| Mount | VFS accepts valid MBR or healthy valid GPT only; degraded GPT can be inspected from its sole valid side but cannot be mounted. |
| Repair GPT | Explicitly allowed only with one valid authority and a repairable peer, writable durable media, trusted Flush where required, all partitions unmounted, not root backing, and boot provenance definitely not the target. No side is selected automatically in conflict. |

`scanDisks()` preserves the exact disk incarnation and, when a partition is selected during a rescan, restores the selection only when `disk_manager_same_partition` finds that identity again. Repair GPT is contextual to the selected disk, so actual UI retention of a simultaneously selected partition through repair was not exercised.

The model/action helpers, confirmation lifecycle, diagnostic code, and rescan identity logic were source-audited and tested. QEMU was run headless (`-display none`); the available CUA interface does not control the native QEMU window. No graphical claim is made for opening Disk Manager, viewing the confirmation, clicking Cancel/Confirm, or watching the button disappear after refresh. This is the remaining qualification gap and sets Outcome B.

## Regression and build matrix

| Regression | Result and evidence |
|---|---|
| Storage Manager suite | **978 checks, 0 failures** (975 DM29 baseline preserved); USB subtests: 249. `out/dm30-evidence/storage-manager-final.log` |
| GPT verifier tests | Pass. `out/dm30-evidence/gpt-verifier-final.log` |
| Three-entry fixture test | Pass for 512-byte and 4Kn fixture metadata/canaries. `out/dm30-evidence/fixture-test-final.log` |
| USB trace classifier | 7/7 pass. `out/dm30-evidence/usb-classifier-final.log` |
| DM25 Delete Partition | Pass with restart and independent image verification. `out/dm30-regression-dm25-delete2/` |
| DM27 Quick Reformat | Pass; same partition identity, FAT32 verification, and byte-identical cold restart. `out/dm30-regression-dm27-qrf/` |
| DM22 10 GiB FAT32 | Pass full lifecycle/restart and independent verification. `out/dm30-regression-dm22-10g/` |
| DM24 4Kn FAT32 | Pass current-source initialize/create/format/mount/read/write/unmount/remount/restart verification over 4096-byte USB blocks. `out/dm30-regression-dm24-4kn-fat32/` |
| Fresh ATA lifecycle | Pass Initialize → Create → Format → Mount → write/read → Unmount → rediscovery/restart verifier. `out/dm30-regression-dm9-ata/` |
| Fresh AHCI baseline | Pass ordinary AHCI lifecycle and restart verifier. `out/dm30-regression-dm15-ahci/` |
| Writable USB lifecycle | Pass current-source lifecycle, Sync Cache durability, ten mount/write/read/unmount cycles, restart, and independent image verification. `out/dm30-regression-dm13-usb-lifecycle/` |
| USB 100-cycle stress | Pass all 100 write → Sync Cache → readback → restore cycles; final source-image hash restored. `out/dm30-regression-dm13-usb-100-cycle/` |
| USB private read-only gate | Pass reads, writes blocked, and backing hash unchanged. `out/dm30-regression-dm13-usb-readonly/` |
| USB read-only FAT32 mount | Pass current-source read-only registration with no write callback, FAT32 read-only mount, directory/file read, unmount, and identical image SHA-256. `out/dm30-regression-dm12-usb-readonly-mount-final/` |
| USB hotplug | Pass removal while idle/open handles, stale-handle safety, same-media reinsertion, different-media replacement, interrupted data-out, and Sync Cache removal. `out/dm30-regression-dm14-usb-hotplug-qualified/` |

The hotplug proof intentionally changes media B and removes it during a multi-TD data-out. Its final filesystem contents are not promised consistent. Media A passes the strict verifier. Media B passes the structure verifier with only payload bytes unchecked and the known stress LBA 90000 exempted; GPT CRCs, FAT metadata/mirror, directory/file shape, partition/gaps, and other zero-data checks pass. Captured result: `out/dm30-evidence/hotplug-media-b-post-interruption-metadata-verification.log`.

NVMe remains safely read-only in production: `NVME_WRITE_PROVEN=0`, `NVME_FLUSH_PROVEN=0`, no shared write/Flush callbacks. Storage tests confirm reads remain available and destructive preflight is rejected.

Builds:

- Current normal AMD64 production kernel: pass; `out/dm30-evidence/kernel-amd64-production-build-final.log`.
- DM30 512-byte AHCI and 4Kn USB GPT-repair proof kernels: pass; retained per-run `kernel-build.log` files.
- DM12 read-only mount proof kernel and DM14 hotplug proof kernel: pass; `out/dm30-evidence/dm12-readonly-mount-kernel-build.log` and `out/dm30-evidence/dm14-hotplug-kernel-build.log`.
- UEFI x64 Release loader build: pass as part of each GPT proof runner; see the corresponding `uefi-release-build.log`.
- Normal full repository `build.ps1`: fails at the same two unrelated PacMan undefined references both before and after DM30. Evidence: `out/dm30-evidence/full-build-baseline.log` and `out/dm30-evidence/full-build-final.log`. No new storage-owned build failure was found.

## Source changes and closeout

DM30 adds model-level action visibility/enabled gates and their tests, a zero-write cancellation test, a reusable three-partition fixture and verifier test, a live QEMU GPT repair runner, 4Kn array corruption support, and proof-mode checks/write traces. The hotplug image verifier can now validate post-interruption metadata while explicitly exempting the single known dirty stress LBA. No GPT architecture, PacMan, or NVMe production write behavior was changed.

`.phase` advances to `last_completed=DM30`, `next_expected=DM31`. The DM30 commit is kept on `DISK_MANAGER_IMPROVEMENTS`; `.gitignore`, `kernel/out/`, and `out/` are excluded. Push was attempted; SSH public-key authentication failed, so the branch was not pushed and upstream freshness remains unknown.

## DM31 recommendation

Continue with **Disk Manager UX and Operational Polish**: click-through confirmation/cancellation/refresh qualification when graphical automation is available, then progress feedback, nonblocking long operations, keyboard/focus behavior, and clearer unsupported-state messaging. Do not add partition wipe/full format in this phase.
