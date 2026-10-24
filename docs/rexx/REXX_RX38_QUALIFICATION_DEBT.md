# REXX RX38 — Qualification Debt and Release Gates

## Release blocker classification

| Priority | RX38 finding | v0.3 disposition |
|---|---|---|
| **P0 — Release blocker** | None demonstrated. RX38 found no memory corruption, unsafe host execution, uncontrolled resource use, data corruption, or failure of an essential operation inside the bounded core profile. | No P0 blocker. This is not a claim that the entire language or all host services are implemented. |
| **P1 — Required qualification** | None for the bounded profile. RX31 injected-input native acceptance and RX37 output-capability query proof remain open, but neither feature is installed or consumed by the production profile. | Do not make those optional provider contracts v0.3 release gates. Reopen as P1 only if a production integration begins using them. |
| **P2 — Post-release integration** | Production line input and streams; trusted wall-clock source for current DATE/TIME; a deliberate host-stream contract; SAY/output adapter behavior if v0.3 requires compact/wrapped text semantics. | Keep the dependencies absent and fail closed for v0.3. Scope any later service to an independent bounded integration phase. |
| **P3 — Future enhancement** | LINESIZE and a shared logical-column policy; richer TRACE/debugger functions; USERID; IBM/Regina optional compatibility; native qualification of the unused RX37 capability query if adopted. | Defer unless the product requirement explicitly selects one of these capabilities. |

The priority describes the bounded Server v0.3 profile, not general IBM compatibility. An IBM built-in being absent is not by itself a P0/P1 issue.

## RX31 — input provider

RX31 remains recorded as **Outcome B — Partial**. Its per-VM callback is hosted-qualified, including queue priority, empty input, case rules, binary bytes, EOF/error/pending responses, limits, allocation failures, replacement, teardown, and VM isolation. Production installs no reader because the shell does not expose a safe reusable line-input service.

Later guest evidence is directly relevant but does not close the entire acceptance contract:

- RX32 native guest: demonstrated provider fail-closed/recovery, and its scorecard explicitly kept `rx31_native_acceptance` open.
- RX33 native guest: demonstrated input-provider fail-closed behavior and recovery; its scorecard again stated RX31 acceptance remained open.
- RX35 native guest: ran `/rx31-empty-pull.rex`, observed the deterministic default-input-unavailable error with no fabricated input, then ran `/rx31-recovery.rex`, observed `RX31_POST_ERROR_RESPONSIVE` and `RX31_RECOVERY_COUNT=0`, exit status 0. This directly qualifies the production no-reader path and queue cleanup.

Those guests did **not** install an injected native input provider or execute a provider-supplied line. Therefore the native injected-provider contract remains unqualified. The gap does not affect the production v0.3 profile because that provider is absent by design; stack-backed PULL remains covered. No guest was launched in RX38 to retry this optional contract.

## RX34 — stream provider

RX34 completed two separate native runs on disposable media with networking disabled:

- Production/no-provider kernel SHA-256 `67edd7fb3bbcf2fc237a4c397d8fb06aec11ee56a8b617a382ff43146d4034d2`: `STREAM()` reported `UNAVAILABLE`; `CHARIN()` failed closed through a REXX trap; state remained unavailable and a recovery script ran. QMP shutdown was acknowledged, QEMU child exit was 0, and QAC released the reservation.
- Fixture kernel SHA-256 `bea97f4a01cacbd8a50fc10e7f4638497b1cb3778e66713832cff760f1b4fe33`: the qualification-only provider demonstrated default and named reads, cursor continuation, binary bytes, zero-length repositioning, exact EOF, short read, provider error, unknown identity, non-seekable position, argument/state recovery, and the oversized-request limit. QMP shutdown, child exit 0, and release were recorded.

This accepts the bounded fixture behavior and the production fail-closed boundary. It does not supply production filesystem, line I/O, stream output, or arbitrary-path access; CHARIN and STREAM remain Partial.

## RX37/RX37R1 — output capability and LINESIZE

RX37's output capability API passed 4,203 hosted assertions (107 direct assertions and 4,096 property cases). Its hosted formatter uses an independent test destination. Source inspection establishes:

- The production kernel shell sets `RexxOutputHost.read_capability` to NULL. The CLI bridge is installed in the VM and reports unsupported if queried; the query was not natively observed.
- Ordinary SAY does not query output capability. Its payload and final LF are separate callbacks. The production writer chunks at 119 bytes, flushes callback-local text to rows, and stores the standalone final LF as an empty row. Renderer pixels are not a logical width, and no resize generation is published.
- RX35 guest scripts showed ordinary production shell output markers, but no RX37 guest observed the capability query or a logical-width/wrap contract.

RX37R1 was **Outcome C — native qualification blocked**. At that time a foreign acceptance guest held QAC; the RX37R1 request waited 20 seconds at queue position 1, received no grant, and was cancelled by the client. No RX37R1 guest, output markers, screenshots, QMP shutdown, runner exit, or reservation release are claimed. RX38 did not retry: the query has no production consumer in the bounded profile. At RX38's final read-only QAC check there were no reservations but two unmanaged/foreign QEMU processes (PIDs 20392 and 21932), which RX38 left untouched.

`LINESIZE` remains **Not implemented**. A 119-byte row limit, pixels, font metrics, or IBM batch constant cannot stand in for a logical column count. Keep the new API and `LINESIZE` proof outside the release gate until a production destination owns a shared SAY/wrap policy and resize generation.

This follows the RX36 blocker report: the VM writer receives bytes and length, the shell stores 119-byte chunks while rendering from separate drawable geometry, and no single authoritative production width exists. RX37 added the query contract but did not add a production width provider or shared SAY policy; the inventory status correctly remains unchanged.

## Native QAC evidence audit

The phase-by-phase kernel hashes, QAC/guest configuration, observed checks, process exit records, QMP acknowledgments, reservation release, and known unverified subcases are recorded in [native-evidence-audit.md](evidence/rx38/native-evidence-audit.md) and `evidence/rx38/native-evidence-audit.json`. All RX22–RX37 claims are historical phase records; they are not represented as RX38 guest runs.

Important recorded limitations include RX24's nonzero immediate post-QMP runner status check despite child exit 0 and later empty QAC state; RX25's uncaptured QAC CLI parent exit field; RX28's runner exit 1 before guest work followed by a passing manual suite under the independently verified QAC child; RX33's unobserved inherited TIME(R) completion; and RX35's unclaimed TIME(R) screenshot completion. These do not imply failure of the REXX core behaviors in the bounded profile.

## RX38 verification and carry-forward

- Hosted RX37 phase reran against the current source tree. Focused core, allocation/recovery, RX30–RX35, CLI, compatibility, and RX37 tests completed; the direct RX37 output-capability target passed 4,203/4,203, exit 0.
- ASan and UBSan were not rerun. RX37R1 audited unchanged production source blobs; accepted RX37 matrices remain 36/36 targets each.
- Freestanding AMD64 was not rebuilt. Accepted RX37 ELF hash remains the evidence for the unchanged production source.
- Regina remained `NOT_RUN` because no executable was available.
- No QAC request, guest, reservation, signal, shutdown, or foreign-process control occurred in RX38.

## Recommended RX39 scope

Run a bounded v0.3 integration phase: define the actual REXX feature profile, link the accepted core into the Server build, verify the production `KERNEL` command/output boundary, and perform only the necessary QAC smoke checks after an exclusive lane is available. Include visible `SAY` output and post-error recovery. Do not expand built-ins or create a new Console, filesystem/stream, wall-clock, debugger, or graphics subsystem. Only promote RX31 input, RX34 streams, RX37 output capability, or LINESIZE if the v0.3 product contract needs the corresponding production service.
