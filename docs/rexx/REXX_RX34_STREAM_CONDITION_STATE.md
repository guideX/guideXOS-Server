# RX34 Stream Condition State

## Scope and profile

RX34 records bounded stream state for the RX33 synchronous byte-stream provider and CHARIN. The reference is IBM z/OS 2.5 byte-oriented CHARIN, adapted to provider-resolved identities. The VM does not open paths or retain stream bytes. A read-only guideXOS `STREAM([name][,S|D])` subset is provided; IBM z/OS 2.5 STREAM command operations, SIGNAL ON NOTREADY delivery, LINEIN/LINEOUT, CHAROUT, CHARS, and LINES remain unsupported.

## Owner and bounds

Each RexxVM owns an inline table of at most 16 records. An entry stores a copied stable identity of at most 128 bytes, a VM provider generation, a provider-issued identity generation, current state, last operation and immediate VM status, error class, requested/returned counts, and the provider’s EOF-after-read bit. The table stores no callback addresses, opaque provider handles, user-data pointers, messages, or stream buffers.

Identity comparison uses exact length and bytes. Providers return a stable identity during resolve; the VM copies it before a later seek/read callback. A nonzero identity generation distinguishes reuse of a stable key. Replacing or clearing the provider clears every entry and advances the VM generation. Internal routines and INTERPRET share the table of their owning VM. Other VMs have separate status.

The provider owns its cursor and resources. The VM never closes or frees provider state. A new identity operation that needs a table slot fails with RESOURCE_LIMIT before the first seek or read callback when all slots are occupied; entries are never evicted. A changed identity generation replaces that identity’s old entry in place during a CHARIN operation. STREAM resolution can report the new generation as UNRESOLVED without mutating the table.

## State transitions

Stored states are UNKNOWN, RESOLVED, READY, and ERROR. The REXX STREAM labels are:

| STREAM label | Stored state and error class |
|---|---|
| UNKNOWN | No provider name matched |
| UNRESOLVED | RESOLVED with no later successful seek/read state |
| READY | Successful read/seek with no EOF-after-read bit |
| EXHAUSTED | Successful full read with EOF-after-read bit |
| SHORT_READ | ERROR/SHORT_READ; available bytes were returned |
| EOF | ERROR/END_OF_STREAM; distinct from provider failure |
| NOTREADY | ERROR/NOT_READY; not REXX condition delivery |
| ERROR | ERROR/PROVIDER or another class without a narrower label |
| UNSUPPORTED | ERROR/UNSUPPORTED_OPERATION |
| INVALID | ERROR/INVALID_METADATA or ERROR/INVALID_POSITION |
| UNAVAILABLE | No registered provider or provider reports unavailable |

| Event | Result and state update |
|---|---|
| STREAM query for unknown name | UNKNOWN; resolver only; no status-table insertion |
| STREAM query for resolved unseen key | UNRESOLVED; no table mutation |
| Full positive CHARIN read | Returns all requested bytes; READY or EXHAUSTED based on provider EOF bit |
| Short positive read | Returns available bytes; ERROR/SHORT_READ with exact request/return metadata |
| Read after exhaustion | Existing distinct CHARIN EOF status; ERROR/END_OF_STREAM |
| Provider read error | Existing provider error path; ERROR/PROVIDER |
| Provider unavailable response | Existing NOTREADY-equivalent runtime error; ERROR/NOT_READY |
| Unknown stream | CHARIN unknown-stream status; no identity record |
| No provider | CHARIN fails closed; STREAM returns UNAVAILABLE |
| Successful explicit seek | READY and clears earlier error; a following read can replace it |
| Explicit zero-length seek | Empty result, provider cursor repositioned, state READY; no read callback |
| Sequential zero-length call | Empty result, no seek/read; prior state/error stays sticky while last operation becomes ZERO_LENGTH_READ and counts become zero |
| Invalid numeric start | Reject before resolver/seek/read; preserve state and cursor |
| Provider rejects a position as invalid | Preserve previous state and cursor |
| Unsupported seek | ERROR/UNSUPPORTED_OPERATION; read is not called |
| Provider replacement/clear | Clear table and increment provider generation |
| VM teardown | Clear inline table; embedding owns provider lifetime |

`STREAM(name)` and `STREAM(name,'S')` return state labels. `D` adds `requested=N returned=N eof=0|1` only for a last positive READ. A sequential zero-length operation updates the last-operation metadata, so the sticky state remains visible but counts from an earlier read are not presented as the latest operation.

A later successful positive read or valid explicit seek replaces the earlier error with READY or EXHAUSTED. Merely attempting another operation does not clear the condition. Invalid arguments and a provider-rejected invalid position preserve the previous state.

## Failure boundary

The VM allocates the bounded result before resolving, seeking, or reading. An allocation failure cannot consume data or move the provider cursor. State entries use inline storage. Capacity exhaustion prevents cursor-changing callbacks. When a provider read completes and a subsequent VM-side result copy fails, CHARIN reports that failure but the recorded state and provider cursor reflect the completed provider read; cursor rollback is not claimed.

A NOT_READY error class is not a REXX NOTREADY condition trap. No SIGNAL ON NOTREADY language behavior is added.