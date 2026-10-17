# Settings S17 — Built-in Wallpaper Mutation Acceptance

**Outcome: A — Accepted.** The native Settings wallpaper transaction succeeded on an isolated writable raw FAT32 ESP using the production AMD64 kernel. `legacy_guidexos_space` was visibly applied, verified from canonical `/desktop.cfg`, survived a cold QEMU relaunch from the same ESP, and was recognized by native Settings and legacy Display Options. Cancel performed no write. The existing native gradient path also committed `gradient_midnight` afterward. The earlier QEMU `fat:rw` host-directory run remains a separate backend discrepancy and is not treated as production raw-device persistence evidence.

## Repository and scope

- Repository: `D:\dev\guideXOSServer`
- Branch: `main`
- Expected starting HEAD from the brief: `30bf2c9a608adfa7dae18d0b1f4251e5596c435f`
- Actual starting HEAD: `c11a97da4a70702ee8f57964a17a90317705dd86` (the repository had advanced; no branch or history changes were made)
- Scope: built-in wallpaper selection and shared desktop appearance mutation; no custom imports, arbitrary paths, or new image formats.
- The existing S16/S16R acceptance artifacts were preserved. S17 validation media is a fresh writable clone of the S16R repaired ESP.

## Architecture and asset ownership

The desktop appearance owner remains authoritative. Its existing built-in wallpaper catalog in `kernel/core/desktop.cpp` defines 15 IDs, names, full GXIMG paths, thumbnail paths, and preview colors. The same owner loads and decodes the existing GXIMG assets, renders the desktop background, exposes bounded catalog descriptors, and reads the supported scale mode. Native Settings does not add an image decoder, renderer, asset store, or configuration store.

The catalog entries are:

| ID | Name | Full asset |
| --- | --- | --- |
| `legacy_blue_flower` | Blue Flower | `/system/wall/blueflwr.gxi` |
| `legacy_dinos` | Dinos | `/system/wall/dinos.gxi` |
| `legacy_flower` | Flower | `/system/wall/flower.gxi` |
| `legacy_guidexos_space` | guideXOS Space | `/system/wall/gspace.gxi` |
| `legacy_guidexos_space2` | guideXOS Space 2 | `/system/wall/gspace2.gxi` |
| `legacy_red_flower` | Red Flower | `/system/wall/redflwr.gxi` |
| `legacy_ameoba` | Ameoba | `/system/wall/ameoba.gxi` |
| `legacy_ameobagx` | Ameoba GX | `/system/wall/ameobagx.gxi` |
| `legacy_tron_porsche` | Tron Porsche | `/system/wall/tronpor.gxi` |
| `legacy_wallpaper2` | Wallpaper 2 | `/system/wall/wallp2.gxi` |
| `legacy_merlin` | Merlin | `/system/wall/merlin.gxi` |
| `legacy_merlin2` | Merlin 2 | `/system/wall/merlin2.gxi` |
| `legacy_green_meadow` | Green Meadow | `/system/wall/greenmedow.gxi` |
| `legacy_cpu` | CPU | `/system/wall/cpu.gxi` |
| `legacy_mountains` | Mountains | `/system/wall/mountains.gxi` |

The provider returns only descriptors whose built-in asset is available through the VFS. The production UI showed thumbnails for the first four entries after the wallpaper pack mounted. Image loading and background application remain in the desktop owner and preserve the existing supported scale mode.

## Native Settings and mutation contract

Personalization now presents the existing gradient controls beside a Wallpapers tab, four wallpaper choices per page, the current background and scale, pending selection, Apply and Cancel, and operation feedback. Selection is separate from the active desktop state. Keyboard navigation and mouse selection were both exercised; selecting Dinos by keyboard and guideXOS Space by mouse changed the pending choice while Forest remained active.

Production acceptance exposed a focus-input mismatch: the PS/2 keyboard path supplies Tab as ASCII `0x09`, while Settings previously recognized only `shell::KEY_TAB`. `kernel/core/kernel_apps.cpp` now accepts both forms. In the raw-FAT guest, Tab focused Apply, and two Tabs focused Cancel, so the normal keyboard transaction path was exercised end to end.

The versioned `ApplyBuiltInWallpaper` request carries an exact structure size, version, bounded built-in ID, and expected appearance revision. The provider authorizes using the runtime-owned Settings application identity assigned by AppManager; it does not trust an identity supplied by the caller. Invalid size/version/ID, unauthorized calls, missing assets, and stale revisions are rejected. A stale revision refreshes authoritative state and clears pending confirmation.

Wallpaper and gradient requests use the same desktop transaction owner. It checks the current revision and asset, captures the current canonical config bytes, applies the requested built-in background, verifies active state, updates only the wallpaper identity in `/desktop.cfg`, and verifies exact saved bytes by readback. On write/readback failure it attempts to restore the prior bytes and runtime background and reports the resulting state. It emits the authoritative appearance change only after a verified commit. The existing gradient API remains a wrapper over the shared persistence path.

Persistence remains an in-place FAT write with readback and rollback checks. It is not crash-atomic and no power-loss guarantee is claimed.

## Regression results

All results below were run during S17, separate from historical S16/S16R counts.

| Suite | Result |
| --- | ---: |
| Native Settings provider/runtime/persistence | 1,289/1,289 passed; lifecycle 100, refresh 250, notification/state transitions 1,000 |
| Settings presentation core | 2,649/2,649 passed |
| Hosted Settings Center model | 89/89 passed |
| Default Apps model and interaction | 39/39 model, 9/9 interaction, 100/100 lifecycle |
| Settings inventory | 21/21 device and 24/24 storage |
| Settings users model/navigation/layout | 11/11, 8/8, 13/13 |
| App Model identity | 25/25 |
| App Model file activation | 60/60 |
| Compositor pointer buttons | Passed |
| Hosted VFS reads | 4/4 |
| Display configuration persistence source smoke | Passed |
| Settings presentation freestanding compile/link/symbol checks | Passed |

The native runtime suite includes built-in catalog enumeration, request validation and authorization, revision conflicts, no-op and apply flows, persistence failures, readback mismatch, rollback reporting, and repeated failure/commit coverage. The hosted VFS suite checks host-backed reads; it does not qualify the production FAT write path. The production raw-FAT guest run below qualifies the production writer for the exercised wallpaper and gradient commits. One native runtime test compile emitted the existing unused-function warning for `current_stack_pointer()` in `app_launch_target_resolver.cpp`.

## Production build

Command:

```powershell
mingw32-make -B -C kernel ARCH=amd64 BUILD_DIR=build/s17r-clean-amd64-20261017
```

The clean AMD64 build completed with zero compiler errors. Its log has 463 warning occurrences across 232 unique warning lines; comparison with the previous S17 production log found no added or removed warning lines. The 1,289-test native runtime build emitted the one existing unused-function warning noted above. The freestanding Settings presentation compile/link/symbol script passed with four object files. `nm -u` reports the same 21 undefined symbols as the prior S17 kernel (zero added or removed), and `nm -C` found no `std::` symbols.

| Artifact | Size | SHA-256 |
| --- | ---: | --- |
| `kernel/build/s17r-clean-amd64-20261017/bin/kernel.elf` | 2,740,176 | `362B3213D1600AA854BA31E718E437A28FC74E49D5C59747E6DCBBBEF1362515` |
| `kernel/build/s17r-clean-amd64-20261017/bin/kernel.pe` | 2,855,932 | `FFCB1AA4C3F24F7726772583E0B88DAD470FA985DAD5AAD03B9CDA8F62486512` |
| Isolated ESP bootloader `EFI/BOOT/BOOTX64.EFI` | 56,832 | `E2FDB6AD78E0416AF250BEBBC6541640E3CCDC7DD9A14453C08143B7F4DA48AA` |
| Isolated ESP wallpaper ramdisk | 67,108,864 | `5C9B3C5302E72F845448688A4C57DA91FD95F111946F5FCA4C30E452449555FD` |

The production build log SHA-256 is `80CA6F0DBC216E8E686E7865E58C7CD836701E9F38C12DD4E02450E8A6DB08C9`. The final raw guest image is 134,249,984 bytes with SHA-256 `36FB48547B67BF9B908D986C682CE7499AC74F2AB5802EAB0EAE89DAC3A73303`; this is the same isolated image used for wallpaper Apply, cold relaunch, Cancel, and the later gradient regression. Its initial pre-guest image hash was `7AD2F44C8B02A88B99A5F27FCA450224BF5D8D33508DD0B180AC099FA6DFCDE4`. Hash details and the exact build identity are in `docs/evidence/settings-s17r-20261017/artifact-hashes.txt` and `build-identity.txt`.

## QEMU evidence and result

The guest used the final ordinary AMD64 production kernel on a fresh writable raw FAT32 image cloned from the S16R-repaired ESP. The image has a single FAT32 LBA partition beginning at LBA 63, 128 MiB volume, and 134,249,984-byte raw media. QEMU used TCG, 2 GiB RAM, a 1280×800 VGA display, disabled networking, and isolated QMP ports 62319–62322. Before boot, the only foreign QEMU process found used `D:\dev\guideXOSServer_FONT_SUPPORT\out\font9-validation`; it was left untouched and later exited. The separate DM36 QMP console/port was also left untouched. Every S17R guest was shut down through its own QMP `quit` command.

The clean writable test image was made from `ESP-production-seed` with the production `kernel.elf`; the untouched development/release media and sibling repositories were not used. The raw image passed `desktop.cfg` writes, flush, exact readback, a fresh process cold relaunch, Cancel/no-write, and a subsequent gradient commit.

Observed workflow:

1. The initial state was `gradient_forest`; Personalization showed the real built-in wallpaper thumbnails. Keyboard arrows selected `legacy_guidexos_space` while Settings continued to report `Current: Gradient / Forest` and `Pending: guideXOS Space`.
2. The Tab focus fix moved focus to Apply. Enter committed the selection. Serial reports `background.apply status=0 active=legacy_guidexos_space persisted=legacy_guidexos_space`; the FAT writer wrote and flushed `0x107` (263) bytes with `FAT_FILE_WRITE_OK` / `VFS_OK`, then the desktop rendered `/system/wall/gspace.gxi`. The wallpaper is visible around the Settings window and on an unobstructed desktop screenshot.
3. Settings navigated Display→Personalization with the committed wallpaper intact. Settings was closed; a new QEMU process booted from the same raw image and the serial trace rendered `legacy_guidexos_space` during startup. The unobstructed post-relaunch desktop and reopened Settings both show guideXOS Space as current.
4. After Apply, Control Panel→Display Options displayed the same wallpaper catalog and highlighted guideXOS Space. The desktop wallpaper remained visible behind the legacy window.
5. On the cold-relaunch guest, Flower was selected as pending, Tab twice moved focus to Cancel, and Enter cleared the pending selection. UI status was “Background selection cancelled”; Space stayed active. After QEMU shutdown, `/desktop.cfg` was byte-identical to the post-Apply snapshot.
6. A separate boot of the same isolated image applied `gradient_midnight` through native Settings. Serial reports `background.apply status=0 active=gradient_midnight persisted=gradient_midnight`; the final desktop was stable and showed the gradient. This regression write happened after the wallpaper cold-relaunch proof.

The baseline `/desktop.cfg` was 257 bytes and had SHA-256 `3E4773874A2E14FBC4D539FF79D3ADF41F87A6909A72AEBE83207D4ECEA6688F`. It contains the inherited incomplete final key `autoArrangeDesktopIcon`; the mutation kept that legacy data unchanged.

| Config snapshot | Bytes | SHA-256 | Difference |
| --- | ---: | --- | --- |
| Before guest run | 257 | `3E4773874A2E14FBC4D539FF79D3ADF41F87A6909A72AEBE83207D4ECEA6688F` | Baseline; final key is inherited/truncated |
| After wallpaper Apply | 263 | `9C54C9CCBA5C4B74833B800E7CA01E21246FD35EA41C79FAAFD1FB27321BD727` | Only `wallpaperId` changed to `legacy_guidexos_space`; remaining bytes identical |
| After Cancel | 263 | `9C54C9CCBA5C4B74833B800E7CA01E21246FD35EA41C79FAAFD1FB27321BD727` | Byte-identical to post-Apply; Cancel did not write |
| After gradient regression | 259 | `1AD536A4F2138461C0165AA480C92D15A5E5390429DB8F2732D1CB7D7E4CA19B` | Only `wallpaperId` changed to `gradient_midnight`; remaining bytes identical |

The final raw image’s non-config contents match the seed for 11 of 12 files; the only content difference is the ESP `NvVars` firmware-state file. The exact file-content hash comparison is preserved in `docs/evidence/settings-s17r-20261017/esp-file-content-comparison.txt`. The QEMU `fat:rw` host-directory run previously recorded in the S17 evidence still has its separate 257→263 readback/mirror discrepancy; raw FAT production media succeeds, so that earlier host-directory behavior remains a follow-up rather than a production raw-device failure.

Selected committed evidence:

- `docs/evidence/settings-s17r-20261017/desktop-initial.png`, `wallpaper-pending-space.png`, `wallpaper-applied-settings.png`, and `desktop-applied-space.png` — initial, pending, applied, and unobstructed desktop states.
- `docs/evidence/settings-s17r-20261017/desktop-cold-relaunch.png` and `settings-after-cold-relaunch.png` — same-image cold relaunch and authoritative Settings state.
- `docs/evidence/settings-s17r-20261017/legacy-display-options-after-apply.png` — legacy gallery after commit.
- `docs/evidence/settings-s17r-20261017/wallpaper-pending-cancel.png`, `cancel-focus.png`, and `cancel-result.png` — pending, Cancel focus, and no-pending state.
- `docs/evidence/settings-s17r-20261017/gradient-applied-settings.png` and `desktop-final-gradient.png` — retained gradient path.
- `docs/evidence/settings-s17r-20261017/serial-*.log.gz` and `qmp-*.log` — byte-preserved guest traces and input/boot transcripts.
- `docs/evidence/settings-s17r-20261017/desktop.cfg.before`, `desktop.cfg.after-wallpaper`, `desktop.cfg.after-cancel`, and `desktop.cfg.after-gradient` — raw FAT config snapshots.

## Limitations and readiness

S17 is accepted for built-in wallpaper selection and persistence on the production raw FAT device path. This evidence does not qualify QEMU’s `fat:rw` host-directory mirror after the earlier readback anomaly, and it does not establish crash/power-loss atomicity. The baseline’s incomplete `autoArrangeDesktopIcon` key remains unchanged. One diagnostic serial label on Cancel still says `gradient.pending cancelled` even when the pending item was a wallpaper; the visible UI status is correct. No broad VFS rewrite was made.

This result does not advance S9, Settings 1.0, or server v0.3 acceptance.

## Recommended S18 scope

1. Isolate the earlier `fat:rw` host-directory readback/mirror discrepancy from raw FAT behavior, inspecting guest sectors, FAT directory length, and host mirror independently.
2. Add focused exact-byte FAT overwrite/shortening coverage for the production config writer, including the inherited truncated trailing key.
3. Re-qualify failed commit/rollback and the misleading `gradient.pending cancelled` serial label for both wallpaper and gradient paths.
4. Continue bounded cross-application coverage for wallpaper→gradient, two-image switching, conflict refresh, and VFS mount-loss behavior without broad filesystem changes.
