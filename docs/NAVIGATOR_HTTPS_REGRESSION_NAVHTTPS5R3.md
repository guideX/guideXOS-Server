# NAVHTTPS5R3 — Intermittent TCP Failure Capture and HTTPS Baseline Closure

**Outcome B — NON_REPRODUCED.** One bounded, instrumented AMD64 QEMU run completed. The first generic local HTTPS connection resolved ARP, sent a TCP SYN, received a SYN-ACK, completed the handshake, and reached TLS certificate verification. The expected untrusted smoke certificate was rejected. All four required controlled HTTPS policy cases passed. The historical pre-SYN failure remains unexplained; no behavioral correction is justified.

## 1. Repository and host

- Repository: `D:\dev\guideXOSServer_NAVIGATOR_IMPROVEMENTS`
- Host: `LEON2_RDT`
- Branch: `NAVIGATOR_GENERAL_IMPROVEMENTS`
- Starting HEAD: `fe638754f1f11a36723f6714046a44c02401a01b`
- Worktree was clean before work. The accepted NAVHTTPS4R and NAVHTTPS5R2 reports were preserved.
- The report is the only repository change for NAVHTTPS5R3. No branch, Git topology, or unrelated project was changed. No push was made.
- I219-LM tracker remains `last_completed=PHASE26`, `next_expected=PHASE27`.

Guest and packet evidence are under `E:\CodexArtifacts\NAVHTTPS5R3`. The final local HEAD is the report commit recorded in the task completion report.

## 2. Historical symptom

NAVHTTPS5R recorded four generic local probe failures. The Test A request was `https://guidexos.test:8443/navigator-smoke/tls-basic.html`, resolving to `10.0.2.2:8443`. Serial output showed socket fd `0x64` created and closed before `[TCP] Connecting`; the smoke result was `TcpConnectFailed / network down` after one TLS TCP connect attempt.

The matching historical pcap contains no TCP flow for guest source port 49152 to `10.0.2.2:8443`, although it contains later successful connections to the same fixture and ARP exchanges with `10.0.2.2`. The historical trace did not preserve the underlying IPv4 send status, so it cannot distinguish route, neighbor, packet submission, NIC, or other pre-SYN conditions.

NAVHTTPS5R2 added bounded IPv4 send-stage/status and TCP connect failure diagnostics. Those diagnostics do not alter transport behavior and emit the TCP failure line only when a connect fails. The instrumented `production_validated` baseline reached TLS verification and did not reproduce the pre-SYN error.

The local smoke certificate is intentionally untrusted in the production-policy scenario. Its `CertificateVerifyFailed` result is the expected security assertion and is not a TCP failure.

## 3. Failing and successful run comparison

| Dimension | Historical NAVHTTPS5R failure | NAVHTTPS5R3 captured run |
|---|---|---|
| Guest image | Source HEAD `a4a682970b53a12db9ff133db06aea8175b83164`; kernel SHA-256 `923384cbb711c96e2e853aa92278b6dbd9a04a968abdfae57be1e1b28e750d77`. This predates the NAVHTTPS5R2 diagnostics. | Source HEAD `fe638754f1f11a36723f6714046a44c02401a01b`; kernel SHA-256 `3dd1649658e066bb157543ee3a46432e95550c9c241dc7f97c439e91293354a8`; source fingerprint `107f09aa43ec2b42450aa98f5366823517f47440ae91edab955339cecb1cc795`. This is the same instrumented kernel binary used by the accepted R2 baseline. |
| QEMU network | QEMU `pc`, `e1000`, user networking; guest `10.0.2.15`, gateway `10.0.2.2`. | Same machine/NIC/network backend and guest/gateway addressing. HTTPS target remained `10.0.2.2:8443`. |
| Fixture | The generic hostname and endpoint were the same. Earlier fixture logs show the servers started and later same-endpoint requests completed. | HTTP and HTTPS fixtures were started before guest launch; the runner waited for ports 8080/8443 and the controlled DNS fixture readiness event. DNS fixture logged ready at `2026-11-08T18:45:11.329802Z`. |
| NIC and IP initialization | Historical serial already showed NIC link UP and QEMU-default IPv4 `10.0.2.15/24`, gateway `10.0.2.2` before the TLS probe. | Same link and IPv4 configuration before the first probe. Both traces therefore lack evidence for a late link/IP initialization race. |
| DNS and endpoint | `guidexos.test` resolved to `10.0.2.2`; no DNS error was recorded. | Same hostname, address, and port; DNS error `(none)`. |
| First socket | fd `0x64` closed before a `[TCP] Connecting` line; historical pcap lacks the corresponding source-port-49152 SYN. | The first generic probe used source port 49152, logged `[TCP] Connecting`, and reached `Connection established`. The pcap contains its SYN, SYN-ACK, and ACK. |
| TLS outcome | No ClientHello or certificate exchange for the failed attempt. | ClientHello and server TLS data were exchanged. Certificate verification failed with flag 8, as expected for the intentionally untrusted local smoke certificate. |
| Trust/policy | No evidence the trust policy caused the pre-SYN error. | Controlled production-source test bundle remains one root with `production_ready=no`; TLS and hostname verification remain enabled, plaintext fallback disabled. |

The relevant observed differences are the guest image revision and the added diagnostic instrumentation. QEMU topology, endpoint, guest addressing, and name resolution match. The evidence does not establish that either the image difference or any startup timing difference caused the historical failure.

## 4. Packet capture preparation and result

The existing runner supports QEMU `filter-dump` on `net0`. It attached the capture object before QEMU started, so capture began before guest boot and the diagnostic probe. This records traffic at the QEMU guest NIC boundary. It is not host-interface or post-NAT capture. Only the isolated qualification guest's controlled local fixture traffic was captured; public browsing remained disabled.

Capture: `E:\CodexArtifacts\NAVHTTPS5R3\guest-net.pcap`

- Classic Ethernet PCAP, 2,204 complete records, 366,777 bytes.
- 2 ARP records: guest request for `10.0.2.2`, followed by the gateway reply.
- 2,200 TCP records and 2 IPv6 records from the isolated guest session.
- First generic flow: `10.0.2.15:49152` to `10.0.2.2:8443`.
- Exactly one SYN and one SYN-ACK were observed for this connect; no SYN retransmission or connect timeout occurred.
- The server TLS response followed the handshake. The guest sent a TLS alert after rejecting the certificate. FIN/RST teardown packets occur after that TLS validation failure, not before or during connection establishment.

The packet sequence is also summarized in `E:\CodexArtifacts\NAVHTTPS5R3\packet-analysis.md`. Current and historical pcaps can be parsed with `E:\CodexArtifacts\NAVHTTPS5R3\pcap_concise.py`; the historical capture remains preserved at `E:\CodexArtifacts\NAVHTTPS5R\test-a.pcap`.

## 5. Correlated IPv4/TCP findings

The captured serial log reports one TLS TCP connect attempt, `tls_status=CertificateVerifyFailed`, `verify_flags=8`, and `tls_smoke.result=PASS`. It contains `[TCP] Connecting` followed by `Connection established`. The pcap shows the matching first connection's guest SYN, gateway SYN-ACK, client ACK, and TLS payloads.

No `[TCP-CONNECT-DIAG]` error line was emitted because the connect succeeded. Therefore no failing IPv4 stage/status exists to report for this run. The pcap and serial trace establish that IPv4 packet submission and TCP establishment succeeded for the generic request; the expected TLS certificate rejection happened afterward.

Classification: **NON_REPRODUCED; root cause INCONCLUSIVE.** None of A–F can be selected without a recurrence. The successful run does not retrospectively explain the historical missing SYN.

## 6. Bounded guest qualification

- Hosted preflight completed before guest launch.
- One `production_validated` guest qualification run was performed under the runner's QAC-exclusive admission. The runner log records one QEMU packet-capture launch; no retry or second guest run occurred.
- The scenario exercised the generic local probe and the existing four required controlled HTTPS policy cases in one run.
- Runner result: `NAVIGATOR-SMOKE result=PASS`; required HTTPS cases executed 4, passed 4, failed 0, blocked 0, skipped 0.
- Generic local probe: expected certificate rejection with verify flag 8, smoke assertion PASS.
- Validated case: `Success`, verify flags 0. Redirect case: `Success`, verify flags 0. Wrong-host rejection: verify flags 12, assertion PASS. HTTPS-to-HTTP downgrade: `PolicyBlocked`, assertion PASS.
- Actual AMD64 kernel build succeeded. No additional build or guest run was needed after the successful qualification.

## 7. Hosted qualification

| Check | Result |
|---|---|
| IPv4 send diagnostics and TCP socket lifecycle | PASS: configuration, packet validation, route, neighbor, NIC submission, failed-SYN cleanup, pending-SYN close, socket bounds |
| DNS protocol and controlled fixture | PASS: 13 protocol scenarios and 6 fixture checks |
| UDP and socket bounds | PASS: 6 cases |
| Navigator URL resolution | PASS: 11 cases |
| HTTPS outcome contract | PASS: 7 cases |
| Secure-random contract | PASS |
| Mbed TLS profile | PASS: Mbed TLS 4.1.0, TF-PSA Crypto 1.1.0, 3 patches |
| Controlled CA fixture validation | PASS: one root, `production_ready=no`, `test_only=yes` |
| Hosted Navigator UI smoke | PASS: `NAVIGATOR_SMOKE_RESULT: PASS`. The accepted 287/287 qualification target in NAVHTTPS5R2 is preserved; this smoke runner emits the result marker but no numeric count. |

The UI smoke changed the `desktop.json` recent-items list as a test side effect. That one file was restored to the clean preflight state and the worktree was clean before this report was added.

Hosted logs and the guest build, serial, fixture, manifest, and packet artifacts are stored under `E:\CodexArtifacts\NAVHTTPS5R3`.

## 8. Code changes and security boundaries

No transport or TLS behavior was changed. No retries or timeout increases were added. TLS verification, hostname checks, date checking, and plaintext-fallback policy were not weakened. Physical I219-LM DMA handling was not changed.

Public browsing remains disabled. The candidate CA remains `production_ready=no`; the guest clock remains `Plausible`. No unapproved trust root was introduced, and no CRL/OCSP support is claimed. NAVHTTPS4R and NAVHTTPS5R2 evidence remains intact.

## 9. Remaining uncertainty and engineering recommendation

The historical failure remains an unresolved observation. The new diagnostic and capture path is ready to identify the failing stage if it recurs, but this bounded run did not trigger it. Because the original symptom occurred repeatedly, residual regression risk is non-zero; the current guest HTTPS regression path is healthy, and the observed failure was a pre-SYN transport error rather than a TLS policy bypass.

Close NAVHTTPS5R3 for its bounded investigation scope as **NON_REPRODUCED**. Keep the historical symptom documented and do not call it fixed. It should not block NAVHTTPS6 production-trust preparation: proceed with preparation while keeping public browsing disabled and `production_ready=no` until the separate trust and release criteria are met. Do not extend this diagnostic phase with repeated QEMU runs solely to force reproduction.

Immediately after the NAVHTTPS5R3 runner exited, no QEMU or controlled fixture process remained, fixture ports were clear, and the QAC mutex was available. All capture and fixture resources were released. A later final check found QEMU PID 27568 for `D:\dev\guideXOSServer_FONT_SUPPORT`, started at 10:56 AM with `-net none`. This is a separate project's session; it was left untouched. The QAC mutex remained free and no NAVHTTPS5R3 fixture process remained.

## 10. Evidence hashes

| Artifact | SHA-256 |
|---|---|
| NAVHTTPS5R3 guest NIC PCAP | `8187326007ecc5bc2dbc0a8a6b5c82761e95fd07cb893fdbd763accf9dc59c41` |
| NAVHTTPS5R3 guest serial log | `bbdb5edde53c40fafcad3d6a911df333210e40f148aecab55984f19b9dd082d1` |
| NAVHTTPS5R3 runner log | `6c9d3875e88e0149db2d5223f8b37e877b7d320402f42a5987ef9141862602c6` |
| Qualification manifest | `7c2d6392a1a5eb65a56c0d6158abf1a496919c2fe441cd1b120b065df41634b8` |
| Built AMD64 kernel | `3dd1649658e066bb157543ee3a46432e95550c9c241dc7f97c439e91293354a8` |
| Packet analysis | `1558d9fdb3156c1db1db3f7f8f134a232603e7ef76980584b0c644e640c9abe1` |
| Current hosted Navigator UI log | `4ad5620960f680fac5b94e7a461d560ef0f27f025ac6f3437b3e43293406cdef` |
| Copied historical failing serial log | `c972c8337dd8331f8fc87ffe7226e5ec97ee86c954944983ed7c91f5e4dd1c9d` |
| Historical NAVHTTPS5R pcap | `6fa072bee7049682c6b98708e076a9433870730b25e2cc631d88d71592b7fb23` |
