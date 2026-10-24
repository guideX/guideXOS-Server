# RX34 Stream Status API

## Public APIs

`rexx_vm_query_byte_stream_status` is a pure lookup by stable provider identity. It never calls the provider, allocates, changes the cursor, or changes the status table. Unknown identities return `OK` with `valid == 0` and state `UNKNOWN`.

`rexx_vm_resolve_byte_stream_status` is the name-facing observation API used by the REXX STREAM subset. It calls the installed provider’s resolver once, validates the returned stable identity/generation/capabilities, then copies the matching per-VM status if one exists. If the key is valid but unseen, it returns a caller-owned `RESOLVED` snapshot without inserting a table entry. It never invokes read or seek and never mutates status. Unknown, unavailable, provider-error, and malformed resolver responses are represented in the output snapshot.

Both APIs return fixed-size caller-owned snapshots and perform no status-table allocation. The snapshot includes validity, state, last operation/status, error class, provider and identity generations, bounded diagnostic code, copied stable identity, requested and returned byte counts, and EOF-after-read metadata. No borrowed provider pointer escapes.

## Validation and ownership

The per-VM status table contains at most 16 entries; stable identities are at most 128 bytes and require a nonzero provider identity generation. The provider owns callback code, user data, opaque handles, cursors, and stream resource lifetime. Provider replacement or clear drops all table entries and advances the VM provider generation. VM teardown clears inline state but does not close provider resources.

Name resolution accepts an omitted name as the provider default and preserves explicit empty names as named lookups. Names over the 128-byte resolver bound return `RESOURCE_LIMIT`. The resolver callback is protected by the VM’s existing reentrancy guard.

## REXX observation subset

`STREAM([name][,S|D])` returns the bounded label for unknown, unresolved, ready, exhausted, short-read, EOF, not-ready, error, unsupported, invalid, and unavailable states. `S` is the default. `D` includes last positive-read counts and EOF-after-read metadata only while the last operation remains a positive READ. Observation never reads, seeks, opens, closes, writes, queries files, resets a condition, or consumes a status-table slot.

This is a guideXOS read-only subset. IBM z/OS 2.5 STREAM uses Command/C stream commands; RX34 does not implement that interface. No `SIGNAL ON NOTREADY` language behavior is implemented.