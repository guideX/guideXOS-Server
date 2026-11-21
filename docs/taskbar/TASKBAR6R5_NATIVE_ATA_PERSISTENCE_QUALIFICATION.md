# TASKBAR6R5 — Native ATA Persistence Qualification

**Outcome:** `TASKBAR6R5_NATIVE_STORAGE_PARTIAL`\
**Branch:** `TASKBAR_IMPROVEMENTS`\
**Starting HEAD:** `f093e181c1b2f72528182cc1198e858315cf2f63`\
**Starting worktree:** clean; branch matched `origin/TASKBAR_IMPROVEMENTS`. It was 10 commits ahead of `origin/main` and 0 behind.

## Result

The TASKBAR6R4 hardened ATA implementation completed 36 native cache-flush commands on a QAC-managed AMD64 guest using a writable raw FAT32 image at the PIIX3 IDE primary master (`ata0m`). The trace recorded the selected master, advertised flush capabilities, status transitions, timing, and final result for every command. All 36 returned `BLOCK_OK`; the original R2 timeout did not recur for this workload.

Boot A created `/r5test.txt` in Notepad and replaced its contents three times. Four taskbar preference transactions also committed `/desktop.cfg`. A fresh QAC-managed Boot B mounted the same disk and loaded the primary desktop configuration. After Boot B, a read-only host parser confirmed the exact test-file bytes and final configuration, matching FAT copies, the Calculator pin record, and no `DESKTOP.TMP` or `DESKTOP.BAK`.

The full persistence acceptance remains partial. Boot A's guest UI shutdown and ACPI powerdown attempts did not stop the guest; QMP `quit` was used to close it. Boot B ended by QMP `quit` after bounded UI attempts. Boot B could not reopen the test file: terminal `desktop.launch` requests for `r5test.txt` and `notepad` were rejected as unknown bare-metal targets, and QEMU pointer movement was unreliable. The offline raw-image read is useful evidence but does not replace guest readback after a normal shutdown. The full requested six-save taskbar sequence, multiple clean guest lifetimes, and taskbar pin loading across clean cold restarts therefore remain unqualified.

No ATA, FAT, or taskbar production source was changed in R5. The successful tested flush path is evidence of current behavior, not proof of which R4 change addressed the historical R2 failure. Its specific cause remains unknown. No power-loss atomicity claim is made.

## Candidate and QAC admission

QAC 2.0.0 was online before the run with no reservations, sessions, queued requests, or QEMU processes. The disposable candidate was started only after exclusive acceptance reservations were granted:

| Boot | QAC reservation | QEMU child PID | Result |
|---|---|---:|---|
| A | `5c4d1366364a468cadbc48f8b7ee3a53` | 20640 | Guest test completed; QMP `quit`; exit 0; reservation released |
| B | `41cc923dac854cdf89b7b2548638268e` | 23080 | Fresh guest loaded settings; QMP `quit`; exit 0; reservation released |

The QAC manager owned each QEMU process directly. Post-run `status`, `list`, and `queue` reported an online coordinator, 0 QEMU processes, 0 reservations, 0 managed sessions, and 0 queued or blocked requests. No QAC configuration was modified.

The candidate is an isolated copy of the untouched TASKBAR6R2 fresh image (`8468F2B9A615333F10FA2ED73027A381195FC8A8389D0AC2A90948FDA4CA126E`). Its diagnostic kernel was staged offline through the production block-device/FAT/VFS image adapter and read back byte-for-byte. Candidate details:

- Diagnostic kernel: 2,873,896 bytes, SHA-256 `A0C57630D35E4F6D8A5AC13DC1ACB741ED2067BB295CAA9D806D0D10F8C04524`, installed at `/kernel.elf`, built with `ATA_QUALIFICATION_DIAGNOSTICS`.
- Production kernel reference: SHA-256 `7B64B1C4A857CF1733B9773ACE346F8665D02ED373FE4B24CACC0E1E3734CBB0`.
- Disposable raw disk before Boot A: 536,870,912 bytes, SHA-256 `B61612D8B63BE2347CCF6C0FD9B8A78E1D3F07130948F2FFD6E57BD8E9E076DC`.
- Raw disk after Boot A: SHA-256 `F7D0A5D80600433717EA380EF4A48B8D65C704DFC5C0177ACF8C67F0410A807F`. Final image after Boot B: SHA-256 `8B278A581D7D52861FF2EE5A834AB16AAF93DDE606647613ABC8B19267797AA0`.
- QEMU 11.0.0 (`v11.0.0-12122-ga4bb4b10c9`), executable `C:\Program Files\qemu\qemu-system-x86_64.exe`; `pc,accel=tcg`, OVMF, 1024 MiB, 1280×800 GOP, default PS/2 keyboard/mouse, QMP loopback port 49231.
- One writable raw disk, `if=ide,index=0,media=disk,cache=writeback`; PIIX3 IDE primary master, no additional data disks, and no `fat:rw` backend. OVMF SHA-256: `2DF617A6FD1BAE41EC7C0E3BCECA96C928A5A88D8DC198468D34733393AB2792`.

The image uses an MBR FAT32 LBA partition (type `0x0c`) starting at LBA 2048. The volume has 1,046,493 sectors, 512-byte sectors, 8 sectors per cluster, 32 reserved sectors, two 1,024-sector FATs, and root cluster 2. The image hash changed across guest runs. The available snapshots do not attribute every changed sector; the read-only inspection after Boot B found the two FAT copies identical and the expected file data and directory entries intact.

## Native ATA observations

The guest identified the primary master as `ata0m` on legacy IDE primary I/O `0x1f0` / control `0x3f6`. IDENTIFY word 83 was `0x7400`; LBA48 and both FLUSH CACHE and FLUSH CACHE EXT capability flags were reported. The driver selected opcode `0xea` and Device/Head `0xa0`, with `selected_target=1`, for all recorded flushes.

Boot A recorded 36 flushes, numbered `0x01` through `0x24`. Every record showed:

- status before selection `0x50`, after select `0x50`, after issue `0xc0`, final `0x50`;
- Error register before and after `0x00`; final `BSY=0`, `DRQ=0`, `DRDY=1`, `ERR=0`, `DF=0`;
- `observed_bsy=1`, `deadline_available=1`, `timeout_source=none`, `retry_count=0`, and `result=BLOCK_OK`.

With a 100 Hz PIT, elapsed time ranged from 0 to 15 ticks (0–150 ms); poll counts ranged from 27 to 578,086. The diagnostic trace was transition-sampled and bounded. No timeout, ATA error, device fault, or retry was observed. These observations qualify only the exercised QEMU device/configuration and command workload. Boot B reported IDENTIFY capability information but no flush records, because no successful settings or file save was performed there.

## FAT32 and taskbar workload

In Boot A, Notepad created `/r5test.txt` and saved three replacements (four saves total). Each save used the production guest filesystem and ATA path. The final read-only inspection found 29 bytes with SHA-256 `6158e3b88a3cac9ecf385cf1272caadd568cf8e9314036f504b314e688e048b0`:

```text
r5taskbar6r5v1
checksumokv2v3
```

The offline read confirms the bytes present in the raw image, not an in-guest readback immediately following each save. No temporary-file create/remove, standalone consecutive-flush, or normal rename/replace acceptance sequence was completed.

Four main taskbar configuration replacement transactions committed through the normal UI path. Their six flushes each account for sequences `0x0d–0x12`, `0x13–0x18`, `0x19–0x1e`, and `0x1f–0x24`; all were `BLOCK_OK`. One saved settings screenshot shows Bottom / Expanded / auto-hide Off / labels Off / previews On / activity On. The final `/desktop.cfg` was 360 bytes, SHA-256 `e4f9f7d87838302aba22e08b0ad04a3c59fb23f426cd81cb2b139758d2c95241`, with these relevant values:

```text
taskbarPosition=bottom
taskbarSize=expanded
taskbarPresentation=icons-only
taskbarAutoHide=0
taskbarShowPreviews=1
taskbarShowActivity=1
```

Other desktop preferences remained in the file. Boot B serial confirms `/desktop.cfg` opened and the primary configuration loaded with taskbar position `bottom`; its guest screenshot shows the bottom taskbar. The saved Calculator pin store contains exactly one record, `appId=gxos.builtin.calculator`, 40 bytes. The evidence does not map all four preference commits to the specific requested sequence of Top, another preference, Bottom, size, and another preference, nor does it prove pin state through multiple clean guest restarts.

Some taskbar-save sidecar writes returned `FAT_FILE_WRITE_INVALID_NAME` for `/display-options.cfg`, `/desktop.taskbar.position`, `/desktop.system.icons`, and `/desktop.background.scale`. The primary `/desktop.cfg` transactions committed, but the UI path ignored these auxiliary VFS write failures. This is a FAT path/error-reporting limitation observed alongside successful ATA flushes, not evidence of an ATA failure. The startup write to `/.startmenu_recent` also failed with `INVALID_NAME`. No speculative repair was made.

The read-only post-Boot-B FAT inspection found 15 root entries, matching FAT copies, the expected test file, config and pin store, and no `DESKTOP.TMP` or `DESKTOP.BAK`. The exact output is preserved in `out/taskbar/TASKBAR6R5/raw-fat-readonly-inspection-after-boot-b.txt`.

## Shutdown, readback, and failure safety

The Start menu shutdown control and ACPI `system_powerdown` did not stop Boot A. Boot A was closed with QMP `quit` after evidence capture; QEMU exited with code 0 and QAC released the reservation. Boot B also ended through QMP `quit` after bounded attempts to reopen the file and reach shutdown. No normal guest shutdown marker was observed. Thus Boot B is a fresh QEMU process using the same image, but not a cold restart proven after a normal guest shutdown.

Boot B terminal requests to launch `r5test.txt` and `notepad` were logged as blocked unknown bare-metal targets. The start menu exposed Notepad, but PS/2 pointer positioning could not be made reliable enough to launch it; keyboard navigation did not select an entry. There is no guest-visible post-boot content readback. The host read-only image parser verifies the exact final bytes and config; it is not a substitute for the requested guest reopen and clean-shutdown persistence cycle.

No ATA failure occurred natively, so native rollback after a device error was not exercised. The hosted FAT/VFS persistence suite includes injected backing-image write failures and reports them to callers, but this does not establish native ATA-failure rollback. Main config replacement left no temp or backup file in the final image. Sidecar `INVALID_NAME` errors were not surfaced by the settings UI, so the full error-reporting gate remains partial.

## Acceptance gates

| Gate | Status | Evidence / remaining limit |
|---|---|---|
| R5-G1 — QAC admission and candidate integrity | **PASS** | Exclusive reservations granted; isolated candidate, kernel, and raw image hashes recorded. |
| R5-G2 — Hardened ATA flush exercised natively | **PASS** | 36 `ata0m` flushes on raw FAT32 guest storage; all returned `BLOCK_OK`. |
| R5-G3 — ATA status, error, timing diagnostics | **PASS** | Capability, selection, status/error registers, PIT ticks, polls, timeout source, and retry state captured for all 36 flushes. |
| R5-G4 — Repeated raw FAT32 write/flush/readback | **PARTIAL** | Native create plus three replacements and 12 file-save flushes; final bytes verified offline. No per-save guest readback, temp/remove, or rename sequence. |
| R5-G5 — Contents survive genuine cold restart | **PARTIAL** | Fresh Boot B loaded `/desktop.cfg`; final file bytes were present offline. Boot A did not shut down normally and the guest did not reopen `/r5test.txt`. |
| R5-G6 — Repeated native taskbar settings saves | **PARTIAL** | Four main config commits and 24 successful flushes; only one save is screenshot-correlated to a specific setting. Sidecar writes returned `INVALID_NAME`; prescribed full sequence not evidenced. |
| R5-G7 — Latest settings and pins survive multiple cold restarts | **PARTIAL** | Boot B loaded Bottom; final config is Bottom/Expanded and pin file has one Calculator record. No clean Boot A/B cycle or Boot C; pin loading not fully verified in GUI. |
| R5-G8 — Previous configuration protection and error reporting | **PARTIAL** | No native ATA failure to test rollback. No stale main temp/backup remained; auxiliary invalid-name failures were ignored by UI. |
| R5-G9 — Focused regressions and builds | **PASS** | All baseline suites/builds passed on final unchanged source. |
| R5-G10 — QAC resources released | **PASS** | Both reservations released; post-run coordinator reports zero processes, reservations, sessions, and queue entries. |
| R5-G11 — Mainline integration readiness reassessed | **PASS** | Reassessed as not ready for merge; retain the branch pending the remaining native persistence gates. |

## Regression and build results

| Check | Result |
|---|---:|
| ATA PIO flush state machine | 80 checks, 0 failures |
| Taskbar application model | 113 checks, 0 failures |
| FAT/VFS persistence, clean fixture | 64 checks, 0 failures |
| App Model identity | 25/25 |
| App Model application actions | 55/55 |
| Compositor pointer buttons | Passed |
| Window UI | Passed |
| Compositor resize notification | Passed |
| Hosted `build.bat` | Passed |
| Production AMD64 kernel (`mingw32-make -C kernel ARCH=amd64`) | Passed |
| Diagnostic AMD64 kernel (`BUILD_DIR=../out/taskbar/TASKBAR6R5/native-diag`, `EXTRA_CFLAGS=-DATA_QUALIFICATION_DIAGNOSTICS`) | Passed |

The first FAT persistence invocation failed 8 of 64 checks because its input fixture was a previously mutated R4 test output image. The input issue was identified; the suite was rerun from the pristine prepared `fat-regression-clean.img` copy and passed 64/64. Both logs are preserved. No production source changed after these runs.

## Integration recommendation and next milestone

Keep `TASKBAR_IMPROVEMENTS` local; do not merge it into `origin/main` yet. The branch is 10 commits ahead and includes substantial overlapping storage changes in `kernel/core/ata.cpp`, `kernel/core/fs_fat.cpp`, and `kernel/core/vfs.cpp`, in addition to the taskbar work. Integrate only after reviewing those storage changes against the target branch and rerunning the storage, taskbar, and AMD64 regressions on the merged candidate.

Recommended next milestone: **`TASKBAR6R5R1 — Complete native taskbar persistence and clean-restart acceptance`**. Capture a shutdown path that demonstrably exits the guest, reopen and checksum `/r5test.txt` inside Boot B, complete the full requested repeated settings sequence with each setting and save result recorded, verify the Calculator pin through the guest UI, and perform Boot C after Boot B succeeds. Investigate the invalid FAT path and ignored sidecar error contract as a separate focused follow-up before claiming settings persistence acceptance. Keep the current ATA trace and historical R2 root cause distinct; if R2 still does not reproduce, leave its exact cause unresolved.

## Preserved evidence

Artifacts are under `out/taskbar/TASKBAR6R5/` (ignored by Git):

- `candidate-manifest.json`, `qemu-config-boot-a.txt`, `qac-preflight.log`, `qac-run-boot-a.log`, `qac-run-boot-b.log`, `qac-post-clean.log`.
- `boot-a-final-preserved.log`, `boot-b-final-preserved.log`, `boot-b-first-screen.png`, `taskbar-save1-expanded.png`, and the Notepad save screenshots.
- `raw-fat-readonly-inspection.txt`, `raw-fat-readonly-inspection-after-boot-b.txt`, and `inspect_raw_fat_readonly.py`.
- Regression and build logs in `tests/`, including the initial fixture-misconfigured FAT run and its clean-fixture rerun.

No taskbar feature, ATA repair, merge, or push was introduced in R5.
