# Developer Studio Phase 27J — `while` Loops and Backward Branches

Phase 27J introduces `while` only to the bounded bootstrap language. It adds
runtime loop execution and genuine backward AMD64 control flow while keeping
the existing Phase 27B–27I compiler, NativeElf, and Developer Studio proof
chain intact.

## Grammar

The implemented statement grammar is:

```text
statement :=
      declaration
    | assignment
    | log_statement
    | return_statement
    | if_statement
    | while_statement
    | block

while_statement := "while" "(" expression ")" statement
```

The body may be a braced block or one statement. A loop condition uses the
same expression grammar as every other expression; there is no separate loop
condition language.

## Lexer and target-neutral IR

`while` is a reserved keyword token. Longest identifier matching remains in
force, so `whilex` is an identifier and `while (` produces separate keyword
and left-parenthesis tokens with deterministic source locations.

The parser emits a dedicated `StatementKind::While` node. Its `expression`
field contains the condition and its target-neutral `thenBlock` field contains
the loop body block. The IR contains no machine addresses, branch offsets, or
AMD64 labels. The backend owns control-flow construction.

## Semantics

`while (condition) body` has pre-test semantics:

```text
loop header:
    evaluate condition
    if false, leave loop
    execute body
    jump back to loop header
```

The condition is evaluated before the first iteration and again after every
body execution. Therefore a false initial condition runs the body zero times.
Integer truth follows the existing rule: zero is false and every nonzero value
is true. Comparisons and `&&` / `||` retain their existing signed comparison,
short-circuit, and canonical-boolean behavior inside loop conditions.

Locals keep the Phase 27H function-scoped storage policy. Every local receives
one stable four-byte stack slot, including a declaration parsed inside a loop
body. A loop-body initializer executes each time control reaches that
declaration, so the value is reinitialized on each iteration without creating
new storage.

## Backward branch generation

For each `While` statement the AMD64 backend creates a condition label and an
exit label, defines the condition label before emitting the condition, emits a
`test eax,eax` and conditional exit branch, emits the body, emits `jmp` back to
the condition label, and finally defines the exit label. The backedge is an
ordinary `E9 rel32` instruction; no loop size or offset is hardcoded and the
body is not constant-folded or unrolled.

The existing label/fixup table is reused for both forward and backward
branches. Labels defined before a fixup remain valid because all fixups are
resolved in the same final patch pass. The patch pass validates label indices,
code-buffer bounds, and that every target is defined.

## Signed rel32 handling

`calculate_signed_rel32(target, addressAfterBranch, output)` computes the
distance in a direction-aware unsigned form and accepts exactly the signed
32-bit range `[-2147483648, 2147483647]`. It rejects overflow before converting
to the four-byte little-endian field. This prevents accidental unsigned wrap
or silent truncation for backward branches. Host tests cover small negative,
zero, positive, and both overflow directions; generated loop code is also
checked for a negative `E9` displacement.

## Limits

The explicit loop nesting limit is eight. Existing limits remain active for
source bytes, tokens, locals, statements, expressions, blocks, block depth,
conditional depth, labels, fixups, emitted code, string data, and ELF output.
Exceeding the loop limit or any backend label/fixup/code capacity fails
deterministically with no artifact publication.

## Returns and analysis

A `return` inside a loop evaluates its expression into `EAX` and jumps to the
existing shared epilogue when a frame or other control flow requires one. It
does not unwind loop labels. Return-path analysis is conservative: a `while`
statement never guarantees that its body executes, even for a constant
condition. Thus a function containing only `while (x) { return 42; }` is still
diagnosed as possibly reaching the end; a later return satisfies the analysis.

Intentional infinite loops are syntactically accepted but are not used in
runtime smoke tests. The NativeElf runtime remains trusted and kernel-owned;
there is no preemption or watchdog for a buggy loop.

## Proof coverage

The Phase 27J fixture and smoke harness cover:

- basic mutation, summation, zero-iteration, and condition re-evaluation;
- `&&` and `||` loop conditions;
- `if` inside `while`, `while` inside `if`, and nested loops;
- loop-body declarations and stable local state;
- three repeated host log calls;
- a return from inside a loop;
- invalid empty and incomplete conditions with source locations;
- valid → invalid → valid failure recovery;
- deterministic repeated ELF emission;
- runtime state/artifact changes for increment-one versus increment-two;
- Developer Studio Edit → Build → Run with `Starting loop.`, `Loop complete.`,
  and exit code 42;
- external `objdump` inspection of a guest-generated ELF containing a negative
  displacement backedge.

The primary Developer Studio program is:

```cpp
int gx_main(gx_app_context* ctx)
{
    int total = 0;
    int i = 1;

    log(ctx, "Starting loop.");

    while (i <= 6)
    {
        total = total + i;
        i = i + 1;
    }

    log(ctx, "Loop complete.");

    return total * 2;
}
```

The smoke harness is invoked with:

```text
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/smoke-compiler-bootstrap.ps1 -BootCount 3 -TimeoutSeconds 180 -Phase27J
```

It requires the Phase 27B–27I markers plus the Phase 27J markers, and performs
the independent `readelf`/`objdump` audit after the guest boots. Physical
hardware testing is separate and is not implied by this QEMU proof.

## Scope and known limitations

Phase 27J adds only bounded `while` loops. It does not add `break`, `continue`,
`for`, `do/while`, increment/decrement operators, user-defined functions,
arrays, general pointers, or full C/C++. The compiler remains a one-source,
one-`gx_main`, signed-integer AMD64 bootstrap compiler with no general linker.
Runtime programs are trusted; a nonterminating loop can still hang the
kernel-owned NativeElf execution path. Debugger attachment is not part of the
phase.
