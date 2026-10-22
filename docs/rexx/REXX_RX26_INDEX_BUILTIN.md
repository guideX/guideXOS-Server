# RX26: IBM INDEX built-in compatibility

## Status and portability

RX26 implements `INDEX(haystack,needle[,start])` as an IBM TSO/E and VM
**non-SAA** REXX extension. IBM documents `POS(needle,haystack[,start])` as the
portable preferred function. The first two arguments are reversed; the search
result and byte-search behavior correspond. See the
[IBM INDEX reference](https://www.ibm.com/docs/en/zos/2.5.0?topic=functions-index).

This does not add other TSO/E functions or claim that INDEX is part of the
portable SAA built-in set.

## Implementation

`rexx_builtin_index` passes its haystack and needle to the existing POS search
core in reversed order. POS continues to pass its arguments in its original
order. Both paths share numeric start validation, bounds checks, empty-needle
handling, one-based result calculation, and the same `memcmp` search loop.
Dispatch does not recurse through the interpreter's public function interface,
and no second search algorithm was added.

The core parses the optional start as a REXX decimal and accepts only a positive
whole number. It validates the value before calculating an offset. An omitted
start is 1; a valid start beyond the haystack returns 0. Zero, negative,
fractional, nonnumeric, and unsupported calls follow the interpreter's existing
built-in error path. Missing required and extra arguments are rejected by the
normal descriptor validation. Rejected calls leave interpreter state available
for later calls.

Search is byte-oriented and case-sensitive. It compares the existing string
bytes exactly, so embedded NUL, high-bit, and nonprintable bytes are ordinary
search data. No character folding, locale conversion, Unicode, or DBCS handling
was added. Inputs remain borrowed and unchanged. Result construction uses the
existing numeric-result helper; the search adds no temporary result buffer.
All strings remain subject to the runtime's 16 MiB limit.

### POS large-start correction

Before RX26, POS converted its optional start through a 32-bit unsigned helper
and returned a resource-limit error for valid positive values above
`UINT32_MAX`. IBM INDEX requires an out-of-range positive start to produce 0,
and every supported haystack is shorter than `UINT32_MAX`. The shared core now
returns 0 for such valid positive starts without narrowing them. This changes
the former POS behavior as well as implementing INDEX. RX26 includes explicit
POS and INDEX regressions for values above `UINT32_MAX`; invalid and fractional
starts retain the error behavior.

## Qualification

The IBM reference examples and additional documented cases are in
`tests/rexx_rx26/index_reference.rex` with expected output in
`tests/rexx_rx26/index_reference.out`. The direct C suite is
`tests/rexx_rx26_index_test.c`.

The direct suite passed **48,166 assertions**, including 12,000 deterministic
generated binary cases with seed `0x52583236`. Each generated case compared
INDEX with POS and also compared the result with a separate byte-by-byte oracle;
the oracle therefore does not rely only on both public functions sharing one
helper. Explicit vectors cover the IBM examples, overlapping and repeated
matches, empty strings, NUL and high-bit bytes, all byte values 00–FF, starts at
boundaries and beyond the string, numeric spellings, NUMERIC DIGITS 2 and 18,
inputs at the 16 MiB limit, and input immutability. Allocation-failure and
subsequent-call recovery checks passed.

RX26 reran the inherited hosted suite and CLI fixtures, including the POS
regressions, RX19 calendar and clock checks, RX20 clock contract, RX24
ADDRESS/TRACE checks, and RX25 RANDOM checks. The RX26 IBM CLI fixture passed
1/1. Regina was unavailable, so independent Regina comparison is
**NOT_RUN**; no external-interpreter compatibility percentage is claimed.

ASan passed 26/26 build/test targets and UBSan passed 26/26. Each matrix also
ran the 196-case local expected-result corpus; Regina's side of that corpus was
not run. Both matrices had zero failed targets and zero nonempty test stderr
logs. The freestanding AMD64 build and link passed with all nine REXX C object
files and the repository's restricted kernel flags. The RX26 kernel ELF SHA-256
is `ce32736f6a0c0e31bd3eb348582baeaa4eab450251bdf5b14fd2464a42ec6678`.

Exclusive QAC guest acceptance passed with networking disabled and disposable
media. The guest ran INDEX success/not-found and binary/POS checks, invalid
start recovery, inherited RX24 ADDRESS/TRACE checks, RX25 RANDOM checks, the
DATE/TIME fail-closed and PIT guard, and the RX20 clock contract. The kernel
heartbeat advanced from 1 to 158 records. QMP acknowledged shutdown; QAC
reported child exit 0 and parent CLI exit 0, released the reservation, and the
runner confirmed the owned QEMU process was absent. A preflight attempt while a
different managed reservation was active was refused without launching RX26 or
controlling that process. A later guest attempt exposed a missing long
DATE/TIME filename in its VFS; it shut down safely and released QAC, then passed
after staging the unchanged guard under a shorter RX26-only name. Each attempt
and its evidence are retained under `evidence/rx26/native-guest/`.

Qualification details and the exact paths are recorded in
`REXX_RX26_SCORECARD.json` and the `evidence/rx26/` directory.

## Remaining limits

INDEX is intentionally a non-SAA compatibility extension. POS remains the
portable choice. Matching is over the runtime's byte representation, not
language-aware character positions; this phase makes no Unicode, locale, or
DBCS claim. Regina was unavailable. The accepted inventory and remaining
unimplemented built-ins are summarized in
`REXX_RX26_BUILTIN_INVENTORY.csv`.
