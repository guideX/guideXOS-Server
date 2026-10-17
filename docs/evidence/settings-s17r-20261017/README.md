# S17R raw FAT acceptance evidence

This directory contains the committed acceptance screenshots, gzip-compressed, byte-preserved serial and plain-text QMP transcripts, exact `/desktop.cfg` snapshots, build identity, hashes, and test summary for the S17R continuation.

The production kernel was booted from a fresh writable raw FAT32 ESP at `out/validation/settings-s17r-20261017/guest/guideXOS-S17R-production.raw`. That same image was used for wallpaper Apply, a new-process cold relaunch, Cancel/no-write verification, and a later gradient regression. The final raw image remains in `out/`; it is not duplicated here.

The wallpaper persistence proof is the sequence `desktop-applied-space.png` → `desktop-cold-relaunch.png` → `settings-after-cold-relaunch.png`, backed by the matching serial/QMP logs and `desktop.cfg.after-wallpaper`. The later gradient regression is documented separately and leaves the raw image in a final `gradient_midnight` state.

`esp-file-content-comparison.txt` compares all files in the seeded ESP against the final raw image. The `NvVars` content is firmware state; every other non-config file matches the seed. The S17 host-directory `fat:rw` mismatch remains separately documented in the acceptance report.
