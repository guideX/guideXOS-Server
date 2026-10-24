# REXX RX37 SAY Formatting Qualification

## Current production behavior

The VM writes the complete SAY value and its terminating LF in separate calls. The kernel adapter chunks each call into at most 119 bytes, rejects NUL, and hands each chunk to the shell's `output_string`. That function stores LF-delimited rows and flushes any nonempty remainder at the end of each call. As a result, chunk and callback boundaries can become stored rows. A SAY's separate final LF creates a blank row after its value. This is current shell behavior; it is not a line-width policy shared with the REXX VM.

The scrollback row capacity is 119 bytes, but the renderer separately uses an 8-pixel glyph advance and the current drawable rectangle. It does not use its computed `charsPerLine` value to wrap stored rows. A row may be clipped horizontally. Resizing changes the visible rectangle but does not reflow history or publish a width to REXX. Non-ASCII bytes draw as blanks; CR is stored and draws blank; NUL is rejected by the kernel writer.

RX37 does not alter these output paths or renderer semantics. It does not treat writer quotas, callback chunk size, scrollback capacity, font pixels, or screenshot appearance as a REXX logical line width.

## Hosted destination model

`tests/rexx_rx37_output_capability_test.c` supplies bounded writers whose formatting behavior is independently specified. Its reference formatter receives a test-vector width independently of the provider snapshot and compares actual bytes, automatic breaks, explicit newlines, and record boundaries. The model carries column state across 119-byte processing chunks.

Coverage includes lengths 0, 1, width minus one, exactly width, width plus one, twice width, multiple wrapped rows, 119/120/238/377-byte boundaries, CR/LF and blank lines, NUL/high-bit byte preservation, record-byte policies, resize expansion and contraction, historical output preservation, provider replacement during observation, stale snapshots, teardown attempts, allocation-free queries, writer failure, partial output, internal calls, INTERPRET, and independent VM destinations.

The deterministic property suite uses xorshift seed `0x52583337` and 4,096 fixed-width output cases. Final RX37 result: 4,203 assertions (107 direct, 4,096 property), 4,096 property cases, zero failures. This proves the host contract and model; it does not prove kernel-shell formatting or native LINESIZE behavior.

## Regression qualification

- Hosted `tests/run_rexx_tests.ps1 -Phase RX37`: passed; focused direct/property test above, inherited RX1–RX36 suites and CLI fixtures passed. Regina was unavailable (`NOT_RUN`).
- Address sanitizer: 36/36 executable targets built and passed, including `rx37_output_capability`.
- Undefined-behavior sanitizer: 36/36 executable targets built and passed, including `rx37_output_capability`.
- Freestanding AMD64 build/link: see the native-build evidence and scorecard.
- QAC guest: not launched because the exclusive coordinator already held a managed acceptance reservation. No guest output or screenshot is claimed as RX37 evidence.
- Protected root artifacts: 31/31 path, size, and SHA-256 checks passed at hosted and sanitizer checkpoints.

## IBM compatibility boundary

IBM defines `LINESIZE()` as the line width at which SAY breaks, with distinct TSO/E foreground, background, and non-TSO/E record-length behavior. RX37's byte storage and visible shell rows do not establish one matching logical character-column result. The foreground rule also includes a terminal-width-minus-one relationship; RX37 does not subtract an offset from pixels or an already-effective width. See the [IBM z/OS 2.5 LINESIZE reference](https://www.ibm.com/docs/en/zos/2.5.0?topic=functions-linesize).
