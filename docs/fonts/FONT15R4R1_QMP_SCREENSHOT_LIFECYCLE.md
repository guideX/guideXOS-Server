# guideXOS Server — FONT15R4R1 QMP Screenshot Lifecycle

**Outcome A — `QMP_SCREENSHOT_LIFECYCLE_QUALIFIED` (host-side helper qualification only).** Retry 3 failed at the established-connection ownership check: Windows returned an empty QMP connection owner set for a newly connected loopback socket, and the helper stopped before the QMP greeting or screenshot command. A bounded retry of the live ownership check on that same socket now handles an empty table snapshot while still rejecting foreign or ambiguous ownership. Screenshot success also requires a fresh, complete P6 PPM artifact. The focused host suite passes 39/39. No guest boot or QAC reservation was used for this work.

`TTF_MINIMUM_SETTINGS_SIZE_ACCEPTANCE_OPEN` remains open. This report does not establish long-movement displacement, real guest Settings interaction, or minimum-size typography acceptance.

## 1. Repository, provenance, and preservation

| Item | State |
|---|---|
| Repository / branch | `D:\dev\guideXOSServer_FONT_SUPPORT` / `FONT_SUPPORT` |
| Starting HEAD | `7fa2b9baa03f9e92f0f122e7aafcc552431bb90f` |
| Ending qualification implementation HEAD | `4488cdc44c4ea2375696b6903a25eefc6cfd0fd9` |
| Qualification commit | `4488cdc44c4ea2375696b6903a25eefc6cfd0fd9` |
| Report delivery | Documentation-only follow-up commit; its SHA is given in the Codex completion message |
| Push | None |

Preflight found an empty index and no untracked files. The only pre-existing worktree edit was in `out/font15r1-validation/guest/font15r1_qmp.py`. Its exact hunk removes the `dwState == LISTEN` filter from the Windows listener-specific table and explains that some Windows builds report state zero for those rows. That hunk was not staged or committed; it remains as the original unstaged change after the qualification commit. The new helper changes were staged by separate hunks, and the staged diff was reviewed to confirm this preservation.

The active helper and its original 18-case safety suite were introduced by Leon Aiossa in commit `d4d419d7f42a20627b28b3bf08b9c9a4d0a63269` on 2026-10-24. The archived FONT12 helper at `docs/evidence/font12r/guest/font12r_qmp.py` is historical evidence, last recorded in `7e7adafcea4beda6f7322dafef03a027f56356d4` on 2026-11-02; it was not modified. No applicable `AGENTS.md` or root phase marker was found. No subagents or other concurrent task edits were observed in this session.

## 2. Retry-3 incident reconstruction

Retry 3 used QAC reservation `826d11c596e04e33bb15d5082982d38a`, QEMU PID `27568`, executable `C:\Program Files\qemu\qemu-system-x86_64.exe`, and loopback QMP endpoint `127.0.0.1:61387`. QAC recorded process start at `2026-11-08T18:56:22.4806523Z`.

The small X-only and Y-only operations each had QMP acknowledgments and a subsequent screenshot. Those screenshots showed the pointer move approximately from `(640,408)` to `(664,409)`, then to `(664,433)`. The long navigation request was X-only (`dx=-610`, `dy=0`, 200 ms pacing). Its QMP trace starts at `2026-11-08T18:59:25.769723Z`; request IDs 2–19 contain 18 `input-send-event` requests, each followed by a `return: {}` response. The last movement response is timestamped `2026-11-08T18:59:29.376223Z`.

The subsequent screenshot invocation opened a **new** helper process and TCP connection. `qmp-post-navigation-screenshot-failure-r4-retry3.json` records helper exit code 1 and:

```text
established QMP connection owner set [] does not match the expected QAC-owned PID 27568
```

The error is raised by `WindowsEndpointVerifier.verify_connection` when the Windows established-connection table contains no matching server-side row for the QMP listener port and client socket port. The socket had connected to the loopback endpoint and passed the endpoint tuple check; the failure was the empty owner-PID set. The helper performed this check before reading the QMP greeting. Therefore it did not negotiate capabilities, dispatch `human-monitor-command screendump`, or inspect a screenshot file. No screenshot trace or PPM was produced by that invocation.

The failure JSON has no exact capture timestamp or raw TCP-table snapshot. QAC's before-cleanup inspection at approximately `2026-11-08T19:00:56Z` still showed PID 27568 running. A later, separately verified connection to the same endpoint completed QMP `quit` at `2026-11-08T19:01:51.570280Z`; the QEMU child exited normally at `2026-11-08T19:01:51.7063421Z`. These later records support that the QAC child and endpoint remained available, but they do not prove the exact state of the TCP table at the failed check.

| Evidence boundary | Retry-3 result |
|---|---|
| QMP accepted the long movement commands | Yes: 18 responses contained `return: {}` |
| Guest consumed those commands | Not established |
| Long movement visibly displaced the pointer | Not measured; do not infer from acknowledgments |
| Screenshot command succeeded | No command was sent |
| Framebuffer artifact was captured and verified | No artifact was created |
| Button state | No button-down was sent; no release was pending |

**Failure classification:** `Endpoint identity verification` — specifically, established-connection owner verification returned an empty owner set. It was not a QMP connect error, handshake/capability failure, command failure, response parse failure, or screenshot artifact failure. The absence of the owner row is the exact source-level condition; why Windows produced that empty observation in retry 3 cannot be recovered from the preserved trace.

## 3. Diagnosis and deterministic reproduction

The prior helper verified the listener PID and process identity, called `socket.create_connection`, then sampled `GetExtendedTcpTable` once for the accepted connection. Any result other than exactly `[expected_pid]` was terminal. Its trace began only after the ownership check and QMP greeting, which explains why the failed screenshot invocation left no helper trace.

The empty-row condition was reproduced in two host-only ways:

1. A deterministic verifier fixture returned `ConnectionOwnershipError([], expected_pid)` during the constructor's first ownership check, before the QMP greeting. The test then returned the exact expected PID on the next live check. The helper recovered on the same socket and proceeded to capability negotiation.
2. A Windows localhost QMP fixture forced its first established-connection table snapshot to be empty, then used the real Windows TCP table for the next snapshot. The helper recovered without opening another connection. This fixture uses a Python loopback server and a test-only process identity override; it does not start QEMU.

During the first test run, the existing Windows ownership safety test also observed an empty row immediately after a localhost client received the QMP greeting. The test was made deterministic by waiting, with a one-second bound, for the accepted socket row it is meant to inspect. This confirms the empty-snapshot condition can occur on an active localhost fixture; it does not establish that the retry-3 snapshot had the same timing cause.

**Supported inference:** retry 3 is consistent with a transient empty TCP owner-table observation on a newly opened connection. A later live connection to the same QAC PID and endpoint succeeded, and an equivalent empty-table condition was reproduced on localhost. Because retry 3 did not preserve the raw table rows or failure timestamp, a transient table snapshot is strongly supported but not directly proven for that incident. The helper now safely retries that exact empty observation and remains fail-closed if it persists.

## 4. QMP lifecycle and ownership design

Each CLI invocation constructs one `QMP` object and one persistent socket. The helper verifies the loopback listener against the exact QAC-owned PID, QEMU executable name, and process creation time; connects to `127.0.0.1`; verifies the connected socket endpoints and server-side owner PID; checks the process identity again; reads the QMP greeting; and negotiates capabilities. Commands in one invocation reuse that socket. The guest workflow launches a separate invocation for each follow-up screenshot, so screenshot capture independently repeats listener and connection ownership verification.

Movement requests are sent as bounded, individually acknowledged events over the invocation's connection. A new connection is never opened after ownership loss or ambiguous command failure. Screenshot capture verifies the current socket immediately before dispatch. The helper retains finite command, cleanup, ownership-recheck, and artifact-check bounds. An empty owner list is retried at most twice after the initial sample, 25 ms apart, within the operation deadline and on the same socket. A nonempty PID mismatch fails immediately. Every successful retry still verifies the exact QAC PID, executable, and unchanged process creation identity.

## 5. Implementation changes

The qualification commit changes the active helper and its focused safety suite:

- Added structured connection-phase trace records before listener verification, socket connection, connection ownership verification, and QMP greeting. A pre-handshake ownership failure now leaves evidence with the socket endpoints, owner-set observations, attempt count, exception type, and error text.
- Added `ConnectionOwnershipError` with the observed PID set. Only an empty set permits a bounded same-socket recheck. Foreign or multiple owners remain terminal failures; there is no blind reconnect and no cached ownership substitution.
- Changed screenshot capture to keep QMP `human-monitor-command` / HMP `screendump`, independently revalidate the active socket, and fail if the output path is stale, the parent directory is missing, or the suffix is not `.ppm`.
- After a successful QMP response, the helper waits no more than one second for a valid P6 PPM: `P6` magic, positive dimensions, max value 255, no more than 100 million pixels, and an exact raster byte count. Partial, missing, wrong-format, and oversized artifacts fail.
- Trace records distinguish QMP response/command success from artifact verification and record command acknowledgment and artifact-verification UTC timestamps separately. A failed capture does not report pointer observations or clear uncertain button state.

The helper's separate-axis movement method, endpoint/PID ownership checks, bounded drag cleanup, and conservative button-state tracking remain enforced. No production guideXOS font, Settings, compositor, PS/2, Modern Input, or other guest code changed.

## 6. Regression and validation results

The focused suite now reports **39/39 passed**: all 18 original QMP safety cases remain, plus 21 connection and screenshot lifecycle cases. The existing Windows listener/connection ownership test now waits boundedly for its localhost accepted row before asserting executable rejection. New cases cover successful X/Y/navigation movement followed by screenshot, persistent and separately verified connections, closure after movement, changed and foreign ownership, transient and persistent empty snapshots, delayed and timed-out replies, asynchronous events, malformed JSON, QMP screenshot rejection, missing/partial/invalid artifacts, no reconnect, uncertain button state, repeated cycles, stale destinations, and teardown.

Additional checks:

- In-memory Python syntax compilation: helper and focused test file both passed.
- `git diff --check`: passed.
- The validator accepted the preserved retry-3 QEMU PPM: P6, 1280×800, 3,072,016 bytes.
- No full hosted typography matrix rerun. Retry 3's `hosted-regression-summary-r4-retry3.json` is historical evidence at execution HEAD `e9d3b86e86b504d806552f4f6e421335eb38f6bb`: FONT6 420/420, FONT10 88/88, FONT11 48/48, FONT14 496/496, total 1,052/1,052; QMP safety 18/18. Those counts are not claimed as newly run on this checkout.

The final focused test log is at `out/font15r4r1-validation/logs/font15r4r1-qmp-safety.log`. No QEMU process was started and no QAC status/reservation action was performed during FONT15R4R1.

## 7. Files and worktree state

| File | Change |
|---|---|
| `out/font15r1-validation/guest/font15r1_qmp.py` | Bounded empty-owner recheck, pre-handshake diagnostics, screenshot PPM verification |
| `tests/test_font15r2r1_qmp_safety.py` | 21 lifecycle tests; deterministic wait in the existing Windows ownership test |
| `docs/fonts/FONT15R4R1_QMP_SCREENSHOT_LIFECYCLE.md` | This report |

The helper/test qualification is committed at `4488cdc44c4ea2375696b6903a25eefc6cfd0fd9`. A documentation-only closeout commit follows. The original unstaged listener-table modification remains untouched and unstaged. The output test log is under the dedicated FONT15R4R1 directory; existing retry evidence and unrelated artifacts were preserved. Nothing was pushed and no other repository was modified.

## 8. Remaining guest qualification dependencies

This outcome qualifies the host helper only. A future guest attempt still requires an authentic AMD64 candidate, QAC exclusive ownership, and a bounded reproducible interaction plan. It must independently demonstrate pointer displacement after the long move, production Settings navigation, same-window resize from 760×500 through 640×400 to 560×360, observed 560×336 client dimensions, minimum-size clamping, clipping/hit-test agreement, font selection, and bitmap fallback.

Keep `TTF_MINIMUM_SETTINGS_SIZE_ACCEPTANCE_OPEN` open. The independent PacMan audio-link, top-level v0.3 build, combined integration, and release authorization gates also remain open. FONT15R4R1 does not close any of them.
