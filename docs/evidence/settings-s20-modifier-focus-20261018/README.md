# S20 modifier-aware focus evidence

This directory contains the committed acceptance artifacts for [the S20 report](../../SETTINGS_S20_MODIFIER_AWARE_FOCUS_ACCEPTANCE.md). Raw validation images, build outputs, test executables, and QEMU working files remain preserved under out/validation.

## Evidence groups

- final-recheck1.* records the corrected production ELF completing the full guest lifecycle, including a real QMP Shift+Tab, ordinary Tab after Shift release and app reopening, actual 660×440 resize and focus review, Settings cancellation, legacy Control Panel Alt+F4, and the stable desktop.
- final-recheck1.retry1.qmp.log and final-recheck1.serial.log contain the actual guest input transcript and final kernel/application diagnostics. The Settings Tab diagnostic is bounded to 16 events per app instance.
- final-recheck1.retry1.launch.json and final-recheck1-retry1.media-staging.json identify the corrected ELF, private raw image, OVMF firmware, QMP endpoint, process termination, and image hashes. The pre-run patch image hash 0C26C66C... was independently reproduced from the recorded source raw image and final kernel, matching the unrun sibling image. The guest raw media post-run hash is 4A51FF25....
- final-recheck1.desktop-cfg-before and final-recheck1.desktop-cfg-after.bin are extracted 234-byte DESKTOP.CFG snapshots. They compare byte-for-byte equal and both have SHA-256 CF310FEA3047F36350F99DB5E2FF458A20E85E6AAE9449F320BFE57F14967249. final-recheck1.desktop-cfg-comparison.json records the comparison.
- final-kernel.* contains the earlier focused real modifier sequence. production-sequence.* contains the ordered Settings and legacy interactions from the immediately preceding S20 build; the corrected final-recheck1.* now repeats that lifecycle on the final ELF.
- resize-attempt.* preserves an earlier non-effective resize attempt for history. final-recheck1.minimum-660x440.png and its focus screenshots record the successful minimum-size qualification.
- tests/ contains the acceptance executables, regression transcripts, freestanding validation, and final production build log and summary.

## Final artifact identity

- Kernel ELF: 2,771,240 bytes, SHA-256 F5AA964DA871DEAD7259D766DBAA4F67BE5485AF9DF3AFCD7812FE559ECA9AEF.
- Kernel PE: 2,888,634 bytes, SHA-256 159D82DADE3B2BF3EEE607FA6C286B561BC9128D4048F1A4096C6DAE17552589.
- Final private raw image: pre-run patch identity 0C26C66C25457A127E3512355058D27D588EB3CB368D5FE4293115D9CDB78C96; post-run identity 4A51FF256BB6E0E463365B66CA1137227553829E11B866FF10C7C0FF95AAC5B2.
- Read-only OVMF firmware: SHA-256 2DF617A6FD1BAE41EC7C0E3BCECA96C928A5A88D8DC198468D34733393AB2792.

SHA256SUMS.txt covers every file in this folder other than the manifest itself.

Trailing alignment whitespace was removed from the committed serial and compiler text-log copies; their original task outputs remain preserved under out/validation.

Evidence text and extracted DESKTOP.CFG snapshots are stored without Git line-ending conversion so the checksum manifest stays reproducible across checkouts.
tests/production-build-final-recheck2.log is a clean successful serial production build transcript; production-build-final-recheck1.json records the tested final ELF and PE identities plus warning comparison.
