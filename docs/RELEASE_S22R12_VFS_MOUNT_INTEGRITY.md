# S22R12 — VFS Mount-Table Integrity Investigation

## Outcome

**`VFS_CORRUPTION_SOURCE_ISOLATED`**. The mount-table damage is caused by an oversized automatic `NavigatorApp` object in the startup typography smoke test. The object’s compiled stack frame extends far beyond the 16 KiB boot stack into BSS, where the VFS mount table lives. The smoke URL copied into `m_title` and a `KernelWindow` widget initialization both map directly onto mount metadata in the pre-fix ELF layout. The automatic objects have been moved to a short-lived heap scope, and the hosted, freestanding, and candidate-build checks pass. Corrected-candidate guest qualification is still outstanding because QAC was occupied by verified reservations during this phase.

## 1. Repository provenance

- Repository branch: `main`. The expected S22R11 checkpoint `396a358ec60ae513240c44f5871e95a588b5bc03` is an ancestor of the investigation tip.
- The investigation began at `f556ae156bb701fb834a858c61f3589f0d940fc2`. While work was in progress, the live branch advanced to `b311cb09` with the QAC artifact-isolation integration; that commit and its working-tree state were preserved. The focused S22R12 source and regression-test commit is `ea786e2da4da01a57b1ab00429c8453edc9446ad`, based on `b311cb09`.
- The repository already contained approximately 4,061 untracked paths. They were preserved. The PacMan-only candidate manifest records `worktreeCleanAtPackagingStart=false`; its `sourceGitCommit` is the S22R12 source/test commit above.
- The original S22R9 candidate remains at `out/release-profile/95cf3e2d-778b-4b31-8948-4acdb7cc6aaa/artifacts/guideXOS-Server-v0.3.0-amd64-pacman-only-candidate.iso`. Its observed SHA-256 remains `e98bf087efc071ddf82db4dce9d79335aacbf38310de43b032744165792d712b`.
- No files under `dist` were changed. The server repository has no `.phase` file, and the sibling QAC repository’s `.phase` ownership was left untouched.

## 2. Mount-table representation and invariants

`kernel/core/vfs.cpp` owns a file-static `MountPoint s_mounts[VFS_MAX_MOUNTS]` and `uint8_t s_mountCount`. `VFS_MAX_MOUNTS` is 8, so valid indexes are 0–7. The count is the number of active registrations, not a high-water mark. Normal writers increment it once when activating a free slot and decrement it once when deactivating a mounted slot. Unmount leaves the remaining slot contents in place; subsequent registration reuses the first inactive slot.

Each `MountPoint` is 536 bytes. Both `path[256]` and `sourcePrefix[256]` are inline arrays. `mount_alias()` copies the resolved source-relative path into `sourcePrefix` with bounded `strcopy`; it does not retain a pointer to the source path or another temporary buffer. A direct mount stores its block-device index, device generation, filesystem type and volume index. An alias copies those source-mount identity fields. Unmount tears down the filesystem volume and marks the mount inactive; block-device lifecycle is managed separately.

The table has no lock around these operations. The recorded startup path performs the smoke synchronously on the boot CPU. A valid steady state has `s_mountCount` equal to the number of active slots. S22R11’s post-smoke state—count 5 with only slots 1 and 2 active—violates that invariant.

## 3. Identified mount-metadata writers

The source audit found four functions that write the table or its count:

| Function | Writes |
|---|---|
| `vfs::init()` (`kernel/core/vfs.cpp:351–365`) | On first initialization only, zeroes `s_mounts`, open-file and directory tables, resets the count and sets initialized. Repeated calls return without clearing live mounts. |
| `vfs::mount_type()` (`:403–485`) | Selects the first inactive slot; writes active, path, filesystem, block-device index/generation, volume index, read-only and alias fields; sets the direct mount prefix empty; increments count. |
| `vfs::mount_alias()` (`:507–573`) | Selects the first inactive slot; copies source-mount identity fields and the inline prefix; increments count. |
| `vfs::unmount()` (`:586–627`) | After filesystem unmount succeeds, marks the matching slot inactive and decrements count once. Repeated unmount returns without another decrement. |

`mount()` dispatches to `mount_type()` after filesystem detection. `mount_partition()` is unimplemented and returns `0xFF`. The other `s_mounts` references read table data, resolve paths, or store mount indexes in file/directory handles. No other VFS routine writes mount metadata. The observed corruption bypasses these APIs and their metadata writes.

## 4. Navigator URL ownership investigation

The URL clue is explained by address overlap, not by a dangling pointer. `MountPoint::sourcePrefix` owns inline storage, and alias registration copies its bytes. Navigator’s current URL, address buffer and title are also object-owned character arrays. `NavigatorApp::parseHtmlDocument()` copies the supplied URL into `m_currentUrl` and `m_title`; the typography fixture supplies `http://guidexos.test:8080/navigator-smoke/typography-phase7a.html` (`kernel/core/kernel_apps.cpp:7531–7560`, `:10334`).

In the exact pre-fix S22R11 v4 ELF, `s_mounts` is at `0x22b218e0`, with boot-stack bounds `0x22b6f000–0x22b73000`. The `NavigatorApp` automatic object begins at `s_mounts + 0x258`; `m_title` begins at `s_mounts + 0x540`. Slot 2’s inline `sourcePrefix` begins at `s_mounts + 0x543`. Thus the parser’s title copy places `p://guidexos.test:8080/navigator-smoke/typography-phase7a.html` at the start of that prefix: the first three URL characters land immediately before it.

The same old frame places `KernelWindow.widgets[27].x` at `s_mounts[0].active`. `Widget` construction initializes `x` to zero (`kernel/core/include/kernel/kernel_app.h:81–103`), explaining how the root mount can become inactive. The compiled `KernelWindow` layout is 3,984 bytes; the widget array begins at offset 136, each widget is 120 bytes, and `x` is at offset 8.

The old `smokeTypographyPhase7A()` prologue reserves `0x52108` bytes plus saved registers. The boot stack is only `0x4000` bytes. The resulting oversized stack frame crosses into BSS and overlays VFS state. The URL text and inactive root are two independently mapped consequences of that frame. There was no evidence of a prefix pointer alias, buffer-length defect in the bounded VFS copy, block-device generation change, or VFS unmount request.

## 5. Diagnostic instrumentation

No new VFS event ring or canary was added. The invalid stores arise from ordinary constructor/parser writes through an oversized stack frame, outside VFS APIs; an event buffer around legitimate VFS writers would not record those stores, and extra instrumentation would perturb the same stack/BSS layout under investigation. Existing S22R11 startup checkpoints already bracket the smoke and later discovery failure.

A debugger data watchpoint would identify the exact instruction and program counter. It was not attempted: QAC reported a verified active `guideXOSServer` acceptance reservation and a second verified reservation queued. No QEMU process was started or reservation queued by S22R12. The exact first machine instruction that crosses into mount metadata therefore remains uncaptured; the compiled layout and observed field contents establish the responsible operation and memory range.

## 6. First observed invalid transition

The preserved S22R11 QAC serial log `out/s22r11-diagnostic-20261021/guest-diagnostic-storage-backed-4/serial.log` records:

1. Before the typography smoke, `/Apps` is present at mount index 0 and `/Apps/PacMan/app.json` opens successfully. Startup then reports five mounts: `/`, `/system`, `/certs`, `/config` and `/config/navigator` (lines 275–276 and 417–418).
2. `typography.phase7a.result=PASS` is printed at line 742.
3. At the next Start refresh, the table still reports count 5, but only `/system` and `/certs` are listed active. `/certs` now points to the `p://...typography-phase7a.html` suffix. `/Apps` is absent and directory open returns `VFS_DIR_OPEN_NO_MOUNT` (lines 804–807).

The block-device snapshot at this checkpoint still reports the three devices at generation 1. The serial log contains no root-unmount event. Discovery then fails before FAT lookup or block I/O, and PacMan disappears from the refreshed Start menu. This is the first invalid transition bracketed by the available guest checkpoints; it follows the stack/BSS overlap without a VFS lifecycle call.

## 7. Root-cause classification

**Classification: out-of-bounds kernel stack use overwrote BSS mount metadata.** The startup typography smoke constructed a roughly `0x51078`-byte `NavigatorApp` automatic object in a function whose frame already reserved `0x52108` bytes. The fixed boot stack is 16 KiB. The object and its local window therefore wrote into neighboring BSS, including VFS mount metadata. `parseHtmlDocument()` explains the exact URL suffix; `Widget` initialization explains the cleared active flag.

The VFS lifecycle implementation itself maintains count/slot semantics in the hosted test. The failure is neither a stale pointer nor an incorrect count transition in `mount()`/`unmount()`. The unchanged count alongside mutated active/prefix fields is the expected signature of stores that bypass the VFS writer functions.

## 8. Focused repair

All seven automatic `NavigatorApp` objects in the startup smoke helpers were replaced with a scoped heap allocation (`NavigatorSmokeAppScope`) that deletes the app on scope exit and returns failure if allocation is unavailable (`kernel/core/kernel_apps.cpp:96–107` and the seven smoke helpers). The large `KernelWindow` remains a local object; its 3,984-byte size fits within the fixed stack. The corrected typography function reserves `0x1088` bytes plus saved registers (about 4.3 KiB total), and the pointer-smoke clone reserves `0x1278` bytes plus saved registers (about 4.8 KiB). Both are below the 16 KiB stack limit.

No VFS registration, remount, recovery, hardcoded PacMan registration, Native ELF compatibility or security logic was changed. Regression checks require all seven smoke fixtures to use the scoped allocation and exercise the VFS slot/count lifecycle directly.

## 9. Hosted and freestanding validation

Post-fix hosted results:

- `tests/navigator_smoke_stack_contract_test.ps1`: PASS; seven heap-scoped smoke fixtures, no automatic `NavigatorApp` instances.
- `scripts/run-vfs-mount-table-integrity-test.ps1`: PASS, 16/16 lifecycle and bounds cases.
- `scripts/run-vfs-hosted-read-test.ps1`: PASS, 4/4.
- `scripts/run-block-device-generation-test.ps1`: PASS, 5/5.
- `scripts/run-native-filesystem-contract-test.ps1`: build and test PASS. Existing compiler warnings remain.
- `scripts/run-navigator-app-action-delivery-test.ps1`: PASS, 20/20.
- `scripts/smoke-navigator-url-resolution.ps1`: PASS.
- `tests/pacman_source_registration_test.ps1`: PASS for pre-audio, audio-bearing, missing implementation and missing definition fixtures.
- `git diff --check`: PASS before the commits.

The freestanding AMD64 kernel build and symbol audit passed. The isolated kernel is under `out/s22r12-diagnostic-20261022/kernel/`; the audit reports no forbidden imports or relocations. The canonical profile build also records `stage.kernel=passed` and `stage.symbolAudit=passed` in its build identity. The MinGW host required the output-only `mingw-cxx-wrapper.py` to add `-include wchar.h` only for `native_elf_baremetal.cpp` and `system_font.cpp`.

The canonical release script’s default Windows PowerShell subprocess failed during PacMan packaging because it could not resolve `Get-FileHash`. A local copy of `scripts/create-release-iso.ps1` in the S22R12 output directory changed only its build-host resolution to PowerShell 7.6.5. The same `ServerV03PacManOnly` profile and canonical `build.ps1` then completed. This host adjustment and wrapper are retained under `out/s22r12-diagnostic-20261022/temp/` for provenance; neither is a source change.

## 10. QAC guest results

Corrected-candidate QAC boots in S22R12: **0**. The QAC coordinator remained online with a verified active acceptance reservation owned by another process and one verified reservation waiting in the queue. To respect exclusive QAC ownership, this phase did not queue a request, start a guest, interrupt a process or release another owner’s reservation.

Therefore this phase has no corrected-candidate observations for cold boot, persistent `/Apps`, repeated Start refresh, Navigator interaction, PacMan launch/gameplay/exit, Settings smoke or recovery. The S22R11 pre-fix observations above are retained only as forensic evidence.

## 11. Candidate hashes and provenance

The corrected candidate was built with the existing PacMan-only profile and remains unpublished under `out`:

- ISO: `out/release-profile/0eed0ed2-df00-403a-9646-0dffb22be3bb/artifacts/guideXOS-Server-v0.3.0-amd64-pacman-only-candidate.iso`
- SHA-256: `b6b7b0faeef779a4ac06abb96ad749d2ce906df2e7751c1b9c329dd0a23fc2af`
- Size: 90,245,120 bytes; manifest says `candidateOnly=true`, `releaseProfile=ServerV03PacManOnly`, `sourceGitCommit=ea786e2da4da01a57b1ab00429c8453edc9446ad`, `isoBackend=PyCdlib`; all required build stages passed.
- Kernel SHA-256: `1bbb1f4ea5f7937fae6b9f6381da3bc8cffcddd2c185b227116fe46e59384fdd`.

The separate bootable diagnostic derivative is `out/s22r12-diagnostic-20261022/diagnostic-boot-image-v3/guideXOS-Server-v0.3.0-amd64-s22r12-diagnostic.iso`, SHA-256 `1f6cefe41044ad71cb7fbe3f06f033e8a8642338c219c0ca20928728e09b5aca`. It is not the PacMan-only profile candidate and was not guest-qualified.

## 12. Remaining release and security blockers

- The fixed candidate still needs at least two independent storage-backed cold boots and the requested `/Apps`, mount-table, Navigator, repeated Start refresh, PacMan launch/gameplay/exit, Settings and recovery checks.
- A QAC/GDB watchpoint should confirm that startup smoke no longer writes the previously affected mount fields and should capture the first write if another invalid transition appears.
- S22R6–S22R11 Native ELF findings remain: kernel-mode Native ELF execution has no process isolation. The stack repair does not make untrusted Native ELF safe.
- The candidate manifest records a dirty worktree because pre-existing untracked files were preserved. It is not published and no `dist` or readiness marker was changed.

## 13. Recommended next Main Integration phase

Continue as S22R13 when QAC is available. Boot this exact candidate from fresh writable storage in two independent cold boots. Capture mount slots and count immediately before and after startup smoke; if GDB is available, set data watchpoints on `s_mounts[0].active` and slot 2’s `sourcePrefix`. Then exercise Navigator, repeat Start refresh, verify persistent PacMan discovery, launch and play PacMan, exit to the desktop, and complete Settings/Navigator smoke. Preserve the serial/QMP/screenshots and report only steps actually completed.
