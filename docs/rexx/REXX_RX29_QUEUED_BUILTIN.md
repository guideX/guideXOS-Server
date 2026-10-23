# RX29 — QUEUED Built-in

## Outcome and scope

**Outcome C — Blocked at the architecture decision gate.** The accepted starting point is branch `REXX_SUPPORT`, HEAD `cdf037707fbc5b9fe4755fff0b94337fb740cb7f`. No REXX source was changed. The runtime has no external data queue for `QUEUED()` to inspect, and its existing `QUEUE`, `PUSH`, and `PULL` tokens are not executable instructions. A truthful implementation therefore depends on a real data-stack subsystem and cannot be delivered as a built-in-only change.

RX29 does not count instruction history or the VM expression stack. Either would return a number unrelated to data elements actually waiting in an external queue. The decision follows the phase gate: defer rather than add the broader stack service solely to force this built-in.

## Requested contract

The RX29 target is `QUEUED()` with exactly zero arguments. It should return a normal whole-number string containing the current number of data lines in the active external queue. It must not consume or change any line; explicit empty strings count as one element. A supported IBM TSO/E stack-level implementation must exclude buffer markers from its count. The implementation and these semantics remain **not implemented and not qualified** in this phase.

The relevant references are the [IBM QUEUED function](https://www.ibm.com/docs/en/zos/2.5.0?topic=functions-queued), [IBM QUEUE instruction](https://www.ibm.com/docs/en/zos/2.5.0?topic=instructions-queue), and [IBM functions supported by the data stack](https://www.ibm.com/docs/en/zos/2.5.0?topic=routine-functions-supported-data-stack).

## Qualification status

The architecture audit is recorded in `REXX_RX29_DATA_STACK_AUDIT.md`. Because the required queue foundation is absent, RX29 direct REXX assertions, model/property operations, allocation-failure tests, inherited suites, sanitizers, freestanding linking, and guest scripts were not run. The 196-case local corpus was available historically but was not rerun. Regina was not found on PATH, so external compatibility is `NOT_RUN`. QAC was checked and was available with no active or queued reservation, but no reservation was requested and no guest was launched.

The exact preflight, untracked-path manifest, protected root object hashes, and QAC snapshot are under `docs/rexx/evidence/rx29/`. The machine-readable results are in `REXX_RX29_SCORECARD.json`.

## Inventory and next phase

The 75-entry inventory retains RX28's counts: 58 Implemented, 12 Not implemented, 3 Partial, 1 Not assessed, and 1 Not implemented by design. `QUEUED` remains Not implemented; DATE, TIME, and TRACE retain their prior classifications.

RX30 should first establish a bounded, length-bearing external data-stack model and executable `QUEUE`, `PUSH`, `PULL`, and `PARSE PULL` operations, with explicit ownership and lifecycle rules across the existing VM, routine, `INTERPRET`, host, and program boundaries. Only then can `QUEUED()` count the active queue without inventing state. Buffer-stack instructions should remain out of scope unless separately implemented and qualified.

## Revalidation on 2026-10-23

The runtime decision remains blocked. The current run started from `2ea2b54e99c30986ba13839f942d29d93a944801`, the documentation-only child of the accepted RX28 HEAD, and left REXX production code unchanged. The inherited RX1–RX28 hosted suite, both 28-target sanitizer matrices, 196 local expected-result cases under each sanitizer, the 123-check RX20 clock/PIT contract, and the freestanding AMD64 link passed. These results do not qualify QUEUED semantics: there is still no data-stack instruction or active queue to count.

The QAC guest gate remained incomplete. No guest was launched because the required operations are absent. A separate verified acceptance reservation was active at one status check; QAC later reported zero sessions, reservations, queued requests, and QEMU processes. RX29 did not request a reservation, alter foreign media, or control that guest. See `docs/rexx/evidence/rx29/` for run-specific logs, kernel hash, QAC snapshots, protected-artifact checks, and path reconciliation.
