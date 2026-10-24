# REXX RX37 LINESIZE Eligibility

## Decision: keep Not implemented

IBM classifies `LINESIZE()` as a non-SAA function. IBM's documented TSO/E behavior distinguishes foreground terminal width minus one, the TSO/E background result of 131, and the logical record length of OUTDD for non-TSO/E execution. The function reports where SAY line breaking occurs. These mappings are not interchangeable. [IBM z/OS 2.5 LINESIZE reference](https://www.ibm.com/docs/en/zos/2.5.0?topic=functions-linesize).

guideXOS does not implement those TSO/E environments. Its hosted CLI has no terminal/redirect detection or record-length provider. Its kernel shell adapter has a 119-byte storage/chunk behavior that becomes row segmentation through the current callback-local `output_string`, while drawing uses separate geometry and can clip rows. That behavior is byte-oriented and includes separate-call/newline effects; it is not an authoritative character-column width shared with SAY. No 131 value or assumed 80-column terminal value is used.

The optional output-capability API distinguishes destination kind, capability state, width unit, policy, record length, identity, and generation. The production shell currently installs no capability reader. The hosted tests exercise provider behavior and independent formatting oracles but do not substitute for a production destination.

**RX37 path: B — capability foundation only.** Keep `LINESIZE()` Not implemented. Do not change the 75-entry inventory classifications. An output-byte storage cap, pixel width, record length, buffer capacity, or font-derived estimate cannot be converted into TSO/E character columns without an explicit compatibility contract and matching SAY policy.

## Reconsideration gates

RX38 may reconsider `LINESIZE()` only after one production destination:

1. Owns and exposes an authoritative logical character-column policy.
2. Uses that same policy for actual SAY wrapping, including output callbacks, explicit CR/LF, empty values, high-bit bytes, and values across the 119-byte chunk size.
3. Publishes a fresh generation and width after supported resize or renderer changes.
4. Distinguishes interactive, redirected, background, and record-oriented destinations and reports unsupported/unknown states without fabricated numbers.
5. Passes hosted formatter-oracle and fault tests, freestanding AMD64, sanitizer runs, and exclusive QAC guest acceptance with visible SAY and capability evidence.

Only then select and document a guideXOS compatibility profile. Do not subtract one twice: the TSO/E foreground mapping is physical/configured terminal width minus one; a provider already reporting the effective SAY break position needs no second offset. Do not use the TSO/E batch value 131 unless guideXOS actually supports that execution contract.
