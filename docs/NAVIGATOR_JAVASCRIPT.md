# guideXOS Navigator JavaScript

## Purpose and current boundary

Navigator currently has a reusable HTML document/parser layer in
`guide_web_document.*` and `guide_web_html_parser.*`, with Navigator-specific
navigation and rendering in `navigator.*`. The independent JavaScript
subsystem now contains a bounded lexer, parser/AST, and standalone runtime
core. It still has no DOM abstraction, event scripting system, or `<script>`
execution path. The existing HTML parser intentionally strips `<script>`
content.

JavaScript is therefore a separate subsystem. It must not become an implicit
part of HTML parsing or page loading. Phase JS1 accepts source text and
produces a deterministic token stream; Phase JS2 consumes that stream and
produces a bounded syntax tree. Neither phase has a connection to document
mutation, rendering, navigation, networking, or normal page loading.

## Phase JS1 architecture

The Phase JS1 implementation lives under `navigator_javascript/`:

```text
navigator_javascript/
  source.h       non-owning pointer/span source representation
  lexer.h        token, location, limit, error, and lexer API
  lexer.cpp      bounded single-pass tokenizer
```

The source representation is a non-owning `SourceView` containing a pointer
and an authoritative byte length. The caller owns the bytes and must keep them
alive while the view or returned token lexemes are used. The lexer never
requires a null terminator and never reads outside the supplied extent.

The lexer is intentionally independent of `navigator.*` and
`guide_web_html_parser.*`. The source list includes the implementation in the
normal hosted/native build, but no runtime initialization or page-loading hook
is added.

## Source locations and token model

Offsets and lengths are zero-based byte counts into the source view. Lines and
columns are one-based. A token's location identifies its first byte, and its
length is the original source span length. Tabs use a simple one-column policy
in JS1. CR, LF, and CRLF each produce one logical line break; CRLF advances
over both bytes while incrementing the line only once. Token lexemes are
non-owning source slices, so the original spelling is preserved without a
second source copy.

The token model has explicit types for end-of-input, identifiers, numeric and
string literals, all JS1 keywords, punctuation, and the supported operators.
`undefined` is deliberately an identifier, not a keyword. Overlapping
operators use longest-match behavior.

## Deliberate compatibility subset

JS1 supports ASCII identifiers (`A-Z`, `a-z`, `_`, `$`, followed by those or
digits), decimal literals `0`, `123`, `12.5`, `.5`, `5.`, exponent forms such
as `1e3` and `1E-3`, and hexadecimal literals `0x10`/`0XFF`. It supports both
quote styles and the conservative escapes `\\`, `\'`, `\"`, `\n`, `\r`, `\t`,
`\b`, and `\f`. Line and block comments are skipped.

Unicode identifiers are future work; this phase does not claim ECMAScript
Unicode identifier support. Hexadecimal and Unicode string escapes are not
partially decoded: they produce an explicit unsupported-escape error.

Slash is context-free in JS1. It is division, `/=`, or the start of a comment.
Regular-expression literals are not supported and remain parser-context work.
JS1 itself does not create an AST or runtime values and does not provide
scopes, functions, objects, arrays, built-ins, DOM, events, timers, or web
APIs. JS2 and JS3 add those layers only as independent host-testable
components; they do not connect them to page loading.

## Resource limits

The default lexer limits are finite and part of the JS1 contract:

| Resource | Default limit |
| --- | ---: |
| Source bytes | 1 MiB |
| Emitted tokens, including end-of-input | 8,192 |
| Individual token or literal span | 64 KiB |

Callers may provide smaller limits for tests or a more constrained host. A
limit failure stops lexing, returns an explicit error, and does not truncate a
valid token into another token. Diagnostics are fixed-size data (error code
and source location); no unbounded diagnostic string is allocated.

## Error model

Successful end-of-input is an emitted `EndOfInput` token and a result with no
error. Failed lexing returns a non-success result with a typed error and an
authoritative source location. Errors distinguish invalid source views,
source-too-large, too-many-tokens, token-too-long, unexpected characters,
unsupported escapes, unterminated strings, unterminated block comments, and
malformed numeric literals. Failed results do not expose a parser-facing
partial token stream.

For unterminated strings and block comments, the location is the opening
delimiter. Numeric-shape errors are reported at the number's first byte.
Character and unsupported-escape errors identify the offending byte. This is
deterministic and gives future parser/runtime diagnostics a stable anchor.

## Relationship to Navigator

The subsystem is compiled by the normal hosted/native source lists but is not
called by HTML parsing, navigation, rendering, or page loading. There is no
`<script>` execution, event-attribute execution, `window`, `document`, timer,
XHR, fetch, cookie, storage, or TLS/networking change in JS1. Existing
Navigator behavior remains the regression control.

The long-term browser boundary is intentionally layered:

```text
Navigator
  |
  +-- HTML / CSS / Layout
  |
  +-- JavaScript
        |
        +-- source
        +-- lexer
        +-- parser
        +-- runtime
        +-- host bindings
```

The runtime must remain separable from the browser/DOM host. Host bindings are
future policy-controlled capabilities, not an automatic consequence of
parsing source.

## Phase JS2 architecture

Phase JS2 extends the independent subsystem with a parser and indexed AST:

```text
navigator_javascript/
  source.h       non-owning source span
  lexer.h/.cpp   JS1 token and lexer abstractions
  ast.h/.cpp     bounded indexed node storage
  parser.h/.cpp  bounded recursive-descent parser
```

`parser.cpp` consumes the successful JS1 `Token` vector and does not include
Navigator rendering, HTML, networking, or host-object headers. The parser is
deterministic recursive descent with explicit parser-depth, expression-depth,
block-depth, statement, node, parameter, and argument limits. It creates no
runtime values and never evaluates a node.

### AST model and ownership

`AstNodeKind` includes `Program`, `EmptyStatement`, `VariableDeclaration`,
`VariableDeclarator`, `ExpressionStatement`, `BlockStatement`,
`ReturnStatement`, `IfStatement`, `WhileStatement`, `ForStatement`,
`BreakStatement`, `ContinueStatement`, `FunctionDeclaration`, `Identifier`,
`NumericLiteral`, `StringLiteral`, `BooleanLiteral`, `NullLiteral`,
`ThisExpression`, `UnaryExpression`, `BinaryExpression`,
`LogicalExpression`, `AssignmentExpression`, `UpdateExpression`,
`CallExpression`, `MemberExpression`, and `NewExpression`.

The `Ast` owns a contiguous `std::vector<AstNode>` and a contiguous child-index
vector. Every relationship is an `AstNodeId` index; no AST node points into a
temporary parser object. `Ast::root()` identifies the `Program` node, and
`Ast::reset()` clears the complete tree and child storage deterministically.
Node IDs remain stable as vector indices even if the backing vector grows.

Nodes retain `SourceLocation` values from lexer tokens. Literal and identifier
spelling is a source slice obtained from the authoritative `SourceView`; the
AST does not copy or own source bytes. The caller must keep the source buffer
alive while inspecting `Ast::sourceSlice()` or `Ast::nodeText()`. Resetting or
destroying the AST releases all node/index storage but does not release the
caller-owned source.

### Supported grammar

JS2 supports the bounded subset needed by the host tests and representative
programs:

- `var` declarations, including multiple declarators and initializers
- expression and empty statements, blocks, `return`, `if`/`else`, `while`,
  representative `for`, `break`, and `continue`
- named function declarations with ordered identifier parameters and block
  bodies
- identifiers, numeric/string/boolean/null literals, `this`, unary `!`, `+`,
  and `-`
- arithmetic, equality, strict equality, relational, logical, assignment and
  compound-assignment operators
- prefix/postfix `++` and `--`, with identifier/member target checks
- dot and computed member access, calls with bounded argument lists, and
  bounded `new` expressions

Function expressions, arrays, object literals, regular-expression literals,
conditional expressions, comma expressions, bitwise operators, `typeof`,
`void`, `delete`, `let`, `const`, labels, `switch`, exceptions, and other
ECMAScript features remain deliberately unsupported. JS1's conservative
string-escape and ASCII-identifier policy also remains in force.

### Precedence and associativity

The parser uses this precedence order, from lowest to highest:

```text
assignment       right associative
logical OR       left associative
logical AND      left associative
equality         left associative
relational       left associative
additive         left associative
multiplicative   left associative
unary            right associative
postfix/update
member/call
primary
```

Thus `a + b * c` stores an additive root with a multiplicative right child,
`a || b && c` stores an OR root with an AND right child, and `a = b = 5`
stores a right-nested assignment tree. Parentheses affect the stored shape and
the resulting composite node location.

Assignment and update targets are checked structurally. Only identifiers and
member expressions are valid targets, so `1 = x`, `++1`, and `foo()++` fail
with `InvalidAssignmentTarget` rather than producing misleading AST nodes.

### Semicolon policy

JS2 requires explicit semicolons after variable declarations, expression
statements, `return`, `break`, and `continue`. Blocks and control-flow or
function declarations do not require a trailing semicolon. Full JavaScript
Automatic Semicolon Insertion is not implemented or claimed; a missing
required semicolon is a deterministic parser error.

### Limits and parser errors

The default parser limits are finite and configurable:

| Resource | Default limit |
| --- | ---: |
| AST nodes | 16,384 |
| parser recursion depth | 256 |
| statements | 4,096 |
| function parameters | 64 |
| call/new arguments | 64 |
| block nesting | 128 |
| expression nesting | 256 |

The parser reports `LexerFailure`, `UnexpectedToken`, `ExpectedToken`,
`InvalidExpression`, `InvalidAssignmentTarget`, `UnexpectedEndOfInput`,
`AstNodeLimitExceeded`, `NestingLimitExceeded`, `TooManyStatements`,
`TooManyParameters`, `TooManyArguments`, and `AllocationFailure`. Every error
contains a source location derived from the token stream (or the authoritative
source extent for a bounded missing EOF). Failed results clear partial AST
storage. Malformed input, limit exhaustion, and repeated parsing of the same
input are bounded and deterministic; no evaluator, host callback, or assertion
is involved.

## Phase JS3 bounded execution core

Phase JS3 extends the same independent `navigator_javascript/` subsystem with
runtime values, one global `var` environment, and a bounded evaluator:

```text
source -> lexer -> tokens -> parser -> indexed Ast
                                      |
                                      v
                              RuntimeContext
                              /      |       \
                         Value  Environment  evaluator
                                      |
                                bounded state
```

`RuntimeContext::execute(SourceView)` is the end-to-end API. The context first
copies a valid source view within the lexer source limit, then runs the
existing JS1 lexer and JS2 parser, and finally evaluates the resulting `Ast`.
Lexical, parse, runtime, and execution-budget failures are separate result
statuses. The context owns the copied source, AST, environment, runtime
strings, execution step counter, final expression value, and current error.
`reset()` clears all script state, including strings and bindings, while
retaining configured limits. This makes separate future page-owned contexts
possible without hidden mutable globals.

### Values and ownership

`Value` is an explicit tagged primitive with these types:

```text
Undefined, Null, Boolean, Number, String
```

Numbers use IEEE-754 `double`. Strings are not source slices: each newly
created runtime string is copied into the context-owned string store and a
typed `RuntimeStringId` is kept in the `Value`. String handles are valid until
their owning context is reset or destroyed. The environment owns binding names
and values. Repeated `var` declarations reuse the existing entry, so
redeclaration cannot grow duplicate bindings; an initializer assigns its value
after declaration instantiation. JS3 has no artificial block
scope; all `var` bindings are in the one global environment.

`var x;` creates a binding containing `Undefined`, which is distinct from an
absent binding. JS1 keeps `undefined` as an identifier token for compatibility;
JS3 resolves an unshadowed identifier with that spelling to the primitive
`Undefined`, while a real binding named `undefined` takes precedence.

### Number, truthiness, and coercion semantics

Arithmetic uses `double` operations for `+`, `-`, `*`, `/`, and `%`. Division
by zero follows the IEEE/JavaScript primitive result policy: non-zero divided
by zero produces signed infinity and zero divided by zero produces `NaN`.
Negative zero is preserved by the value representation and is falsy. `NaN` is
represented and is falsy; strict numeric equality with `NaN` is false. Numeric
literal and primitive string-to-number parsing is manual and ASCII-based, not
locale-sensitive. Strings support the JS1 decoded escape subset.

Falsy values are `Undefined`, `Null`, `false`, `0`, negative zero, `NaN`, and
the empty string. Non-zero numbers, `true`, and non-empty strings are truthy.
Unary `!`, unary `+`, and unary `-` use these explicit primitive rules.

`+` concatenates when either operand is a string. The current deterministic
primitive string forms are `undefined`, `null`, `true`/`false`, JS-style
number text, and the string itself, so for example `"answer=" + 42` produces
`String("answer=42")`. Other arithmetic and relational operators convert the
current primitive subset to numbers; malformed numeric strings produce `NaN`.
Relational comparison is numeric in JS3 rather than full ECMAScript string
ordering.

Strict equality is type-sensitive: `null === null` is true, `null ===
undefined` is false, and `1 === "1"` is false. Loose equality deliberately
implements only the documented primitive subset: `Undefined == Null`,
boolean-to-number conversion, and number/string numeric conversion. It is not
silently treated as strict equality and does not claim full ECMAScript abstract
equality.

Logical `&&` and `||` short-circuit and return an operand, so the right-hand
side is not evaluated when the left-hand truthiness decides the result.

### Executable AST and runtime-unsupported AST

JS3 executes `Program`, `EmptyStatement`, `VariableDeclaration`,
`VariableDeclarator`, `ExpressionStatement`, `BlockStatement`, `IfStatement`,
`WhileStatement`, `ForStatement`, `BreakStatement`, `ContinueStatement`,
identifiers, number/string/boolean/null literals, unary expressions, binary
expressions, logical expressions, assignment expressions, and update
expressions. It supports identifier assignment and compound assignment plus
prefix/postfix `++` and `--`. Blocks use the same global `var` environment.

The parser still accepts several constructs so later phases have stable AST
shapes, but JS3 deliberately does not execute them. Function declarations and
calls, `return`, `this`, member access (including computed access), `new`, and
member/property assignment return explicit runtime errors. Top-level `break`
and `continue` return `IllegalBreak` and `IllegalContinue`; top-level `return`
returns `IllegalReturn`. JS4 owns function values, call frames, parameters,
lexical scope, and return propagation.

### Bounded execution and limits

Every evaluated AST node consumes one step from a single context-wide budget.
The counter is never reset by a loop. The default budget is 100,000 steps and
exhaustion returns `ExecutionBudgetExceeded`, allowing `while (true) {}` and
`for (;;) {}` to terminate without hanging the host. The result exposes the
deterministic step count.

The effective JS3 defaults are finite and configurable:

| Resource | Default limit |
| --- | ---: |
| Source bytes | 1 MiB |
| Emitted tokens, including EOF | 8,192 |
| Token/literal span | 64 KiB |
| AST nodes | 16,384 |
| Parser recursion depth | 256 |
| Statements | 4,096 |
| Function parameters | 64 |
| Call/new arguments | 64 |
| Block nesting | 128 |
| Expression nesting | 256 |
| Runtime bindings | 256 |
| Binding-name length | 256 bytes |
| Runtime string length | 64 KiB |
| Total runtime string bytes | 256 KiB |
| Runtime string values | 4,096 |
| Execution steps | 100,000 |

There is no silent truncation. Binding and string exhaustion fails
deterministically. Runtime errors carry a fixed error code and the source
offset, line, and column from the authoritative AST node where available.
The categories include `UnknownIdentifier`, `InvalidAssignmentTarget`,
`InvalidOperandType`, `UnsupportedFeature`, `BindingLimitExceeded`,
`BindingNameTooLong`, `StringLimitExceeded`, `ExecutionBudgetExceeded`,
`IllegalBreak`, `IllegalContinue`, `IllegalReturn`, `InvalidAstState`, and
`AllocationFailure`. Ordinary evaluator control flow uses explicit boolean and
control-status propagation rather than C++ exceptions.

**guideXOS now has a standalone bounded JavaScript execution core, but
Navigator web pages still do not execute JavaScript.**

### JS3 validation policy

Tier 1 is the mandatory isolated proof: JS1 lexer regressions, JS2 parser
regressions, JS3 runtime tests, strict MinGW/g++ warning-as-error builds,
runtime resource/error tests, deterministic budget termination, build-list
integration, and `git diff --check`. Tier 2 attempts the normal guideXOS and
Navigator builds, hosted/bare-metal smoke, QEMU where available, and existing
HTTP/HTTPS/TLS controls. Inherited Mbed TLS, MSBuild, or QEMU environment
blockers remain documented and are not bypassed or weakened for this isolated
runtime phase.

### Validation tiers

Tier 1 is the mandatory isolated JavaScript-engine gate for JS3: focused
lexer, AST, and runtime tests, MinGW/g++ tests, MSVC `/W4 /WX` tests,
deterministic limits and malformed-input coverage, source/build-list integration, and
`git diff --check`. Tier 2 is the Navigator integration gate: the full guideXOS
build, hosted Navigator regression, bare-metal/QEMU regression, layout/image/
resource regression, and HTTP/HTTPS/TLS regression. Tier 2 is attempted when
practical and becomes mandatory before JavaScript affects page execution or
browser behavior. Existing Mbed TLS dependency/profile and QEMU/full-project
environment failures remain inherited integration blockers for this isolated
phase; they do not weaken or bypass TLS/security requirements.

The JS3 runtime remains standalone. There is no `<script>` execution,
`window`, `document`, DOM binding, events, timers, networking API, or
page-loading hook.

## Phase JS4: functions, call frames, and lexical scope

Phase JS4 extends the same standalone JS3 evaluator. It does not create a
second execution engine:

```text
source -> lexer -> bounded AST -> RuntimeContext
                              -> tagged Value
                              -> global environment
                              -> function value
                              -> call frame
                              -> indexed lexical environment
                              -> return/control status
```

The standalone guideXOS JavaScript engine can execute user-defined functions,
but Navigator web pages still do not execute JavaScript.

### Function values and identity

`ValueType::Function` is an explicit tagged value. Its payload is a stable
`RuntimeFunctionId` into the context-owned function table. A function record
contains the stable `AstNodeId` of its parsed `FunctionDeclaration` and the
`EnvironmentId` of the lexical environment captured at declaration
instantiation. It never stores raw function source or a pointer into a
temporary parser object.

Copying a function value copies its ID, so two reads of the same binding compare
equal with strict equality. Function instances created by nested declarations
in separate invocations receive different IDs. Function values are ordinary
values: they can be assigned to variables and called through any callee
expression that evaluates to a function.

### Call frames and calls

Calls evaluate the callee first, reject non-functions with `NotCallable`, then
evaluate arguments from left to right. The bounded argument vector is passed
to an explicit call frame, which records the function ID, caller and callee
environment IDs, call-site location, and current depth. Parameters bind in
order; a missing argument binds `Undefined`. Extra arguments are still
evaluated left to right and then ignored because the `arguments` object is not
implemented. A function's body executes in a fresh environment and its return
value is propagated to the call expression. Falling off the end and `return;`
both produce `Undefined`.

The evaluator keeps call control explicit. `Return` is a control status that
travels through blocks, conditionals, and loops until the current function
invocation consumes it. There is no global mutable return flag. Frame cleanup
restores the caller environment on normal return, runtime error, call-depth
failure, and execution-budget failure.

### Lexical environments and closures

The global `Environment` remains the authoritative root and retains the JS3
public lookup behavior. Function environments are indexed records with a
parent `EnvironmentId`. Lookup walks the current environment and then its
lexical parents. Assignment updates the nearest existing binding, so an
assignment without a nearer local can modify a global or outer function
binding. A local `var` shadows that outer binding.

`var` is function-scoped. Declaration instantiation scans bounded statement
trees through blocks and control-flow bodies, but never scans into a nested
function body. Function environments are fresh for every invocation, so local
state is not accidentally shared. Direct nested function declarations capture
their active lexical environment. Environment records are retained in a
bounded context-owned pool until `RuntimeContext::reset()`; this makes escaped
closures safe as well as active nested calls. The pool uses IDs rather than
raw parent pointers, so no captured environment can dangle.

The supported declaration policy is deliberately conservative: declarations
at program level and directly in a function body are supported. A function
declaration reached through an `if`, `while`, `for`, or nested block is
rejected with `UnsupportedFunctionConstruct`; Annex B and full block-function
semantics are not claimed.

### Declaration instantiation and `var`

Before executing a program or function body, JS4 establishes direct `var`
bindings as `Undefined`, then installs the applicable direct function
declarations. A later function declaration of the same name is the applicable
one. Repeated `var` declarations do not erase an existing value; an initializer
assigns its evaluated value afterward. This supports both:

```javascript
var result = add(2, 3);
function add(a, b) { return a + b; }
```

and:

```javascript
x = 5;
var x;
```

This is a bounded declaration subset, not a claim of complete ECMAScript
hoisting or Annex B compatibility.

### Limits and errors

JS1, JS2, and JS3 limits remain in force. JS4 adds these finite defaults:

| Resource | Default |
| --- | ---: |
| Function-environment bindings | 256 |
| Function values | 4,096 |
| Retained environments, including global | 256 |
| Call depth | 64 |
| Execution steps shared across all calls | 100,000 |

The existing defaults remain 256 global bindings, 256-byte binding names,
64 parameters, 64 call arguments, 64 KiB runtime strings, 256 KiB total
runtime string bytes, and 4,096 runtime string values. Environment and
function limits are configurable through `RuntimeLimits`. The shared
execution budget is never reset by a call; recursion and loops consume the
same context-wide counter.

JS4 adds deterministic `NotCallable`, `CallDepthExceeded`,
`EnvironmentLimitExceeded`, `FunctionLimitExceeded`, `InvalidFunction`, and
`UnsupportedFunctionConstruct` errors. They retain the authoritative AST
offset, line, and column. `IllegalReturn` continues to reject top-level
`return`.

### Deliberately unsupported constructs

Function expressions, arrow functions, async functions, generators, default or
rest parameters, destructuring, methods, classes, spread arguments, and the
`arguments` object remain unsupported. Objects, arrays, member reads/writes,
member calls, `new`, and browser `this` semantics remain outside JS4. The
standalone runtime does not expose `window`, `document`, the DOM, events,
timers, networking APIs, storage, cookies, or `console`.

### JS4 validation boundary

Tier 1 proves primitive regressions, function values and identity, declarations,
calls through values, ordered arguments, missing and extra arguments, fresh
locals, nested lexical lookup, shadowing, outer assignment, returns,
recursion, call-depth protection, shared execution budget, non-callable
errors, closure lifetime, reset cleanup, and strict MinGW/g++ warning-as-error
builds. Tier 2 remains an attempted guideXOS/Navigator integration check; it
does not change page behavior and does not weaken unrelated Mbed TLS, MSVC,
or QEMU requirements.

## Roadmap

```text
JS lexer
  -> bounded parser / AST
  -> runtime values
  -> expressions/statements
  -> scopes/functions
  -> objects/arrays
  -> built-ins
  -> Navigator host interface
  -> DOM access
  -> events
  -> timers
  -> increasingly capable web APIs
```

The next milestone is **Phase JS5 — Objects, Arrays & Property Semantics**:
object values, stable object identity, bounded property storage, arrays,
indexed elements, object and array literals, member reads and assignments,
computed members, array length, and function/object interaction. JS5 still
must not expose the DOM itself. Full ECMAScript compatibility is not promised.
