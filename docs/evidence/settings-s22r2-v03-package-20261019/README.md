# S22R2 evidence — DeveloperStudio release contract

**Outcome C — Blocked at Gate A.** The exact Phase 25 DeveloperStudio package is identifiable and was signed off for its paired Server v0.5 rehearsal. The repository contains no approval to ship it with Server v0.3. Its manifest declares minimum Server version 0.5.0, and the current v0.3 guest does not provide the workspace, build, Run, and debugger services required by the application. The current source package is newer and unapproved. The release preflight correctly rejects that source pair. No artifact was selected, no preflight exception was added, and no production build or guest run was started.

The decision packet is in [release-contract-decision.md](release-contract-decision.md). [validation-status.md](validation-status.md) records the repository baseline, existing integrity results, preserved outputs, and the work stopped at the policy boundary. SHA-256 values for the substantive evidence files and updated qualification report are listed in [evidence-manifest.sha256](evidence-manifest.sha256); the checksum file does not hash itself.

The separate pre-existing DeveloperStudio compatibility note at docs/validations/DSBUILD1R2_PHASE25_COMPATIBILITY.md was read and preserved unchanged and unstaged. Existing S22 and S22R evidence and build outputs were also preserved.

No ISO, QEMU, Settings, legacy Control Panel, or DeveloperStudio smoke evidence was produced in S22R2.
