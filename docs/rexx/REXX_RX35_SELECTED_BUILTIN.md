# REXX RX35 Selected Built-in — EXTERNALS()

## Scope and dialect

RX35 implements IBM TSO/E 2.5 `EXTERNALS()` semantics. This is a non-SAA built-in. TSO/E has no terminal input buffer, so the function returns numeric zero. IBM z/VM uses a different contract and counts logical lines typed ahead at a terminal; RX35 does not implement or claim that behavior.

`EXTERNALS()` is independent of the REXX external data stack measured by `QUEUED()`. RX30's `QUEUED()` implementation is left unchanged.

## Implementation

The zero-argument descriptor is registered in `rexx_builtin.c`. The handler rejects any supplied argument and returns decimal string `0` through the existing numeric-result helper. It has no host, kernel, stream, terminal, or mutable-state dependency.

## Qualification results

- Direct and property tests passed: 4,114 assertions (18 direct, 4,096 fixed-seed model cases, four boundary groups), with no failures. The independent model expects constant zero and does not call the production dispatcher.
- Hosted RX35 regression passed. The general CLI set passed 28/28; the EXTERNALS language fixture passed 1/1. The fixture covers the empty buffer, numeric settings, repeated calls, the separate queued stack, nested internal routines, `INTERPRET`, argument rejection through a syntax trap, and recovery.
- AddressSanitizer and UndefinedBehaviorSanitizer each passed 35/35 targets, including `rx35_externals`. Each sanitizer corpus run had 196/196 local expected-result cases. Regina was unavailable, so external differential comparison is `NOT_RUN`.
- The production freestanding AMD64 kernel linked ten REXX C objects. The build emitted 785 compiler warning lines and no error lines. `kernel.elf` SHA-256: `4f7ad559768e02564e0f16db50578c7b799676ed845291b3f62d7e7eaddbae53`.
- QAC acceptance passed with that exact kernel hash, `-net none`, copied disposable media, eight guest commands, and one captured Console screenshot per command. The EXTERNALS guest showed `0` for normal, repeated, numeric-setting, nested, `INTERPRET`, and recovery calls, and `EXTERNALS()/QUEUED()` returned `0/1`. QMP shutdown was acknowledged; the QAC child exited zero, the reservation was released, and the final QEMU census was empty.

The native guest also showed current DATE and TIME operations failing closed, elapsed time increasing from `0.000000` to `0.179987`, and the kernel PIT heartbeat increasing from one record before the guest suite to 172 after it. The inherited `rx20clk.rex` screenshot did not show its `TIME('R')` or final completion markers; RX35 does not claim native `TIME('R')` completion from this run.

## Inventory and retained limits

The RX35 inventory has 75 entries: 61 Implemented, seven Not implemented, five Partial, one Not assessed, and one Not implemented by design. Only EXTERNALS changes classification. RX31 Outcome B remains open. RX34 CHARIN remains Partial; no production input provider or stream behavior was added.

The claim is limited to IBM TSO/E's constant-zero behavior. It does not apply to z/VM terminal type-ahead. Regina comparison and an external compatibility percentage are not claimed.

## Evidence

- Selection checkpoint: `docs/rexx/REXX_RX35_BUILTIN_SELECTION.md`
- Inventory: `docs/rexx/REXX_RX35_BUILTIN_INVENTORY.csv`
- Scorecard: `docs/rexx/REXX_RX35_SCORECARD.json`
- Native link evidence: `docs/rexx/evidence/rx35/native-build/native-link-evidence-027d406368c84c43aceeff8082901071.json`
- Successful QAC run: `docs/rexx/evidence/rx35/native-guest/run-c89be1a5e65645d29cfce2eaea1477da/`
- Sanitizer evidence: `docs/rexx/evidence/rx35/sanitizer-address-758cb45a9d7249d1924f2640d455e5ec/` and `docs/rexx/evidence/rx35/sanitizer-undefined-a521c759f18041bfb88fe37b1e25c313/`
- Initial root artifact guard: `docs/rexx/evidence/rx35/protected-artifacts-guard.json`
