# Developer Studio Phase 27I — Short-Circuit Logical Operators

Phase 27I adds target-neutral logical `&&` and `||` to the bounded bootstrap
language. They are logical operators, not bitwise operators, and both
short-circuit.

## Grammar and precedence

The complete expression grammar is:

```text
expression := logical_or

logical_or := logical_and { "||" logical_and }
logical_and := equality { "&&" equality }
equality := relational { ("==" | "!=") relational }
relational := additive { ("<" | "<=" | ">" | ">=") additive }
additive := multiplicative { ("+" | "-") multiplicative }
multiplicative := unary { "*" unary }
unary := "-" unary | primary
primary := integer_literal | identifier | "(" expression ")"
```

From highest to lowest precedence:

```text
* 
+ -
< <= > >=
== !=
&&
||
```

Thus `a == 1 && b == 2 || c == 3` parses as
`((a == 1) && (b == 2)) || (c == 3)`.

## Lexer and diagnostics

The lexer uses longest matching for `&&` and `||`. A single `&` or `|` is not
accepted as a bootstrap-language operator. It produces a source-located
diagnostic such as `unexpected '&'; logical AND is '&&'` or
`unexpected '|'; logical OR is '||'`.

## Truth and value semantics

Zero is false and every nonzero signed 32-bit value is true. Both operators
produce a canonical signed 32-bit result: false is `0`, true is `1`.
Logical expressions are ordinary value-producing expressions, so they can be
returned, assigned to locals, used in arithmetic, or consumed by `if`.

## Target-neutral IR

The IR retains `ExpressionKind::LogicalAnd` and
`ExpressionKind::LogicalOr`. The parser does not create target labels or
jumps. This preserves the semantic distinction from arithmetic and bitwise
operations and lets the backend select the target lowering.

## Short-circuit lowering

The AMD64 backend emits expression-level control flow while preserving its
external contract: the expression leaves a signed/canonical 32-bit result in
EAX.

For `A && B`, the backend evaluates A, tests EAX, and emits a rel32 `je` to a
shared false label. Only when A is nonzero does it emit and execute B. B is
tested in the same way, followed by `mov eax,1`; the false label emits
`mov eax,0`.

For `A || B`, the backend evaluates A, tests EAX, and emits a rel32 `jne` to a
shared true label. Only when A is zero does it emit and execute B. B is tested
in the same way, followed by `mov eax,0`; the true label emits `mov eax,1`.

Nested logical expressions reuse the same recursive expression emitter and
therefore compose with arithmetic, comparisons, assignments, and `if`.

Representative AMD64 forms are:

```asm
; &&
... evaluate left ...
test eax,eax
je   logical_false
... evaluate right ...
test eax,eax
je   logical_false
mov  eax,1
jmp  logical_end
logical_false:
mov  eax,0
logical_end:

; ||
... evaluate left ...
test eax,eax
jne  logical_true
... evaluate right ...
test eax,eax
jne  logical_true
mov  eax,0
jmp  logical_end
logical_true:
mov  eax,1
logical_end:
```

The `je`, `jne`, and `jmp` instructions use the existing bounded label and
rel32-fixup machinery from Phase 27H. Label and fixup exhaustion, code-buffer
overflow, expression-node exhaustion, and expression nesting remain explicit
bounded failures.

## Proof coverage

The host compiler test covers lexer tokens and locations, malformed syntax,
single-character rejection, IR kinds, precedence, canonical results, nested
expressions, assignment, branch target formation, RHS-load bypass, and branch
capacity. The Phase 27I QEMU smoke covers both truth tables, comparison
precedence, `if`, mixed/nested expressions, Developer Studio Edit → Run → Save
All → Build → NativeElf → RunController, invalid-source recovery, deterministic
ELF output, and kernel/IDE survival across three fresh boots.

The short-circuit proof combines guest runtime outcomes with generated-code
inspection. The generated `false && RHS` and `true || RHS` artifacts contain a
forward conditional branch whose resolved target is after the RHS local-load
instructions; `objdump` output is captured by the smoke harness. The language
does not yet have side-effecting expression calls, so no new side-effect
feature was added solely to manufacture a dangerous RHS.

## Limits and known limitations

The current bounds remain 2,048 tokens, 1,024 expression nodes, 16 expression
nesting levels, 128 branch labels, 128 branch fixups, 8 KiB emitted code, 32
locals, and 32 blocks. This language still supports one source file, one
`gx_main`, integer-only expressions, and the trusted kernel-owned runtime. It
has no loops, user-defined functions, bitwise operators, general pointers,
arrays, full C/C++, general linking, or debugger attachment. The backend is
AMD64-only.
