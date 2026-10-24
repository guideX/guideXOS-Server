# REXX RX33 — Stream Compatibility Audit

## Scope and reference

The chosen reference is the IBM z/OS 2.5 stream I/O profile for CHARIN. IBM describes CHARIN as an unedited byte read, uses the default input stream when name is omitted, permits start only for persistent streams, returns fewer bytes when the requested length is unavailable while recording a stream error, and defines length zero as a no-read repositioning operation. IBM's stream I/O guide defines a character as one byte in the 0x00–0xFF range. See [CHARIN](https://www.ibm.com/docs/en/zos/2.5.0?topic=functions-charin) and [The Stream I/O Functions](https://www.ibm.com/docs/en/zos/2.5.0?topic=implementation-stream-io-functions).

The separate IBM description of transient and persistent streams provides host-specific stream context; RX33 does not claim to implement z/OS stream opening or persistence lifecycle. See [IBM transient and persistent streams](https://www.ibm.com/docs/en/zos/3.1.0?topic=implementation-transient-persistent-streams). A provider's seekable capability is the bounded embedded contract used for explicit start.

## Compatibility matrix

| Area | IBM z/OS 2.5 reference | RX33 behavior | Result |
|---|---|---|---|
| Byte model | Character is one byte, 0x00–0xFF | Length-bearing binary-safe REXX strings; no strlen on data | Supported |
| Omitted name | Standard input | Provider default identity; unavailable when no provider is installed | Embedded default supported; production stdin integration absent |
| Named stream | IBM stream or path identity | Provider-recognized opaque identity only | Deliberately restricted; arbitrary paths are not opened |
| Explicit empty name | Not treated as an omitted argument by RX33 | Passed as a zero-length named identity | Provider must register it explicitly |
| Omitted start | Current read location | Provider-owned current cursor | Supported |
| Explicit start | Byte number for persistent streams | One-based position only when provider advertises seekable | Supported for provider's seekable identities |
| Length omitted | Default one character | One byte | Supported |
| Length zero | Empty result and optional reposition | Empty result; explicit start still seeks | Supported |
| Full sequential read | Advance by bytes returned | Provider advances once by actual byte count | Supported |
| Short read | Return available bytes and mark stream error | Return available bytes; no retained error state | Partial |
| EOF before bytes | Stream I/O condition state is observable through STREAM | Distinct VM EOF status routed to generic SYNTAX error | Partial |
| Temporarily unavailable | Host/file-type behavior can block or return short | NOTREADY status is surfaced as generic SYNTAX stream-I/O error; no blocking | Partial |
| Pending operation | May depend on transient-stream implementation | Immediate PENDING_UNSUPPORTED error | Deliberately unsupported |
| Persistent/open/close behavior | STREAM controls lifecycle and status | No open/close table or STREAM BIF | Unsupported by scope |
| Encoding | Byte stream | Raw bytes only; no EBCDIC, DBCS, Unicode, or locale conversion | Supported only as a byte contract |

## Condition and recovery gap

The VM can preserve a provider's immediate status internally. It cannot retain an IBM-style stream error flag or expose that flag through STREAM, because neither the stream state API nor STREAM exists. All byte-stream statuses are routed through the existing SYNTAX condition with the generic description “Stream I/O error”. A short read returns the supplied bytes, but the EOF-after-read metadata is not retained for a later query. A subsequent zero-byte EOF becomes an explicit stream error.

The VM never blocks or polls. A provider that reports unavailable or pending input returns immediately with a distinct status; the interpreter reports the generic stream I/O error. This avoids hangs but does not reproduce the z/OS host's transient-stream waiting rules.

## Security and future work

The resolver is the sole authority for names. It receives byte lengths and can recognize only a bounded set of registered identities. The VM never maps a name to a host path or device and does not add an operating-system file call. The current kernel leaves the provider unset. A native successful read requires a separately approved bounded provider or guest fixture; RX33 does not add an unsafe production provider to satisfy qualification.

RX34 should close the actual Partial CHARIN gap first: define per-stream EOF/NOTREADY error state and a small condition observation path that fits the interpreter's condition model, then qualify it against a bounded native fixture if one is approved. It should keep arbitrary filesystem access and the other stream built-ins out of scope until there is a reviewed provider and lifetime model.
