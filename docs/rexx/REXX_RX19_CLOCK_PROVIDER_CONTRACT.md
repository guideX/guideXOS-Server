# REXX RX19 clock provider contract

## Scope

The interface is private to the native REXX VM and its embedding shell. It intentionally does not change a public kernel ABI. A provider callback returns a complete bounded reading for one synchronous VM execution; it must not return plausible-looking substitute values on failure.

## Data contract

`RexxVMClockReading` in `rexx_vm.h` contains:

| Field | Contract |
| --- | --- |
| `capabilities` | `REXX_CLOCK_CAP_WALL_CIVIL` and/or `REXX_CLOCK_CAP_MONOTONIC`; unknown bits invalidate the reading. |
| `wall_interpretation` | `UNSPECIFIED`, `LOCAL_CIVIL`, or `UTC_FIXED_OFFSET`. Current wall output requires a supported explicit interpretation. |
| `utc_offset_seconds` | Signed offset applied only to `UTC_FIXED_OFFSET`, strictly within one day of UTC. It is a fixed offset for this reading and carries no daylight-saving transition rules. |
| `wall_resolution_nanoseconds` | Required for wall readings; 1 through 1,000,000,000 ns. Fractional output is quantized down to this precision before microsecond formatting. |
| Calendar fields | Gregorian year 0001–9999, valid month/day, hour 0–23, minute/second 0–59, and fraction 0–999,999,999 ns. Leap seconds are not represented. |
| `monotonic_nanoseconds` | Nondecreasing elapsed timestamp from an unspecified origin. No calendar interpretation is attached. |
| `monotonic_resolution_nanoseconds` | Required and nonzero when the monotonic capability is present. |

`REXX_VM_CLOCK_READ_OK` is accepted only after field validation. `UNAVAILABLE`, `INVALID`, a missing callback, or invalid fields map to `REXX_VM_STATUS_CLOCK_UNAVAILABLE`. A failed reading is cached as failed for its clause, so repeated calls cannot turn a failed clock into stale or inconsistent output. A later clause can retry.

## Lifetime and isolation

Install the callback with `rexx_vm_set_clock_reader(vm, reader, user_data)`. The VM borrows both pointers until the synchronous VM execution returns. Provider state, clause snapshots, and elapsed timer state are per VM; separate interpreter instances cannot share mutable timer state through the runtime.

The first clock-dependent operation in a clause invokes the callback. Further DATE/TIME calls in that clause use the same wall and monotonic reading. A subsequent clause takes a fresh reading. Compiler-emitted `clause_id` values distinguish AST clauses within one bytecode chunk; dynamic INTERPRET chunks are distinguished by their chunk address. A same-depth backward control transfer invalidates the current clause cache before the next loop pass.

Internal routine frames copy the full clock context. The callee inherits the caller's elapsed timer start, but a callee reset or snapshot does not overwrite the caller's saved state. Returning from INTERPRET restores the parent snapshot while carrying forward the elapsed timer updates produced during dynamic execution.

## Production adapters

The shell passes an invocation-local adapter through `guide_rexx_main_with_services`. On bare-metal AMD64, the adapter reports:

- RTC fields as wall-capable but `WALL_UNSPECIFIED`, at one-second resolution. This supports an honest capability report but does not satisfy local DATE/TIME operations.
- PIT nanoseconds as monotonic-capable after the first PIT heartbeat, with resolution computed from the actual PIT divisor.

On the hosted RX2 shell bridge the callback returns unavailable. The ordinary CLI installs no provider. No freestanding path calls `time()`, `localtime()`, C++ chrono, or host OS time APIs.

## Error contract

Clock absence, invalid provider data, and missing wall or monotonic capabilities produce an explicit status and diagnostic. In VM script execution, this status participates in the existing SYNTAX condition path as code 40. Pure date conversions do not request the clock unless their selected option requires a current date or a rolling two-digit-year window.

## Limits

- The wall contract currently cannot advertise a trusted production local timezone or UTC offset.
- The PIT timestamp is approximate and tick-quantized; it does not gain precision from the nanosecond storage unit.
- A fixed UTC offset cannot describe daylight-saving transitions or future/past timezone rules.
- `TIME` input-conversion parameters, leap seconds, and timezone database conversion are outside RX19.
