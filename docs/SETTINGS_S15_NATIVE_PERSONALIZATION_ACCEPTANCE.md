# Settings S15 — Native Personalization and Desktop Appearance

Status: **S15 — Outcome A: Native Personalization Accepted**

## Preflight

- Repository: `D:\dev\guideXOSServer`
- Branch: `main`
- Starting HEAD: `6b89e42ef9a188da4c55fc0a5e80863e32ad79a4`
- S14 and S14R were already accepted at the starting checkpoint. The S14R report records that S15 had not started. This work implements S15 only; S14R was not rerun.
- Pre-existing untracked outputs under `out/` were preserved: `out/desktop_control_theme_test.exe`, `out/v03-amd64-qemu-candidate/`, `out/v03-amd64-scifi-final-build.log`, `out/v03-amd64-scifi-final-candidate/`, `out/v03-amd64-scifi-repro-build.log`, `out/v03-scifi-hosted-experimental.log`, and `out/v03-scifi-hosted-standard.log`.

## Implementation

Native Settings now has a Personalization page backed by a versioned, bounded, value-only snapshot of the live desktop owner's appearance configuration. The provider reads the effective built-in wallpaper or gradient, stable ID, display name, scale mode, gradient palette when applicable, and the four supported system desktop-icon visibility settings. The snapshot uses a desktop-owned revision and exposes no pointers into kernel desktop state.

The provider authorizes the existing `gxos.builtin.settings` runtime identity, validates version, structure sizes, capacity, revision, background kind, scale mode, colors, and known icon flags, and returns caller-owned data. The Settings adapter continues to use the existing mutation-rejecting operation callback; S15 adds no appearance write API and does not change `/desktop.cfg` persistence. The native page refreshes when selected and when the Settings window regains focus.

The existing Control Panel and Display Options code paths were not changed. Their production acceptance is already recorded in the S14R report, so they were not repeated here.

## Validation

The native runtime/provider suite passed **842/842** checks, including wallpaper and gradient snapshots, desktop icon flags, authorization, stale revisions, invalid requests, and unavailable source data. It ran 100 lifecycle repetitions, 250 provider refresh cycles, and 1,000 notification/state transitions. The Settings freestanding validation passed compile, bounded link, and unresolved-symbol checks.

A clean AMD64 production kernel build completed with **0 errors** and 233 warning lines. No warning was emitted from the new appearance provider code. The kernel ELF is 2,700,152 bytes with SHA-256 `1809207B87597DD97EE42CA3068C308EBF52744F6FB492CC642CCA2865220489`. The isolated guest copies used that exact kernel, the accepted bootloader hash, and copied OVMF firmware; each ESP has a file hash manifest.

## Production QEMU acceptance

Two task-owned QEMU guests booted from independent writable ESP copies and copied OVMF firmware, with networking disabled and distinct loopback QMP ports. Both reached the desktop and were stopped through their own QMP endpoints.

1. **Gradient guest** — Start → Settings → Personalization, selected with the mouse. The page reported Gradient / Sunset, ID `gradient_sunset`, scale `fill`, top `#FF5E1B45`, bottom `#FF17101E`, and accent `#FFEB6A55`. It reported Trash, This System, and File Manager as Shown, and Settings as Hidden.
2. **Built-in wallpaper guest** — An independent test fixture selected `legacy_ameobagx` before boot. The desktop rendered that wallpaper, and the native Settings page reported Wallpaper image / Ameoba GX, ID `legacy_ameobagx`, scale `fill`, and the same four icon visibility values. The page was selected with its `P` keyboard shortcut.

For the gradient guest, `/desktop.cfg` remained byte-for-byte unchanged at SHA-256 `ED2CED9BD60AC15E67663BD955CBFC9DAAD36A47D368AB9DD3C2EF1CCA074A1D`. For the separate wallpaper fixture guest, `/desktop.cfg` remained byte-for-byte unchanged at SHA-256 `2107E99B3E3A6E729E0BC97AF2246B1CA782BF5FA95434A3BCB45C04192A45FF`. Thus Settings displayed the production configuration without writing it.

The screenshots, serial logs, QMP transcripts, launch records, configuration copies, ESP manifests, and build/test logs are indexed in [the S15 evidence directory](evidence/settings-s15-20261016/). The [test results summary](evidence/settings-s15-20261016/test-results.md) contains suite outcomes. The [S14/S14R acceptance report](SETTINGS_S14_DISPLAY_REFRESH_AND_TOPOLOGY_ACCEPTANCE.md) remains the record for previously accepted Control Panel behavior.

Direct evidence: [gradient Personalization page](evidence/settings-s15-20261016/guest/gradient/personalization-gradient.png), [built-in wallpaper Personalization page](evidence/settings-s15-20261016/guest/image-wallpaper/personalization-image-wallpaper.png), and [SHA-256 evidence manifest](evidence/settings-s15-20261016/evidence-manifest.sha256). Each QEMU stderr log contains one `Ignoring request for interrupt vector 0` warning; both guests reached the desktop, ran through Settings, and exited cleanly through QMP. Only the two task-owned QMP endpoints were used; no command targeted the previously observed foreign QEMU process.

## Acceptance result

**S15 — Outcome A: Native Personalization Accepted.** The production Settings page reports the active built-in wallpaper or gradient and the supported system icon settings from the existing desktop configuration owner. The read-only contract and legacy `/desktop.cfg` persistence were preserved.
