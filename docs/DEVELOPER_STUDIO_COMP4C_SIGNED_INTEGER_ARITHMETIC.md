# Developer Studio COMP4C — Signed Integer Division and Remainder

**Outcome:** COMP4C_HOSTED_ACCEPTED

**Branch:** `v0.5_DEVELOPER_STUDIO`

**Starting HEAD:** `f63a7710a30b49f785aac134abf06a3a6c5b5aca`

**Guest qualification:** Pending. This phase used hosted production compiler components and did not launch QEMU or request QAC admission.

## Repository preflight

- Repository: `D:\dev\guideXOSServerV0.5_DEVELOPER_STUDIO`.
- Branch and starting HEAD matched the requested branch and expected commit.
- Upstream divergence at preflight: one local commit ahead, zero behind.
- The local commit is the COMP4B cooperative Build lifecycle implementation.
- The COMP4B Build service, manager, cancellation callback, ABI assertions, and hosted tests were present.
- Existing untracked `out/` and `kernel/out/` validation directories were left in place.
- The COMP4A and COMP4B reports were inspected. No `AGENTS.md` or separate phase marker file was present in this checkout.

## Existing compiler path and arithmetic contract

The guest Build service reads project sources from VFS and calls the production incremental compiler. The compiler front end is the bounded lexer, recursive-descent parser, type checks, and expression IR in `kernel/core/compiler`. AMD64 lowering is in `kernel/arch/amd64/compiler_backend.cpp`; the compiler then emits bounded ELF object data, links modules, writes the final ELF, validates it, and lets the COMP4B Build service validate and publish the temporary artifact. This path does not invoke a hosted compiler.

Before COMP4C, integer expressions included signed 32-bit literals, unary minus, addition, subtraction, multiplication, signed comparisons, and short-circuit logical operators. The lexer had `*` but no `/` or `%` token. The parser placed multiplication at the multiplicative precedence level and restricted its operands to `int`. For binary arithmetic, the backend evaluated the left expression into EAX, pushed RAX to the tracked transient stack, evaluated the right expression, then used ECX and the matching 32-bit instruction. Addition and multiplication used EAX/ECX; subtraction exchanged the values before subtracting. Local and parameter values use the existing frame layout. Nested expressions and calls use the existing bounded temporary stack accounting and call-stack alignment logic.

The AMD64 backend uses caller-volatile RAX, RCX, and RDX for expression evaluation. The generated function ABI, saved registers, function frame layout, object format, link format, Build snapshot, and COMP4B host-call table were not changed. The expression IR kinds and lexer token kinds were appended, preserving the numeric values already assigned to earlier kinds.

## COMP4C implementation

The lexer recognizes `/` and `%`. Both operators share multiplication's precedence level, associate left to right, and bind more tightly than `+` and `-`. Parentheses and all expression forms already accepted at that precedence level continue through the same parser path.

The supported operand type is signed 32-bit `int` on both sides. This includes supported integer literals, local and global scalar values, function parameters, and integer-returning supported function calls. Pointer, string, structure, and other non-integer operands are rejected with a semantic diagnostic at the operator. The language remains a bounded C-like subset; no floating-point, unsigned, wider integer, general C, or general C++ operator support is implied.

For division and remainder, the backend evaluates the dividend, pushes RAX using its tracked transient stack, and evaluates the divisor. It then moves the divisor from EAX to ECX, restores the dividend to RAX, sign-extends EAX into EDX:EAX with `CDQ`, and emits signed `IDIV ECX`. `IDIV` returns the quotient in EAX and remainder in EDX; remainder lowering copies EDX to EAX. The stack push and pop update the existing stack-depth and 16-byte alignment accounting. This lets the divisor contain nested expressions or calls without keeping the dividend in a volatile register.

The emitted instruction encodings asserted by the COMP4C hosted test are:

| Purpose | AMD64 encoding |
|---|---|
| Move divisor EAX to ECX | `89 C1` |
| Restore dividend from the transient stack to RAX | `58` |
| Sign extend EAX into EDX:EAX | `99` (`CDQ`) |
| Signed divide EDX:EAX by ECX | `F7 F9` (`IDIV ECX`) |
| Return remainder from EDX in EAX | `89 D0` |

This implements truncation toward zero. For valid signed 32-bit operands, the remainder is zero or has the dividend's sign, and `a == (a / b) * b + (a % b)`.

## Exceptional arithmetic

- If the divisor is a compile-time constant expression evaluating to zero, `/` reports `constant division by zero` and `%` reports `constant remainder by zero`. The diagnostic kind is semantic and its location is the operator.
- If both operands are compile-time constants and the pair is `INT32_MIN` and `-1`, both `/` and `%` report `constant signed division or remainder overflow` at the operator. The parser rejects the case before host-side constant evaluation can perform undefined signed division.
- A divisor that depends on a runtime value is not checked by the compiler. A runtime zero divisor reaches `IDIV` and raises the AMD64 divide-error exception (`#DE`). A runtime `INT32_MIN / -1` or `INT32_MIN % -1` also raises `#DE` because the signed result is not representable. This phase adds no runtime guard or recovery behavior; guest application-level fault handling is unqualified.
- Other unsupported operand types receive a semantic error. The source grammar still rejects unsupported constructs under its existing syntax, unsupported-construct, and resource-limit diagnostic contract.

The hosted constant-expression evaluator checks zero and signed overflow before evaluating `/` or `%`, so a rejected input does not trigger undefined behavior in the hosted compiler.

## Diagnostics and Build lifecycle

Division and remainder type mismatches, detectable constant zero divisors, and detectable constant overflow are classified as semantic diagnostics with operator source locations. Existing lexical, syntax, unsupported-construct, resource-limit, code-generation, object, and linker classifications are unchanged. Backend capacity failures continue to use the existing code-generation failure path.

The COMP4B lifecycle and ABI remain unchanged: one bounded job slot, generation-tagged ownership, cooperative service stages, cancellation boundaries, temporary sidecar generation, and validated publication/rollback. The COMP4B service regression now also drives a `7 / 0` source through Build and verifies a semantic failure leaves the previous artifact bytes intact, publishes no artifact identity, removes the temporary file, and allows a later successful Build.

## Hosted qualification

The dedicated `scripts/run-compiler-signed-arithmetic-host-test.ps1` suite links the production lexer, parser, and AMD64 backend. It verifies emitted `CDQ`/`IDIV`/remainder instruction bytes and executes generated machine code on the Windows host. The COMP4B Build service regression also compiles and links a successful two-module project containing division, publishes its ELF, and checks the linked ELF for the signed `IDIV` sequence. Runtime assertions cover positive and negative dividends/divisors, both operands negative, positive and negative remainder, locals, parameters, nested expressions, multiplication/division precedence, parentheses, repeated operations, loops, nested calls, and the `INT32_MIN` boundary for valid operations.

Negative and boundary checks cover constant division and remainder by zero, `INT32_MIN / -1` and `% -1`, pointer and string operands, dynamic divisor code generation, expression nesting and output-size limits, malformed-source diagnostic kind and location, recovery by compiling valid input afterward, and failed Build artifact preservation. The generated test also executes expressions with calls on both sides of division, exercising the existing stack and register preservation path.

All 14 hosted test scripts run for COMP4C passed:

- All 12 existing COMP4A compiler regression suites: driver diagnostics, Build job manager, bootstrap, arrays, functions, globals, pointers, structures, structure arrays, object format, multifile linking, and stack-frame bounds.
- The COMP4B cooperative Build service regression, including the new arithmetic semantic-failure artifact-preservation case.
- The dedicated COMP4C signed arithmetic regression.

These results establish parser acceptance, AMD64 emission, ELF/compiler integration covered by existing suites, and hosted execution of generated arithmetic code. No QEMU or genuine guideXOS guest Build/Run arithmetic test was performed. Guest qualification remains pending.

## Remaining language gaps and next scope

The compiler remains a bounded bootstrap language, not a C99/C11 or C++ compiler. It still lacks general preprocessing, full type conversions, unsigned and wider scalar arithmetic, floating point, bitwise arithmetic, static storage, global pointers, array parameters, aggregate assignment, and standard-library support. Runtime division faults are not intercepted by this change.

**Recommended COMP4D candidate:** place guest Build/Run qualification of COMP4C arithmetic in the guest acceptance backlog once QEMU allocation is available. Verify real guest results for signed division/remainder and confirm the actual guest behavior for runtime zero and overflow faults. Keep the scope separate from Debugging and Tools. COMP4D was not started by this change.

## Git closeout

Hosted qualification passed. The implementation and documentation were reviewed; no unrelated tracked changes were included. Existing untracked validation directories were preserved. No push was performed.
