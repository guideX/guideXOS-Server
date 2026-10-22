# REXX RX24 Interpreter State Audit

Audit baseline: branch `REXX_SUPPORT`, commit `db3c10308923118bbb408328c33c23c64c801fb3`.

## Host-command environment

The VM already owns an environment identity per interpreter instance. A null `RexxVM.address_environment` means the runtime's established default, `KERNEL`; a non-null value stores the name selected by an `ADDRESS` instruction. The VM uses this same value when it dispatches an ordinary command. This is the runtime's existing identity, not a new RX24 environment.

The parser/compiler/VM implement both persistent `ADDRESS environment` and the one-command `ADDRESS environment command` form. Persistent selection copies the name into the VM. The one-command form passes the selected name to dispatch without changing the persistent setting. An environment-aware embedding callback receives both name and command. A legacy command-only callback is also supported but cannot distinguish environments. With neither callback installed, the VM uses its safe command stub, which returns RC 127 and does not invoke a shell.

The VM does not maintain a registry of valid environments. Environment-aware callbacks decide how a selected name is handled and report command outcomes in RC. The default stub is fail-closed; there is no unrestricted system-shell fallback in the VM.

Internal routine frames copy the active environment on entry and restore it on return and routine unwind. That bookkeeping is per VM. The existing RX7–RX9 tests exercise persistent and temporary environment dispatch and restoration, including condition paths. The built-in can read the same state without changing command routing.

## Trace subsystem

At the audit baseline, the lexer recognizes the `TRACE` keyword token, but the parser has no TRACE clause form and the compiler/VM have no TRACE instruction or trace event hooks. The VM has no active action, interactive-debug flag, command-inhibition flag, or scan state. The existing output writer is used for `SAY`; it is not wired to instruction, expression, label, or command trace events. There is no debugger input callback.

Consequently, accepting a trace mode and storing its letter would not make that mode operational. The current runtime cannot safely pause for interactive input, prevent dispatch through a command-inhibition control, skip execution for scan mode, or emit clause/intermediate/result trace output. The standard command dispatcher does provide a real command RC after dispatch, so the runtime has enough information to implement a narrowly scoped normal trace of negative command RCs through the existing output boundary.

Internal routine frames currently save and restore numeric settings, clock context, condition state, and address environment, but no trace state exists to save. Error and exceptional unwind use the shared routine-unwind path. `INTERPRET` executes in the active VM and has bounded dynamic-code frames; it has no independent trace state. Any RX24 trace state must be per VM and join the same routine-frame return/unwind bookkeeping.

The implementation adds a per-VM trace action, initialized to the runtime's reported default `N`, and an explicit-request flag. The flag prevents the new diagnostic output from changing RX1–RX22 command output before a program requests tracing. `TRACE()` only queries state; `TRACE(option)` atomically returns the previous action and enables the supported action, while `TRACE('O')` disables it. Both fields are saved/restored with routine frames on return and unwind. Trace output is emitted after actual command dispatch through the existing output writer (hosted stdout fallback where available); the action is evaluated against the real command RC. There is no output on a failed built-in allocation or unsupported action.

Operational support is intentionally limited to `N`, `E`, `F`, and `O`. `N` and `F` report negative command RCs (the VM's negative-return failure signal), and `E` reports any nonzero RC; `O` emits none. These modes use the existing command boundary and are not clause, expression, label, or result tracing. The first alphabetic option character selects an action, matching IBM's character-option rule; numeric options fail as required for the built-in.

## RX24 implementation boundary

`ADDRESS()` uses the established VM environment and is fully implemented without changing command dispatch. It reports the actual initial `KERNEL` default or the active ADDRESS instruction environment, trims trailing blanks, and leaves state unchanged. Existing environment-aware dispatch and the fail-closed RC 127 fallback remain unchanged.

For `TRACE`, RX24 claims only options whose effects can be enforced by the existing VM. Unsupported standard actions `A`, `C`, `I`, `L`, `R`, and `S`, plus the `?`/`!` prefixes, fail explicitly without changing state. Interactive debugging, full clause/expression/result tracing, scan mode, and command inhibition remain separate interpreter integration dependencies. No debugger callback is present, so no interactive mode is enabled in the QEMU guest.

## Inventory baseline

The RX22 inventory contains 75 entries: 53 implemented, 18 not implemented, 2 Partial (`DATE`, `TIME`), 1 not assessed, and 1 not implemented by design. RX24 preflight captured 51,630 pre-existing untracked paths and SHA-256 records for 31 root `.obj`/`.pdb` files in `evidence/rx24/`.

## References

- IBM z/OS 2.5 TSO/E REXX [`ADDRESS()`](https://www.ibm.com/docs/en/zos/2.5.0?topic=functions-address) built-in.
- IBM z/OS 2.5 TSO/E REXX [`TRACE()`](https://www.ibm.com/docs/en/zos/2.5.0?topic=functions-trace) built-in and [trace options](https://www.ibm.com/docs/en/zos/2.5.0?topic=trace-alphabetic-character-word-options).
