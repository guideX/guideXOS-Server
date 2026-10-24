# REXX RX34 — CHARIN Compatibility and Limits

## Reference behavior

The selected reference is IBM z/OS 2.5 byte-oriented REXX stream input, not an aggregate of z/VM, TSO/E, and z/OS UNIX behavior. IBM describes CHARIN as returning up to the requested byte length, advancing by the number actually returned, and returning available bytes while marking an error when fewer than requested remain. A zero length returns an empty string without reading; an explicit start still sets the read position. IBM’s stream I/O profile treats each “character” as one byte from X'00' through X'FF'. See [CHARIN](https://www.ibm.com/docs/en/zos/2.5.0?topic=functions-charin) and [The Stream I/O Functions](https://www.ibm.com/docs/en/zos/2.5.0?topic=implementation-stream-io-functions).

RX34 keeps RX33’s call shape, `CHARIN([name][,[start][,length]])`, with a synchronous registered provider. Omitted name selects the provider default; an explicit empty name is a named lookup. Omitted start uses the provider cursor. Omitted length reads one byte. Explicit starts are one-based. The form `CHARIN('sample',5)` reads one byte at position five. A start with length zero seeks without reading when the provider supports seeking. NUL and high-bit bytes are preserved in the returned length-bearing string. The existing maximum result length remains 16 MiB.

## GuideXOS stateful behavior

RX34 retains one bounded status record per stable provider identity in each VM. Full reads set READY, or EXHAUSTED when the provider says that the returned final byte reached EOF. A short read returns every byte supplied by the provider and stores SHORT_READ with requested and returned counts. A later zero-byte EOF response stores EOF separately from a provider I/O error. Provider errors, temporary unavailability, unsupported seeking, and invalid metadata have distinct internal error classes and observable STREAM labels.

An explicit successful seek is a recovery event and sets READY; a subsequent positive read can replace it with READY, EXHAUSTED, or SHORT_READ. A sequential zero-length call returns empty and does not invoke read or seek. It preserves the sticky state/error while updating last-operation metadata to ZERO_LENGTH_READ and counts to zero. Invalid numeric positions are rejected before provider callbacks. A provider-rejected invalid position preserves the prior state/cursor. Result allocation occurs before callbacks; if a VM-side result copy fails after the provider has advanced, the state records the completed read and the cursor is not rolled back.

## Observation subset

RX34 exposes `STREAM([name][,S|D])` as an explicitly limited guideXOS adaptation. S (default) returns a state label. D adds byte counts and the EOF bit for the last positive read. It calls the provider’s resolver but never reads, seeks, opens, closes, writes, or discovers files. State observation does not clear the stored state. Unknown names return UNKNOWN; no provider returns UNAVAILABLE.

IBM z/OS 2.5 STREAM uses the Command/C operation and stream commands such as OPEN, CLOSE, and QUERY EXISTS/REFDATE. RX34’s S/D status form does not implement those commands and is not a complete IBM STREAM implementation. IBM z/OS 3.1 also documents S/D labels and resets an error after STREAM returns it; RX34 deliberately retains its state on observation. Do not project that newer profile’s behavior onto the selected z/OS 2.5 reference. No REXX `SIGNAL ON NOTREADY` condition delivery is implemented.

## Remaining compatibility gaps

- No production byte-stream provider is installed; ordinary production remains fail-closed.
- The provider is synchronous; a pending response is unsupported and does not wait.
- No OPEN/CLOSE, output, line-oriented I/O, CHARS/LINES, filesystem, or network stream operations exist.
- Provider identity and status are bounded to 128 identity bytes and 16 entries per VM.
- No IBM stream-error reset command, transient/persistent stream model, or NOTREADY condition trap is implemented.
- Regina comparison is `NOT_RUN` when the executable is unavailable; no external compatibility score is inferred.
- Native fixture qualification proves only the explicitly installed in-memory test provider, not production input integration or general file access.

## RX34 native qualification

The production guest was separately built without the fixture provider and
showed `STREAM() = UNAVAILABLE`, a fail-closed `CHARIN()` syntax trap, and
`UNAVAILABLE` after the error. The fixture guest exercised default/named reads,
`CHARIN('rx34sample',5)`, cursor continuation, zero-length positioning, a
successful exact-final-byte read (`EXHAUSTED`), binary NUL/high-bit bytes,
short-read bytes/status, and the subsequent EOF observation. Provider error,
unknown/missing names, unsupported seek, invalid position/argument, the
oversized-request resource-limit boundary, and shell recovery were also
observed in the real guest. QAC reservation/shutdown and per-command screenshot
evidence are detailed in
[`REXX_RX34_NATIVE_STREAM_FIXTURE.md`](REXX_RX34_NATIVE_STREAM_FIXTURE.md) and
[`REXX_RX34_SCORECARD.json`](REXX_RX34_SCORECARD.json).
