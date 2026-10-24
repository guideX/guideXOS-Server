# RX38 Evidence

This directory stores the RX38 preflight and end-state evidence. The detailed historical QAC review is in `native-evidence-audit.md` and `.json`.

- `preflight.json`, `source-delta.json`: starting branch/HEAD and source-current check.
- `inventory-reconciliation.json`: the current 75-entry inventory count, hash, and incomplete names.
- `hosted-regression.json`: newly executed hosted regression summary and direct RX37 result.
- `native-evidence-audit.md` / `.json`: RX22-onward guest evidence, exact open checks, and QAC lifecycle details.
- `qac-status-start.txt`, `qac-list-start.txt`, `qac-status-final.txt`, `qac-list-final.txt`: read-only coordinator snapshots. The final list saw two unmanaged/foreign QEMU processes and no reservations.
- `qemu-process-final.json`: read-only identity snapshots for those foreign QEMU processes. RX38 did not control it.
- `protected-root-start.json` / `protected-root-comparison.json`: all 31 protected root object/PDB paths, sizes, and SHA-256 values.
- `starting-untracked.paths.gz` / `ending-untracked.paths.gz` / `untracked-reconciliation.json`: canonical path manifests and exact path-set reconciliation. The uncompressed starting bytes are UTF-8 with LF and ordinal-sorted before gzip.
- `new-untracked-paths.txt`: additional paths still untracked after staging RX38 (hosted build/test outputs), regenerated immediately before commit.

The starting untracked path set was compared with the RX37R1 external baseline by exact ordinal path set: 141,969 entries matched. RX38 stores a fresh UTF-8/LF ordinal serialization; therefore its manifest byte hash is a new serialization hash, not the RX37R1 file hash.
