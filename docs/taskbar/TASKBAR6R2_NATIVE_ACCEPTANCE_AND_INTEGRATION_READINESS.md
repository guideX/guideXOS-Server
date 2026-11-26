# TASKBAR6R2 — Native Acceptance and Integration Readiness

- **Outcome:** `TASKBAR6R2_PRODUCTION_DEFECT_OPEN`
- **Integration recommendation:** **Not ready for controlled integration**
- **Repository:** `D:\dev\guideXOSServer_TASKBAR_IMPROVEMENTS`
- **Branch:** `TASKBAR_IMPROVEMENTS`
- **Starting HEAD:** `a08a7ebafd20354e3e6107f53c77d1b365cfb766`
- **Frozen v0.3 artifacts:** unchanged

## Disposition

TASKBAR6R2 adds genuine PS/2 interaction, taskbar pin persistence across cold boots, and a second native settings profile that loaded after restart. The raw FAT32 guest also exposed a remaining production defect: replacing the existing settings file can fail after three bounded retries because ATA cache flushes time out. A failed update leaves the previously committed configuration intact, but the requested new setting is not saved. The timeout is now propagated accurately through FAT and VFS; the underlying ATA/QEMU flush failure remains unresolved.

The earlier QEMU directory-backed FAT rename failure and the raw IDE FAT flush timeout are distinct. The first occurs in QEMU's `fat:rw:` host-directory backend during rename reconciliation. The second occurs on a conventional raw FAT32 disk after the guest issues a device cache flush. Passing hosted FAT tests or using one backend does not clear the other.

Native grouping, preview selection, overflow pagination, and real File Explorer copy/move progress were not qualified. Keep the branch out of main until the raw-volume settings update is reliable and those native acceptance gaps are closed.

## Repository and candidate

The repository was on `TASKBAR_IMPROVEMENTS` at `a08a7ebafd20354e3e6107f53c77d1b365cfb766`, matching `origin/TASKBAR_IMPROVEMENTS`. The starting worktree was clean. The final local change set contains the production updates, focused regressions, and this report; it has not been pushed or merged.

`git fetch origin --prune` could not refresh the configured SSH remote: `Permission denied (publickey)`. The available local snapshot `refs/remotes/taskbar6r2/main` is `50c6cff0993227de7c582b00eb1daabed62cf816`; `origin/main` is `f5cb9f4c50aabec24e5c3b42b1a0247bba3605c4`. Against the available `taskbar6r2/main` snapshot, the branch was 7 commits ahead and 1 behind before this acceptance commit. A `git merge-tree --write-tree refs/remotes/taskbar6r2/main HEAD` simulation completed without conflicts (tree `11cebff947872055a1bff018ca323f07b406db84`). The single main-only commit in that snapshot is documentation-only. This is a local snapshot review, not a current remote refresh.

No release image, ISO, or frozen v0.3 artifact was edited. Disposable guest and test media are under `out/taskbar/TASKBAR6R2/` and `out/validation/`.

## QAC and guest identity

- Coordinator: QAC 2.0.0, online; final inventory: 0 reservations and 0 managed sessions.
- QEMU: `C:\Program Files\qemu\qemu-system-x86_64.exe`, version 11.0.0 (`v11.0.0-12122-ga4bb4b10c9`). All guest launches used the QAC CLI in `acceptance` mode; no unmanaged QEMU process was started.
- Guest: AMD64 UEFI, OVMF, 1024 MiB RAM, 1280×800 GOP, QEMU PS/2 mouse, QMP on loopback port 49231, one writable 512 MiB raw FAT32 disk and a separate disposable `fat:rw:` directory-backed disk.
- Writable raw disk: `out/taskbar/TASKBAR6R2/guest-volume-fat32-fresh.img`, partition offset 1 MiB. Final post-run SHA-256: `8468F2B9A615333F10FA2ED73027A381195FC8A8389D0AC2A90948FDA4CA126E`.
- Final production kernel: SHA-256 `608C2C62C0C66BF294DD28CEB58A517200569A15799D8C6587A8B3CE88653A1E`, 2,856,328 bytes. The kernel read back from the guest disk matched this hash.
- Firmware `OVMF.fd`: SHA-256 `2DF617A6FD1BAE41EC7C0E3BCECA96C928A5A88D8DC198468D34733393AB2792`.

Key QAC reservations (all released):

| Reservation | QEMU PID | Evidence |
| --- | ---: | --- |
| `e3533adaabe54014b34d9e6f907b8d79` | 30208 | Native settings and pin interaction on raw FAT32; serial `guest-fat32-r2-settings-retry-serial.log`. |
| `130c10d500be49d386f33bf4f0450a0f` | 24844 | Fresh same-volume boot loaded Bottom position and Calculator pin; `guest-fat32-r2-settings-retry-cold-serial.log` and `guest-fat32-r2-settings-retry-cold-desktop.png`. |
| `e4645d7629ed4a8c8d14e052fee2f8da` | 34304 | Updated VFS/FAT status path; native Position change retried three times, then failed with a reported ATA flush timeout; serial `guest-fat32-r2-final-retry-serial.log`. |

A later diagnostic build was used only to confirm guest boot and context-menu input; it did not exercise a settings save and was removed from the final candidate. QAC reported zero active sessions after shutdown.

## FAT persistence investigation

### Directory-backed FAT (`fat:rw:`)

The TASKBAR6R1 report records QEMU 11 printing `Error handling renames (-2)` during a second settings replacement through the directory-backed FAT backend, followed by guest exit. That result does not establish whether QEMU's host-directory reconciliation or a guest FAT operation triggered the rejection. The accepted LFN checksum and stale-record protections address a separate FAT metadata defect; they do not make QEMU's directory backend a qualified persistence target. TASKBAR6R2 did not repeat the full directory-backend operation matrix, so that backend remains unqualified.

### Conventional raw FAT32 disk

The raw disk is a different backend. Serial identifies the settings volume as FAT32 on `ata0m` (primary master, partition offset `0x800`). On a native PS/2 settings change, the guest reported `LFPASTE_FLUSH_END status=0x02` (`BLOCK_ERR_TIMEOUT`). After the FAT rollback-status fix, VFS returned `VFS_ERR_IO_TIMEOUT` (`-14`) instead of disguising the result as `VFS_ERR_NOT_SUPPORTED`. The desktop writer retried three total times and reported:

```text
[desktop] taskbar settings save retry attempt=2 reason=io-timeout
[desktop] taskbar settings save retry attempt=3 reason=io-timeout
[desktop] taskbar settings persistence failed status=FFFFFFF2
```

After QMP shutdown, mtools showed the prior `/DESKTOP.CFG` still contained Bottom / Standard / icons-only / auto-hide Off, and `/DESKTOP.TMP` was empty. The update to Top was not committed. The old configuration was preserved rather than partially replaced. The cause below the block layer—why the ATA cache flush stays busy long enough to time out—has not been isolated.

An earlier native settings cycle did commit Bottom / Standard / icons-only / auto-hide Off. A later cold boot loaded that profile and the Calculator pin. This proves a successful save and cold reload on this raw volume, but it does not clear the reproducible failed replacement above.

The guest also logs rejected compatibility writes to long paths such as `/display-options.cfg` and `/desktop.taskbar.position`; the FAT driver accepts 8.3 names for creation. The active settings file `/desktop.cfg` and pin store `/taskbar.pin` use supported short names. The pin store is 40 bytes and contains `gxos.builtin.calculator`.

### Hosted FAT regression

`tests/fs_fat_persistence_test.cpp` ran against the disposable prepared FAT32 image and passed 64 checks. Coverage includes supported 8.3 writes, LFN checksum association, same-directory rename/delete, stale LFN rejection, preservation of the original file after an injected one-shot flush timeout, timeout propagation after a successful rollback, and successful retry after the injected fault clears. This validates FAT/VFS transaction behavior on the hosted block fixture; it does not emulate the native ATA controller or QEMU `fat:rw:` backend.

## Native interaction results

| Scenario | Result | Evidence and limits |
| --- | --- | --- |
| QAC-managed UEFI boot and PS/2 input | PASS | Serial reports the active PS/2 mouse; HMP mouse movement and right-click visibly open guest menus. |
| Start menu and application launch | PASS | Native Calculator launch was exercised in the earlier settings/pin run. |
| Taskbar context menu and pin | PASS | Genuine taskbar right-click, `Pin to Taskbar`, and Calculator launcher; see `guest-fat32-r2-pinretry-context.png`, `guest-fat32-r2-pinretry-pinned.png`. |
| Pin cold restart | PASS | New QAC process loaded `/taskbar.pin`; `guest-fat32-r2-pin-cold-serial.log` and `guest-fat32-r2-pin-cold-desktop.png`. |
| Settings cold restart | PARTIAL | Bottom / Standard / icon-only / auto-hide Off survived a cold restart, with the Calculator pin. A later Top update failed and retained the old profile. |
| Auto-hide | PARTIAL | Top-docked hide and edge reveal were observed with PS/2 input in `guest-fat32-autohide-top-hidden.png` and `guest-fat32-autohide-reveal-check.png`. Bottom, left, and right behavior plus popup/preview/overflow interactions were not qualified. |
| Application grouping and member selection | NOT QUALIFIED | No genuine same-app two-window lifecycle was run. |
| Previews | NOT QUALIFIED | No complete hover, movement, activation, dismissal, clipping, and invalidation flow was exercised in the guest. |
| Overflow and pagination | NOT QUALIFIED | No native capacity-overflow scenario was created. |
| Real File Explorer copy/move progress | NOT QUALIFIED | File Explorer was opened natively (`guest-fat32-file-explorer-open.png`), but no guest copy/move lifecycle was run; no progress result is claimed. |

Other actual screenshots under `out/taskbar/TASKBAR6R2/` include `guest-fat32-r2-settings-retry-menu.png`, `guest-fat32-r2-settings-retry-position-bottom.png`, `guest-fat32-r2-settings-retry-size-standard.png`, `guest-fat32-r2-settings-retry-labels-off.png`, `guest-fat32-r2-settings-retry-cold-desktop.png`, `guest-fat32-r2-final-retry-desktop.png`, and `guest-fat32-r2-final-retry-position-updated.png`. They are unmodified guest captures.

## Acceptance gates

| Gate | Status | Reason |
| --- | --- | --- |
| R2-G1 — QAC guest boot and verified PS/2 input | **PASS** | Managed UEFI boots and visible guest actions were delivered through PS/2. |
| R2-G2 — FAT settings-save failure resolved or conclusively characterized | **PARTIAL** | The raw-volume failure is localized to an ATA cache-flush timeout and now propagates correctly; a native update still fails after three retries. The QEMU directory backend remains separately unqualified. |
| R2-G3 — Pins and settings survive cold restart | **PARTIAL** | Calculator pin and one complete taskbar profile survived; a subsequent profile update did not. |
| R2-G4 — Native application grouping and selection | **PARTIAL** | Hosted model coverage exists, but no multi-window native sequence was qualified. |
| R2-G5 — Context menus and previews | **PARTIAL** | Native taskbar context menu and pin action passed; the full preview lifecycle remains open. |
| R2-G6 — Native overflow and pagination | **PARTIAL** | Hosted geometry/model coverage passed; native overflow was not exercised. |
| R2-G7 — Auto-hide and reveal | **PARTIAL** | Top-dock hide/reveal passed visually; other docks and popup interactions remain open. |
| R2-G8 — Real File Explorer progress lifecycle | **PARTIAL** | Explorer launch passed; real copy/move progress was not run. |
| R2-G9 — Focused regressions and builds | **PASS** | Focused tests and hosted/native builds passed; details below. |
| R2-G10 — Mainline divergence and integration risks | **PASS** | Local divergence and merge-tree were assessed. Remote refresh failed; no merge or push occurred. |

## Regressions and builds

- Taskbar application model: **113 checks passed**, including settings-load deferral and popup routing.
- FAT persistence: **64 checks passed**.
- App Model identity: **25/25**.
- App Model application actions: **55/55**.
- Compositor pointer/buttons: **PASS**.
- Window UI regression: **PASS**.
- Compositor resize notification: **PASS**.
- Hosted build (`cmd /c build.bat`): **PASS**.
- Native AMD64 kernel (`mingw32-make -C kernel ARCH=amd64 all`): **PASS**.
- `git diff --check`: **PASS**.

Compiler output includes existing warning classes such as misleading indentation and unused functions; they did not fail the builds.

## Changes in this milestone

- `kernel/core/kernel_compositor.cpp`: use FAT-supported `/taskbar.pin` for writes, retain legacy-store read fallback, and retry a transient pin-store timeout.
- `kernel/core/desktop.cpp`: defer settings load until a writable mount exists, reapply layout when settings load or auto-hide transitions, preserve save failure statuses, and retry transient I/O up to three times.
- `kernel/core/fs_fat.cpp` and `kernel/core/vfs.cpp`: preserve and expose a rename flush failure across a successful same-directory rollback.
- `taskbar_application_model_core.h` and focused tests: cover mounted-settings load gating, overlapping popup routing, pin path, and timeout rollback/retry.

## Integration plan

Do not integrate this branch yet. First isolate and fix the native ATA cache-flush timeout on the conventional raw FAT32 volume, then repeat the taskbar settings replacement and verify the full profile after a clean cold boot. Keep QEMU's directory-backed FAT result separate and either qualify its rename semantics or document it as unsupported for this persistence workflow. After storage passes, finish genuine native grouping/selection, preview behavior, overflow pagination, and File Explorer copy/move progress.

Before any later merge, refresh main through an authorized remote path, repeat the merge-tree review, preserve the mainline documentation-only change, then run the hosted suite, native AMD64 build, and QAC cold-boot/persistence checks on the merged candidate. No push, merge, rebase, or release change was performed in TASKBAR6R2.
