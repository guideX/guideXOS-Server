# S22R evidence — 2026-10-19

**Outcome B — Partial.** The shipped default wallpaper conflict is reproduced in the native runtime harness and repaired with a narrow effective-value comparison. The changed native runtime suite passes 1,426/1,426 checks. The release build integrity harness passes 13/13 checks. A fresh isolated freestanding AMD64 kernel build and its 21-name symbol audit pass.

The full v0.3 package remains blocked by DeveloperStudio package acceptance. The current `Apps/DeveloperStudio` manifest and ELF differ from the exact Phase 25 signed-off pair. The pair is pinned from the paired Server commit: a 1,112-byte manifest whose raw bytes hash to `D59C12267D360C4E31B70DEC7F9C1B0689FB5816011D681B38EE9CDEF0D2F53B`, plus the signed-off ELF. The Phase 25 status text omits the final `B` from its 63-character manifest digest. The 1,176-byte manifest under `ESP/Apps/DeveloperStudio` is a different historical file; its hashes were preserved and it was not used as a new package input.

No S22R ISO was assembled and no QEMU guest was started. The wallpaper configuration files in `appearance/` are source and expected unit-test fixtures; they are not guest readback. This evidence does not establish fresh UI Apply, close/reopen, or legacy Display Options behavior for an S22R candidate.

## Evidence map

- `appearance/repro-before-fix.log`: focused test failure before the persistence correction.
- `appearance/regression-after-fix.log` and `appearance/native-runtime-regression.log`: post-fix results; 1,426/1,426.
- `appearance/desktop.cfg.shipped-default`: exact 257-byte configuration input, SHA-256 `95b274e39e9dde431286f4eb00fd2175888430744e60ffdec3ba49375ba52947`.
- `appearance/desktop.cfg.after-dinos.expected`: unit-test expected bytes after changing only `wallpaperId` to `legacy_dinos`; this was not produced by a guest.
- `build/integrity-tests.log`: isolated build receipt and command failure tests, 13 assertions.
- The exact Phase 25 manifest fixture used by the acceptance-path test is `tests/fixtures/developerstudio-phase25-app.json` (1,112 bytes, SHA-256 `D59C12267D360C4E31B70DEC7F9C1B0689FB5816011D681B38EE9CDEF0D2F53B`); its digest was computed from the paired Server commit bytes.
- `build/developerstudio-preflight.log`: actual `build.ps1 -Arch amd64` preflight, exit code 1, with protected ISO/ESP hashes checked unchanged.
- `kernel/kernel-build.log` and `kernel/kernel-build-noop.log`: fresh normal-target compile and subsequent no-op make exit 0.
- `kernel/undefined-symbol-classification.md`: complete classification for all 21 distinct undefined names.
- `preflight/`: exact initial Git status and untracked path inventory; those pre-existing files were preserved.

`evidence-manifest.sha256` covers every file in this directory except itself. The original S22 package evidence remains under `settings-s22-v03-package-20261018/`.

Terminal trailing padding was removed from captured text logs; their reported messages and diagnostics are unchanged.
