# RX25: Classic `RANDOM` Built-in

RX25 implements the classic REXX call form `RANDOM([min][,[max][,seed]])`, following the positional and range rules in the [IBM z/OS REXX `RANDOM` reference](https://www.ibm.com/docs/en/zos/2.5.0?topic=functions-random). IBM does not require different REXX implementations to produce the same sequence, so guideXOS defines and versions its own deterministic sequence.

## Arguments and results

The result is an inclusive whole number. With no arguments, the range is 0 through 999. A single supplied argument is the maximum, with a minimum of 0. With two or more positions, arguments mean minimum, maximum, and seed. An omitted position keeps its default:

| Call | Meaning |
| --- | --- |
| `RANDOM()` | 0 through 999 |
| `RANDOM(10)` | 0 through 10 |
| `RANDOM(10,)` | 10 through 999 |
| `RANDOM(,10)` | 0 through 10 |
| `RANDOM(5,8)` | 5 through 8 |
| `RANDOM(,,12345)` | Reset the sequence to seed 12345; use 0 through 999 |
| `RANDOM(1,6,12345)` | Reset the sequence to seed 12345; use 1 through 6 |

The VM dispatch interface retains omitted arguments as `REXX_VALUE_UNSET`. An explicitly supplied empty string remains supplied and is rejected as a malformed number.

Minimum and maximum must be nonnegative whole decimal values. Minimum cannot exceed maximum, and `maximum - minimum` cannot exceed 100,000. A difference of exactly 100,000 is valid, giving 100,001 possible results. The optional seed must be a whole decimal from 0 through 999,999,999. Invalid argument counts, malformed or fractional numbers, negative values, reversed bounds, and out-of-range seeds fail with the existing built-in error statuses.

Bounds are parsed with the runtime's exact decimal representation; they are not converted through `double` or narrowed to a machine word. RX25 tests endpoints above `UINT64_MAX`. The supported endpoint size follows the existing decimal parser and VM work/output budgets (512 input bytes, up to 512 coefficient digits, and the parser's exponent limit). The interval width is computed from decimal digits, and the selected offset is added to the minimum exactly.

Values are formatted through the existing numeric result path, so `NUMERIC DIGITS` and `NUMERIC FORM` continue to control presentation. Whole-number validation is performed before any PRNG state change. Failed validation, range arithmetic, formatting, or result allocation leaves the sequence unchanged and returns no partial result. A rejected `RANDOM` call consumes no random value. When a valid call has equal bounds, it returns that bound without drawing a range word. An unseeded equal-bound call still establishes the initial sequence; a seeded equal-bound call still applies the seed.

## Sequence and state

The implementation uses PCG-XSH-RR 64/32 with an 8-byte unsigned state, multiplier `6364136223846793005`, and fixed odd stream increment `1442695040888963407`. The exact recurrence, output transform, range mapping, and termination argument are in [the RX25 PRNG contract](REXX_RX25_PRNG_CONTRACT.md).

An explicit seed resets the active VM's sequence, including seed zero. State belongs to the VM for the current program execution: it advances across calls, survives internal routine return and error unwinding, and is not stored in routine frames. `rexx_vm_execute` starts a new program execution with an uninitialized RANDOM state. Separate VMs have separate state.

The first unseeded call uses a documented deterministic fallback equivalent to seed zero. No suitable general-purpose guideXOS seed service was available; the existing Virtio RNG is scoped to TLS and is not a general REXX seed provider. Consequently, independent program executions may produce identical sequences. This behavior avoids unresolved wall-clock interpretation and makes freestanding output reproducible.

## Qualification

The RX25 direct suite checks positional omissions, defaults, inclusive boundaries, large decimal endpoints, seed limits, malformed inputs, result-allocation failure, error-state preservation, VM isolation, 1,000-call reference sequences, reseeding, routine and unwind state, program reset, and a fixed-seed distribution smoke test. Its 60,000 values in range 0 through 5 were `9855, 10003, 9985, 9975, 9992, 10190`; each category passed the stated 9,500–10,500 tolerance around 10,000.

The hosted, sanitizer, freestanding build, and QAC guest evidence is summarized in [the RX25 scorecard](REXX_RX25_SCORECARD.json). The guest showed the seed-zero fallback value `422` on two independent program executions, checked seeded range and reseed behavior, caught invalid seed and range calls, then completed another valid call. It also exercised the existing fail-closed current DATE/TIME behavior and PIT monotonic checks.

## Compatibility and security

The algorithm and fixed stream define guideXOS's sequence for this implementation version; no claim of bit-for-bit IBM sequence compatibility is made. Regina was unavailable during RX25, so no external differential percentage is reported.

**`RANDOM` is not cryptographic randomness.** Do not use it for passwords, security tokens, keys, authentication challenges, or other security-sensitive values. The deterministic seed-zero fallback provides no unpredictability.
