# Disk Manager Phase DM31 — UX and Operational Polish

## Outcome

**Outcome B.** Storage behavior and builds remain qualified by existing DM30 evidence and current-source regressions. DM31 improves action terminology and Initialize Disk wording, and captures a real current-source degraded-GPT Disk Manager view. The central graphical repair transaction could not be completed: the repair confirmation, Cancel, Confirm, post-repair Healthy refresh, and cold restart were not visually exercised in this run. Do not treat this phase as closing the DM30 graphical click-through gap.

## Gate and starting state

- Project: `guideXOS_SERVER_DISK_MANAGER_IMPROVEMENTS`.
- Branch: `DISK_MANAGER_IMPROVEMENTS`.
- Starting HEAD: `ccca01f5fa1d029cfdd859dff9ac27f48a7e6114` (`storage: qualify DM30 GPT repair operations`).
- Starting `.phase`: `last_completed=DM30`, `next_expected=DM31`.
- No DM31 report or commit existed at start.
- Initial worktree contained a user-owned `.gitignore` modification and untracked `kernel/out/`; both are preserved and excluded from this commit.
- Cached ahead/behind was `0/0`. Fetch failed with `git@github.com: Permission denied (publickey)`; upstream freshness is unknown.
- Historical reports DM5, DM22–DM25, DM28–DM30 and the implementation status were reviewed.

## UI audit and change

Disk Manager presents device rows, disk/partition information and map, a partition/unallocated list, a properties/diagnostics pane, and context-sensitive bottom actions. Refresh is manual. Diagnostics on the observed degraded disk fit in the pane but truncate some longer explanations. DM30 had established service and model policy but no actual graphical repair confirmation click-through.

Action labels now identify the target and operation more clearly: `Delete Partition...`, `Format FAT32...`, and `Quick Reformat...`; the confirmation action names FAT32 explicitly. Initialize Disk confirmation now states that partition metadata is replaced, existing partitions become inaccessible, old sectors are not securely erased, and the operation does not format a filesystem or repair GPT. Existing GPT Repair wording and service path were not changed.

## Live QEMU visual evidence

A disposable copy of the DM30 600 MiB three-partition degraded fixture was attached as a USB mass-storage device to a current-source AMD64 guideXOS QEMU guest. The actual Disk Manager view showed Disk 3 selected, 512-byte logical sectors, three partitions, four unallocated regions, `GPT | Primary valid, backup invalid`, and an enabled `Repair GPT...` action. Its diagnostics reported Primary Valid, Backup ArrayCrcInvalid, GPT Degraded, and repair available after separate confirmation. The rendered screen also showed zero write operations, zero sectors written, and zero flush attempts at capture. QEMU serial confirms USB registration.

Evidence is preserved under ignored `out/dm31-live-ui/`, especially `degraded-state-selected.png`, `repair-target2.png`, `usb-hotplug-serial.log`, and `blockstats-before-cancel.json`. The QEMU UI is real rendered guest output. No claim is made that Cancel or Confirm was clicked. The captured QMP block statistics are a before-state only, so they do not establish before/after cancellation accounting.

## Acceptance audit

| Area | DM31 result |
|---|---|
| Healthy GPT / action hidden | Not newly rendered; source/model policy and DM30 evidence remain |
| Degraded GPT | Rendered; Repair visible/enabled on disposable USB 512-byte fixture |
| Conflict / unrecoverable | Not rendered in guest during DM31 |
| Read-only / mounted blocker | Not newly exercised in guest |
| Repair confirmation / Cancel / Confirm | Not completed in GUI |
| Writes and Flushes on Cancel | No Cancel action captured; before-state counters were zero writes and zero flushes |
| Service routing / one-use confirmation | Existing production service/model path retained; no GUI invocation proof in DM31 |
| Healthy refresh / action disappearance / cold restart | Not visually qualified in DM31 |
| Repair selection retention | Disk selection shown before operation; post-repair retention unverified |
| Delete selection, Quick Reformat refresh | Existing DM25/DM28 service/runtime evidence; no new GUI click-through |
| Manual Refresh / hotplug identity | Existing manual-refresh architecture and DM14 identity evidence; DM31 removal/reinsert UI behavior not exercised |
| Action matrix, stale action, busy/double-submit | No new deterministic DM31 matrix added; existing service preflight remains authoritative |
| Keyboard focus/default, Escape, hover/pressed/disabled | Not fully qualified; source path audit only. No destructive default-focus change made |
| 4Kn / 10 GiB property display | Not newly rendered |
| Disk map hit testing | Device selection and Diagnostics view were clicked; partition/gap hit testing not fully audited |
| NVMe write gate | Preserved as `NVME_WRITE_PROVEN=0`, `NVME_FLUSH_PROVEN=0` per DM30 |

## Verification and builds

- Storage suite: **978 checks, 0 failures**, including 249 USB checks; USB trace classifier 7/7, GPT verifier test and DM30 fixture test passed. This suite was run after UI wording changes.
- AMD64 kernel build passed with profile verification and produced `kernel/build/amd64/bin/kernel.elf`.
- Full app `cmd /c build.bat` passed and produced `guideXOSServer.exe` (existing warnings remain).
- `build.ps1 -Arch amd64` still fails at unrelated PacMan links: `pacman_audio_load_resources(gx_app_context*)` and `pacman_audio_submit(void*, PacManSoundId)`. The failure matches the DM30 baseline; PacMan was not changed.
- UEFI x64 build was not run in DM31.
- No storage primitive or backend semantics changed. DM30 real AHCI GPT repair and independent cold-restart verifier results remain the latest actual AHCI repair qualification; the requested current-source post-DM31 AHCI transaction was not rerun.
- Existing DM25 delete, DM28 Quick Reformat, 4Kn, USB, and NVMe evidence remains as documented in those reports; DM31 did not repeat full transport qualification.

## Remaining gap and next phase

The principal limitation is QEMU UI input reliability for opening the repair confirmation and exercising its controls. Therefore no claims are made for Cancel zero-write/zero-Flush delta, production service execution from the GUI, post-repair Healthy state, action disappearance, mounted/read-only/conflict/unrecoverable rendered blockers, or post-repair selection retention. The current screenshot captures the actual degraded state and action availability only.

Recommended DM32: **Long-Operation UX and Progress**, following the project default, with the DM31 graphical repair transaction carried forward as an explicit qualification item.
