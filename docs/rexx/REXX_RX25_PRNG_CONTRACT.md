# RX25 PRNG Contract

This document defines the deterministic pseudorandom sequence used by guideXOS `RANDOM` in RX25. It is an implementation contract, not a promise of sequence compatibility with IBM or another REXX runtime.

## Generator

The generator is PCG-XSH-RR 64/32. Its complete mutable state is one `uint64_t` per `RexxVM`:

```text
state = state * 6364136223846793005 + 1442695040888963407  (mod 2^64)
```

For each output, let `old` be the state before the recurrence. Compute:

```text
xorshifted = uint32_t(((old >> 18) XOR old) >> 27)
rotation   = uint32_t(old >> 59)
output     = rotate_right_32(xorshifted, rotation)
```

The fixed increment is odd and the multiplier is 1 modulo 4, so the linear-congruential state transition has period `2^64`. All arithmetic is unsigned; wraparound is defined by C and there is no signed-overflow dependency, floating point, allocation, `rand()`, hosted entropy, or platform-specific operation in generator advancement.

## Seeding and lifetime

Seed initialization is the standard two-step PCG procedure with the RX25 fixed stream:

```text
state = 0
advance once
state += seed
advance once
```

The explicit seed is the inclusive integer range 0 through 999,999,999. Zero follows the same procedure as every other seed and is not a sentinel. Calling `RANDOM` with a seed always resets the active VM sequence, even if the requested result range is a single value.

On the first unseeded call, RX25 uses seed zero. No general seed provider is wired into the REXX runtime: the existing Virtio RNG path is part of TLS rather than a general interpreter service, and the wall-clock interpretation remains unresolved. This fallback is repeatable and makes no unpredictability claim.

The state is VM-owned and program-level. It survives internal routine calls, routine returns, and condition unwinding. It is not copied into or restored from routine frames. `rexx_vm_execute` resets the state and initialization flag when a new program execution begins. Distinct VMs do not share state. Calls made through the lower-level built-in dispatcher without a new `rexx_vm_execute` share that VM's state until the VM is freed or another program execution begins.

## Inclusive bounded mapping

After exact decimal validation, RX25 computes `difference = max - min` and requires `0 <= difference <= 100000`. The number of possible results is `bound = difference + 1`, so `1 <= bound <= 100001`. If the bounds are equal, the bound is returned directly and no range word is drawn.

For a nonconstant range, the mapper computes:

```text
threshold = 2^32 mod bound
repeat:
    candidate = next_u32()
until candidate >= threshold
offset = candidate mod bound
result = min + offset
```

The accepted candidate domain has `2^32 - threshold` elements, which is divisible by `bound`. Every offset therefore has the same number of source words if the 32-bit source word is uniform; this removes the avoidable modulo bias from direct `% bound` mapping. The deterministic PCG stream is not thereby claimed to be mathematically independent or cryptographically uniform.

The rejection loop has a finite termination proof for every supported nonconstant bound. Since `bound <= 100001`, `threshold <= 100000`. The output transform produces `0xffffffff` with rotation zero from old state `0x07fffe0000000000`, so at least one accepted word exists for every supported bound. The state cycle visits all `2^64` states, giving a worst-case bound of at most `2^64` output draws. The bound is deliberately a correctness proof, not a practical runtime estimate.

## Decimal bounds

The interval endpoints remain `RexxDecimal` values. RX25 computes their difference from virtual base-10 integer digits, rejecting high nonzero digits and widths above 100,000 before narrowing anything. It adds the sampled `uint32_t` offset with decimal carry and keeps trailing zeroes as a negative scale. Thus endpoint magnitude can exceed 64 bits without a floating-point conversion or a native-width cast. The existing decimal parser, VM numeric-work budget, and result-output limits remain the upper bound on accepted inputs.

## State transition and errors

The function validates all supplied values and the range before it copies the VM state locally. Reseeding and sampling operate on that temporary state. Exact decimal addition, formatting under the VM's numeric settings, and result allocation must all succeed before the VM's state and initialized flag are committed. A failed call consumes no random word and does not apply a tentative seed. A successfully evaluated `RANDOM` call commits even if a later, separate output operation fails.

## Reproducibility and security

Explicit seeds and identical call sequences produce identical outputs within this guideXOS implementation version. Sequence constants and mapping are recorded in source and qualification evidence; a future algorithm change must be versioned and tested as a compatibility change.

The fixed seed-zero fallback means separate unseeded program executions can repeat. `RANDOM` is not suitable for passwords, keys, security tokens, authentication challenges, or any security-sensitive purpose. Distribution smoke tests detect obvious implementation defects only; they do not establish independence, security, or IBM sequence identity.
