# S22R28R1 evidence index

This directory preserves the qualification evidence independently from the external build workspace.

## Candidate 1

Candidate 1 used the BootInfo v2 loader from the reused accepted ESP with the S22R28 kernel. Its serial log records the first guest failure at frame-owner initialization and the CPL3 skip. The QAC records identify its exact managed child, QMP stop, and verified reservation release.

- artifact-manifest.json records ISO, kernel, embedded ESP, and package-policy identities.
- artifacts/guest-com1-r2.serial.log is the complete serial transcript.
- artifacts/guest-com1-gate-a-failure.log is the retained first-failure extract.
- artifacts/qac-run-r2-console.log and artifacts/qac-run-v1-inspect.json record the reservation and child disposition.
- artifacts/qmp-status-r2.log and artifacts/qmp-gate-a-stop-r2.log record guest status and host-requested QMP quit.
- artifacts/iso-verify-report.json and artifacts/embedded-esp-verify-report.json record independent media verification.

## Candidate 2

Candidate 2 uses the current BootInfo v3 loader and is the exact ISO run that completed three CPL3 cycles.

- artifact-manifest.json and artifacts/iso-verify-report.json identify the ISO and embedded image.
- artifacts/qemu-child-identity-v3c.json records the direct QEMU child and exact command line.
- artifacts/qac-run-v3c-console.log, artifacts/qac-final-v3c-inspect.json, and the final status/list/queue files record QAC ownership and release.
- artifacts/qmp-pre-cont-v3c.json and artifacts/qmp-session-v3c.log record the paused state, continuation, and running state; artifacts/qmp-gate-d-stop-v3c.log records host-requested shutdown.
- artifacts/qac-closeout-after-task.txt records a later read-only QAC snapshot. It identifies a separate managed session that appeared after the CPL3 reservation was already Released; that session was left untouched.
- artifacts/guest-com1-v3c.serial.log is the complete guest transcript. Its SHA-256 is recorded in run-summary.json.
- The v3a and v3b QAC logs preserve two firmware configuration attempts that exited before guest execution.
- run-summary.json records session IDs, child identity, ISO hash, timing, observed milestones, and limitations.

## Preflight

preflight/preflight-results.json records focused hosted tests, build identities, symbol audit, toolchain, and stack measurements. The symbol-audit output and current-loader build log are preserved alongside it.

The full ISO files, staged ESPs, build trees, and complete external receipts remain under E:\CodexArtifacts\S22R28R1. No ISO or firmware binary is copied into the repository.
