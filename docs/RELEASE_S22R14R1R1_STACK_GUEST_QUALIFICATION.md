# S22R14R1R1 — Stack Repair Guest Qualification

## Disposition

**Current outcome — `STACK_GUEST_EVIDENCE_INCOMPLETE`.** The repaired candidate, receipt, package hashes, and compiler stack-budget guard are verified. The original 2026-10-24 runner did not obtain QAC admission. A 2026-11-04 follow-up obtained two QAC-managed QEMU launches, but the runner could not establish ownership of its QMP listener before guest start. QEMU remained paused with `-S`; both candidate serial logs are empty and no guest OS boot occurred. QAC later independently verified both children exited and both reservations were released. The Settings paths, COM2 service path, and repaired-candidate integration checks remain unqualified. This is a guest-control tooling failure, not evidence of an OS runtime regression.

This report does not close the stack blockers or authorize release publication.

## 1. Repository provenance and retained state

- Repository: `D:\dev\guideXOSServer`
- Branch: `main`
- Starting and final pre-report HEAD: `58b684f5aadbc67d1a7dd8f6176e81bf3d2fb9c6`
- The expected HEAD is an ancestor of the current HEAD. Candidate source commit `5b62216efe78158fb12707c126ec38582faecf59` is also an ancestor.
- Tracked files were clean before this report. The Server repository has no `.phase` file. The QAC phase marker was read and left unchanged: QAC1, QAC2, QAC3A, QAC3B, QAC3C1, and QAC3C2 are accepted; QAC3 is open; next is QAC3C3.
- The task-start untracked inventory contained 13,029 paths, including the 13,027 previously reported paths and later additions. The saved inventory is `out/s22r14r1r1-stack-qualification/preflight/untracked-before.txt`; all 13,029 paths still existed at the verification checkpoint. New qualification support and evidence files are retained under `out/s22r14r1r1-stack-qualification/` and remain untracked. No earlier path was removed or moved.
- No branch switch, worktree, reset, clean, stash, rebase, merge, cherry-pick, push, `dist` change, release-readiness change, publication change, or release-marker change was made.

## 2. Prior reservation disposition

The prior S22R14R1 reservation was `3db20d3f6ad94171916b2a470a64ca01`.

Its retained QAC logs establish the final disposition:

- The request was queued at position 1 at `2026-10-24T18:11:04Z` with a 1,800-second admission bound.
- QAC expired it before launch at `2026-10-24T18:41:06Z` with reason `Reservation wait timed out.`
- The final `qac inspect` record says `Expired`, has no grant timestamp, no child PID or process identity, and records no launched child. The runner recorded QAC CLI exit 124, verified the expired reservation state, and marked the guest result `INCOMPLETE`.
- The QMP transcript is empty. That workspace contains no candidate serial log, screenshot, or guest evidence. The candidate was not booted in that run.
- QAC later reported zero reservations and zero queued work, confirming that this runner's reservation had ended. The expiration was established from the final logs and coordinator state, not inferred from the deadline alone.

Evidence remains at `out/s22r14r1-stack-audit/qac/boot1/workspaces/s22r14r1-boot1-e42126679fad4d418ff6bbacb801a636/output/`.

## 3. Candidate identity and receipt

| Item | Verified value |
|---|---|
| Release profile | `ServerV03PacManOnly` |
| Build ID | `6ad9bb77-12ce-4b4d-8ed0-56ff3944ee5d` |
| Candidate ISO | `out/release-profile/6ad9bb77-12ce-4b4d-8ed0-56ff3944ee5d/artifacts/guideXOS-Server-v0.3.0-amd64-pacman-only-candidate.iso` |
| Candidate SHA-256 | `2fe2f5eda87c9a80164715a156af5de12c7cf3891d29a961c40012c9d032241e` |
| Receipt | `out/release-profile/6ad9bb77-12ce-4b4d-8ed0-56ff3944ee5d/artifacts/guideXOS-Server-v0.3.0-amd64-pacman-only-candidate.manifest.json` |
| Candidate source commit | `5b62216efe78158fb12707c126ec38582faecf59` |
| Candidate kernel SHA-256 | `b5e029d23f9c21b5ff0838d9388322bf95b211bf3ef98145ddd344170dfe95dd` |
| Earlier accepted ISO SHA-256 | `b6b7b0faeef779a4ac06abb96ad749d2ce906df2e7751c1b9c329dd0a23fc2af` |

The candidate's full ISO hash and the earlier accepted ISO hash both match their recorded identities. The candidate receipt reports a successful canonical build and a bootable UEFI no-emulation image. Its embedded UEFI boot-image bytes independently match the receipt hash. All 16 receipt input-file hashes match the candidate ESP; all 11 PacMan package-file hashes match the package directory. `nativeElfPackages` contains exactly PacMan, and Developer Studio is absent from both the ESP and package directory.

The source commit includes the repaired Settings and COM2 sources. The packaged candidate kernel hash matches both the release ESP kernel and the S22R14R1 stack-audit kernel ELF. The candidate was not rebuilt or altered. Its recorded packaging worktree was not clean, and the broader clean isolated-build reproducibility gate remains open.

Machine-readable verification is retained in `out/s22r14r1r1-stack-qualification/preflight/candidate-and-preflight-verification.json`, `receipt-input-hash-checks.csv`, and `pacman-package-hash-checks.csv`.

## 4. Stack-budget confirmation and limits

The 16,384-byte boot stack budget guard was rerun over the retained GCC `.su` records. It passed with the same kernel ELF hash as the candidate:

| Path | Guarded use | Headroom |
|---|---:|---:|
| Modern Settings Display | 11,512 B | 4,872 B |
| Legacy Display Options | 11,896 B | 4,488 B |
| COM2 storage service | 7,768 B | 8,616 B |

Each retains at least the required 4,096 bytes. The guard includes enumerated synchronous callers, 1,024 bytes of unmodeled call glue, 512 bytes for interrupt/exception entry, and 40 bytes for boot entry/alignment. The COM2 path includes `kernel_main`, bridge polling and serving, storage provider and MBR/block-read work, validation, and the diagnostic decode path. Settings and COM2 do not switch away from the boot stack. Regular hardware IRQs use the separate 16 KiB IST1 stack. Exception entry still consumes the bounded reserve on the interrupted stack before switching.

These are static compiler estimates for the enumerated paths. They are not runtime high-water measurements or a whole-kernel proof. The kernel has no runtime boot-stack guard, canary, or watermark; the separate exception stack also lacks one. The guard does not discover every indirect call, recursion, arbitrary future branch, or stack-switching path. No comprehensive kernel stack-safety claim is made.

Guard output: `out/s22r14r1r1-stack-qualification/preflight/stack-budget-guard-confirmation.txt`.

## 5. QAC state and candidate boot count

The earlier reservation produced **zero** candidate boots. This qualification phase also produced **zero** candidate boots and requested **zero** new reservations.

At the final-state snapshot (`2026-10-24T21:53:23Z`), QAC was online with zero managed reservations and zero queued requests, but reported two foreign QEMU processes and blocked acceptance admission. The owner-verified Windows process census identified PID `20392`, parent PID `18816`, and PID `21932`, parent PID `12684`; both are owned by `LEON2_RDT\guideX` and QAC classified them as `unmanaged/foreign`. Both command lines point to the separate `E:\BM3B` checkout and the `guideXOS-CSharp-UEFI-v0.2-BM3B-Diagnostic.iso`; the second process is suspended with `-S`. Neither process is owned by a QAC reservation. Both were left untouched. No QAC request was submitted while acceptance was blocked.

The QAC status, list, queue, and owner-verified process census are retained under `out/s22r14r1r1-stack-qualification/preflight/`, including `qac-final-snapshot.json`. The existing runner and input helpers were copied into the same output tree for a future bounded run; the copy adds a clearly labeled manual result path while preserving the original marker-gated `result pass` behavior. Those copied tools were not used to launch a guest.

## 6. Settings modern path

**Runtime coverage: not performed.** No repaired-candidate boot occurred, so Start, Settings launch, Display navigation, repeated modern Display interaction, close, responsiveness, and post-interaction VFS checks were not observed. The static compiler guard passed as described in §4. No serial, screenshot, stack watermark, or high-water evidence exists for this candidate.

## 7. Settings legacy path

**Runtime coverage: not performed.** The legacy `DisplayOptionsApp` query, refresh, and apply frame guard passed statically. No release-guest entry point was attempted because QAC blocked admission; reachability and runtime path execution remain unclassified. Static qualification does not substitute for runtime coverage.

## 8. COM2 storage-service path

**Runtime coverage: not performed.** No COM2 request was sent to the repaired candidate. The supported read-only workflow is the existing `system_service_runtime_client.exe` over the QEMU COM2 TCP serial endpoint; its `GetStorageSnapshot` request reaches the production bridge and storage provider. It also checks normal response/error handling. The retained client is `out/s22r14r1-stack-audit/host/system_service_runtime_client.exe`. It was not run because the guest was not admitted. No request, response, storage-state, or service-error serial evidence exists for this candidate.

## 9. PacMan gameplay and cleanup

**Runtime coverage: not performed on the repaired candidate.** The candidate receipt still contains the expected PacMan package, and the package hashes pass. Start-menu appearance, launch, gameplay input, visible progression, close, cleanup marker, and responsive desktop return were not observed. S22R13R5 evidence belongs to the earlier ISO hash `b6b7…fc2af` and does not qualify this repaired kernel. No `PACMAN-SMOKE` marker was created or inferred.

## 10. Navigator and VFS

**Runtime coverage: not performed on the repaired candidate.** No Navigator launch or `file:///Apps/PacMan/app.json` lookup occurred. `/Apps`, the five-mount state, PacMan registration, desktop responsiveness, and VFS integrity were not observed in this candidate's guest. The earlier accepted ISO's VFS and Navigator evidence remains historical evidence for that earlier hash only.

## 11. Remaining runtime qualification gaps

The exact remaining checks are:

1. Before the next attempt, take a fresh QAC snapshot and use the QAC-managed isolated writable guest workflow on this exact ISO hash. A clear QAC snapshot was captured during the 2026-11-04 follow-up, but the remaining QMP startup/ownership failure must be resolved in the qualification runner first.
2. Cold boot, kernel initialization, desktop responsiveness, five-mount VFS state, `/Apps`, PacMan registration, and absence of unexpected panic, exception, hang, or mount invalidation.
3. Execute and capture the modern Settings Display path; separately attempt the supported legacy Display Options entry and report reachability accurately.
4. Send the existing non-destructive COM2 storage request and capture request/response, storage identity, error handling, and stable VFS state.
5. Exercise PacMan gameplay and cleanup, then Navigator's PacMan manifest lookup, Start refresh, desktop responsiveness, and VFS integrity.
6. Capture serial, QMP transcript, screenshots, reservation ID, QEMU PID, command line, evidence paths, and verified QAC release. Repeat one independent cold boot if the first run passes and QAC availability permits.

## 12. Outstanding release blockers

Carry forward without unrelated implementation:

- End-to-end isolated release reproducibility remains open; the earlier `_wcsicmp` failure was corrected in the current bare-metal translation unit, but a clean full-source and content reproduction was not established.
- `/Documents` creation remains unsupported.
- `/.startmenu_recent` persistence remains unsupported.
- Native ELF applications execute with kernel privilege.
- Native application packages remain writable at runtime.
- Cross-version ABI negotiation is inadequate.
- Enforceable native application isolation is absent.

These blockers remain separate from the stack repair and are not approved for deferral by this report.

## 13. Recommended next Main Integration phase

Resolve the QMP startup/ownership failure in the qualification tooling, then resume a QAC-managed candidate-specific guest run on the exact hash above without rebuilding. Close the modern Settings, legacy Display Options, COM2, PacMan, Navigator, and VFS checks before deciding whether the two stack blockers can be closed. If the first cold boot passes and QAC remains available, perform one independent repeat boot. After this guest gate, continue release reproducibility qualification; do not treat stack qualification as publication readiness.

**Final disposition:** `STACK_GUEST_EVIDENCE_INCOMPLETE` — static and package evidence confirmed; repaired-candidate guest evidence remains blocked by the QMP listener startup/ownership failure described below.

## 14. 2026-11-04 candidate runtime follow-up

This follow-up adds runtime evidence while preserving the original 2026-10-24 source logs, reservation records, and historical results above. It supersedes the earlier QAC-blocked status for the current coordinator snapshot, but it does not change any release artifact or qualify guest behavior.

### Repository and candidate identity

- Follow-up started on `main` at `586e7c52d8021f2bac21ab8010b882afe1510fce`; required commit `58b684f5aadbc67d1a7dd8f6176e81bf3d2fb9c6` and candidate source commit `5b62216efe78158fb12707c126ec38582faecf59` are ancestors. The tracked worktree was clean before this documentation update.
- The repaired candidate remains `out/release-profile/6ad9bb77-12ce-4b4d-8ed0-56ff3944ee5d/artifacts/guideXOS-Server-v0.3.0-amd64-pacman-only-candidate.iso`, SHA-256 `2fe2f5eda87c9a80164715a156af5de12c7cf3891d29a961c40012c9d032241e`. The post-run hash still matches. The earlier accepted ISO still hashes to `b6b7b0faeef779a4ac06abb96ad749d2ce906df2e7751c1b9c329dd0a23fc2af`.
- The retained receipt checks continue to identify `ServerV03PacManOnly`, PacMan as the only Native ELF package, no Developer Studio package, 16 matching release input hashes, 11 matching PacMan package hashes, and an embedded boot image matching its receipt. The candidate kernel hash remains `b5e029d23f9c21b5ff0838d9388322bf95b211bf3ef98145ddd344170dfe95dd`, matching the stack-audit kernel. No ISO was rebuilt or altered.
- The retained stack guard reports Settings modern `11,512/16,384 B` with `4,872 B` headroom, Settings legacy `11,896/16,384 B` with `4,488 B`, and COM2 storage `7,768/16,384 B` with `8,616 B`. Its caller, interrupt-entry, and stack-switch assumptions and limitations remain those in §4; these compiler estimates are not runtime high-water data or a whole-kernel proof.

### Prior reservation and live QAC ownership

- The original reservation `3db20d3f6ad94171916b2a470a64ca01` is confirmed `Expired` before child launch by its retained final inspection and runner logs. It produced no candidate boot or guest evidence.
- Before the 2026-11-04 attempts, QAC was Online with zero managed sessions, zero queued requests, zero blocked requests, and zero QEMU processes. A separate `guideXOSServer` acceptance reservation `aecd0b1ab9964162a7fd7cdccd31de28` became active while the follow-up was in progress; its QAC owner and child were verified, and it was left untouched until the owner completed. A queued `guideXOSServer-AARCH64-BOOTDTB3` reservation `039272c5e27a42dbb61bf56fb4e9bb7f` was also left untouched and was later cancelled by its own client. Neither was cleaned up or interrupted by this qualification.
- Final QAC snapshot at `2026-11-04T23:22:12Z` was Online with zero reservations, sessions, queued or blocked requests, foreign QEMU processes, and QEMU children. The two follow-up reservations below independently inspect as `Released`; no owner remains held by this task. The QAC `.phase` marker was not changed; the server repository still has no `.phase` file.

### Launch attempts and boot count

Both attempts used the exact ISO above as a read-only CD-ROM, read-only OVMF, a per-run copied writable ESP with SHA-256 `cf8fe881beb41ea5b3114a39d3dcd029cd9a96b129463bfe7dfffd9009bf4e4f`, and the production-representative `pc,accel=whpx`, two-vCPU, 2 GiB, standard VGA, networking-disabled configuration. Both were launched as direct QAC-managed QEMU children.

| Attempt | QAC reservation / QEMU PID | Result |
|---|---|---|
| 1 | `48a521ae53b54531bba42dfc06f05565` / `25808` | The copied runner threw while passing an empty listener list to a mandatory `Connections` parameter. QEMU never left its deliberate `-S` prelaunch pause. QAC later inspected the child as exited and the reservation as `Released`. |
| 2 | `62db03804b484717bd7534ede8a77a77` / `16104` | A run-specific helper copy accepted an empty listener list, but the runner still could not prove the QMP listener on `127.0.0.1:62003` within its 15-second startup bound. QEMU remained paused. The runner retained a `RecoveryRequired` record; later QAC inspection independently verified the child exited and the reservation was `Released`. |

Attempt 2 included the existing COM2 TCP endpoint at `127.0.0.1:49202`; no listener or guest service became available, and the system-service client was not run. The QMP transcript, serial log, and screenshot directory are empty for both attempts. No UEFI transfer, kernel initialization, desktop, or application runtime was observed. The QEMU `-S` option holds the guest before execution until it is continued, consistent with the [QEMU invocation documentation](https://www.qemu.org/docs/master/system/qemu-manpage.html). Thus the follow-up launched QEMU twice but produced **zero candidate guest boots**.

Run evidence is retained under `out/s22r14r1r1-stack-qualification/boot1-20261104/`. It includes the preflight and final QAC inspections, both `execution.json` records, QAC logs, serial and QEMU diagnostic logs, manifests, exact child arguments, the run-specific helper copy and its one-line change record, and post-run candidate hashes. The first failure is classified as a runner parameter-binding error. The second is classified as QMP listener startup/ownership failure. Neither is evidence of a guest or kernel defect.

### Runtime qualification result

- **Settings modern Display:** not exercised. No guest boot, Start interaction, Display navigation, repetition, close, desktop responsiveness, or post-interaction VFS observation occurred.
- **Settings legacy Display Options:** not exercised. Static frame-budget evidence remains valid; guest reachability and runtime execution remain unclassified.
- **COM2 storage service:** not exercised. No request, normal response, unauthorized-mutation error response, storage snapshot, or VFS stability check reached the guest.
- **PacMan:** not exercised. No Start registration, launch, gameplay progression, cleanup marker, or desktop return was observed.
- **Navigator and VFS:** not exercised. No manifest lookup, five-mount observation, `/Apps` resolution, or guest VFS integrity check occurred.
- **Outcome:** `STACK_GUEST_EVIDENCE_INCOMPLETE`. The two identified stack blockers remain open because no corrected Settings or COM2 runtime path executed. No `STACK_RUNTIME_REGRESSION` was demonstrated, and the original S22R14 release blockers remain open and are not approved for deferral.

The saved inventory `out/s22r14r1r1-stack-qualification/preflight/untracked-before.txt` still lists all 13,029 paths present at the previous checkpoint, and a follow-up existence check found zero missing paths. New run output remains under ignored `out/` paths; it was not added to Git. Raw run evidence, candidate artifacts, source trees, `dist`, markers, and publication state were preserved. This report's summary and recommendation were updated to reflect the follow-up.
