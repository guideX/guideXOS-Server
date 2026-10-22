# REXX RX24 — ADDRESS and TRACE built-ins

## Result

Outcome: **Partial**. `ADDRESS()` is implemented against the environment state the VM already uses. `TRACE()` supports real command-return tracing for `N`, `E`, `F`, and `O`. The other classic trace actions require execution hooks or a bounded debugger that this interpreter does not provide, so they fail explicitly instead of behaving as silent no-ops.

## `ADDRESS()`

`ADDRESS()` accepts no arguments and returns the current command environment name, with trailing ASCII blanks removed. The VM's established initial environment is `KERNEL`; this is the same default used at command dispatch, not an RX24-created shell environment. A persistent `ADDRESS name` change is visible to the function. The one-command `ADDRESS name command` form temporarily selects `name` for that dispatch and leaves the persistent environment unchanged.

The active environment is per VM. Internal function and subroutine frames save and restore it on valid return and on error unwind. The built-in does not dispatch commands or change routing. An environment-aware embedding dispatcher receives the selected environment; the VM's existing no-dispatcher behavior remains the safe RC 127 stub with no system-shell call.

## `TRACE([option])`

With no argument, `TRACE()` returns the current action as its canonical one-letter form and changes nothing. With an option, it returns the previous action and then applies the new request. The argument must be a string; numeric trace options are rejected. The option's first alphabetic character selects the action, and remaining alphabetic characters are ignored, matching the documented character-option rule.

| Option | Implemented behavior |
| --- | --- |
| `N` / `Normal` | After explicit activation, trace commands whose actual RC is negative. |
| `F` / `Failure` | After explicit activation, trace commands whose actual RC is negative, the runtime's available failure signal. |
| `E` / `Error` | After explicit activation, trace commands whose actual RC is nonzero. |
| `O` / `Off` | Disable trace output. |

`N` traces negative return codes, `F` traces the negative return codes used as the runtime's failure signal, and `E` traces every nonzero return code. Output uses the existing VM output writer (or the hosted stdout fallback where available) and includes the action, source line, command, and signed RC. Example:

```text
TRACE E: line 6: command RC(127)
```

The VM reports default action `N`, but it does not emit new diagnostics until the program explicitly calls `TRACE(option)`. This keeps existing programs' output unchanged when they never request tracing. A query call does not activate tracing.

The VM action and activation flag are instance-local. Internal routine frames save and restore both on return and error unwind. An output-writer error is returned after the command has actually dispatched. Invalid options, unsupported options, and allocation failures leave the active trace state unchanged.

## Deferred classic actions

`A` (All), `C` (Commands), `I` (Intermediates), `L` (Labels), `R` (Results), and `S` (Scan) are recognized as classic options but rejected with an unsupported-capability error. The `?` interactive-debug and `!` command-inhibition prefixes, whether alone or combined with an action, are likewise rejected without changing state. The parser has no `TRACE` instruction form, the VM has no clause/expression/label event stream or scan control, and it has no debugger input callback. In particular, `!` does not inhibit command dispatch. These require separate interpreter integration work; RX24 does not simulate them.

This also means interactive debugging cannot pause an automated guest. Guest qualification covers supported command-return trace output, invalid-option recovery, and post-error responsiveness only.

## Qualification

Dedicated RX24 coverage is in [the direct ADDRESS/TRACE test](../../tests/rexx_rx24_address_trace_test.c). Guest inputs are under [the RX24 native guest scripts](evidence/rx24/native-guest/scripts/). Phase evidence, including preflight and protected artifact manifests, is under [evidence/rx24](evidence/rx24/).

Final hosted, sanitizer, native AMD64, QAC/QEMU, and preservation results are summarized in [the RX24 scorecard](REXX_RX24_SCORECARD.json). Regina differential comparison is `NOT_RUN` when no Regina executable is installed; no external compatibility percentage is claimed.

## References

- IBM z/OS 2.5 TSO/E REXX [`ADDRESS()`](https://www.ibm.com/docs/en/zos/2.5.0?topic=functions-address).
- IBM z/OS 2.5 TSO/E REXX [`TRACE()`](https://www.ibm.com/docs/en/zos/2.5.0?topic=functions-trace), [prefix options](https://www.ibm.com/docs/en/zos/2.5.0?topic=trace-prefix-options), and [alphabetic trace options](https://www.ibm.com/docs/en/zos/2.5.0?topic=trace-alphabetic-character-word-options).

## Recommended RX25 scope

Consider `RANDOM` with explicit-seed deterministic coverage. It can be implemented independently of wall-clock provenance, persistent settings, and host-command routing.
