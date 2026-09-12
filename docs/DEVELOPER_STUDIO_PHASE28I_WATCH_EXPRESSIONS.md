# Developer Studio Phase 28I — Read-Only Watch Expressions

Phase 28I adds one bounded expression query to the server-side native
debugger. It builds on Phase 28H selected-frame variable inspection and does
not add a persistent Watches pane or a Set Value operation.

## Scope and safety

The evaluator accepts only validated scalar values already returned by the
selected-frame Phase 28H inspection path and integer literals. It never reads
target memory from an expression, changes registers or stack slots, calls
target code, resumes, steps, changes frame selection, installs breakpoints, or
arms Trap Flag. There is no expression-to-address conversion and no
dereference path.

The expression engine is a dedicated fixed-buffer parser. It does not invoke
the source compiler or inspect GXSM records directly. The native run service
owns frame/session validation; the evaluator receives only a bounded
NativeDebugWatchFrame containing the authenticated variable result and an
optional metadata resolver used to distinguish an unknown name from a declared
but non-live name.

## Grammar and limits

Supported tokens are identifiers, decimal integers, hexadecimal integers
(0x/0X), parentheses, unary + and -, and binary +, -, *, /, and %. ASCII
whitespace is accepted between tokens. Precedence is conventional: parentheses,
unary operators, multiplicative operators, then additive operators. Evaluation
is left-to-right within one precedence level.

The hard bounds are:

- expression text: 256 bytes;
- tokens: 64;
- AST nodes: 64;
- operators: 32;
- parenthesis parse depth: 16;
- recursive evaluation depth: 32;
- identifier and numeric-literal text: 64 and 32 bytes respectively.

Malformed text, assignments, increment/decrement, comparisons, logical and
bitwise operators, dereference, member/array access, casts, calls, strings,
floating-point literals, declarations, and comma expressions are rejected
deterministically. * is multiplication only.

Arithmetic uses checked signed 64-bit values. Addition, subtraction, and
multiplication detect overflow. Division and modulo truncate toward zero and
reject zero divisors; INT64_MIN / -1 and the corresponding modulo are also
overflow errors. The literal -9223372036854775808 is handled without
intermediate signed overflow.

The result currently has two kinds: signed integer and pointer. A plain
pointer identifier is returned as a pointer value only when the validated
debug-variable provider exposes that pointer. Pointer arithmetic and pointer
dereference are rejected. The bootstrap compiler's current local-variable
metadata intentionally exposes signed locals and only its already-supported
raw pointer parameter forms; the QEMU Phase 28I fixture therefore proves the
integer path, while host evaluator tests cover pointer-scalar handling.

## Identifier and frame semantics

auxiliaryAddress remains a query-local selected-frame index. Zero preserves
the Phase 28G top-frame behavior. The server reconstructs the current
validated Call Stack for every expression request and then calls the same
selected-frame inspection path as Phase 28H. No parent-frame or global lookup
is implicit.

For an identifier, the engine first checks the selected-frame variable result.
If it is absent, the metadata resolver may classify it as unknown, declared but
not live, unsupported type, or unavailable metadata. It never returns stale
stack-slot bytes. A changed stop generation, session, artifact, runtime, or
selected frame rejects the request as stale or invalid before identifier
evaluation.

## Public debugger ABI

GX_DEVELOPMENT_DEBUG_EVALUATE_EXPRESSION is command 21, appended after
Phase 28H command 20. The existing request prefix remains 104 bytes; the
request's appended expression pointer extends the full request to 112 bytes.
The selected frame continues to use auxiliaryAddress, so object ABI 10 and
GXSM remain unchanged.

The appended result is gx_development_debug_expression. It has fixed-size
function/source/error strings, selected-frame and stop identity, signed,
unsigned, and pointer value slots, a result kind, an expression status, an
error category, and a diagnostic byte offset. Error categories include syntax,
unknown identifier, not live, unsupported type/operator, divide-by-zero,
overflow, invalid frame, stale generation, target not paused, expression too
long, complexity limit, metadata unavailable, and invalid request.

Host and bare-metal entry points validate the full request only for command
21. Existing commands accept their legacy 104-byte request prefix, preserving
their ABI behavior. No public command was renumbered and no object ABI field
was added.

## Verification

Host checks cover identifier and literal evaluation, whitespace, precedence,
parentheses, unary arithmetic, signed division/modulo, hex and malformed hex,
unknown/dead/unvalidated variables, unsupported syntax, divide-by-zero,
overflow, minimum-integer handling, pointer identifiers, length/token/depth
limits, stale generation, invalid frame, and running-state rejection.

The public QEMU smoke uses the Phase 28G/H nested fixture and a warm-cache
second build. It proves:

- frame 0 helper_tail: tail_value is 33 and tail_value + 1 is 34;
- frame 1 helper: adjusted is 14, input + delta is 14, and
  doubled - adjusted is 14;
- frame 2 gx_main: root_value is 100;
- value resolves to 33 in frame 0 and 14 in frame 1;
- 0x2A + 5, precedence, and parenthesized arithmetic;
- unknown identifier, dead after_call, divide-by-zero, overflow, and
  expression-length rejection;
- repeated evaluation has identical value and frame identity;
- a real Phase 28G source step changes adjusted from 14 to 15, and the
  public expression query observes both values;
- stale-stop, invalid-frame, and running-state rejection;
- unchanged GPRs, RIP/RSP/RBP/RFLAGS, Call Stack, and variable results;
- the existing Phase 28G/28H stepping, Run, GUI render, close, and cleanup
  regressions.

-Phase28IOnly enables the Phase 28G session plus the Phase 28H and Phase 28I
smokes, so its required-marker gate retains the G/H debugger regressions.
The final validation run used three fresh isolated QEMU boots. The actual
Developer Studio GUI was not mouse-driven; this phase is server-side and the
guest compositor lifecycle was exercised by the existing automated smoke.

The standalone repository D:\dev\guideXOS_Developer_Studio is intentionally
outside this change.

## Limitations and deferred work

There is no persistent watch list, Watches UI, conditional breakpoint
expression, Set Value, pointer arithmetic, pointer dereference, arrays,
members, structs, casts, floating point, calls, globals, or arbitrary memory
expression. These remain future work and must retain the same validated,
side-effect-free boundary if added.
