# RX27 — IBM JUSTIFY Built-in

## Scope and status

RX27 adds `JUSTIFY(string,length[,pad])` as an IBM TSO/E and VM non-SAA built-in. Its implementation uses the existing bounded `SPACE` word scanner and the existing result-string allocator. It does not add a tokenizer or a general formatting subsystem.

The source implementation, direct tests, hosted regressions, sanitizers, and freestanding AMD64 link passed. Overall RX27 remains **Partial** because exclusive QAC/QEMU guest acceptance could not run while a foreign managed QAC reservation was active. Regina was also unavailable, so no external-interpreter comparison or compatibility percentage is claimed.

## Contract and implementation

The [IBM z/OS REXX reference](https://www.ibm.com/docs/en/zos/2.5.0?topic=functions-justify) and [IBM z/VM 7.4 REXX reference](https://www.vm.ibm.com/library/740pdfs/74631400.pdf) describe JUSTIFY as a non-SAA function that first applies `SPACE` normalization, truncates on the right when needed, drops a trailing blank left by truncation, then distributes added pad characters evenly from left to right between the remaining blank-delimited words. The four IBM examples in the reference are exercised exactly by the CLI fixture.

The implementation makes three bounded scans over the source view. It counts words and content bytes, determines the normalized prefix that survives truncation, then writes the result into one appropriately sized result allocation. It reuses `rexx_builtin_next_word`, so its byte-oriented blank class is the same as `SPACE`: ASCII space and bytes tab through carriage return. Original word bytes, including NUL and high-bit bytes, are copied without conversion. The input is not modified.

For `g` surviving gaps, it divides the added width by `g`; each gap gets the quotient and the leftmost `remainder` gaps get one more byte. The selected one-byte pad replaces each normalized separator. An explicit NUL pad is valid because validation uses the supplied byte length, not C-string termination.

IBM describes padding between words. There are no such gaps for empty or single-word normalized input, so the result is not extended outside the words: empty/all-blank input stays empty, a single word is returned unchanged if it fits, and is right-truncated if it does not. Width zero returns the empty prefix. The reference's examples do not list these three edge cases separately; these results follow the documented between-word rule and truncation rule, and are covered locally. The [Classic Rexx Functions Reference](https://rexxinfo.org/reference/articles/classic_rexx_functions.html) is an additional non-IBM example source for gap padding and right truncation. No Regina execution was available in this phase.

The required length uses the existing nonnegative whole-number helper. Existing numeric parsing/resource statuses are preserved; values beyond the helper's representable range and outputs beyond the 16 MiB runtime string cap fail before result allocation. Empty and one-word values do not divide by a gap count. A normal result needs one result allocation and no intermediate normalized string.

## Examples and boundaries

The CLI fixture checks the four IBM vectors, all additional RX27 examples, a binary NUL pad (`6100000062` under `C2X`), empty and one-word behavior, zero width, and truncation at a separator. The direct C suite additionally checks:

- missing, extra, fractional, negative, nonnumeric, oversized, and valid integral numeric forms;
- omitted, empty, multibyte, high-bit, explicit-space, and NUL pads;
- repeated blanks, leading/trailing blanks, tabs and classic blank bytes;
- truncation within a word, at or immediately after gaps, and at the normalized end;
- binary words with embedded NUL/high-bit bytes and unchanged source buffers;
- the 16 MiB output limit and injected output-allocation failure followed by successful recovery.

The deterministic property oracle materializes an independently normalized byte string, applies right-prefix/trailing-separator rules, counts surviving gaps, and independently distributes the remainder. Seed `0x52583237` produces 12,000 cases with random binary word bytes, blank-run widths, pad bytes, and truncation widths. Every case is checked against two independent VMs with `NUMERIC DIGITS` set to 2 and 34; both results must match the oracle and each other.

## Qualification

- Hosted direct test: 42,589 assertions; 12,000 property cases; zero failures.
- IBM JUSTIFY CLI fixture: 1/1 passed.
- Inherited RX1–RX26 hosted suites and compatibility fixtures: passed, including RX19 calendar/clock, RX24 ADDRESS/TRACE, RX25 RANDOM, and RX26 INDEX/POS coverage. Hosted CLI cases passed 24/24.
- AddressSanitizer: 27/27 build/test targets passed. UndefinedBehaviorSanitizer: 27/27 passed. Each also ran the local expected-result corpus (196/196); Regina comparisons were `NOT_RUN` because Regina was unavailable.
- Freestanding AMD64: nine REXX C objects compiled and linked. `kernel.elf` SHA-256 is `da340faf9ba3a5aa59a774477a28a20797c248ea6f3d205bbac2d2c95b058c4c`.
- Allocation recovery: all five VM allocation failpoints measured during interpreter execution were injected; after each `OUT_OF_MEMORY`, the same VM executed the JUSTIFY program successfully with the expected output.
- Protected root `.obj`/`.pdb` files: 31/31 exact path, size, and SHA-256 matches at every hosted, sanitizer, and native-build preservation gate.
- QAC guest acceptance: `NOT_RUN` / blocked by an active foreign QAC/QEMU run. No RX27 QAC parent or QEMU child was launched; their exit codes and reservation release are unknown/not applicable. The foreign guest was left untouched.

Compact machine-readable results and preflight evidence are in `evidence/rx27/`; detailed build, sanitizer, and preservation logs remain in their phase-specific subdirectories. Kernel binaries and build outputs remain under `build/rexx/rx27/` and are not committed.

## Inventory and follow-up

The inventory remains at 75 entries: 57 implemented, 13 not implemented, 3 partial, 1 not assessed, and 1 not implemented by design. Existing `DATE`, `TIME`, and `TRACE` partial classifications are unchanged. Regina comparison and native guest acceptance remain unqualified.

Recommended RX28 candidate: `FIND`, which remains a P3 unimplemented entry in the RX27 inventory and is a compact, non-I/O word-search candidate. Confirm its IBM contract and alias/behavior relationship to the existing word-position implementation during RX28 preflight.
