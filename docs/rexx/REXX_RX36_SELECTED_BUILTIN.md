# REXX RX36 Selected Built-in

## No built-in selected

RX36 selects no function. The authoritative RX35 inventory has seven remaining Not implemented functions. The LINESIZE audit found no production logical line-width capability, and the other six candidates require missing stream or identity services. Adding a dispatch entry alone would claim semantics that the current host contracts cannot support.

## LINESIZE decision

Decision C: the output-width architecture is missing. The VM's SAY writer receives bytes, and the shell's storage splitter uses a 119-byte line buffer limit. The shell renderer receives a separate drawable width; its computed columns do not flow back through the output callback. A native CLI writer can target stdout or a host callback without reporting whether it is a terminal or what logical record width it uses.

The buffer bound is not treated as a terminal width. No value is returned by LINESIZE, and no IBM TSO/E foreground, batch, or non-TSO/E behavior is claimed.

## Alternative decision

No independently feasible alternative was found. CHAROUT and LINEOUT need a production writer; CHARS and LINES need non-consuming stream queries not exposed by RX34; LINEIN needs production stream input plus defined line framing and line-position behavior; USERID needs a trusted identity service. Test fixtures cannot establish production support for those absent host capabilities.

## RX36 disposition

Outcome C. No production source or test target was changed. All inventory classifications carry forward: 61 Implemented, 7 Not implemented, 5 Partial, 1 Not assessed, and 1 Not implemented by design. RX31 remains Outcome B and CHARIN remains Partial.

The required next dependency is an output-destination capability whose logical width is shared by SAY wrapping and a per-VM query, with explicit behavior for resizing and noninteractive destinations. Qualification can start after that production contract exists.
