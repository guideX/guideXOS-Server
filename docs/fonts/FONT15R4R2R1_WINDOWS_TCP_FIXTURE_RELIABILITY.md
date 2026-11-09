# guideXOS Server — FONT15R4R2R1 Windows TCP Fixture Reliability

**Outcome A — WINDOWS_TCP_FIXTURE_QUALIFIED (host-side helper and test gate only).**

The intermittent timeout came from using TCP_TABLE_OWNER_PID_ALL to verify an established QMP connection. On this host, that table sometimes returned a non-established row for the live accepted endpoint even though the fixture had completed its handshake and the row contained the correct endpoint tuple and PID. The connected-endpoints table returned the server and client rows as ESTABLISHED. The helper now queries that table and retains every ownership check. The complete 43-test suite and the bounded 10-run integration repetition pass after the change.

No QEMU process was launched, no QAC reservation was requested, and no guest input was sent. This report does not claim real guest delivery or minimum-size Settings acceptance.

## 1. Repository, host, and preservation

| Item | Result |
|---|---|
| Host | LEON2_RDT |
| Windows registry values | ProductName Windows 10 Pro; DisplayVersion 26H2; build 26300.9818 |
| Repository / branch | D:/dev/guideXOSServer_FONT_SUPPORT / FONT_SUPPORT |
| Starting HEAD | 7a883ac6ba16e97fd7de1d7b33e164a71535153f |
| Ending implementation HEAD | 3c5f474e9c049fe9b5e71c9df211b8a8e1ddcb01 |
| Implementation commit | 3c5f474e9c049fe9b5e71c9df211b8a8e1ddcb01 |
| Report delivery | Documentation-only follow-up; its SHA is in the completion message. |
| Push command | None was invoked by this task. See remote-ref note below. |

Before editing, the index was clean and there were no untracked files. The only pre-existing worktree change was in out/font15r1-validation/guest/font15r1_qmp.py: it removes the LISTEN state filter from the listener-only table and notes that some Windows builds return state zero for those rows. That hunk was preserved unstaged and is not in the implementation commit. No other agent was active in this checkout.

During closeout, the local origin/FONT_SUPPORT tracking ref advanced to the implementation commit. Its reflog records “update by push” at 2026-11-08 20:12:07 -0800. This task did not invoke git push; no active Git hooks or configured push alias were found. A read-only git ls-remote could not verify the server because SSH public-key access was denied. The source and remote status of that tracking-ref update remain unverified; no further remote changes were attempted.

The requested E:/CodexArtifacts/guideXOSServer_FONT_SUPPORT/FONT15R4R2R1 directory was unavailable. New logs were written under the existing, git-ignored project artifact root at out/font15r4r2r1-validation/tcp-readiness-20261108T194538/. No prior evidence was overwritten. The test-created tests/__pycache__ directory was removed.

## 2. Original failure and reproduction

The failing test was tests.test_font15r2r1_qmp_safety.QMPDragSafetyTests.test_windows_owner_query_rejects_a_non_qemu_loopback_listener. Its original timeout assertion was:

    self.fail("Windows TCP table did not expose the localhost accepted socket")

The test bound an IPv4 listener on 127.0.0.1 with a dynamic port, connected a client with a dynamic source port, received the 42-byte QMP greeting, and waited up to one second for a matching ESTABLISHED server row. The accepted socket and client stayed open during the poll.

The failure was reproduced before the helper correction. One complete-suite run timed out after 77 queries with the accepted server socket open, the fixture thread alive, and a row for the exact listener/client tuple owned by the test PID (state=0). In a bounded 10-run repetition, two runs timed out: the same live tuple was reported as state=1 and state=6. Those rows did not satisfy the test's state == ESTABLISHED (5) predicate, so the timeout text incorrectly described them as absent. The other eight runs passed.

A successful diagnostic run recorded listener 127.0.0.1:52428, client 127.0.0.1:52429, PID 14968, the Python process image, and both table rows at state 5. That run found the row on its first query. The failure was intermittent and was not a consistent endpoint-tuple or socket-lifetime error.

## 3. Windows table investigation and cause

The test and helper queried TCP_TABLE_OWNER_PID_ALL (class 5), then selected rows whose state was 5. On the same live localhost socket, a direct comparison queried both class 5 and TCP_TABLE_OWNER_PID_CONNECTIONS (class 4) 77 times:

- Class 5 returned the client-side row at state 5, while the row matching the accepted server tuple varied among states 5, 2, and 1 in that comparison. Other reproduced failures showed states 0 and 6 for that same live tuple.
- Class 4 returned both directions of the same four-tuple at state 5 in all 77 samples. Both rows had the current Python PID.
- The client had received the greeting; the fixture's accepted socket was open; the server thread was alive; and the endpoint addresses and ports matched in both directions.

Microsoft documents class 4 as the table of connected TCP endpoints and class 5 as the table of all TCP endpoints. The row structure carries state, local and remote endpoints, and owner PID; state 5 denotes ESTABLISHED. See the [TCP_TABLE_CLASS reference](https://learn.microsoft.com/en-us/windows/win32/api/iprtrmib/ne-iprtrmib-tcp_table_class) and [MIB_TCPROW_OWNER_PID reference](https://learn.microsoft.com/en-us/windows/win32/api/tcpmib/ns-tcpmib-mib_tcprow_owner_pid).

**Confirmed cause:** the helper used the all-endpoints table for an established connection lookup. On this host, rows from that table did not consistently represent the accepted endpoint as established, despite the live socket and correct tuple/PID. The test therefore timed out, and the helper could also reject that active connection fail-closed. Querying the connected-endpoints table fixes the demonstrated mismatch while keeping the state and ownership requirements intact.

**Not the cause:** fixture acceptance/readiness, premature socket closure, incorrect endpoint order, IPv4/IPv6 mismatch, or wrong PID. Those conditions were checked directly. The precise Windows networking component behind the inconsistent class-5 state values is not established here. A recent [Microsoft Q&A report](https://learn.microsoft.com/en-us/answers/questions/6057787/getextendedtcptable-returns-dwstate-0-for-listenin) describes state-zero listener rows on build 26300.9818, but it does not explain the accepted-connection rows observed in this investigation.

## 4. Changes made

WindowsEndpointVerifier now defines TCP_TABLE_OWNER_PID_CONNECTIONS = 4 and uses that table in verify_connection. The helper still requires all of the following:

1. The live socket endpoints match the QMP loopback host and port.
2. The table row is ESTABLISHED and matches the exact server/client tuple.
3. The unique owner PID set is exactly the expected QAC-owned PID.
4. The current process image and creation time still match the verified QEMU process identity.

The empty-owner retries remain bounded and use the same socket. Foreign and ambiguous owners remain rejected. No blind reconnect, drag cleanup, unresolved button-state, screenshot freshness, or P6 completeness behavior changed.

The localhost fixture now signals accepted and greeting-ready states, exposes the accepted socket for endpoint/lifetime assertions, and shuts down that socket deterministically. The timeout diagnostic reports only rows matching the fixture's allocated local port and client port. The test suite adds explicit foreign/ambiguous owner, invalid tuple, close-during-lookup, and repeated fixture teardown cases. The real Windows integration cases remain present.

## 5. Test coverage and results

| Required case | Coverage |
|---|---|
| Normal established localhost ownership | Native Windows connected-table lookup in test_windows_tcp_owner_recheck_recovers_on_localhost_fixture; process identity is test-overridden because the fixture is Python, never QEMU. |
| Accepted-socket endpoint mapping | Native Windows integration checks the accepted socket's endpoints and exact server-side tuple/PID. |
| Empty owner before greeting readiness | Deterministic empty-first fake-verifier case; retry remains on the same socket. |
| Empty owner after fixture readiness | Windows fixture's ready event is required before injecting the empty first snapshot. |
| Delayed table publication | Empty-first injected snapshot followed by the native connected table on the same socket. |
| Foreign and ambiguous owners | Injected exact-tuple rows with foreign-only and expected-plus-foreign PID sets are rejected. |
| Socket closure during verification | The socket closes during table lookup; an empty owner set is rejected. |
| Invalid endpoint tuple | Rejected before the TCP table provider is queried. |
| Repeated fixture initialization and teardown | Three accepted connections and one unopened fixture are closed; threads finish and accepted sockets close. |
| No leaked sockets/background fixture threads | Windows integration and repeated teardown assert closed sockets, finished events, and joined threads. |

Commands and results:

- Original test after correction: **1/1 passed**.
- Complete suite, python -B -m unittest tests.test_font15r2r1_qmp_safety -v: **43/43 passed**.
- Previously failing Windows test after correction: **10/10 passed** in a bounded series; no timeout or ownership mismatch occurred.
- Before correction, the same bounded test series was **8/10**; failures recorded exact matching rows in states 1 and 6. A separate full-suite run recorded state 0 before timing out.
- Python AST syntax validation passed for the test and helper files.
- git diff --check passed. Git emitted only its existing LF-to-CRLF working-copy notices; there were no whitespace errors.

The complete suite retains the prior fail-closed, no-reconnect, bounded drag cleanup, fresh screenshot, and P6 completeness cases. The native Windows integration confirms the Python fixture PID is rejected as not QEMU. The connection-recheck integration uses one injected empty snapshot and then the native connected table; only its test process identity is substituted.

## 6. Artifacts and Git disposition

Generated logs are under the ignored directory out/font15r4r2r1-validation/tcp-readiness-20261108T194538/, including:

- tcp-table-diagnostic-final.jsonl
- tcp-table-state-history.json
- tcp-table-class-comparison.json
- isolated-repetition-final2.log (pre-fix 8/10)
- isolated-repetition-after-fix.log (post-fix 10/10)
- full-suite-final-after-fix.log (43/43)

The implementation commit is 3c5f474e9c049fe9b5e71c9df211b8a8e1ddcb01. It contains only the intended helper table-selection change and the safety tests. The pre-existing listener table edit remains unstaged and uncommitted. The report is delivered in a separate documentation-only commit. No push command was issued by this task; the remote-tracking-ref movement described above could not be attributed or independently verified.

## 7. Remaining gates

Keep TTF_MINIMUM_SETTINGS_SIZE_ACCEPTANCE_OPEN open. The independent PacMan audio-link prerequisite, top-level Server v0.3 release build, combined integration qualification, public technical preview security requirements, and final release authorization remain open.

This host-side result permits a future **single bounded FONT15R4R2 guest retry**, subject to QAC ownership and that phase's own preflight. That guest retry was not started here. The result establishes no real guest input delivery or typography acceptance.
