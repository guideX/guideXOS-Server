# S22R14R1R1 — Stack Repair Guest Qualification

## Disposition

**Outcome B — `STACK_GUEST_EVIDENCE_INCOMPLETE`.** The repaired candidate, receipt, package hashes, and compiler stack-budget guard were verified. The repaired ISO was not booted in this phase because QAC detected a foreign QEMU process and blocked acceptance admission. No new QAC reservation was requested and no guest was launched. The two runtime stack paths, COM2 service path, and repaired-candidate integration checks remain unqualified.

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

1. Obtain a fresh QAC snapshot with no foreign QEMU, managed reservation, or queued work; then use the QAC-managed isolated writable guest workflow on this exact ISO hash.
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

Continue candidate-specific guest qualification when QAC reports no foreign QEMU and permits exclusive acceptance. Use the exact candidate hash above without rebuilding. Close the modern Settings, legacy Display Options, COM2, PacMan, Navigator, and VFS checks before deciding whether the two stack blockers can be closed. If the first cold boot passes and QAC remains available, perform one independent repeat boot. After this guest gate, continue release reproducibility qualification; do not treat stack qualification as publication readiness.

**Final disposition:** `STACK_GUEST_EVIDENCE_INCOMPLETE` — static and package evidence confirmed; repaired-candidate guest evidence blocked by QAC foreign-process admission control.
