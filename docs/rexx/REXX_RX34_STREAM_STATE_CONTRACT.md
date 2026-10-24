# REXX RX34 — Bounded Stream State Contract

## Selected profile and scope

RX34 uses IBM z/OS 2.5 byte-oriented `CHARIN` as the read-semantics reference and adapts it to guideXOS’s registered, synchronous embedded-stream provider. IBM describes a character as one byte in X'00'–X'FF'. If fewer bytes than requested remain, CHARIN returns the available bytes and marks a stream error. A zero-length request reads no bytes and an explicit start still sets the position. See [IBM CHARIN](https://www.ibm.com/docs/en/zos/2.5.0?topic=functions-charin) and [IBM stream I/O functions](https://www.ibm.com/docs/en/zos/2.5.0?topic=implementation-stream-io-functions).

The z/OS 2.5 [STREAM (Operations)](https://www.ibm.com/docs/en/zos/2.5.0?topic=functions-stream-operations) interface uses a command operation and commands such as OPEN, CLOSE, and QUERY. RX34 implements only a guideXOS read-only status subset, `STREAM([name][,S|D])`; it does not claim z/OS STREAM command compatibility. The subset does not open, close, write, query files, or interpret names as paths. RX34 does not implement `SIGNAL ON NOTREADY`.

## State ownership and bounds

The fixed-size 16-entry status table belongs to one `RexxVM`. Entries are keyed by a copied provider-issued stable identity of at most 128 bytes, the VM provider generation, and a nonzero provider identity generation. Replacing or clearing the provider clears the table and advances provider generation. A changed identity generation cannot observe a stale entry for a reused identity. VM initialization and teardown initialize and clear the table; the embedding retains ownership of callback code, user data, cursors, and stream resources.

The provider remains responsible for stream identity and cursor movement. REXX names are opaque provider lookup keys. The VM stores status and the last operation’s bounded counts; it stores no stream contents, paths, file descriptors, or provider pointers. If a new stream needs a table slot and all 16 are occupied, CHARIN returns `RESOURCE_LIMIT` before any seek or read callback. No entry is evicted. STREAM observation does not consume table capacity.

## State labels

| STREAM result | Stored condition |
|---|---|
| `UNKNOWN` | The provider reports no such name, or there is no status record for a stable identity query. |
| `UNRESOLVED` | The provider resolved a valid key but no read or explicit seek has established a later condition. |
| `READY` | The latest relevant operation succeeded; the provider did not report EOF after the last full read. |
| `EXHAUSTED` | The latest full-length read succeeded and the provider reported that it ended exactly at EOF. |
| `SHORT_READ` | A positive read returned fewer bytes than requested; returned bytes are still delivered. |
| `EOF` | A read after exhaustion returned no bytes through the provider EOF result. |
| `NOTREADY` | The provider reported temporary unavailability. This is a status label, not REXX condition delivery. |
| `ERROR` | A provider read/resolution error or another error without a narrower label occurred. |
| `UNSUPPORTED` | Explicit positioning was requested for a nonseekable provider stream. |
| `INVALID` | Provider metadata was malformed, or an observation operation was invalid. |
| `UNAVAILABLE` | No provider is installed or the provider reports the stream unavailable. |

`STREAM(name)` and `STREAM(name,'S')` return the label. `S` is the default. `STREAM(name,'D')` returns the label plus `requested=N returned=N eof=0|1` only when the most recent operation is a positive READ. A zero-length read, seek, or resolver observation does not show stale read counts. The operation letter is accepted case-insensitively. Other operation values and more than two arguments fail as invalid function calls.

Omitting the name selects the provider default. An explicit empty name remains a named lookup. The name passed to the provider is length-bearing; it is never NUL-terminated or treated as a path. Names longer than the 128-byte resolver bound fail with `RESOURCE_LIMIT`.

## Transition matrix

| Event | CHARIN behavior | Recorded result |
|---|---|---|
| First resolution followed by a complete positive read | Returns requested bytes | `READY`, or `EXHAUSTED` if EOF-after-read is true |
| Complete read that ends exactly at EOF | Returns requested bytes successfully | `EXHAUSTED`; this is not a short-read error |
| Fewer bytes than requested | Returns all available bytes | `SHORT_READ` with exact requested/returned counts and EOF bit |
| Read attempted after exhaustion | Follows the existing distinct CHARIN EOF error path | `EOF`; replaces previous short-read/exhausted observation |
| Provider reports read error | Follows provider-error path | `ERROR`; retains the provider status code |
| Provider reports temporary unavailability | Follows NOTREADY-equivalent runtime error path | `NOTREADY`; no language-level NOTREADY trap is delivered |
| Unknown provider name | Fails through CHARIN’s unknown-stream status | No identity record; STREAM returns `UNKNOWN` |
| No provider | CHARIN remains fail-closed | STREAM returns `UNAVAILABLE` |
| Successful explicit seek | Does not read by itself | `READY`; clears earlier error |
| Explicit zero-length seek | Returns an empty string and positions the provider | `READY`; read callback is not called |
| Sequential zero-length call | Returns an empty string without a seek/read callback | Preserves state and error class while setting last operation to `ZERO_LENGTH_READ` and counts to zero |
| Invalid numeric start | Rejected before callbacks | Previous state and cursor unchanged |
| Invalid-position response | Provider seek is called and rejects the position | Previous state and cursor unchanged |
| Unsupported seek | Fails before the read callback | `UNSUPPORTED` |
| Malformed successful resolver metadata | CHARIN rejects the provider result | `INVALID` when the stable key is valid |
| Provider replacement or clear | Not applicable | Clears all old entries and advances provider generation |
| VM teardown | Not applicable | Clears all inline state; embedding still owns provider resources |
| Result allocation failure before callbacks | CHARIN fails | No provider callback, cursor movement, or state change |
| Result-copy refusal after provider read | CHARIN returns copy/allocation error | The completed read remains recorded; no cursor rollback is claimed |
| STREAM status observation | Returns a bounded state string | Calls resolve only; no read/seek and no status-table mutation |

A later successful positive read or valid explicit seek replaces a prior error with `READY` or `EXHAUSTED`. An attempted operation does not automatically clear an error: a failed or zero-length sequential operation retains the condition. A zero-length sequential read updates the last-operation metadata, so `D` reports only the sticky state label rather than counts from an earlier read.

## Observation API

The REXX `STREAM` subset resolves the caller’s name through the installed provider and validates its bounded identity. It can return current status for the key/generation or `UNRESOLVED` without reading, seeking, or inserting a table record. Unknown, unavailable, provider-error, and malformed metadata results remain distinct.

The host API `rexx_vm_query_byte_stream_status` remains a pure lookup by stable identity and does not invoke the provider. The additional `rexx_vm_resolve_byte_stream_status` performs a provider resolve callback and returns a caller-owned snapshot without a read/seek callback or table mutation.

## Qualification record

Hosted/property tests use seed `0x52583434`, 4,096 dedicated property cases, 16,387 property assertions, and 32 lifecycle cycles in addition to direct assertions. The inherited RX33 extension retains its separate deterministic model and assertion totals. Native build hashes, guest serial results, sanitizer totals, preservation reconciliation, and remaining qualification limits are recorded in `REXX_RX34_SCORECARD.json`.