# RX37R1 Evidence Index

RX37R1 is Outcome C. Native acceptance did not start because the bounded FIFO request was not granted while a verified QAC acceptance guest remained active. No guest marker, screenshot, QMP action, runner exit, or RX37R1 QEMU child is claimed.

- `../REXX_RX37R1_SCORECARD.json`: gate results and remaining acceptance limits.
- `preflight.json`: branch/HEAD, phase guards, tracked state, and canonical starting untracked manifest.
- `source-audit.json`: callback wiring, query-status behavior, provider lifetime/generation checks, and SAY-path comparison.
- `regression-evidence.json`: historical RX37 hosted, sanitizer, and freestanding results; not rerun for RX37R1.
- `inventory-confirmation.json`: latest 75-entry inventory counts and unchanged classifications.
- `qac-help.txt`, `qac-status-final.txt`, `qac-list-final.txt`: QAC CLI help and final read-only snapshots; all commands exited 0.
- `qac-admission.json` and `qac-cancelled-request-inspect.json`: bounded request identity, waiting state, cancellation, and no-grant result.
- `qemu-qac-process-census.json`: coordinator, active QAC CLI, and QEMU child identities. The active guest belongs to a separate checkout and was left untouched.
- `protected-artifact-comparison.json`: all 31 root `.obj`/`.pdb` paths with starting/final sizes and hashes.
- `untracked-reconciliation.json`: canonical starting/final path-set manifests stored outside the repository and intentionally tracked RX37R1 documentation paths.

No raw guest disk, generated kernel image, protected object, or large temporary artifact was added to this evidence directory.
