# Settings S17 — Built-in Wallpaper Mutation Acceptance

**Outcome: B — Partial.** S17 implementation, automated tests, freestanding checks, and the production AMD64 build are complete. The real guest selection UI and asset thumbnails work, but a wallpaper Apply did not pass configuration readback. The transaction reported rollback to Forest, while the QEMU host-backed FAT directory retained a longer file with unexpected trailing bytes after shutdown. No successful wallpaper commit or same-ESP wallpaper persistence is claimed.

## Repository and scope

- Repository: `D:\dev\guideXOSServer`
- Branch: `main`
- Starting HEAD: `30bf2c9a608adfa7dae18d0b1f4251e5596c435f`
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

The native runtime suite includes built-in catalog enumeration, request validation and authorization, revision conflicts, no-op and apply flows, persistence failures, readback mismatch, rollback reporting, and repeated failure/commit coverage. The hosted VFS suite checks host-backed reads; it does not qualify the production FAT write path.

## Production build

Command:

```powershell
mingw32-make -B -C kernel ARCH=amd64 BUILD_DIR=build/s17-clean-amd64-20261017
```

The clean AMD64 build compiled 255 units and linked the ELF and PE boot kernels with zero errors. The build log contains 232 unique warning lines, one fewer than the 233 unique warning lines in the accepted S16 comparison; no new warning category or unresolved symbol was introduced. The only unmatched symbols in the comparison were existing platform/runtime hooks (`__dso_handle` and `mainCRTStartup`).

| Artifact | Size | SHA-256 |
| --- | ---: | --- |
| `kernel/build/s17-clean-amd64-20261017/bin/kernel.elf` | 2,740,176 | `274612CF9DF401460FCF21EA8DC276BA76862893439DA426A8AA90532B0C6EB2` |
| `kernel/build/s17-clean-amd64-20261017/bin/kernel.pe` | 2,855,932 | `DE22F5E806944BAEFE6C40EE6E704435878B1313F6F552C00122A449EB331B6` |
| Isolated ESP bootloader `EFI/BOOT/BOOTX64.EFI` | 56,832 | `E2FDB6AD78E0416AF250BEBBC6541640E3CCDC7DD9A14453C08143B7F4DA48AA` |
| Isolated ESP wallpaper ramdisk | 67,108,864 | `5C9B3C5302E72F845448688A4C57DA91FD95F111946F5FCA4C30E452449555FD` |

The S17 ESP manifest is preserved at `docs/evidence/settings-s17-20261017/esp-after.sha256`; build and guest artifact hashes are in `artifact-hashes.txt`.

## QEMU evidence and result

The guest used the clean production AMD64 kernel on a fresh writable S16R ESP clone, WHPX, 2 GiB RAM, 1280×800 display, disabled networking, and task-specific QMP port 62022. Other QEMU processes were identified and left untouched. The 2 GiB profile was required for the kernel to mount the wallpaper pack; the guest then displayed actual thumbnails.

Observed workflow:

1. Initial active state was Forest gradient (`gradient_forest`). The wallpaper page showed real Blue Flower, Dinos, Flower, and guideXOS Space thumbnails.
2. Keyboard selection of Dinos and mouse selection of guideXOS Space changed the pending selection. The desktop and active identity remained Forest before Apply.
3. Apply for `legacy_guidexos_space` returned status 8 (`ReadbackFailedRolledBack`). The native UI said “Save failed; previous state was restored.” Serial output reported `active=gradient_forest persisted=gradient_forest`; no wallpaper commit notification was accepted as success.
4. Cancel cleared a subsequent pending Dinos selection and left Forest active. Legacy Control Panel → Display Options also showed Forest as current.
5. The S17 host-backed FAT directory’s `desktop.cfg` was 257 bytes before Apply and 263 bytes after the first guest was shut down. The wallpaper ID remained `gradient_forest`, but the 263-byte file ended with unexpected trailing bytes (`autoArrangeDesktopIconurce-b`). The guest serial trace reported successful FAT write calls for 263-byte commit and 257-byte rollback, but commit readback failed. This mismatch between guest rollback verification and the QEMU host directory mirror remains unresolved.
6. To isolate the second cold boot, the test ESP’s config was restored from the untouched S16R clone’s original 257-byte file. A fresh QEMU process booted from the same isolated ESP and again loaded `gradient_forest`. This verifies the original state loads after relaunch; it does **not** prove wallpaper persistence.

The baseline config itself is 257 bytes and ends with the inherited truncated key `autoArrangeDesktopIcon`. Its SHA-256 is `3E4773874A2E14FBC4D539FF79D3ADF41F87A6909A72AEBE83207D4ECEA6688F`. The post-rollback host-mirror copy is preserved separately with its 263-byte hash. The config restored for the second boot is byte-for-byte equal to baseline. No claim is made that S17 preserved all config bytes through the failed host-backed transaction.

| Config snapshot | Bytes | SHA-256 | Difference |
| --- | ---: | --- | --- |
| Before guest run | 257 | `3E4773874A2E14FBC4D539FF79D3ADF41F87A6909A72AEBE83207D4ECEA6688F` | Baseline; final key is truncated |
| Host directory after failed apply/rollback | 263 | `B6D6CA321F6B60FC14F44490C4AE34A99DBE4448A8D082B4EA910AFFFE842536` | Same wallpaper identity and preceding fields; six unexpected trailing bytes |
| Test ESP after restoring baseline and cold relaunch | 257 | `3E4773874A2E14FBC4D539FF79D3ADF41F87A6909A72AEBE83207D4ECEA6688F` | Byte-for-byte baseline copy |

Selected evidence:

- `docs/evidence/settings-s17-20261017/desktop-whpx2g-initial.png` — initial desktop.
- `docs/evidence/settings-s17-20261017/wallpapers-tab.png` — populated built-in wallpaper catalog.
- `docs/evidence/settings-s17-20261017/wallpaper-pending-keyboard.png` and `wallpaper-pending-mouse.png` — pending selection while Forest stays active.
- `docs/evidence/settings-s17-20261017/wallpaper-applied.png` — apply failure and rollback feedback.
- `docs/evidence/settings-s17-20261017/cancel-after.png` and `desktop-after-cancel.png` — Cancel result and unchanged desktop.
- `docs/evidence/settings-s17-20261017/legacy-display-open.png` — legacy Display Options showing Forest as current.
- `docs/evidence/settings-s17-20261017/desktop-relaunch3-initial.png` — same isolated ESP cold relaunch, still Forest.
- `docs/evidence/settings-s17-20261017/serial-apply-failure.zip`, `serial-cold-relaunch.zip`, and `qmp-transcript.log` — raw guest serial and input/boot transcripts.
- `docs/evidence/settings-s17-20261017/desktop.cfg.before`, `desktop.cfg.after-rollback-host`, and `desktop.cfg.after-cold-relaunch` — config copies for byte-level comparison.

## Limitations and readiness

S17 is not accepted because native wallpaper Apply did not complete a verified persistent commit. The guest did not prove visible image application, Settings close/reopen after a successful commit, image recognition through legacy Display Options, or wallpaper restoration after a same-ESP cold boot. The underlying host-backed FAT readback/mirror discrepancy must be resolved before claiming exact config preservation. No VFS defect was repaired in S17, and no crash-atomic write behavior is claimed.

This result does not advance S9, Settings 1.0, or server v0.3 acceptance.

## Recommended S18 scope

1. Reproduce the 257→263→257 config mutation on a raw FAT image as well as the QEMU `fat:rw` directory backend, recording guest-sector data, FAT directory length, and host mirror independently.
2. Correct the demonstrated persistence/readback mismatch at the narrowest layer and add exact-byte FAT overwrite/shortening tests for the production config writer.
3. Re-run wallpaper commit, full unrelated-field comparison, cancel/no-write verification, legacy Display Options state, Settings close/reopen, and same-ESP cold relaunch using the committed production kernel.
4. Re-qualify gradient→wallpaper, wallpaper→gradient, two-image switching, rollback, and VFS mount-loss behavior without broad filesystem changes.
