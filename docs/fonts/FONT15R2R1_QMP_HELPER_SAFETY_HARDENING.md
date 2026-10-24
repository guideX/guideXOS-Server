# guideXOS Server — FONT15R2R1 QMP Helper Safety Hardening

**Outcome A — `QMP_HELPER_SAFETY_QUALIFIED`.** The host-side helper now has bounded, observable drag cleanup, classifies QMP transport and protocol failures, checks loopback endpoint ownership against the expected QAC-owned QEMU PID, and reports an unresolved button state as failure. The focused local suite passed 18/18 tests. This qualifies the helper for a controlled live qualification attempt; it does not establish that a guest consumed any QMP input event.

**Date:** 2026-10-24 PDT

**Starting HEAD:** `4d67c76be214d6e8799dd482c554605208cdc29c`

**Ending HEAD for helper and test qualification:** `d4d419d7f42a20627b28b3bf08b9c9a4d0a63269`

**Implementation commit:** `d4d419d7f42a20627b28b3bf08b9c9a4d0a63269` (`Harden FONT15 QMP drag cleanup`)

## 1. Preflight and preservation

The branch was `FONT_SUPPORT`, at the requested baseline HEAD. The starting index was empty. The worktree contained eight tracked modifications and 55 untracked entries in normal short status; all were preserved:

- Tracked modifications: `build.ps1`, `docs/fonts/FONT12_V03_RELEASE_QUALIFICATION.md`, `scripts/generate-wallpaper-pack.ps1`, `scripts/run-native-filesystem-contract-test.ps1`, `tests/font_raster_test.cpp`, `tests/font_truetype_test.cpp`, `tests/native_abi_layout_test.cpp`, and `tests/system_font_selection_test.cpp`.
- Existing untracked fonts, reports, evidence, validation output, and other artifacts were left in place.

There was no root `.phase` marker and no prior FONT15R2R1 report. The original QMP helper existed at `out/font15r1-validation/guest/font15r1_qmp.py`; it was not ignored by Git. The helper and the new focused test were committed explicitly. The staged review contained only those two paths and passed `git diff --cached --check`. No stash, reset, clean, branch switch, rebase, history rewrite, or push occurred.

The existing FONT15R2 acceptance report remains byte-for-byte unchanged (SHA-256 `FE78A63F0D9CCD486C9B0AE7934D9E1CFD8CFF38DB1FA8AAC227D7099CFB5A84`). No FONT12–FONT15 guest evidence was intentionally changed. The report is being added in a documentation-only follow-up commit after the implementation commit above.

## 2. Audited helper paths and confirmed findings

The audit covered `QMP.__init__`, `_read`, `_command`, `input_event`, `relative`, `button`, the `drag` and `click` CLI branches, and `main` cleanup.

In the original helper, a button-down was sent as an individual `input-send-event` command and paced for 150 ms. Drag movement sent separate relative X and Y commands in chunks of at most 35 pixels, sleeping for the requested delay (200 ms by default). A separate button-up followed normal movement. `_command` matched response IDs and raised on a QMP `error`, but it did not validate the response shape or impose a total action deadline. The five-second socket timeout bounded individual I/O, not a multi-command action. Arbitrarily large movements and delays could therefore extend an action for an unbounded period.

The confirmed source-level safety gap was that a failed or timed-out movement raised before the drag's button-up call. The outer `finally` only closed the socket; it did not release the mouse button. The click path used the same unprotected down/sleep/up sequence. The helper had no logical button-state record, no endpoint process identity check beyond connecting to `127.0.0.1`, and no cleanup result. A timeout, disconnect, malformed response, or exception could therefore leave the helper unable to say whether a down or up request had taken effect. FONT15R2 produced no guest boot or input attempt, so this was not a defect demonstrated in a real guest.

QMP response acknowledgments were checked for command errors, but they do not prove that the guest consumed an event. The helper now preserves that distinction in its result and trace.

## 3. Button-state rules

The drag result records `down_attempted`, `down_result`, `down_acknowledged`, `release_attempts`, `release_acknowledged`, `button_state`, and `guest_event_consumption`.

1. Before sending button-down, state is recorded as attempted and unconfirmed.
2. A matching successful QMP response records `down_result=acknowledged`. A QMP error records `failed`; a timeout or connection failure after a possible write remains unconfirmed.
3. Normal completion attempts button-up. If any drag step fails, cleanup attempts button-up through the same verified connection.
4. If button-up receives a matching successful QMP response, the result says `release-command-acknowledged`. The trace records the QMP response; `guest_event_consumption` remains `unverified`.
5. If button-up cannot be acknowledged, the result is `success=false`, `button_state=unknown`, and the helper blocks further input on that connection. It does not reconnect or retry button-down.

There is at most one normal release attempt and one cleanup release attempt. A movement failure gets one cleanup attempt. A failed normal release can get one additional cleanup attempt; repeated release errors terminate with an explicit unknown state.

## 4. Implemented changes

- Replaced the buffered `makefile().readline()` transport with a bounded newline reader that retains partial responses, checks response IDs and error/return fields, records requests and responses, and classifies protocol, connection, command, and timeout failures.
- Added Windows TCP-table and process identity checks. The helper requires `--expected-pid`, verifies the loopback listener PID, verifies the accepted connection's server-side TCP owner PID, and checks the process image and creation time before each input command. The caller must supply the PID reported for the active QAC-owned QEMU child; if identity cannot be verified, the helper fails closed.
- Serialized commands and held the same lock across the full drag, including cleanup, to prevent other QMP commands from interleaving.
- Added a 60-second drag deadline, a three-second cleanup deadline, a five-second per-command limit, a 128-event movement cap, a 35-pixel maximum step, and a five-second maximum pacing delay. A timeout does not trigger a reconnect. A disconnected or malformed stream is marked unusable, so cleanup reports unknown without sending more input.
- Kept drag movement on separate relative X and Y commands, at most 35 pixels per event, with 200 ms default pacing and explicit button-down/up commands paced 150 ms apart. `click` now uses the same protected drag state machine with zero movement.
- Made drag failure a structured JSON result and a nonzero CLI exit. A completed drag returns success only when movement and its release command were acknowledged. The result still marks guest event consumption unverified.

## 5. Focused tests and results

Command: `python -B tests/test_font15r2r1_qmp_safety.py`

Result: **18 tests passed, 0 failed**.

The suite covers normal command order and trace evidence; movement command failure; movement timeout and bounded cleanup; button-down failure; normal and repeated button-up failure; disconnect without reconnect; malformed response; preservation of primary and cleanup errors; repeated drags; invalid endpoint identity; ownership loss during a drag; interrupted operation; command serialization during cleanup; closed-port classification; localhost handshake/query/close; silent-response timeout and transport close; CLI failure for an unowned endpoint; and Windows listener/accepted-connection PID checks against a local non-QEMU stub.

The timeouts in drag state-machine cases use an injected clock and fake transport. Loopback fixtures and concurrency threads were joined and their sockets closed. The CLI subprocess test waited for the helper to exit with code 2. No test fixture left a child process. The Windows ownership test confirmed that the native TCP table mapped the localhost listener and established connection to the test process, then rejected its non-QEMU executable.

The existing FONT15R2 closed-port and silent-QMP-startup checks were repeated in these local fixtures. No QEMU instance or QAC reservation was started or requested. The optional FONT6/FONT10/FONT11/FONT14 suites were not rerun; no production source changed, so no freestanding rebuild was needed.

## 6. Recovery and remaining uncertainty

If button release remains unknown because the verified transport is disconnected, malformed, or no longer owned, the helper stops sending input and does not reconnect. Prior FONT15R1 evidence records a QAC-managed guest exiting with code 0 after a QMP `quit`, followed by QAC releasing its reservation. That remains a future operational option only while the same QAC-owned endpoint is responsive and identity-verified. This phase did not exercise shutdown or establish a second recovery path for a lost QMP channel.

Even an acknowledged QMP button-up does not prove guest consumption. The helper reports command acknowledgment separately from guest state. A controlled guest run must still verify actual pointer behavior and the intended release effect; no guest delivery claim is made here.

## 7. Files and Git disposition

- `out/font15r1-validation/guest/font15r1_qmp.py` — bounded, observable QMP transport and drag cleanup.
- `tests/test_font15r2r1_qmp_safety.py` — fake-transport, localhost, Windows ownership, and CLI lifecycle tests.
- `docs/fonts/FONT15R2R1_QMP_HELPER_SAFETY_HARDENING.md` — this report.

The helper and test are in implementation commit `d4d419d7f42a20627b28b3bf08b9c9a4d0a63269`. The report is committed separately as documentation after qualification. No production guideXOS source, existing FONT15R2 evidence, or unrelated worktree change was included. Nothing was pushed.

## 8. Gates and next-stage recommendation

Keep `TTF_MINIMUM_SETTINGS_SIZE_ACCEPTANCE_OPEN` open. The independent PacMan audio-link prerequisite, top-level Server v0.3 build gate, combined release qualification, and final publication authorization also remain open. FONT15R3 was not started.

Recommend one controlled FONT15R3 real-guest qualification when QAC can grant exclusive admission. Use the PID reported for that active QAC-owned child with `--expected-pid`, preserve the input trace, and verify guest-visible pointer and button behavior separately from QMP acknowledgments.
