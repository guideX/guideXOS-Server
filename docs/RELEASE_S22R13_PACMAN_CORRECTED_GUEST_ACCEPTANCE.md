# guideXOS Server v0.3 — S22R13 PacMan Corrected Guest Acceptance

**Outcome B — GUEST_ACCEPTANCE_INCOMPLETE.** The exact corrected candidate and its package receipt pass integrity checks, but QAC did not admit an S22R13 guest within the bounded wait. No corrected-candidate guest boot or interaction was performed. PacMan launch, gameplay, the VFS lifecycle, and the startup stack correction therefore remain unqualified in the real AMD64 guest. This phase found no new runtime defect and does not authorize RC publication.

## 1. Repository and candidate provenance

- Repository: D:\dev\guideXOSServer
- Required branch: main
- Preflight HEAD: 9efd5adb2b9eb821b2cb3a88e8831506389ee95e, matching the expected S22R12 HEAD.
- The tracked worktree was clean at preflight. There were 9,252 pre-existing untracked paths; they were preserved. No .phase or AGENTS.md was present.
- The candidate receipt names source commit ea786e2da4da01a57b1ab00429c8453edc9446ad (Fix Navigator smoke stack overlap with VFS mounts), an ancestor of the preflight HEAD.
- The candidate manifest records a non-clean packaging worktree because untracked files were present; the tracked tree was clean, and those paths were preserved.
- No branch switch, worktree, rebuild, candidate substitution, or source change was made. dist and release readiness markers were not modified.

The corrected candidate remains at:

out/release-profile/0eed0ed2-df00-403a-9646-0dffb22be3bb/artifacts/guideXOS-Server-v0.3.0-amd64-pacman-only-candidate.iso

Its size is 90,245,120 bytes and its complete SHA-256 is:

b6b7b0faeef779a4ac06abb96ad749d2ce906df2e7751c1b9c329dd0a23fc2af

The ISO hash matches the expected value, the .sha256 sidecar, and the candidate manifest. The manifest records build ID 0eed0ed2-df00-403a-9646-0dffb22be3bb, profile ServerV03PacManOnly, source commit above, and candidateOnly=true. Its UEFI bootability record is present. The exact S22R12 fix source commit and the receipt-provenanced kernel are included in this candidate.

## 2. Receipt and package integrity

The existing scripts/release-build-integrity.psm1::Assert-ReleaseBuildIdentity validator passed against the candidate ESP using the expected source commit, build ID, profile, AMD64 architecture, and Server 0.3.0 target.

- All 16 manifest input-file paths, byte lengths, and SHA-256 values matched the candidate ESP.
- All 11 PacMan package files matched their manifest hashes in both the ESP package and the package staging directory.
- The ESP Native ELF directories and package staging contain only PacMan.
- stage.developerStudio=not_selected; Developer Studio is absent from both candidate package locations.
- The candidate ISO hash matches its receipt; its embedded UEFI image identity is recorded in the manifest.
- The original S22R9 ISO remains unchanged at out/release-profile/95cf3e2d-778b-4b31-8948-4acdb7cc6aaa/artifacts/guideXOS-Server-v0.3.0-amd64-pacman-only-candidate.iso, SHA-256 e98bf087efc071ddf82db4dce9d79335aacbf38310de43b032744165792d712b.

Integrity evidence is in [candidate-integrity.json](evidence/s22r13-pacman-corrected-guest-acceptance-20261022/candidate-integrity.json), [candidate-esp-inventory.json](evidence/s22r13-pacman-corrected-guest-acceptance-20261022/candidate-esp-inventory.json), and [release-build-identity-validation.json](evidence/s22r13-pacman-corrected-guest-acceptance-20261022/release-build-identity-validation.json).

## 3. QAC ownership and admission

The established QAC acceptance workflow was used. At preflight, QAC 2.0.0 was online with one verified acceptance guest running and one verified reservation waiting. The active QEMU process was PID 25532, managed by reservation 2c060850d2f640428021acbcc836ece5; it belonged to a different checkout and was left untouched. The QAC process census reported one managed QEMU and zero foreign QEMU processes.

S22R13 requested acceptance reservation 59e70bbe9c26459fb1adea05ab796cc0 at 2026-10-23T00:05:13.7078895Z. QAC admitted the request to the queue at position 2. After approximately five minutes it remained at position 2, behind the running guest and an earlier waiting request. The S22R13 reservation alone was cancelled through QAC. Its final state was Cancelled, reason Cancelled by client. The QAC runner returned 3 with “Reservation ended before launch: Cancelled”; this was the cancelled-before-launch result, not a QEMU or guest failure.

The final snapshot at 2026-10-22 17:10:45 PDT showed QAC online, one verified active acceptance guest, two other verified queued reservations, one managed QEMU, zero foreign QEMU, and no S22R13 reservation. No other guest or reservation was interrupted, terminated, or released.

See [QAC preflight](evidence/s22r13-pacman-corrected-guest-acceptance-20261022/qac-preflight.json), [admission attempt](evidence/s22r13-pacman-corrected-guest-acceptance-20261022/qac-admission-attempt.md), [final QAC snapshot](evidence/s22r13-pacman-corrected-guest-acceptance-20261022/qac-final-status.json), and the [QAC runner log](evidence/s22r13-pacman-corrected-guest-acceptance-20261022/guest-boot1-storage-backed/qac-run.log).

## 4. Guest configurations and cold boots

A fresh writable FAT directory copy of all 16 candidate ESP files was prepared for boot 1 and hash-checked against the candidate before any guest access. The planned configuration uses QEMU AMD64 with pc,accel=whpx, two processors, 2048 MiB RAM, read-only OVMF firmware, the exact read-only candidate ISO as CD-ROM, the private writable ESP copy, disabled networking, serial capture, and QMP screendumps. The firmware SHA-256 is 2df617a6fd1bae41ec7c0e3bceca96c928a5a88d8dc198468d34733393ab2792.

The argument vector is preserved in [qemu-launch.json](evidence/s22r13-pacman-corrected-guest-acceptance-20261022/guest-boot1-storage-backed/qemu-launch.json), but it is a prepared configuration, not an actual guest command line. QAC did not start a QEMU child for this phase.

| Run | Result |
| --- | --- |
| Boot 1 | Not launched: QAC reservation remained queued and was cancelled at the wait bound. No serial log or guest screenshot exists. |
| Boot 2 | Not attempted: the first corrected-candidate guest did not start. |

There were zero independent corrected-candidate cold boots in S22R13. No boot result is being inferred from S22R10–S22R12 or from static package checks.

## 5. VFS, /Apps, Start menu, and application interaction

These guest checks were **not performed** because QAC did not admit a guest:

- Firmware, kernel, desktop responsiveness, and storage device registration
- Startup typography smoke completion and mount-table count, slot, prefix, active-flag, and ownership invariants
- /Apps access and PacMan registration at startup and after refresh
- Start-menu discovery, repeated refresh persistence, or PacMan launch
- Native ELF load, PacMan rendering, input response, gameplay progression, exit, or desktop recovery
- Navigator URL/address-bar interaction, Settings interaction, or post-sequence system responsiveness

The QMP helper and the fresh writable ESP copy are retained in the phase evidence directory, but there is no QMP session, serial diagnostic checkpoint, screenshot, or PacMan gameplay evidence from S22R13. Historical S22R10 Settings and Navigator observations do not count as corrected-candidate acceptance.

## 6. Comparison with S22R10–S22R12

- **S22R10:** Two storage-backed cold boots of the original S22R9 candidate registered PacMan during startup, then lost /Apps before Start-menu rediscovery. PacMan disappeared from the refreshed menu and was not launched.
- **S22R11:** Added bounded lifecycle diagnostics. The first observed failure was VFS mount resolution: /Apps had no mount while device generations remained stable. The corrupted table had inconsistent active/count state and an alias prefix containing Navigator URL text; the writer was still unresolved.
- **S22R12:** Isolated the writer to the startup typography smoke’s oversized automatic NavigatorApp frame, 0x52108 bytes against a 16 KiB boot stack, overwriting neighboring BSS mount metadata. All seven smoke instances moved to scoped heap allocations. Hosted and freestanding checks passed, and the corrected candidate was built.
- **S22R13:** Verified that exact corrected candidate and its package receipt, but QAC contention prevented any guest run. This phase neither confirms the fix in a guest nor demonstrates a regression.

## 7. Architecture and security limits

The S22R6–S22R12 findings remain in force. Native ELF applications execute with kernel privilege and have no enforceable process isolation. Runtime discovery can load applications from writable storage, so a PacMan-only build composition does not constrain later package additions or create a sandbox. The runtime does not authenticate manifests or bind them to executables, and the current ABI identity does not safely distinguish all incompatible host-table layouts.

The source-level correction for the demonstrated startup stack overwrite is included in the candidate, but S22R13 did not audit every kernel stack frame. Broader boot-stack safety remains open.

Release-owner decisions still required:

| Decision | Required disposition |
| --- | --- |
| Trusted first-party composition | Approve or reject PacMan as the only first-party Native ELF in the Server 0.3.0 image. |
| Runtime compatibility | Define the required guarantees when the loader ignores the manifest minimum version and does not negotiate ABI layouts. |
| Writable-storage execution | Decide whether packages added to writable persistent storage may execute with kernel privilege, or whether release operation must prevent additions or loading. |
| Third-party applications | State whether untrusted Native ELF applications are supported; if so, require a separately designed and qualified privilege/address-space boundary before release. |

## 8. Disposition and recommended next phase

Disposition: GUEST_ACCEPTANCE_INCOMPLETE due to QAC availability, not a demonstrated candidate defect. The corrected candidate and original S22R9 ISO are preserved. No guest acceptance claim or RC publication authorization is made.

When QAC is available, continue Main Integration with the same SHA-256-verified candidate. Use two independent QAC-managed storage-backed cold boots with fresh writable state, capture actual QAC reservation identities and guest command lines, and collect serial diagnostics and screenshots. Verify the A–D VFS/Apps checkpoints around startup and repeated Start refresh; then launch PacMan from the Start menu, demonstrate responsive gameplay, exit cleanly, and exercise Navigator and Settings. If mount corruption recurs, capture the first invalid write with the established debugger/watchpoint mechanism before proposing a correction. Do not broaden this phase into release publication or a complete kernel stack audit.
