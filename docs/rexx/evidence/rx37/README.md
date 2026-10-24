# RX37 Evidence Index

RX37 ends as Outcome B. The destination-capability API and hosted formatter model passed, and the freestanding AMD64 build passed. Exclusive QAC acceptance was not run because a managed acceptance reservation and queued requests already occupied the coordinator. No RX37 guest, screenshot, output marker, QMP session, or reservation release is claimed.

## Summary records

- `preflight.json`: starting branch, HEAD, tracked state, RX36 inventory, protected artifacts, and canonical RX36 untracked manifest identity.
- `protected-artifacts-guard.json` and `protected-artifacts-end.json`: 31 protected root objects/PDBs at start and end.
- `qualification-summary.json` and `../REXX_RX37_SCORECARD.json`: gate results and evidence references.
- `hosted-regression-runner.log` and `hosted-regression-exit.txt`: final hosted RX37 suite transcript and exit code.
- `hosted-say-output.json`, `.stdout.bin`, and `.stderr.txt`: captured output from the hosted CLI fixture only.
- `sanitizers-final-run.log` and `sanitizers-final-exit.txt`: final ASan and UBSan run; durable per-target logs and preservation records are in the two `sanitizer-*` directories.
- `native-build/`: isolated AMD64 output audit, make logs/status, ELF/PE hashes, object inventory, and protected-artifact preservation evidence.
- `qac-help-preflight.txt`, `qac-status-preflight.txt`, `qac-list-preflight.txt`, `qac-status-final.txt`, `qac-list-final.txt`, `qac-preflight.json`, and `qemu-process-census-final.json`: read-only coordinator identity/ownership snapshots. RX37 did not request a reservation.
- `untracked-reconciliation.json`: exact end path-set counts and hashes, with canonical ordinal-sorted start/end manifests stored outside the repository to avoid copying a 134,930-path baseline into Git.

The inventory snapshot is byte-identical to RX36. Regina remains NOT_RUN because no Regina executable was available. No external compatibility percentage is claimed.
