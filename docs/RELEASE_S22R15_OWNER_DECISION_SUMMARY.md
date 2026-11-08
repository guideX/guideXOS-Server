# Server v0.3 — S22R15 Owner Decisions

**Decision package:** ready for owner review. No release approval is recorded. Public technical preview and general-purpose third-party Native ELF distribution are blocked by the current runtime security boundary.

## Recommended decisions

| Decision | Recommended choice | Consequence |
|---|---|---|
| Intended audience | Controlled internal / experimental preview only | Restrict use to trusted first-party software and operators, disposable VMs or dedicated test hardware, no sensitive data, and controlled writes to `/Apps`. The OS does not enforce these controls. |
| `/Documents` | Exclude from preview contract and document | The visible standard-folder workflow fails. Fix before any release whose contract promises Documents or standard user folders. No data loss was observed. |
| Recent Programs persistence | Document as a preview limitation | Recent items may be empty or stale after restart; app discovery still works. |
| Legacy Display Options | Exclude from preview support, explain unavailability | The visible route cannot configure display settings. Supporting it requires a real backend and a new qualified candidate. |
| Native ELF security | Accept only the controlled first-party Model A risk, if desired | Native code runs with kernel privilege and no address-space isolation. Documentation alone does not protect users. |
| Writable `/Apps` | Control writes operationally for Model A; block public distribution until runtime-restricted | Added native packages can be discovered and execute with kernel privilege. |
| ABI compatibility | Promise no cross-version compatibility | The loader does not reliably reject incompatible v0.3/v0.5 host-table layouts. Broader support needs fail-closed runtime negotiation. |
| Reproducibility | Accept content equivalence; do not claim byte-identical ISO builds | Kernel/PacMan content matches; bootloader metadata and accepted raw ramdisk layout do not. |
| COM2 | Optional unless explicitly included in v0.3 contract | The read-only provider passed one real guest request; ordinary UI startup does not exercise the external host API. |

All approvals are **Pending**. The technical details and evidence are in [S22R15 readiness](RELEASE_S22R15_READINESS_AND_OWNER_DECISIONS.md).

## S22R16 recommendation

Run a release-owner decision closure phase first. If the owner requires Documents, functional legacy Display, public writable-package protections, broader ABI guarantees, or byte-identical ISO output, schedule only the corresponding bounded remediation and requalification. Do not treat this decision package as release authorization.
