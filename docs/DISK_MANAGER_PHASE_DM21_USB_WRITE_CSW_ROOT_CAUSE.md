# Disk Manager Phase DM21 — USB WRITE(10) CSW Capture and Root Cause

## Result status

**Outcome B — the historical WRITE(10) → CSW failure was not reproduced in ten final-source lifecycle passes or 10,000 BOT commands.** Automatic tracing and failure diagnostics are now enabled for qualification runs, and the requested current-source USB, hotplug, ATA, and AHCI gates passed. The DM20 failure itself remains unclassified: the missing run-specific QEMU trace, WRITE(10) data accounting, endpoint toggle state, CSW buffer, and command history cannot be reconstructed. No historical root cause or repair is claimed.

## Starting repository state

- Repository: `D:\dev\guideXOSServer_DiskManagerImprovements`
- Branch: `DISK_MANAGER_IMPROVEMENTS`
- Starting HEAD: `b8b131c1595e71643293b29fca269f05698a2b74` (`DM20: classify USB CSW timeout`)
- Tracked worktree was clean before DM21 changes; all pre-existing `out/` evidence is preserved and remains untracked.
- The cached ahead/behind count was recorded before editing. `git fetch origin` failed with `git@github.com: Permission denied (publickey)`, so remote freshness is unknown.

## DM20 failure signature and limitation

DM20 reported four passes in a five-run current-source writable lifecycle cohort. Its fifth run timed out waiting for a 13-byte bulk-IN CSW after a 512-byte WRITE(10) OUT transfer, CBW/expected CSW tag `0x1BC`, at the 1,000-frame production deadline. The CSW TD remained active. The failing boot had no QEMU UHCI/USB trace, and the guest had not captured the WRITE(10) LBA, block count, actual OUT byte total, endpoint toggle history, pre-recovery CSW buffer, or preceding BOT command history. Its known guest QH/TD physical addresses were `0x3BF60690` and `0x3BF606A0`; USBSTS was 0 and USBCMD was `0x81`.

That historical run cannot be retrospectively traced. Its LBA, data-stage accounting, toggle result, CSW buffer contents, QEMU behavior, and prior command sequence remain unknown. A successful traced run with the same opcode and tag does not prove the historical failure had the same transport history.

## Automatic capture and classification

DM21 updates the USB qualification runners to start focused QEMU tracing on every boot, save a QMP transcript and guest serial output, record the disposable image's initial and post-QEMU hashes, and run the classifier automatically after a traced failure. Each lifecycle attempt has its own directory; first and restart boots have separate trace and serial files. The QEMU event list is limited to 24 UHCI queue/TD, USB packet, and USB Mass Storage events.

Diagnostic AMD64 builds retain a 64-entry BOT transition ring and a 16-command history. A timeout emits the ring, command metadata, endpoint incarnation/registration, QH and TD state, CSW buffer bytes, FRNUM, USBSTS, USBCMD, PORTSC values and change latches before BOT recovery can reset toggles, clear endpoint halts, or reuse descriptors. The normal 1,000-frame bulk timeout remains unchanged. Diagnostic builds observe up to 4,000 additional frames after the deadline without converting a late completion into production success.

WRITE(10) capture records opcode, tag, LBA, block count, logical block size, expected and actual OUT bytes, every completed TD's packet accounting and toggle, first and last OUT TD addresses, and the expected CSW toggle/TD. HCI owns the bulk endpoint toggle state; BOT diagnostics read that state rather than maintaining a second toggle counter. Diagnostic records contain no write payload bytes; only the 13-byte CSW buffer is captured.

The classifier joins guest command sequence/tag records to QEMU Mass Storage status spans and compares QEMU TD/QH addresses with guest physical addresses. It separates missing TD fetch, asynchronous CSW handling, QH movement, packet errors, and exact TD/QH completion. The selected QEMU trace events do not directly prove USB NAK responses, so the classifier does not infer NAK from an active TD or asynchronous event.

## Successful current-source comparison for tag 0x1BC

Run 2 used the final diagnostic kernel (`773469070FE76A8D635FA53CF9C110DC57892D6910B6856E445B23484807EA55`) and completed the same WRITE(10) opcode/tag successfully:

- Guest BOT sequence 444, tag `0x1BC`, USB incarnation 1.
- LBA `0x27FDF`, one 512-byte logical block; expected and actual data-OUT totals were both 512 bytes.
- Eight 64-byte OUT TDs summed to 512 bytes; the first and last TDs were `0x3BF60B00` and `0x3BF60B70`.
- OUT toggle began and ended at 0. Expected CSW IN toggle was 1.
- Guest QH was `0x3BF60AF0`; CSW TD was `0x3BF60B00`. QEMU fetched the matching guest TD and observed the matching QH/TD completion.
- The guest submitted the CSW at FRNUM `0x015C` and completed it at `0x018B`, 47 frames later, receiving all 13 bytes. Its 13-byte sentinel was `A5` before scheduling; afterward the caller buffer held `55534253BC0100000000000000` (the expected `USBS` signature, matching tag, zero residue, and passed status). CSW IN toggle advanced from 1 to 0, with the final hardware and software toggle values matching.
- QEMU observed asynchronous CSW processing, then command status 0, a 13-byte status response, successful packet completion, and completion of the same QH/TD. This is an asynchronous interval followed by success; it is not evidence of a NAK or of the historical failure's cause.
- The preceding 16 guest commands (sequences 428–443, tags `0x1AC`–`0x1BB`) were successful 512-byte READ(10) commands from LBA 0. The historical failing run's previous-command history was not recorded.

The final classifier output for the stable run reports 12,599 guest CSW submits and completions, 12,599 joined QEMU status records (all status 0), zero unmatched joins, and zero active-CSW timeout faults. See `out/dm21-evidence/tag-1bc-correlation-run-02-summary.txt` and `out/dm21-evidence/tag-1bc-correlation-run-02-final.json`.

## Deterministic coverage and builds

The storage suite reports **816 checks, 0 failures** (including 249 USB checks), and all **7 synthetic QEMU trace-classifier tests** pass. Coverage includes WRITE(10) decode and expected byte arithmetic, multi-TD byte accumulation, endpoint toggle parity, diagnostic-ring wrap and ordering, bounded 16-command history, CSW length/signature/tag/residue validation, and timeout classification. The pre-DM21 suite had 764 checks.

The AMD64 production kernel, diagnostic kernel, and UEFI x64 Release build passed. DM21 proof kernels compile with `NVME_WRITE_PROVEN=0` and `NVME_FLUSH_PROVEN=0`. Final storage tests are in `out/dm21-evidence/storage-manager-tests-final.log`; build logs are `out/dm21-production-kernel-build-final3.log`, `out/dm21-diagnostic-kernel-build-final3.log`, and `out/dm21-uefi-x64-release-build.log`. PowerShell parser checks, Python compilation, classifier tests, and `git diff --check` are included in final verification.

## Qualification campaign

All proofs use disposable repository-owned images. No host physical disk is passed to QEMU. The campaign is serial; no QEMU proof is started while another proof is using QEMU.

| Gate | Status |
|---|---|
| Fresh full writable USB lifecycle cohort | **PASS: 10/10.** Final-kernel runs: `lifecycle-cohort-10/run-02` and `run-03`; `lifecycle-current-source-probe-01/run-01`; and `lifecycle-final-source-cohort-7/run-01` through `run-07`. Each completed lifecycle, ten mount/write/read/unmount cycles, cold restart, and independent GPT/FAT32 verification. Exploratory `lifecycle-cohort-10/run-01` used a different kernel hash and is excluded. |
| 10,000 BOT command completions with repeated WRITE(10)/CSW sequences and size boundaries | **PASS: 10,000/10,000**, including repeated sequence groups, a 128-sector transfer, and restoration of the disposable image hash. Evidence: `bot-stress-10000/dm20-t1000-p0-c10000-20261001-233132-637/manifest.txt`. |
| Buffer offsets 4083, 65523, and 65524; descriptor pad 4080; frame-list pads 4096 and 65536 | **PASS: six profiles × 100 BOT commands.** Evidence directories: `layout-buffer-page-4083`, `layout-buffer-64k-13`, `layout-buffer-64k-12`, `layout-descriptor-pad-4080`, `layout-frame-list-pad-4096`, and `layout-frame-list-pad-65536`. |
| 100 write/Sync Cache/read/restore cycles, canaries, and host image restoration | **PASS.** Private production-write path, Sync Cache, exact read-back, canaries, and host-image restoration all passed; see `write100-run/manifest.txt`. |
| USB read-only reference | **PASS.** QEMU write protection, reads, blocked writes, and unchanged host image; see `readonly-run/manifest.txt`. |
| DM14 hotplug regression | **PASS.** Unplug while mounted/open, stale mount/handle cleanup, same-media reinsert, and different-media replacement on the same port; see `hotplug-run/manifest.txt`. |
| ATA fresh lifecycle/restart/verifier | **PASS.** Fresh disposable 600 MiB image, lifecycle and restart rediscovery, independent GPT/FAT32 verifier; see `ata-regression/dm10-manifest.txt`. |
| AHCI fresh lifecycle/restart/verifier | **PASS.** Fresh disposable 600 MiB image, full lifecycle and restart rediscovery, independent GPT/FAT32 verifier; see `ahci-regression-retry-01/dm15-manifest.txt`. The initial runner attempt failed before QEMU due to a PowerShell build-log handling issue; its preserved evidence is in `ahci-regression/`, and the direct-build retry completed the gate. |
| NVMe read-only gate | **PASS, unchanged.** Read callback remains; write and Flush callbacks are disabled (`NVME_WRITE_PROVEN=0`, `NVME_FLUSH_PROVEN=0`); no NVMe graduation work was performed. |

All runtime proof artifacts remain untracked under `out/`. Closed QEMU trace logs were NTFS-compressed in place to conserve disk space; every trace's SHA256 and logical length were verified unchanged and recorded in `out/dm21-evidence/ntfs-trace-compression-manifest.txt`.

### Run 04 early exit

`run-04` reached `create-partition=PASS`, then its QEMU process exited early while the guest was handling a 64 KiB READ(10) at LBA 0, tag `0x869`. The cohort proof failed after 585 seconds total. The serial log ends after the command-submit record, before any CSW submit. The trace classifier joined 2,152 earlier guest CSW completions to QEMU; all 2,152 reported command status 0. It found no active-CSW timeout fault. QEMU's final trace records the READ(10) backend command completing with status 0, but it contains no joined 13-byte status record for tag `0x869`. This is not the historical WRITE(10)/CSW failure, and the captured events do not prove a USB NAK or identify why QEMU exited.

The image hash changed from the blank source hash to `95214734D16B2F25C0D8BE1766C38ED640AEDF6A17782F6F8ABAF18C069B2E04`; the independent image verifier reports an invalid FAT32 boot-sector signature because the lifecycle stopped before format completion. The old runner wrote a generic marker-timeout message and did not preserve QEMU's exit code. The runner now distinguishes early QEMU exit from a real proof deadline and records the exit code for subsequent attempts. Full trace, serial, manifest, and classifier output remain in `out/dm21-evidence/lifecycle-cohort-10/run-04/`.

A fresh-image retry with the same final kernel passed after 2,269 seconds. Its first boot completed 12,599 guest commands and 12,599 CSWs; its cold restart and independent image verifier passed. The final image SHA256 is `5ECB52E27E6B88BCFE80B7699526E70A279E1C6C162348FB6F2CC5223DC6A8BC`. Evidence is in `out/dm21-evidence/lifecycle-current-source-probe-01/run-01/`. This retry did not reproduce the early QEMU exit, but it does not establish why run 04 exited.

## Outcome and remaining limitations

The targeted DM20 WRITE(10) → CSW timeout was **not reproduced** in the ten qualifying current-source lifecycle runs or the 10,000-command BOT stress. Accordingly DM21 records **Outcome B**: current USB is operational for the qualified workload, automatic tracing/diagnostics are in place, and the historical cause remains unknown. The successful control for tag `0x1BC` is described above; it does not reconstruct the old failure.

There was a separate exploratory QEMU exit in `lifecycle-cohort-10/run-04` while processing a 64 KiB READ(10), LBA 0, tag `0x869`. It is not the targeted WRITE(10) signature and did not recur in its fresh-image retry or the final cohort. The trace shows a successful backend command but no joined CSW status for that tag, so this exit also remains unexplained; it is not labeled environmental. The complete evidence and limitations are documented in the Run 04 section above.

The final commit, cached ahead/behind count, fetch freshness, and push result are recorded in the completion summary. DM22 should prioritize FAT32 scalability while NVMe remains read-only and its prior backing-store integrity issue remains unresolved.
