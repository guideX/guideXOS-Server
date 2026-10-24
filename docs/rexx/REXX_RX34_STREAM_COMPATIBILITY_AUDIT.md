# RX34 Stream Compatibility Audit

## Selected profile

RX34 follows IBM z/OS 2.5 byte-oriented CHARIN semantics, adapted to guideXOS’s explicitly registered embedded-stream model. It does not combine z/VM, TSO/E, and z/OS UNIX behavior.

Primary references:

- [IBM z/OS 2.5 CHARIN](https://www.ibm.com/docs/en/zos/2.5.0?topic=functions-charin)
- [IBM z/OS 2.5 stream I/O functions](https://www.ibm.com/docs/en/zos/2.5.0?topic=implementation-stream-io-functions)
- [IBM z/OS 2.5 STREAM (Operations)](https://www.ibm.com/docs/en/zos/2.5.0?topic=functions-stream-operations)

IBM defines CHARIN characters as single bytes from X'00' through X'FF'. A short request returns available bytes and marks an error on the stream. A zero-length call reads nothing and may reposition when a start is specified. RX34 preserves those byte and position rules within the provider contract.

## Distinct concepts

| Concept | RX34 treatment |
|---|---|
| Stored stream error | Bounded per-VM record keyed by provider identity and generations. |
| Interpreter NOTREADY condition | Not delivered by RX34. NOTREADY is only an observable status label/error class. |
| STREAM observation | Read-only guideXOS S/D status subset. Name resolution calls only the provider resolver; query never reads or seeks and does not clear state. |
| IBM z/OS STREAM operation | Command/C stream commands such as OPEN, CLOSE, and QUERY. Not implemented. |
| EOF | A zero-byte provider EOF result is ERROR/END_OF_STREAM, distinct from a provider I/O error. |
| Successful short read | Returns the actual bytes and stores ERROR/SHORT_READ with request/return counts. |
| Exact final read | Returns requested bytes successfully and records the provider EOF-after-read bit as EXHAUSTED. |

## Implemented behavior and limits

Each VM owns an inline 16-entry status table keyed by at most 128 stable identity bytes, provider generation, and nonzero identity generation. Provider replacement clears the table. The new public observation API resolves the caller-facing name, returns current status or UNRESOLVED, and does not change the table. The original host query by stable identity remains a pure table lookup.

STREAM S/default returns a bounded state label. STREAM D adds exact counts and EOF metadata only when the last operation is a positive READ. A sequential zero-length CHARIN keeps the error/state class sticky but sets last operation to ZERO_LENGTH_READ and counts to zero. A successful explicit seek or successful later read can recover.

There is no production stream provider. The kernel fixture provider is test-only, in-memory, deterministic, and gated at build time. No names become paths; no filesystem, output, network, or general stream command support is added. No language-level SIGNAL ON NOTREADY handling is claimed. Regina comparison is NOT_RUN when the runtime is unavailable.