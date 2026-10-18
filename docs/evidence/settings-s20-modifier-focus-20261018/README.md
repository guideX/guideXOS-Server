# S20 modifier-aware focus evidence

This folder contains reviewable evidence for [the S20 acceptance report](../../SETTINGS_S20_MODIFIER_AWARE_FOCUS_ACCEPTANCE.md). It was copied from task-owned validation outputs; the originals under `out/validation` were preserved.

## Evidence groups

- `final-kernel.*` records the corrected production ELF run. It contains the actual Tab, `sendkey shift-tab`, post-Shift-release Tab, disabled-wallpaper-Previous reverse boundary, and final desktop screenshots, with its QMP/serial logs and launch identity.
- `production-sequence.*` records the longer Settings, Personalization, lifecycle, and legacy Control Panel interaction from the preceding S20 ELF. That kernel differed from the corrected ELF by the later out-of-line compatibility fallback definition. The corrected ELF's focused modifier sequence is kept separate above.
- `resize-attempt.*` preserves the attempted minimum-size drag screenshots. The window remained at 760×560, so these are evidence of an unqualified attempt, not a 660×440 result.
- `tests/` contains output from the acceptance executables. `production-build.log` and `production-build-summary.json` preserve the final production build record.
- `desktop.cfg-before.bin` and `desktop.cfg-after.bin` are the extracted 234-byte `DESKTOP.CFG` snapshots from the first S20 private image before the corrected final-ELF run and from the final image after that run. They compare byte-for-byte equal. The final launch's explicit pre-run and post-run per-file records are also preserved as JSON; both report SHA-256 `CF310FEA3047F36350F99DB5E2FF458A20E85E6AAE9449F320BFE57F14967249` and size 234 bytes.

## Artifact identity

- Final kernel ELF: 2,770,944 bytes, SHA-256 `9495CCB3670C94F4C2AEAA321113858DC57C53C79876E89B56C191D1002BB9B3`.
- Final kernel PE: 2,887,206 bytes, SHA-256 `4042D82FF7B6100D0B0843FD3679D2A745A6DA94227CC0E412A027F702D47E22`.
- Private final boot media before guest run: SHA-256 `7071C7F711009D674F740989AB2A41BF45DA4C4A08BA6FCAB7FBA10407ED8475`.
- Private final boot media after guest run: SHA-256 `16774805ADC462AD88E3FBDC8988103855B883E13C9D01B92A05A6DA924F9AC6`.
- Read-only OVMF firmware: SHA-256 `2DF617A6FD1BAE41EC7C0E3BCECA96C928A5A88D8DC198468D34733393AB2792`.

Trailing alignment spaces were removed from the committed text-log copies; the original task outputs remain preserved under `out/validation`. `SHA256SUMS.txt` covers every file in this evidence folder other than the manifest itself.
