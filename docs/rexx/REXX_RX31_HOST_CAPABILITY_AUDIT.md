# REXX RX31 — Host Capability Audit

## Scope and decision

RX31 defines the safe empty-queue `PULL` integration point and records the production boundary for wall-clock and TRACE behavior. It does not add a clock service, persistent RTC policy, debugger, or general host-services framework. The VM input hook is independent of the accepted RX19/RX20 clock contract.

| Capability | RX30 baseline | RX31 evidence and status |
| --- | --- | --- |
| VM external data stack | Implemented | Implemented and inherited; input does not alter its accounting |
| `QUEUED()` | Implemented | Implemented and inherited; provider reads do not count |
| Empty-queue input fallback | Unavailable | Available through injected per-VM callback; no production reader |
| Raw CMOS clock | Available | Available as raw calendar fields only |
| Valid local wall-clock time | Unavailable | Unavailable; production interpretation remains unknown |
| Valid UTC wall-clock time | Unavailable | Unavailable; offset/provenance are not established |
| PIT elapsed timing | Implemented | Implemented and inherited independently of wall-clock support |
| TRACE N/E/F/O | Implemented | Implemented and inherited for actual command-return tracing |
| Full clause/expression TRACE | Unsupported | Unsupported; execution/intermediate hooks and trace sink/control are absent |
| Interactive TRACE debugger | Unsupported | Unsupported; interactive input and pause/resume control are absent |

RX31 improves the input row only for injected embeddings and keeps production empty-queue behavior fail-closed. DATE, TIME, and TRACE remain Partial.

### Qualification categories

| Category | RX31 capabilities and evidence |
| --- | --- |
| Implemented and qualified | Per-VM external data stack and `QUEUED()`; RX31 queue-first fallback logic under deterministic host tests; PIT elapsed timing; TRACE N/E/F/O command-return behavior. The input callback is qualified as an embedding contract, not as a live Console source. |
| Available only through injected tests | Provider-supplied PULL lines; deterministic local/UTC clock readings, offsets, and clock failure cases. These adapters do not establish a production Console or wall-clock source. |
| Present but unqualified for REXX input | The kernel shell receives characters into its own private command buffer and synchronously executes complete commands. It has no reusable REXX line-read service or safe reentrant pull operation. |
| Unsupported in production | A live default-input provider; valid local or UTC wall-clock output from the raw CMOS source; full TRACE clause/expression events; interactive TRACE debugging. |
| Deferred due to OS architecture dependency | Console line input needs a safe kernel input service with bounded cancellation and VM-safe lifetime. RTC interpretation needs a versioned Settings-to-kernel policy contract. Interactive debugging needs interpreter pause/resume control and an input/output lifecycle. |

## Existing host integration surfaces

The VM already has per-VM clock, output, and command dispatch callbacks. REXX VFS operations and default ADDRESS routing use process-wide startup services. There was no REXX line-input callback. The new `RexxVMInputReader` is a small per-VM extension, not a generalized host-services table.

The kernel shell's character path fills a private shell command buffer and executes full commands synchronously. Its public surface accepts complete lines and writes output; it does not expose a safe line-source operation. Calling that machinery from an executing REXX pull could reenter shell execution and would couple VM lifetimes to shell buffers. The native adapter therefore leaves the new callback unset. The contract does not use hosted `stdin`, spin for input, or claim actual console typing.

## Clock contract and future integration point

RX19–RX21 evidence remains controlling. Production CMOS returns a raw calendar observation with unknown UTC/local interpretation, unknown UTC offset, one-second resolution, and unknown accuracy. PIT monotonic elapsed timing works. RX21 found no supported persistent RTC interpretation service. RX31's input provider is not a clock source and supplies no authenticated clock metadata.

The existing `RexxVMClockReading` / kernel `ClockReading` contract remains the only clock integration path. For current DATE/TIME wall output to become safe, a future source must provide and validate:

- Complete Gregorian calendar fields and a declared supported wall capability.
- Explicit provenance: source-defined local civil time or UTC, and a known signed UTC offset when both interpretations are claimed. Unknown interpretation must remain unavailable.
- Validity and failure status, with invalid or unsupported data clearing stale values and mapping to unavailable behavior.
- Resolution and accuracy metadata, with honest bounds and no implication that storage precision equals clock accuracy.
- A provider/context lifetime bounded by the synchronous VM execution, per-VM isolation, and a single clause-consistent wall/monotonic snapshot.
- Tests for metadata validation, source failure/recovery, invalidation, date boundaries, offsets, and stale-data clearing.

The existing deterministic adapters already exercise source-defined UTC/local examples, fixed offsets, invalid metadata and calendars, source failure/recovery, clause snapshots, VM isolation, and PIT conversion/overflow. These injected tests do not promote raw CMOS to UTC/local and do not prove firmware runtime services. Do not create a parallel RX31 clock provider, infer a timezone, use unsupported UEFI runtime calls, or add network time. A future persistent RTC policy must first have a versioned, failure-safe Settings/kernel contract; RX21 found no such supported service.

## TRACE host capability boundaries

RX24 implements per-VM `N`, `E`, `F`, and `O` state and emits real command-dispatch return traces through the existing VM output writer. Routine restoration is implemented. Unsupported options fail explicitly; storing a state bit is not treated as support.

| TRACE request | Required runtime facility | RX31 status |
| --- | --- | --- |
| `A`, `C` | Clause/command execution notifications and trace formatting | Unsupported; no clause event stream |
| `I`, `R` | Expression/intermediate/result notifications | Unsupported; evaluator does not emit these events |
| `L` | Label/branch event notifications | Unsupported; no trace label hook |
| `S` | Scan-only or execution-control path | Unsupported; no scan mode |
| `?` | Debugger input, pause state, and resume control | Unsupported; outside RX31 |
| `!` | Command inhibition wired into dispatch semantics | Unsupported; no inhibition control |
| `N`, `E`, `F`, `O` | Command-return hook plus trace output | Implemented and inherited from RX24 |

The smallest future noninteractive integration would be VM-owned clause/expression event hooks routed to an already installed output writer, with explicit tests for actual emitted values and source positions. Interactive debugging also requires a bounded input/pause/resume lifecycle and is a separate project. RX31 adds no trace output framework or debugger.

## Integration decisions

- Input uses the existing `PULL` parse-plan entry after the per-VM data stack is empty. Provider bytes are copied and passed to the existing template parser.
- Clock work remains behind RX19/RX20 capability contracts and needs a production provenance source plus a proper policy owner before DATE/TIME changes.
- TRACE work needs interpreter execution events and/or command-dispatch controls before new actions can be claimed.
- Keep persistent RTC policy, full clause/expression TRACE, interactive debugging, host stream I/O built-ins, IBM-specific EXTERNALS/LINESIZE/USERID, and shared/named/persistent queues as independent future milestones.
