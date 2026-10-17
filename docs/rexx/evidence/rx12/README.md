# RX12 qualification evidence

- `hosted-baseline.log` records the inherited hosted run before edits.
- `hosted-final.log` records the full RX1–RX12 hosted regression after implementation.
- `sanitizers-isolated.log` records the corrected isolated MSVC ASan and Clang UBSan runs. Both ran 14 hosted C executables and 108 local corpus cases; Regina remained unavailable.
- `native-build.log` records the forced full AMD64 kernel build. Build artifacts remain outside the repository under the user's C: Codex directory.
- `native-link-checks.txt` summarizes the PE import-directory and RX11-vs-RX12 undefined-symbol comparison.
- `root-artifact-preservation-incident.md` documents the pre-existing root object-file overwrite and the correction to future sanitizer output paths.
- `native-guest/README.md` summarizes the disposable QEMU guest run. Its copied ESP, firmware, QEMU binary outputs, and raw PPM captures remain outside the repository.

The expanded corpus locally passed 108 expected-exit checks. Its Regina comparison status is `NOT_RUN` for all 108 cases, not a differential match.
