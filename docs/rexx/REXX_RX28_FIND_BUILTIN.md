# RX28 — IBM FIND Built-in

## Scope and status

RX28 adds IBM TSO/E and VM's non-SAA `FIND(string,phrase)` built-in. IBM defines FIND as returning the word number of the first word of `phrase` found in `string`, or zero when no match exists or the phrase has no words. IBM also states that repeated blanks are treated as one for comparison and recommends `WORDPOS` for this search. See the [IBM z/OS 2.5 FIND reference](https://www.ibm.com/docs/en/zos/2.5.0?topic=functions-find).

FIND has exactly two required arguments. `WORDPOS(phrase,string[,start])` takes the phrase first and also supports an optional start word. FIND reverses the first two values and calls the existing WORDPOS implementation with an explicit argument count of two, which selects WORDPOS's default start word and prevents the optional third argument from applying. No separate search engine, tokenizer, or temporary whole-string buffer was added.

## Word and byte behavior

FIND shares WORDPOS's existing bounded scanner and exact, consecutive whole-word comparison. The scanner uses the established `SPACE` word-separator class: ASCII space and bytes tab through carriage return. Repeated separators and leading or trailing separators do not change the compared words. Case and punctuation are preserved. Inputs are read by explicit byte length, so NUL and high-bit bytes remain ordinary word bytes unless they equal an existing separator byte; C-string termination is not a boundary. Neither argument is modified.

The interpreter's RX16 X-literal syntax discards grouping blanks, as IBM documents for hexadecimal strings in [REXX tokens](https://www.ibm.com/docs/en/zos/2.5.0?topic=syntax-tokens). Thus `'80 00FF 41'X` contains bytes `80 00 FF 41` with no byte `20` separator; it is one word, and `FIND('80 00FF 41'X,'00FF'X)` returns `0`. To separate those binary words, the source must include an actual blank byte, for example `FIND('80'X || ' ' || '00FF'X,'00FF'X)`, which returns `2`. The RX28 CLI fixture checks both results and verifies the packed bytes with `C2X`.

FIND inherits WORDPOS's existing search-work bounds and the 16 MiB runtime string cap. Processing is byte-oriented; this phase adds no Unicode, DBCS, locale, or EBCDIC transcoding behavior. An external Regina differential is `NOT_RUN` because no Regina executable is installed, so no cross-interpreter percentage is claimed.

## Qualification

- Direct RX28 suite: 36,175 assertions; 12,000 deterministic property cases with seed `0x52583238`.
- Independent bounded word-span oracle checked IBM vectors, random byte strings, exact word boundaries, repeated and consecutive phrases, byte-class separators, punctuation, case, embedded NUL, high-bit bytes, and empty values. The suite also compares every case against `WORDPOS(phrase,string)`.
- Boundary coverage includes 10,000-word haystacks and a single 16 MiB word. Direct dispatch tests cover missing and unset required arguments, an explicit empty phrase, a third start argument, and further excess arguments. VM tests cover nested calls, internal routines, repeated calls, two independent VMs, syntax-trap recovery, malformed source, and recovery after every measured interpreter allocation failpoint.
- Binary coverage includes complete NUL and `FF` words, partial-word rejection, nonprintable bytes, high-bit bytes adjacent to letters, and real separators around binary words. The test follows RX16 X-literal packing rules.
- Direct result-allocation failure returns `OUT_OF_MEMORY` with an unset result; the same inputs then succeed. FIND adds no intermediate allocation before WORDPOS constructs its numeric result.
- The native guest demo prints the single-word result (`2`), multiword/repeated-blank result (`2`), not-found result (`0`), binary whole-word result (`2`), and corresponding `WORDPOS` result (`2`) before its completion marker; a separate guest script traps the rejected third argument and confirms the next FIND call succeeds.
- The exact hosted, sanitizer, freestanding, QAC guest, and artifact-integrity results are recorded in `REXX_RX28_SCORECARD.json` and the `evidence/rx28/` directory.

## Inventory

The inventory remains at 75 entries. FIND moves from `Not implemented` to `Implemented`; DATE, TIME, and TRACE retain their RX27 classifications. The resulting counts are 58 implemented, 12 not implemented, 3 partial, 1 not assessed, and 1 not implemented by design.
