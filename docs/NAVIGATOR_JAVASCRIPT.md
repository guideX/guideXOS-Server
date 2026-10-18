# guideXOS Navigator JavaScript

## Purpose and current boundary

Navigator currently has a reusable HTML document/parser layer in
`guide_web_document.*` and `guide_web_html_parser.*`, with Navigator-specific
navigation and rendering in `navigator.*`. The independent JavaScript
subsystem now contains a bounded lexer, parser/AST, runtime core, and the JS8
controlled document bridge described below. It has no general event system,
but JS9–JS13 provide a deliberately bounded inline-script and synchronous
click-callback path. The existing HTML parser preserves only the bounded
inline script sources needed by that Navigator path; this is not browser
script compatibility.

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

Named function expressions, arrays, object literals, regular-expression literals,
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

Anonymous function expressions are added only as the narrow JS9 `onclick`
prerequisite. Named function expressions, arrow functions, async functions,
generators, default or rest parameters, destructuring, methods, classes, spread
arguments, and the `arguments` object remain unsupported. Objects, arrays, member reads/writes,
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

## Phase JS5: objects, arrays, and property semantics

JS5 extends the same standalone evaluator with native JavaScript object and
array values. It does not create a second runtime model:

```text
source -> lexer -> bounded AST -> RuntimeContext
                              -> tagged Value
                              -> object ID -> context-owned object record
                              -> own properties / bounded array elements
```

`ValueType::Object` carries a stable `RuntimeObjectId`. The ID is an index into
a context-owned object pool and is valid only until `RuntimeContext::reset()`.
Object records are never addressed through pointers retained by values, so
vector relocation cannot invalidate an alias. Ordinary objects and arrays are
both runtime-owned objects; an object record has an explicit array kind and
arrays use a separate bounded dense element vector.

The property model is deliberately small: own enumerable data properties only,
represented by a string key and a `Value`. Replacing an existing key updates
the existing entry, so duplicate object-literal keys and repeated assignment
do not create unreachable duplicate entries. Object literals evaluate and
initialize properties left-to-right, with the last duplicate assignment
winning. String and identifier literal keys are supported; computed literal
property names, methods, getters, setters, descriptors, and spread are not.

Member reads support both `obj.x` and `obj[key]`. Missing own properties return
`Undefined`. Member writes create or replace own properties. Computed keys
support String, Number, Boolean, Null, and Undefined values using bounded,
locale-independent primitive spelling; `-0` becomes `"0"`. Function and
object keys are rejected with `InvalidPropertyKey`. A plain object's numeric
key is therefore shared with its string spelling, while an array treats only
canonical decimal spellings (`0` or a non-zero decimal string without leading
zeroes) within the configured index bound as dense indices. Spellings such as
`"01"`, `"-1"`, and `"1.5"` remain ordinary string properties.

Arrays expose a special read-only `length` value. Literal elements and indexed
writes use a bounded dense-growth policy: assigning beyond the current length
fills intervening positions with `Undefined`, then sets `length` to index plus
one. Array ordinary string properties are supported, but `length` is not a
writable ordinary property. Indexed reads outside the current length return
`Undefined`; absurd canonical indices fail with `ArrayIndexOutOfRange`, and
growth failures use `ArrayLimitExceeded`.

Objects and arrays are passed through bindings, parameters, returns, and
closures by identity. A function-valued property can be read into a variable
and called. Member-call syntax is intentionally rejected with
`UnsupportedFeature`, because JS5 does not define a receiver or `this`
policy. Primitive boxing is also unsupported, so property access on primitive
values fails with `NotObject` (and access on Null or Undefined fails with
`CannotReadProperty`).

The object pool, property storage, array storage, strings, functions, and
retained environments are all owned by the runtime context and retained until
reset. There is no object garbage collector yet: unreachable objects can
remain in the bounded pool until reset. Failed allocation or limit checks do
not expose a partially initialized value. JavaScript objects remain separate
from future Navigator host objects; JS5 does not expose `window`, `document`,
DOM wrappers, page scripts, events, timers, networking APIs, storage, cookies,
or console.

### Effective JS1-JS5 limits

All limits are finite and configurable through `LexerLimits`, `ParserLimits`,
or `RuntimeLimits`:

| Resource | Default limit |
| --- | ---: |
| Source bytes | 1 MiB |
| Emitted tokens, including EOF | 8,192 |
| Token/literal span | 64 KiB |
| AST nodes | 16,384 |
| Parser recursion depth | 256 |
| Statements | 4,096 |
| Function parameters | 64 |
| Call arguments | 64 |
| Block nesting | 128 |
| Expression nesting | 256 |
| Object-literal properties | 256 |
| Array-literal elements | 1,024 |
| Global bindings | 256 |
| Function-environment bindings | 256 |
| Binding-name length | 256 bytes |
| Runtime string length | 64 KiB |
| Total runtime string bytes | 256 KiB |
| Runtime string values | 4,096 |
| Function values | 4,096 |
| Retained environments, including global | 256 |
| Call depth | 64 |
| Live context-owned objects | 1,024 |
| Own properties per object | 256 |
| Total own properties | 4,096 |
| Property-name length | 256 bytes |
| Total property-key bytes | 256 KiB |
| Elements per array | 1,024 |
| Total array elements | 4,096 |
| Maximum dense array index | 1,023 |
| Shared execution steps | 100,000 |

Object/property/array errors are explicit and source-located:
`NotObject`, `CannotReadProperty`, `CannotWriteProperty`,
`ObjectLimitExceeded`, `PropertyLimitExceeded`, `PropertyNameTooLong`,
`ArrayLimitExceeded`, `ArrayIndexOutOfRange`, and `InvalidPropertyKey`.
Object and array creation plus property operations consume the existing shared
execution budget; they do not have an unbounded side path.

**The standalone guideXOS JavaScript engine now supports native JavaScript objects and arrays, but Navigator web pages still do not execute JavaScript and no DOM objects are exposed.**

### JS5 validation boundary

Tier 1 proves object identity, own-property reads and writes, computed keys,
missing-property `Undefined`, object literals, arrays, indexed assignment,
length and bounded growth, nested members, function arguments and returns,
function-valued properties, closure/object retention, limit failures, reset
cleanup, all JS1-JS4 regressions, strict warning-as-error compilation, and
deterministic execution-budget behavior. Tier 2 remains an attempted normal
guideXOS/Navigator build and smoke check; JS5 does not alter Navigator page
execution or browser behavior.

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

### Phase JS6: core built-ins, native functions, and prototypes

JS6 extends the existing bounded runtime in place. It does not introduce a
second evaluator or a host/browser object path:

```text
Value(Function)
  -> stable RuntimeFunctionId
  -> User declaration or bounded NativeFunctionId

Value(Object)
  -> stable RuntimeObjectId
  -> own properties / array elements / prototype RuntimeObjectId
```

User functions and native functions are both ordinary callable `Function`
values. Native records use a fixed enum-like callable ID and are installed once
per context, so repeated reads such as `Math.abs === Math.abs` preserve
identity. Calls evaluate arguments left-to-right before invocation, bind
missing user parameters to `Undefined`, respect the parser argument bound, and
propagate native failures through the same `RuntimeError` model. Native entry
and bounded internal argument work consume the shared execution budget and
native calls participate in call-depth accounting.

Member calls preserve the base object as a receiver. In `obj.method()`, both
user and native functions receive `this = obj`; extracting `var f = obj.method`
and calling `f()` does not retain the receiver. A plain call uses
`this = Undefined`, including calls through a higher-order function. Top-level
`this` follows the same standalone policy. This is deliberately not browser
global-object behavior.

Runtime objects have an optional ID-based prototype link. Reads search the own
property first, then each prototype, and finally return `Undefined`. Writes
always create or update an own property, so assigning `a.push = 5` shadows the
shared method without changing another array. Traversal is bounded by
`maxPrototypeDepth` (32 by default); overflow and cyclic chains terminate with
`PrototypeChainExceeded`, and every visited link consumes execution work.
There are no script-facing prototype mutation APIs yet.

Fresh contexts initialize these bounded built-ins in deterministic order:

```text
Object.prototype -> null
Array.prototype  -> Object.prototype
Math             -> Object.prototype
array instance   -> Array.prototype
ordinary object  -> Object.prototype
```

`Object.prototype.hasOwnProperty` is the minimal shared object method.
`Array.prototype.push` and `pop` are receiver-checked native methods. `push`
supports multiple already-evaluated arguments and uses an all-or-nothing
capacity check; `pop` returns `Undefined` for an empty array. `Math` is a
normal runtime object with `abs`, `min`, `max`, `floor`, `ceil`, and `round`.
`Math.min()` and `Math.max()` return positive and negative infinity,
respectively. Numeric coercion uses the JS3 primitive rules; `round` uses the
host-independent `std::round` policy (ties away from zero). `Math.random` is
absent. Primitive strings support the direct read-only `length` property and
are not boxed.

Built-in initialization consumes ordinary bounded resources: three runtime
objects, nine own properties, one global `Math` binding, and nine native
function values are installed by default. User functions remain bounded by
`maxFunctions` (4,096); native functions use the separate
`maxNativeFunctions` limit (64). If object, property, binding, native-function,
or related limits cannot accommodate initialization, reset reports
`BuiltInInitializationFailed` and clears the partial state. Successful reset
recreates the same built-in IDs and prototype graph. All values remain owned
by the context until reset; there is no garbage collector.

The effective JS1-JS6 defaults are finite:

| Resource | Default limit |
| --- | ---: |
| Source bytes | 1 MiB |
| Emitted tokens, including EOF | 8,192 |
| Token/literal span | 64 KiB |
| AST nodes | 16,384 |
| Parser recursion depth | 256 |
| Statements | 4,096 |
| Function parameters | 64 |
| Call arguments | 64 |
| Block nesting | 128 |
| Expression nesting | 256 |
| Global bindings | 256 |
| Function-environment bindings | 256 |
| Binding-name length | 256 bytes |
| Runtime string length | 64 KiB |
| Total runtime string bytes | 256 KiB |
| Runtime string values | 4,096 |
| User function values | 4,096 |
| Native function values | 64 |
| Retained environments, including global | 256 |
| Logical call depth | 64 |
| Live context-owned objects | 1,024 |
| Own properties per object | 256 |
| Total own properties | 4,096 |
| Property-name length | 256 bytes |
| Total property-key bytes | 256 KiB |
| Elements per array | 1,024 |
| Total array elements | 4,096 |
| Maximum dense array index | 1,023 |
| Prototype-chain depth | 32 |
| Shared execution steps | 100,000 |

JS6 Tier 1 covers native identity and values, argument order, native errors,
receiver-aware user/native calls, detached method behavior, prototype
structure and shadowing, bounded lookup/cycle safety, built-in resource
failure, array push/pop and receiver validation, Math integration, primitive
string length, reset/lifetime behavior, and JS1-JS5 regressions. The focused
suite is `scripts/smoke-navigator-javascript-js6.ps1`.

**The standalone guideXOS JavaScript engine now supports native built-ins,
receiver-aware method calls, and prototype-backed properties, but Navigator web
pages still do not execute JavaScript and no DOM/browser objects are exposed.**

Constructors and `new` remain unsupported, as do `Object.create`,
`Object.setPrototypeOf`, `__proto__`, browser globals, DOM objects, events,
timers, networking APIs, storage, cookies, and page-script execution. The
prototype/native machinery is intentionally reusable by the future Navigator
host-object adapter without enabling that adapter in JS6.

The next milestone is **Phase JS7 — Script Host Contract & Navigator
Integration Boundary**: a generic bounded host-object interface, host
properties and receiver-aware host methods, and page/realm lifetime controls.

### Phase JS7: bounded script-host contract

JS7 adds a generic boundary between the JavaScript runtime and an externally
owned synchronous host adapter. It does not add a second evaluator and it does
not put Navigator-specific conditions in the evaluator:

```text
JavaScript source
  -> lexer / parser / AST
  -> RuntimeContext evaluator
  -> Value(HostObject)
  -> bounded host registry
  -> HostAdapter
  -> external page or application state
```

`ValueType::HostObject` is distinct from `ValueType::Object`. A host value
contains only an opaque `RuntimeHostObjectId`; it never contains a Navigator
pointer or any other external pointer. The ID encodes a registry slot and a
realm generation. Host objects therefore remain aliases to external state,
while ordinary object literals and arrays continue to use the JS object pool.
Host objects are truthy, compare strictly by stable handle identity, and do
not use ordinary JS prototype chains.

#### Adapter and ownership contract

`navigator_javascript/host.h` defines the small `HostAdapter` interface:

```text
validate(reference)
getProperty(reference, name, result)
setProperty(reference, name, value)
call(optional receiver, methodId, arguments, result)
```

The adapter is externally owned and must outlive its `RuntimeContext`. The
context owns the bounded registry, host handles, host method wrappers, and
JavaScript values. It never destroys the adapter or the host objects behind
it. Multiple contexts may use different adapters without process-global host
state. `HostObjectReference` carries an adapter-owned instance ID and kind
plus the context generation supplied by the runtime.

`RuntimeContext::installHostGlobal("host", instance, kind, error)` is the
embedding API. The language does not hardcode the name `host`; a future
embedding can install `document` through the same mechanism. Host globals are
ordinary environment bindings whose values are HostObjects. `reset()` clears
the host registry and host globals, and `execute()` retains the existing JS
reset semantics while replaying explicitly installed host-global descriptors
for that next execution.

#### Properties and methods

Host property reads and writes use the same member-expression path as ordinary
objects. The runtime validates the handle, charges one host operation, and
asks the adapter. A missing read returns `Undefined`; a missing write is
decided by the adapter and is never silently converted into a JS shadow
property. The test adapter permits `counter`, `name`, and child `value`, and
rejects `readOnlyValue` with `HostPropertyReadOnly`.

Host methods are returned as cached ordinary callable `Function` values backed
by the existing function dispatch path. The adapter returns a stable method ID
and says whether it requires a receiver. `host.increment()` supplies the
HostObject receiver; extracting it and calling `f()` supplies `Undefined` and
returns `InvalidReceiver`. The test adapter's `add` method is explicitly
receiver-independent, so `var f = host.add; f(4, 5)` is valid. Repeated reads
of the same method on the same host handle preserve function identity.

Host errors are source-located runtime errors, including
`InvalidHostObject`, `StaleHostObject`, `HostPropertyReadOnly`,
`HostPropertyWriteFailed`, `HostCallFailed`, `HostOperationBudgetExceeded`,
`HostReentryUnsupported`, `HostObjectLimitExceeded`, and
`InvalidHostReturn`. Host adapter failures after a partial mutation are not
rolled back by the runtime; adapters own their transaction semantics.

#### Value conversion

The boundary supports Undefined, Null, Boolean, Number, String, ordinary JS
Object IDs, and HostObject references. Ordinary JS objects passed to a host
call are synchronous read-only identity views represented by a
`RuntimeObjectId`; JS7 does not deep-copy arbitrary graphs. Functions are not
passed to the test adapter. Host-returned strings are copied immediately into
the context-owned bounded string store, so adapter stack or temporary string
storage cannot escape. Host-returned HostObjects are registered and validated
before becoming JavaScript values. Invalid object IDs, stale generations,
oversized strings, and malformed host returns fail deterministically.

#### Identity, registry, and lifetime safety

The registry has a finite `maxHostObjects` bound. Dead slots may be reused,
but a reused handle carries the new generation and cannot match an old value.
`invalidateHostGeneration()` marks all prior entries dead and advances the
generation. Any old property read, write, host method call, or closure access
then fails as `StaleHostObject`; it can never observe a new object that reused
the slot. Reset advances the generation as well as clearing the registry,
globals, method wrappers, and host operation counters.

The intended future page model is:

```text
Page A: RuntimeContext A + HostGeneration A
  navigate -> invalidate A -> reset/destroy realm
Page B: RuntimeContext B + HostGeneration B
```

The generic contract is context-owned, so a future controlled Navigator
harness can keep one realm for multiple explicitly submitted scripts and
share its bindings, functions, host identities, and page generation. JS7 does
not implement script-tag sequencing or navigation execution.

Host operations have their own finite budget of 10,000 by default. Reads,
writes, calls, and host-object registration/returned-object lookup consume
that budget in addition to the shared 100,000 execution-step budget. Adapter
calls are synchronous and non-reentrant. There are no async callbacks,
timers, promises, events, network operations, or host-to-JS callbacks.

#### Navigator boundary and deliberate non-goals

Before JS8, `navigator_script_host.h/.cpp` provided only an inert
Navigator-facing adapter shell. JS8 extends that boundary with the deliberately
small real-document mapping described below. The evaluator still knows only
`HostAdapter`, never `WebDocument`, `Navigator`, layout nodes, or property names
such as `textContent`.

The JS7-to-JS8 mapping is:

```text
document                         -> HostObject
DOM element                      -> HostObject
element.textContent              -> HostProperty
element.getAttribute(...)        -> HostMethod
document.getElementById(...)     -> HostMethod returning HostObject
```

JS7 deliberately does not expose a full DOM, browser globals (`window`,
`document`, `location`, `navigator`, `history`, `console`), page JavaScript,
HTML `<script>` execution, inline event attributes, timers, async I/O,
networking, storage, or DOM mutation. Existing HTML script tags remain inert.

The required bounded proof is in
`scripts/smoke-navigator-javascript-js7.ps1` and
`tests/navigator_javascript_js7_test.cpp`. It covers host reads and writes,
strings, missing properties, read-only errors, child and method identity,
receiver-aware and detached calls, host values through functions and
closures, ordinary-object independence, stale generation after slot reuse,
stale closure access, host limits, operation budgets, reset, invalid adapter
returns, and multiple-context isolation.

#### Effective JS7 additions to the finite limits

The JS1-JS6 limits above remain unchanged. JS7 adds:

| Resource | Default limit |
| --- | ---: |
| Live HostObject registry entries | 1,024 |
| Host property-name length | 256 bytes |
| Host operations per execution/context phase | 10,000 |
| Host generations | 4,096 |
| Cached host method values | 64 |
| Host method arguments | 64, via the existing call-argument bound |
| Host reentry depth | 0; synchronous adapter calls are non-reentrant |

All host IDs, slots, generations, method wrappers, property names, strings,
arguments, adapter spans, and operation counts remain bounded. The focused JS7
test is compiled with strict C++17 `-Wall -Wextra -Werror -pedantic` settings.

**The guideXOS JavaScript runtime can now communicate through a bounded generic
script-host contract, but Navigator still does not automatically execute page
JavaScript and no full DOM API is exposed.**

## Phase JS8: minimal document host and controlled execution

JS8 is the first intentional JavaScript-to-Navigator document mutation
milestone. The bridge is implemented by
`navigator_javascript/navigator_script_host.*` and uses the JS7 `HostAdapter`
boundary:

```text
WebDocument / structural element serial
        ↑
NavigatorScriptHostAdapter
        ↑
RuntimeContext HostObject registry
        ↑
controlled JavaScript source
```

The existing Navigator document is a bounded compact model, not a retained
general-purpose DOM. `WebDocument::structuralElements` is the authoritative
node metadata table; its `HtmlElementRef::serial`, parent serial, tag name, and
ID are the node identity used by JS8. Renderable text remains in the existing
`WebInlineItem`/`DocBlock` streams. JS8 adds no JavaScript-side tree and no
raw node pointer to a JavaScript `Value`.

The controlled realm installs one `document` HostObject through
`RuntimeContext::installHostGlobal`. Its fixed adapter instance is paired with
the current JS7 host generation. A returned Element HostObject carries the
authoritative structural serial and the same generation. Runtime registry
deduplication therefore gives repeated lookup of one node stable strict
identity, while a generation advance makes every old document and element
handle stale. `RuntimeContext::reset()` is the normal replacement boundary;
`invalidateHostGeneration()` is also available for the explicit stale-closure
proof before a new realm is installed.

### Supported surface

The complete JS8 public surface is intentionally only:

```text
document.getElementById(id)

Element.id             read-only String
Element.tagName        read-only uppercase canonical String
Element.textContent    read/write bounded String/primitive text
```

`getElementById` requires a document receiver, accepts only a bounded String ID,
searches the authoritative structural-element table in document order, and
returns `Null` when no ID matches. It never returns `Undefined` for a missing
element. The traversal is capped by the document-node limit. `id` is read-only
because changing it would require safely updating future ID indexes. `tagName`
is canonicalized to uppercase (`DIV`, `SPAN`, `P`). Unknown reads return the
generic JS7 `Undefined`; unknown writes are rejected and do not create shadow
properties. Detached `getElementById` calls fail with `InvalidReceiver`.

`textContent` reads the bounded decoded text runs belonging to the element and
its structural descendants in document order, including bounded forced breaks.
The existing compact parser stream is the source of truth; this is not a
claim of full browser DOM whitespace or node semantics. Where one compact
render block directly owns the element, its normalized parser text is used.
Text aggregation has an explicit operation and byte bound.

Assignments are validated before mutation. String, Number, Boolean, Null, and
Undefined are converted using deterministic primitive spelling (`123` becomes
`"123"`, `null` becomes `"null"`, and `undefined` becomes `"undefined"`).
Arbitrary object/function stringification is not supported. The assignment
bound is 64 KiB by default (and is lowered by any smaller configured runtime
or document bound). A successful write updates the existing `DocBlock` and
inline text stream, increments the document mutation count, and sets the
authoritative `WebDocument::layoutDirty` flag. Oversized or over-budget writes
fail before changing document content; earlier successful writes remain.

### Layout and controlled harness

`NavigatorScriptExecutionHarness` is the deterministic proof path:

```text
parse known HTML fixture
  -> install document HostObject
  -> execute explicit source in the current realm
  -> inspect WebDocument independently
  -> if layoutDirty, request one explicit relayout
```

The harness never reads or executes HTML `<script>` elements. Multiple calls
to its explicit execution API use `RuntimeContext::executeInSameRealm`, which
preserves global bindings, closures, HostObject identity, and shared execution
and host-operation budgets. Same-realm source and AST storage is cumulative and
bounded. Document replacement resets the realm and advances the host
generation, so old handles cannot retarget a new document.

The normal Navigator layout path observes the same `WebDocument::layoutDirty`
signal. Its existing inline-layout rebuild clears the signal only after the
real layout pipeline has consumed the changed document and records a bounded
layout revision/extent. The standalone harness uses the same document model
and an explicit bounded extent checkpoint for Tier 1 proof; it does not copy
renderer code or trigger a re-layout after every property write.

### Deliberate non-goals

The following remain disabled in JS8:

```text
Automatic <script> execution: DISABLED
Inline event handlers: DISABLED
External script loading: DISABLED
window: DISABLED
navigator / guideXOS globals: DISABLED
innerHTML, attributes, style, node creation, DOM traversal: DISABLED
events, addEventListener, timers, async callbacks: DISABLED
location, history, navigation, fetch, XHR, WebSocket: DISABLED
cookies, localStorage, sessionStorage, console: DISABLED
```

Normal page loading therefore remains inert with respect to JavaScript. No
public-site JavaScript compatibility is claimed. JS9 will define tightly
bounded inline `<script>` source preservation and sequencing on this same
document realm; it should not add external scripts, events, timers, or network
execution in its first proof.

### JS8 effective limits

JS1–JS7 defaults remain in force. The consolidated defaults relevant to JS8
are:

| Resource | Default |
| --- | ---: |
| Source bytes | 1 MiB |
| Tokens, including EOF | 8,192 |
| Token/literal bytes | 64 KiB |
| AST nodes per script | 16,384 |
| Parser depth | 256 |
| Statements | 4,096 |
| Function parameters / call arguments | 64 / 64 |
| Block nesting / expression nesting | 128 / 256 |
| Object literal properties / array literal elements | 256 / 1,024 |
| Runtime bindings / function environments | 256 / 256 |
| Runtime environments / functions | 256 / 4,096 |
| Runtime strings / total string bytes | 4,096 / 256 KiB |
| Runtime objects / properties per object / total properties | 1,024 / 256 / 4,096 |
| Runtime arrays / total elements / maximum index | 1,024 / 4,096 / 1,023 |
| Prototype depth | 32 |
| Execution steps | 100,000 |
| Host objects / host operations | 1,024 / 10,000 |
| Host generations / cached host methods | 4,096 / 64 |
| Same-realm cumulative source | 1 MiB |
| Document ID length | 256 bytes |
| Exported Element HostObjects | 1,024 runtime host entries |
| TextContent assignment | 64 KiB |
| Text aggregation operations | 1,024 inline items |
| DOM mutations per controlled document execution | 1,024 |
| Document/node lookup table | 1,024 structural nodes |

All limits are finite and failures are typed. Host operations for document
lookup, property reads/writes, returned Element registration, and method calls
continue to consume the JS7 host-operation budget; DOM code cannot reset or
bypass the 100,000-step execution budget.

The focused proof is
`tests/navigator_javascript_js8_test.cpp`, run by
`scripts/smoke-navigator-javascript-js8.ps1`. It covers real parsed
`WebDocument` lookup and mutation, missing IDs, stable document/element
identity, read-only properties, canonical tags, text reads/writes, primitive
conversion, independent host inspection, dirty/re-layout state, measurable
text extent change, function and closure mutation, stale document/element
handles, stale closures, same-realm scripts, inert page scripts, mutation and
text bounds, and host-operation exhaustion.

**guideXOS Navigator now has a controlled JavaScript-to-document bridge capable
of bounded text mutation, but normal web pages still do not automatically
execute JavaScript.**

## Phase JS9: minimal direct click callbacks

JS9 extends the JS8 Navigator boundary with one deliberately narrow interaction
path. It is not DOM Events compatibility and it is not a general browser event
system.

```text
rendered element
  -> existing Navigator hit test
  -> handleDocumentClick(element block)
  -> NavigatorScriptHostAdapter::dispatchClick(serial)
  -> RuntimeContext::invokeFunctionInSameRealm()
  -> Element.textContent mutation
  -> WebDocument::layoutDirty
  -> existing controlled relayout/repaint
```

### JS9 API and semantics

The supported surface is now:

```text
document.getElementById(id)

Element.id             read-only String
Element.tagName        read-only uppercase String
Element.textContent    read/write bounded String/primitive text
Element.onclick        read/write Function or Null
```

`onclick` accepts a JavaScript function value and installs or replaces the one
direct callback for that element. Reading an unassigned handler returns `null`.
Assigning `null` clears it. Unsupported non-callable values fail with the
bounded `HostInvalidValue` runtime error and do not change an existing handler.
There is no `addEventListener` implementation in JS9.

The callback is invoked with zero arguments. There is no Event object. A
callback replacement made during its own execution becomes effective on the
next click; the currently copied function ID completes once. Reentrant event
dispatch is rejected by the adapter, and relayout never synthesizes another
click. A callback runtime error is contained, recorded in the existing bounded
script diagnostic path, and is not automatically retried.

Navigator dispatches `onclick` before the pre-existing activation behavior. A
link therefore runs its direct callback first and then retains normal link
navigation; form controls retain their existing activation behavior. JS9 adds
no bubbling, capturing, parent traversal, propagation, keyboard, pointer,
hover, touch, timer, asynchronous, promise, or navigation event APIs.

### Same-realm ownership and generation safety

The adapter retains only a `RuntimeFunctionId` plus the stable
`HtmlElementRef::serial` in a fixed callback table. It never retains a raw
function pointer, closure pointer, layout pointer, or temporary host value.
The function ID remains valid because the active `RuntimeContext` owns its
function table and captured environments until the realm is reset. The table
is cleared on document attach/detach, realm reset, and host-generation change.
Navigation resets the runtime and advances its host generation, so old Element
values and old callback records cannot execute against a replacement document.

Inline `<script>` sources are preserved by the bounded HTML parser in source
order and submitted to the existing same realm after the current document host
is installed. At most 16 inline scripts of at most 64 KiB each are retained.
They execute synchronously in source order; the realm is not rebuilt between
scripts or clicks. This page-loading path is separate from the JS8
`NavigatorScriptExecutionHarness`, whose explicit-script API still does not
execute fixture HTML script tags.

### Bounds and cost

The maximum active handler count is 64. Each callback record is 16 bytes on the
current 64-bit ABI (`HostInstanceId` plus `RuntimeFunctionId` and alignment),
so the fixed table contributes 1,024 bytes of static adapter storage. A new
assignment does not allocate another callback record; replacing a handler is a
linear scan of at most 64 records. Dispatch performs one serial lookup and at
most one callback invocation. Runtime function, closure/environment, AST,
source, host-object, operation, document-node, text, mutation, call-depth, and
execution-step limits remain the JS1-JS8 limits listed above; retained function
values remain bounded by the existing 4,096-function runtime limit.

### JS9 proof fixtures and results

`navigator-smoke/javascript-js9.html` contains two rendered buttons. Its
first handler closes over a counter and changes its own `textContent`; the
second changes only its own text. The required sequence is A → A → B → A,
observing `1`, `2`, `B clicked`, and `3`. The hosted smoke extends
`navigator.smoke` with rendered geometry, Navigator form hit-target, callback
count, mutation, layout-revision, clean-layout, and same-realm checks.

The focused JS9 smoke passes with strict C++17 diagnostics. It covers handler
property/readback, function expressions, replacement, non-callable rejection,
matching and non-matching dispatch, closure persistence, DOM mutation and
controlled relayout, independent elements, error containment, self-replacement,
navigation cleanup, stale-generation safety, the 64-record limit, links, and
missing elements. The hosted JS9 smoke is integrated into `navigator.smoke` and
passes on the repaired hosted production path.

Intentional JS9 non-goals remain full EventTarget behavior, `addEventListener`,
event objects, bubbling/capture, `preventDefault`, dynamic DOM creation,
attributes, CSSOM, timers, async work, promises, modules, and navigation APIs.

## Phase JS10: bounded Element.addEventListener click listeners

JS10 adds one narrow extension on the existing JS9 direct-click machinery:

```javascript
element.addEventListener("click", callback)
```

This is a bounded Navigator subset, not browser-complete DOM Events. The
dispatch path remains:

```text
rendered element
  -> existing Navigator hit test
  -> handleDocumentClick(element block)
  -> bounded onclick record lookup
  -> original onclick function, if present
  -> original addEventListener function, if present
  -> RuntimeContext::invokeFunctionInSameRealm()
  -> existing Element.textContent mutation / layout invalidation
  -> existing controlled relayout/repaint
```

### JS10 contract

The supported addition is exactly:

```text
Element.addEventListener("click", Function)
```

Only the exact event name `"click"` is supported. Other event names are
rejected with the existing bounded `HostInvalidValue` runtime error and are
never silently registered. The callback must be a callable JavaScript
function value supported by the runtime; non-callable values fail with the
same error and do not change an existing listener. The Element host method is
receiver-aware, so a method retained from a stale Element cannot register on a
new generation.

The callback is invoked with zero arguments. JS10 does not create a DOM
`Event` object. Dispatch is synchronous and has no event queue, timers,
promises, microtasks, asynchronous callback path, bubbling, capture phase,
propagation path, `stopPropagation`, `stopImmediatePropagation`,
`preventDefault`, or listener option objects. Keyboard, pointer, form,
mousemove, mousedown, mouseup, submit, change, and input event types remain
out of scope.

If an element has both JS9 `onclick` and a JS10 click listener, JS10 preserves
both in the same bounded record and invokes `onclick` first, followed by the
listener. Function IDs are copied before dispatch, so replacement or clearing
during a callback takes effect on the next click. A callback error is handled
by the existing runtime error path; the other callback is still dispatched and
future clicks remain usable.

### Same realm, lifetime, and bounds

The adapter retains only the element serial and runtime-owned function IDs. It
never reparses callback source, stores a JavaScript pointer, recreates a realm,
or recreates a captured environment. The existing RuntimeContext owns the
function and closure environment until the realm is reset, so a closure such
as `count = count + 1` naturally persists across clicks.

JS10 keeps the JS9 fixed table. There are at most 64 element records, each
exactly 16 bytes (`HostInstanceId` plus two `RuntimeFunctionId` values), for a
fixed 1,024-byte callback table. Each record has one JS9 `onclick` slot and
one JS10 click-listener slot. JS10 permits at most 64 listener registrations
per document and at most one listener per element. A duplicate
`addEventListener("click", ...)` call deterministically replaces that
element's existing listener without consuming another listener slot. The
record table itself is shared with `onclick`; if it is full of other element
records, a new element cannot claim another record. No per-click listener
allocation or click-count-proportional storage is introduced.

The receiver-aware `addEventListener` host method uses one bounded cached
runtime wrapper across Element receivers, while the actual receiver is still
validated on every call. This keeps the pre-existing 64 cached-host-method
limit intact and does not add a dynamic dispatch or reflection system.

Document attach/detach, realm reset, host-generation changes, and navigation
clear all listener records. Old Element host objects and old callback values
therefore cannot register into or dispatch against a replacement document.
Stale generation failures use the existing `StaleHostObject` convention.

### JS10 proof fixtures and results

`tests/navigator_javascript_js10_test.cpp`, run by
`scripts/smoke-navigator-javascript-js10.ps1`, covers method exposure,
callable validation, unsupported event rejection, same-realm closure state,
three counter clicks, `onclick` ordering and coexistence, independent
listeners, callback-error containment, stale-generation cleanup, document
replacement cleanup, duplicate replacement, and listener exhaustion. It
proves registrations 1 through 64 succeed, registration 65 fails with the
bounded callback-limit error, and existing listeners continue working.

The hosted `navigator.smoke` path uses
`navigator-smoke/javascript-js10.html` to prove page/script loading, Element
method resolution, physical hit-tested clicks, `onclick` then listener order,
closure persistence `1 -> 2 -> 3`, independent listener state, mutation and
relayout, callback error recovery, ordinary link navigation, and listener
cleanup after navigation. JS10-specific assertions are reported separately
from unrelated CSS smoke phases.

JS10 intentionally does not add `removeEventListener`, multiple listeners per
element, listener options, event objects, event propagation, default-action
control, keyboard or other event types, timers, asynchronous dispatch, or
general browser DOM Events behavior.

## Phase JS11: remove bounded click listeners

JS11 adds the matching narrow removal API:

```javascript
element.removeEventListener("click", callback)
```

The supported signature is exactly `Element.removeEventListener("click",
Function)`. A successful removal returns the runtime's normal `undefined`
result. Matching requires all three values in the bounded registration record:
the target Element's current-generation identity, the exact event type
`"click"`, and JavaScript function identity. Function source text, function
names, and structurally identical function bodies are not compared. Two
function objects with identical code are therefore different callbacks.

Removal is a lookup-only operation. If the target has no matching listener,
including when the callback was never registered, it is a deterministic no-op
and does not create a record. Removing the same listener repeatedly is also
harmless. A removed listener record is cleared, and if its `onclick` slot is
also empty the shared record is compacted immediately. The freed listener
slot can be reused by a later `addEventListener` call. Removal on another
Element cannot affect the original Element.

`onclick` remains an independent slot. Removing an `addEventListener` listener
does not clear or mutate `onclick`; changing or clearing `onclick` does not
remove the registered listener. When both are present, the existing JS10
dispatch order remains `onclick` first and the registered listener second.
JS10's deliberate one-listener-per-element rule remains in force: a duplicate
`addEventListener("click", ...)` call replaces that Element's one listener;
JS11 does not introduce multiple listeners for one Element.

The listener table remains fixed and bounded at 64 records of 16 bytes each
(1,024 bytes total). The table is shared with `onclick`, and the listener
registration count is capped at 64. Removing a listener decrements that count
and makes the slot reusable; repeated remove/re-add cycles do not consume
capacity permanently. No per-removal allocation or general-purpose event
listener heap was added.

Only `"click"` is supported. Removal of another event name uses the existing
`HostInvalidValue` runtime error convention and does not mutate click-listener
state. A non-function callback such as `123` or `null` is rejected with the
same `HostInvalidValue` result and does not change an existing listener. These
validation failures are handled by the existing JavaScript/host error path.

Callbacks continue to run synchronously in the already-installed, same-realm
`RuntimeContext`, with zero arguments and no Event object. Function IDs are
runtime-owned identities; captured environments remain owned by the runtime
until realm reset, so removing a registration does not invalidate unrelated
JavaScript references to that function or its closure. The bounded lifetime
model is therefore: a registration retains only an element serial and a
runtime function ID; document attach/detach, navigation, realm reset, or host
generation change clears registration records, while the runtime owns function
and closure storage until reset. A stale Element fails with the existing
`StaleHostObject` result and cannot remove or register against a replacement
document.

The focused proof is `tests/navigator_javascript_js11_test.cpp`, run by
`scripts/smoke-navigator-javascript-js11.ps1`. It covers identity mismatch,
identical-code functions, wrong elements, nonexistent and repeated removal,
onclick independence, remove/re-add, slot reuse after all 64 registrations,
closure mutation and lifetime, unsupported and invalid values, callback-error
containment, DOM mutation/relayout, and navigation-generation cleanup. The
script compiles the relevant adapter/runtime path with `GXOS_BARE_METAL` and
also performs a strict hosted syntax check.

The authentic hosted proof uses
`navigator-smoke/javascript-js11.html` and
`navigator-smoke/javascript-js11-target.html` through the `navigator.smoke`
aggregate. It uses Navigator's parsed page scripts, real element hit testing,
physical click handling, callback-driven DOM mutation, listener self-removal,
re-registration, onclick coexistence, error recovery, ordinary-link
navigation, and document cleanup. JS11 assertions are reported separately
from the repository's unrelated CSS smoke phases.

JS11 remains intentionally incomplete browser behavior. It does not add Event
objects or callback arguments, `target`/`currentTarget`, bubbling, capture,
propagation controls, listener options, `{ once: true }`, multiple listeners
per element, other event types, timers, promises, microtasks, task queues,
asynchronous JavaScript, or broader DOM APIs. A clean next phase is JS12:
introduce a minimal click Event object and pass it as the first callback
argument, initially exposing only `type`, `target`, and `currentTarget`
without propagation.

## Phase JS12: minimal click Event objects

JS12 adds the first host-created object passed through Navigator's production
click callback path. Both supported callback styles now receive one argument:

```javascript
element.onclick = function (event) { /* ... */ };
element.addEventListener("click", function (event) { /* ... */ });
```

The argument is an ordinary runtime object created by `RuntimeContext` and
passed through the existing function argument vector. Its complete JS12
surface is deliberately only:

| Property | JS12 value |
| --- | --- |
| `event.type` | the ordinary JavaScript string `"click"` |
| `event.target` | the clicked Element wrapper |
| `event.currentTarget` | the Element whose callback is executing |

JS12 has no propagation path. For the current direct click model,
`target` and `currentTarget` are separate Event properties that carry the same
Element handle. They are not implemented as a single hard-coded semantic;
the separate properties leave the correct shape for a future bubbling phase.
No parent dispatch, bubbling, capture, phases, propagation controls, default
prevention, mouse coordinates, or other event types are added.

### Object and Element identity

The runtime keeps one cached Event object per same-realm document lifetime.
It uses normal runtime object property lookup, so local aliases and
`event === event` follow ordinary object identity. The host updates the two
Event handle properties before each synchronous click and passes the same
Event value to `onclick` and the registered listener. A callback may ignore
the argument; a callback with multiple formals receives Event in the first
formal and the existing runtime's normal `undefined` value for an omitted
extra argument.

Each Event Element property is a value from the existing generation-scoped
host-object registry. A handle returned by `event.target` therefore compares
identically with the value returned by `document.getElementById` for the same
element, and reads such as `event.target.id` and mutations such as
`event.target.textContent = "Clicked"` address the live document element.
Repeated reads return the same canonical Element wrapper. Event properties are
host-defined read-only fields from script: assigning `event.type`,
`event.target`, or `event.currentTarget` is a deterministic no-op and cannot
change dispatch identity. Unknown fields use the normal missing-property
result (`undefined`).

### Lifetime, navigation, and bounds

The Event object is synchronous but is retained as one runtime object so a
callback may safely store it. `type` is immutable; `target` and
`currentTarget` are read-only to script and are refreshed for the next click
because the bounded Event object is reused. During and after a callback, while
its document generation remains valid, those values are safe runtime values.
The object itself contains runtime values, never a native document pointer. On
a generation invalidation, the referenced Element handles become stale under
the existing `StaleHostObject` convention; attempting `savedEvent.target.id`
therefore fails closed instead of dereferencing old document storage. Normal
navigation resets the realm and clears the Event object, listener table, and
old callback values before the replacement document is installed.

There is no per-click Event allocation and no per-click host record. The
bounded cost is one normal three-property runtime object per realm, plus the
already bounded canonical Element host-wrapper records for the two handles.
The fixed JS11 listener table remains 64 records of 16 bytes (1,024 bytes)
and one listener per Element. The Event object is reused on every click; realm
reset releases it and the normal host-generation cleanup invalidates old
Element values.

### JS12 proof fixtures and results

The focused proof is
`tests/navigator_javascript_js12_test.cpp`, run by
`scripts/smoke-navigator-javascript-js12.ps1`. It compiles the runtime,
adapter, parser, and real `WebDocument` fixture path with `GXOS_BARE_METAL`,
then performs a strict warning-as-error syntax check. The suite covers
callback arguments, zero- and multi-parameter functions, `type`, target and
currentTarget reads, canonical identity and repeated reads, local aliases,
two-element dispatch, shared Event identity across `onclick` and the listener,
read-only assignments, DOM mutation and relayout, JS11 removal and slot
capacity, unsupported and invalid inputs, callback-error containment,
generation invalidation, replacement-document cleanup, and a 100-click
boundedness stress run. The final checked result is recorded with the
JS1–JS12 validation report below. At JS12 closeout it passes all 259 checks;
the dedicated script also passes the `GXOS_BARE_METAL` compile and strict
hosted syntax-check lanes.

The authentic hosted proof uses
`navigator-smoke/javascript-js12.html` and
`navigator-smoke/javascript-js12-target.html` through the existing
`navigator.smoke` aggregate. It uses actual HTML parsing, page-script
execution, Element lookup, Navigator hit testing, hosted click input, callback
mutation, callback-error recovery, ordinary link navigation, and
document-scoped cleanup. The aggregate reports JS12 assertions separately
from its known unrelated CSS failures. At JS12 closeout the aggregate is
`299 passed, 7 failed`; the nine additional JS12 checks all pass, and the
seven failures are the pre-existing CSS 3C, CSS 3G, CSS 6A, CSS 6B (three
checks), and CSS 6C failures. JS11's baseline was `290 passed, 7 failed`, so
JS12 introduces no new aggregate failure.

### JS12 validation closeout

The complete available focused regression set passes: JS1 lexer, JS2 parser,
JS3 runtime, JS6, JS7, JS8, JS9, JS10, JS11 (164 checks), and JS12 (259
checks). The hosted/native JS12 implementation is compiled by the normal
server source list, and the focused JS12 path compiles successfully with
`GXOS_BARE_METAL`; no hosted-only Event storage or RTTI/exception dependency
was added.

JS12 does not claim full DOM Events. Listener options, multiple listeners per
Element, `once`, passive listeners, bubbling, capture, propagation paths,
`stopPropagation`, `stopImmediatePropagation`, `preventDefault`, cancellation,
other event types, mouse or pointer data, keyboard/input events, timers,
promises, task queues, microtasks, asynchronous callbacks, and broad DOM
expansion remain outside that milestone. JS13 is the narrow next step: basic
click bubbling and bounded propagation-path construction.

## Phase JS13: bounded click bubbling

JS13 extends the existing synchronous click path from the directly hit
Element to its DOM ancestors. The path is constructed from the authoritative
`HtmlElementRef::parentSerial` links, not from layout overlap or a scan of all
registered listeners. It is built before any callback runs, in target-to-root
order, and is then dispatched in that same order. No capture traversal or
event-phase machinery is introduced.

The adapter uses one fixed
`std::array<HostInstanceId, kNavigatorScriptMaxPropagationDepth>` for the
snapshot. The bound is 32 Element serials, including the clicked Element and
the `html`/`body` records when present. Each path record is one 8-byte
`HostInstanceId`, for a fixed 256-byte path cost per dispatch. The array is
stack-local and is not retained between clicks. A missing parent record,
self-parent cycle, generation mismatch, or other invalid entry fails closed;
exceeding 32 entries returns the dedicated
`PropagationPathLimitExceeded` runtime error before JavaScript is invoked.
The overflow policy is explicit abort of that click's JavaScript propagation;
Navigator still follows its existing default action path afterward, including
ordinary link navigation. No native pointer is read beyond the bounded
document lookup.

For the whole bubbling dispatch, `event.target` is initialized once to the
authentically clicked Element and remains that canonical wrapper at every
node. Before a node's callbacks execute, `event.currentTarget` is refreshed
to that node's canonical wrapper. Thus a child callback sees child/child,
while an ancestor callback sees child/ancestor. The Event is still one cached
ordinary runtime object per realm; there is no per-node or per-click Event
allocation. Its target/currentTarget values are safe generation-scoped host
handles, and retained references observe the same deterministic JS12 reuse:
later clicks refresh the cached object's fields, while generation invalidation
makes old Element fields fail with `StaleHostObject`. Navigation resets the
realm and clears the Event, function IDs, listener table, and path context.

At each propagation node the adapter looks up the current bounded record when
that node is reached. A child callback that removes a later ancestor listener
therefore prevents that listener from firing; a replacement installed before
the later node is reached is the one used. Within one node, the two IDs are
copied on arrival so the established local order remains `onclick` followed
by its one `addEventListener("click", ...)` listener. Handlerless ancestors
are skipped without terminating the path, and an ancestor-only listener can
receive a click from an otherwise unregistered descendant. Callback runtime
errors retain JS12 containment: the failing callback reports its first error,
the remaining local listener and later ancestors continue when possible, and
the next independent click starts with a clean runtime diagnostic state.

### JS13 proof fixtures and results

The focused proof is
`tests/navigator_javascript_js13_test.cpp`, run by
`scripts/smoke-navigator-javascript-js13.ps1`. It uses the real parsed
`WebDocument` structural records and the adapter's production-boundary click
entrypoint. The suite covers direct, parent, and grandparent handlers;
target/currentTarget divergence and canonical identity; onclick/listener
ordering at every node; handlerless gaps; ancestor-only dispatch; independent
branches; listener removal and replacement during dispatch; zero-argument
callbacks; closure state; DOM mutation and relayout; callback-error
continuation; deep hierarchies; the 32-entry boundary and overflow; 64-slot
listener reuse; 100 repeated bubbling clicks; retained Event reuse; and
navigation cleanup with stale-reference failure and a new nested document.
The JS13 focused suite passes 332 checks with 0 failures.

The authentic hosted proof is
`navigator-smoke/javascript-js13.html` plus
`navigator-smoke/javascript-js13-target.html`, exercised by the existing
`navigator.smoke` aggregate. It uses Navigator HTML parsing, page-script
execution, real layout, form-button hit testing, physical mouse down/up
handling, production click dispatch, ancestor callbacks, callback mutation,
callback-error recovery, and ordinary-link navigation. The hosted fixture
proves the hit button is the original target, parent and grandparent handlers
run in target-to-root order, an unregistered descendant reaches its ancestor,
unrelated branches stay isolated, a removed ancestor listener is skipped, and
navigation clears the old records. Nested-link behavior is not claimed: the
current compact hit model reports the hit link block as the target and does
not provide a separate generic descendant hit node for that case.

The focused JS1–JS13 set contains 11 available suites: lexer, parser, runtime,
JS6, JS7, JS8, JS9, JS10, JS11, JS12, and JS13. JS13's adapter/runtime path
passes the `GXOS_BARE_METAL` compile lane and the strict warning-as-error
syntax lane. Its bounded per-dispatch addition is 256 bytes of stack path
storage; the existing 64 listener records remain 16 bytes each (1,024 bytes),
and the existing one cached Event object and canonical host-wrapper registry
remain bounded. The hosted Navigator aggregate is `309 passed, 7 failed`
after the ten JS13 assertions are added; this is the JS12 baseline of
`299 passed, 7 failed` plus ten new passing JS13 checks. The seven known
pre-existing failures remain CSS 3C, CSS 3G, CSS 6A, CSS 6B (three checks),
and CSS 6C. No JS13 aggregate failure is introduced.

JS13 still does not implement full DOM Events: capture, `useCapture`, event
phases or `eventPhase`, `bubbles`, `cancelable`, propagation controls,
`preventDefault`, cancellation, listener options, `once`, `passive`, multiple
listeners per Element, non-click events, keyboard/input/change events,
MouseEvent/PointerEvent data, coordinates/buttons, timers, promises,
microtasks, asynchronous dispatch, or broader DOM expansion remain excluded.
The recommended JS14 milestone is `event.stopPropagation()` with target-local
callbacks completing under a deterministic local policy and ancestor bubbling
stopping without adding capture or default-action cancellation.

## Phase JS14: `event.stopPropagation()`

JS14 adds a callable `event.stopPropagation()` method to the cached JS12/JS13
Event object. It is installed as the existing runtime native-function kind and
stored as a read-only Event member; no parser-only or adapter-only JavaScript
special case is used. The method accepts the normal JavaScript call shape,
including extra arguments, which are ignored by the native function. A wrong
receiver, including a detached `var stop = event.stopPropagation; stop()`
call, follows the host method convention and reports `InvalidReceiver`.

The method implements the precise JS14 boundary: it marks the active
synchronous dispatch stopped, but it does not stop the remaining callbacks on
the current propagation node. The adapter always invokes that node's
`onclick` first and its one registered click listener second, then checks the
stop state before reaching the next target-to-root node. Therefore an
`onclick` that calls `stopPropagation()` still permits the node listener, while
parent, grandparent, and all other later ancestors are skipped. This is not
`stopImmediatePropagation()`; JS14 itself did not implement that stronger
method, and current-node handlers still finished. It is also not
`preventDefault()`: JS14 does not cancel
default actions, so an ordinary clicked link still navigates normally while
its JavaScript ancestor propagation is stopped.

Propagation state is dispatch-scoped. `RuntimeContext::beginEventDispatch()`
sets the active flag and resets the stopped flag before callback execution;
`endEventDispatch()` clears both flags on every normal or allocation-failure
exit. Repeated calls are idempotent and allocate no records. A stop on the
root-most node is harmless because there is no later node. The cached Event
object remains one object per realm, and the existing 32-entry,
256-byte stack-local propagation snapshot remains the only path storage.
There is no per-click Event, listener, path, or stop-state allocation. The new
declared persistent RuntimeContext storage is one 4-byte `RuntimeFunctionId`
and two `bool` fields (6 bytes of field storage; normal compiler alignment may
pad the enclosing object). One existing bounded FunctionRecord and one
existing Event RuntimeProperty are populated once for the realm. The 64-slot,
16-byte listener table remains unchanged at 1,024 bytes.

The stop state cannot leak across clicks or branches: each new dispatch starts
enabled, and a later click can bubble normally. A retained Event call outside
an active matching dispatch is a harmless no-op, so it cannot pre-stop a future
click. The Event still holds generation-scoped host values rather than native
document pointers. After navigation, old Element fields fail with
`StaleHostObject`, and an old retained Event method cannot access a freed path
or mutate a later dispatch. The current adapter has no script-visible nested
click/event dispatch API; its synchronous dispatch guard reports
`HostReentryUnsupported`, so JS14 does not broaden the runtime to add a
reentrant event surface.

Callback containment preserves the JS13 policy. If a callback calls
`stopPropagation()` and then errors, the error remains contained, the current
node's remaining handler still follows the established local order, and
ancestors remain stopped. If execution errors before the method call, the
method was not called and ordinary JS13 callback-error continuation applies.
Listener removal or replacement during a stopped callback remains safe; no
later ancestor lookup is needed after the current node. A path overflow still
returns the deterministic `PropagationPathLimitExceeded` error before
callbacks, and a subsequent normal click recovers with a clean dispatch.

### JS14 proof fixtures and results

The focused proof is
`tests/navigator_javascript_js14_test.cpp`, run by
`scripts/smoke-navigator-javascript-js14.ps1`. It covers the callable method,
target-listener and target-`onclick` stops, current-node ordering, ancestor
stops, handlerless gaps, root and repeated calls, per-dispatch reset,
independent branches, target/currentTarget identity and canonical Element
identity, closures, zero-argument callbacks, DOM mutation and relayout,
listener-removal regressions and 64-slot capacity, unsupported and invalid
inputs, errors before and after stopping, listener mutation, retained and
stale Event behavior, navigation invalidation, path overflow and recovery,
and 100 stopped clicks with bounded object/listener/path state. It passes
`313 checks, 0 failures`.

The authentic hosted proof is
`navigator-smoke/javascript-js14.html` plus
`navigator-smoke/javascript-js14-target.html`, exercised by the production
`navigator.smoke` path. It uses real Navigator HTML parsing, page JavaScript,
DOM hierarchy and layout, hosted mouse down/up input, hit testing, production
click dispatch, actual JavaScript calls to `stopPropagation()`, callback DOM
mutation and relayout, ancestor suppression, error recovery, and ordinary
link navigation. The JS14 aggregate fixture contributes 12 passing checks,
including proof that stopping link propagation does not cancel default
navigation. The final hosted aggregate is `321 passed, 7 failed`; the seven
known unrelated failures remain CSS 3C, CSS 3G, CSS 6A, CSS 6B (three checks),
and CSS 6C. No JS14 aggregate failure is introduced.

The complete JS1–JS14 focused set contains 12 available suites: lexer, parser,
runtime, JS6, JS7, JS8, JS9, JS10, JS11, JS12, JS13, and JS14. All 12 pass.
The JS14 implementation and focused test compile successfully with
`GXOS_BARE_METAL`, and the strict warning-as-error hosted syntax lane also
passes. The normal production server build passes as well.

JS14 remains intentionally incomplete DOM Events compatibility. It does not
provide `preventDefault()`, `defaultPrevented`, default-action cancellation,
capture, capture listeners, event phases, `eventPhase`, `bubbles`, `cancelable`,
listener options, multiple listeners per Element, `once`, passive listeners,
other event types, coordinates, keyboard/input events, timers, promises, task
queues, microtasks, asynchronous dispatch, or broad DOM expansion.

## Phase JS15: `event.stopImmediatePropagation()`

JS15 adds a callable `event.stopImmediatePropagation()` method to the same
cached click Event object. It uses the existing runtime native-function and
read-only Event-member infrastructure, accepts extra arguments under the
normal native call convention, and reports `InvalidReceiver` for a detached
call such as `var stop = event.stopImmediatePropagation; stop()`. Unknown Event
properties retain the existing missing-property behavior.

The exact distinction is now:

* `stopPropagation()` sets propagation-stopped state, so the current node's
  remaining handlers finish but later ancestor nodes do not dispatch.
* `stopImmediatePropagation()` sets both propagation-stopped and
  immediate-propagation-stopped state, so remaining handlers on the current
  node are skipped and later ancestor nodes do not dispatch.
* Neither method is `preventDefault()`. JS15 does not cancel ordinary click
  default actions such as link navigation.

Navigator's bounded per-node order remains `onclick` followed by its one
registered click listener. The adapter checks immediate-stop state after
`onclick` and before invoking that listener, then checks propagation state
before reaching the next target-to-root path entry. Consequently, an onclick
that calls immediate stop suppresses its same-node listener, parent, and all
higher ancestors; a listener-origin immediate stop still allows that listener
to finish and suppresses later ancestors. The JavaScript callback itself
continues executing after the method call: the method is not an exception,
return, longjmp, or callback abort. This leaves DOM mutation after the call and
error containment deterministic.

Both flags are dispatch-scoped. `beginEventDispatch()` clears them before
callbacks, and `endEventDispatch()` clears them on every completed or failed
dispatch. Repeated calls are idempotent, and a weaker later
`stopPropagation()` call cannot undo an immediate stop. A retained Event method
called outside an active dispatch is a harmless no-op, including after
generation invalidation, and cannot pre-stop an unrelated future click.
Event `target`, `currentTarget`, and canonical Element identity are untouched
by either stop method. Navigation still invalidates old generation-scoped
Element handles and clears active dispatch state, listener registrations, and
path ownership; stale access fails closed with `StaleHostObject`. A path that
exceeds the fixed 32-entry snapshot still fails before callbacks with
`PropagationPathLimitExceeded`, leaving no stop state to leak into recovery.

JS15 adds no per-click Event, listener, path, or stop-state allocation. The
new persistent RuntimeContext storage is one 4-byte RuntimeFunctionId and one
bool (5 declared bytes before normal enclosing-object alignment), plus one
bounded native FunctionRecord and one Event RuntimeProperty populated once per
realm. The existing 256-byte stack-local propagation path and 64-slot,
16-byte listener table remain unchanged. The focused stress proof confirms
stable Event/object, host-wrapper, listener-table, and path storage over
repeated immediate-stop clicks.

### JS15 proof fixtures and results

The focused proof is
`tests/navigator_javascript_js15_test.cpp`, run by
`scripts/smoke-navigator-javascript-js15.ps1`. It retains the JS14 coverage
for listener identity/removal, slot reuse, 64-listener capacity, unsupported
inputs, callback errors, retained Events, stale navigation, path overflow and
recovery, and bounded repeated clicks. It adds direct assertions for callable
method exposure, onclick-vs-listener suppression, parent and grandparent
suppression, parent-origin stopping, stopPropagation regression, repeated and
mixed calls, target/currentTarget identity, callback continuation, mutation
before and after the call, per-dispatch reset, independent branches, zero-
argument callbacks, immediate-stop callback errors, detached receivers, and
outside-dispatch retained calls.

The authentic hosted fixture is
`navigator-smoke/javascript-js15.html` plus
`navigator-smoke/javascript-js15-target.html`, exercised by `navigator.smoke`.
It uses Navigator parsing, real DOM hierarchy and layout, hosted mouse
down/up, authentic hit testing, production Event construction/reuse,
target-to-root dispatch, onclick/listener ordering, actual JavaScript calls to
`stopImmediatePropagation()`, DOM mutation and relayout, contained callback
errors, independent branches, and ordinary link navigation. The aggregate
check explicitly proves that immediate propagation stopping suppresses the
link's remaining handlers without cancelling its default navigation.

The complete JS1–JS15 focused set contains 13 available suites: lexer, parser,
runtime, JS6, JS7, JS8, JS9, JS10, JS11, JS12, JS13, JS14, and JS15. All 13
focused suites pass, including 509 JS15 checks with 0 failures. The normal
production server build passes, the JS15 bare-metal and strict warning-as-error
lanes pass, and the hosted aggregate is `333 passed, 7 failed`: the JS14
baseline was `321 passed, 7 failed`, so JS15 adds 12 passing hosted checks and
no aggregate failure. The seven known unrelated failures remain CSS 3C, CSS
3G, CSS 6A, CSS 6B (three checks), and CSS 6C.

JS15 remains intentionally incomplete DOM Events compatibility. It does not
provide `preventDefault()`, `defaultPrevented`, default-action cancellation,
capture, capture listeners, event phases, `eventPhase`, `bubbles`, `cancelable`,
listener options, multiple listeners per Element, `once`, passive listeners,
other event types, MouseEvent/PointerEvent data, coordinates/buttons,
keyboard/input/change events, timers, promises, task queues, microtasks,
asynchronous dispatch, or broad DOM expansion. The recommended JS16 milestone
is `event.preventDefault()` with read-only `event.defaultPrevented` for
cancellable click default actions, while leaving handler propagation controls
otherwise independent.

## Phase JS16: `event.preventDefault()` and `event.defaultPrevented`

JS16 adds the first cancellable click default action to the cached JS12–JS15
Event model:

```javascript
event.preventDefault();
event.defaultPrevented;
```

`preventDefault` is exposed as a callable read-only Event method through the
same runtime-native-function path as `stopPropagation` and
`stopImmediatePropagation`. A detached call uses the existing deterministic
`InvalidReceiver` convention. Extra arguments are ignored by the existing
native-call convention. `defaultPrevented` is an immutable host-owned Boolean
property: script assignment is a non-strict no-op, so assignment cannot cancel
or clear the actual dispatch state.

The dispatch state now has three independent bounded flags:

```text
propagationStopped
immediatePropagationStopped
defaultPrevented
```

`preventDefault()` only changes the third flag. It does not stop the current
node, prevent same-node handlers, or suppress bubbling. `stopPropagation()` and
`stopImmediatePropagation()` likewise do not cancel the default action unless a
callback explicitly calls `preventDefault()` as well. The cached Event
property is reset to `false` at dispatch start, becomes `true` monotonically
after a successful call, and remains visible to all later handlers in that
dispatch. Dispatch cleanup clears native state after the default-action
decision; the retained cached property therefore reflects the most recent
completed dispatch until the next dispatch resets it.

The initial default action is ordinary Navigator link navigation. The
production click boundary now captures the dispatch cancellation result before
cleanup and checks it immediately before `navigateTo()`. A cancelled hosted
link still receives authentic mouse down/up input, hit testing, JavaScript
dispatch, callback mutation, and relayout, but the current document remains
active. An uncancelled link follows the existing navigation path. Ancestor
handlers observe and may cancel the same descendant link dispatch through the
shared Event. Buttons and other supported non-link targets can set the Event
property safely, but JS16 does not invent a default action for them.

Cancellation does not use an exception, early callback return, or longjmp-like
control transfer. JavaScript after the call continues normally. If a callback
errors after calling `preventDefault()`, the error remains contained and the
captured cancellation still suppresses link navigation. If it errors before
the call, cancellation remains false and the existing callback-error/default-
action policy applies. Repeated calls are harmless and monotonic.

The focused proof is
`tests/navigator_javascript_js16_test.cpp`, run by
`scripts/smoke-navigator-javascript-js16.ps1`. It covers callable method and
read-only property exposure, initial and updated state, extra arguments,
wrong receivers, onclick/listener ordering, bubbling visibility through parent
and grandparent, ancestor cancellation, target/currentTarget and canonical
identity, callback continuation and DOM mutation/relayout, both propagation
controls independently, all combined stop-plus-cancellation orders, repeated
calls, conditional reset, non-link cancellation, callback errors before and
after cancellation, retained/outside-dispatch calls, stale generation safety,
independent branches, closure and zero-argument callbacks, listener removal,
slot reuse, 64-listener capacity, path overflow/recovery, and 100 cancelled
bounded clicks.

The authentic hosted proof is
`navigator-smoke/javascript-js16.html`, with
`navigator-smoke/javascript-js16-target.html` and
`navigator-smoke/javascript-js16-uncancelled.html`. It uses real Navigator
HTML parsing, the production JavaScript realm, actual DOM element wrappers,
real layout and hit testing, hosted mouse down/up, the shared Event object,
the actual JavaScript `preventDefault()` call, the production default-action
decision, and observable cancelled versus uncancelled navigation. The
aggregate also proves that child cancellation bubbles to the parent, that a
listener after `onclick` sees `defaultPrevented`, that the first click can be
cancelled while a later click is not, and that navigation resets old handler
and error state.

### JS16 proof results

The focused JS16 proof reports 338 checks with 0 failures. The complete
available JS1–JS16 focused set contains 14 suites: lexer, parser, runtime,
JS6, JS7, JS8, JS9, JS10, JS11, JS12, JS13, JS14, JS15, and JS16; all 14
pass. The JS16 smoke script also passes its bare-metal compilation lane and
strict warning-as-error syntax lane. The normal hosted native build links
successfully.

The latest required hosted aggregate proves all 9 JS16 checks, including the
cancelled-link `revision=0->1` relayout result, with 0 JS16 failures. It
reports 342 passed and 7 failed in this workspace; the 7 failures are the
known pre-existing CSS baseline failures. The pre-JS16 baseline was 333
passed and 7 failed, so the JS16-specific contribution is 9 passing checks
and no new failure beyond baseline.

JS16 adds no per-click allocation. The persistent cost is one additional
cached native function ID and one Boolean in `RuntimeContext`, one bounded
native function record, and one read-only Boolean Event property populated
once per realm.
The fixed 32-entry propagation snapshot and fixed 64-record listener table
remain unchanged. Path overflow still fails before Event creation or callback
execution, so it cannot set `defaultPrevented`; a subsequent valid dispatch
starts cleanly. Navigation and generation invalidation continue to clear
listeners, active dispatch state, host wrappers, and old Event access. A
retained Event method outside active dispatch is harmless and cannot
pre-cancel a later click.

JS16 remains intentionally incomplete DOM Events compatibility. It does not
provide capture, capture listeners, event phases, `eventPhase`, `bubbles`,
`cancelable`, passive listeners, multiple listeners per Element, listener
options, `once`, additional event types, mouse/pointer coordinates,
keyboard/input/change events, timers, promises, task queues, microtasks,
asynchronous dispatch, or broad DOM expansion. Only the existing ordinary
Navigator link default action is cancellable; form and other activation
semantics remain outside this milestone.

## Phase JS17: bounded multiple click listeners per Element

JS17 replaces JS10's one-registered-listener-per-Element limitation with a
bounded list of exact click registrations:

```javascript
element.addEventListener("click", first);
element.addEventListener("click", second);
element.addEventListener("click", third);
```

The listener table remains global to one document and is capped at 64 active
registrations. `onclick` is independent: it has its own fixed 64-record table
and does not consume listener registrations. `clickListenerCount()` reports
registrations; the historical `clickHandlerCount()` diagnostic reports the
number of Elements represented by either table. Navigation, generation
invalidation, and document replacement clear both tables.

Each listener record is 24 bytes, up from the JS16 mixed record size of 16
bytes. The new record contains the Element serial, exact runtime Function ID,
and a 64-bit registration sequence. The fixed listener table therefore costs
1,536 bytes. The independent onclick table remains 64 records at 16 bytes
(1,024 bytes), for a combined fixed callback-table cost of 2,560 bytes. The
sequence is both logical order and registration identity; a removed slot may
be reused physically, but a replacement receives a new sequence. Sequence
wraparound is deterministic: outside dispatch, active records are compacted
and resequenced in order; at the boundary during dispatch, a new registration
is rejected rather than invalidating an active snapshot.

Registration order is logical sequence order, never physical slot order. An
exact duplicate `(Element, "click", Function ID)` is a no-op and consumes no
slot. Function IDs preserve runtime identity, so different function objects
with identical source can coexist. Removal only removes the exact matching
registration; repeated removal, wrong callbacks, and wrong Elements are
harmless. Removing and adding again appends the callback at the end of the
current order. Released slots are immediately reusable.

Dispatch uses a fixed per-node snapshot of at most 64 entries. Each snapshot
entry is 16 bytes (sequence plus Function ID), so the stack-local snapshot
cost is 1,024 bytes. At each propagation node Navigator collects active
registrations in logical order before running `onclick`, then runs `onclick`
first and validates each captured sequence, Element serial, and Function ID
before invoking it. A listener removed before its turn is skipped; a listener
added after the node snapshot, including from `onclick`, waits for the next
dispatch. Remove-then-readd invalidates the old identity and cannot invoke a
new callback through a reused physical slot. Self-removal is safe, and
`stopImmediatePropagation()` terminates the snapshot iteration immediately.
The snapshot is automatic storage only and is gone when the synchronous
dispatch returns.

The existing Event state is shared by all callbacks in a dispatch. Ordinary
`stopPropagation()` still permits later listeners on the current Element but
blocks ancestors. `stopImmediatePropagation()` blocks later listeners on the
current Element and all ancestors. `preventDefault()` remains independent:
later listeners observe `defaultPrevented`, and an uncancelled ordinary link
still navigates while a cancelled link does not. Callback errors remain
contained according to the established JS13–JS16 policy: later eligible
listeners and ancestors continue unless propagation was explicitly stopped;
an immediate stop remains effective after an error, and cancellation remains
effective after an error.

The focused proof is
`tests/navigator_javascript_js17_test.cpp`, run by
`scripts/smoke-navigator-javascript-js17.ps1`. It uses the real HTML parser,
runtime, host adapter, canonical Element wrappers, cached Event, bubbling
path, callback invocation, and document lifecycle. It covers two and three
listeners, ordering, onclick precedence, duplicate and identity semantics,
all removal positions, slot reuse, stale-slot reuse protection, 64/65 global
capacity, multi-Element capacity, propagation controls, cancellation,
target/currentTarget identity, mutation during dispatch, self-removal,
onclick mutation, errors, closure and zero-argument callbacks, navigation
cleanup, stale references, path overflow/recovery, unsupported events,
invalid callbacks, and 100 repeated multi-listener clicks.

The authentic hosted proof is
`navigator-smoke/javascript-js17.html`, with
`navigator-smoke/javascript-js17-target.html`. The aggregate loads these
files through the production HTTP path, parses the real page, executes its
`<script>` source in Navigator's document realm, performs layout and
hit-tested hosted mouse down/up, and verifies production order, bubbling,
immediate stopping, mutation snapshots, Event state, cancellation, and
navigation cleanup.

### JS17 proof results

The JS17 focused proof reports 394 checks with 0 failures. The complete
available JS1–JS17 focused set contains 15 suites: lexer, parser, runtime,
JS6, JS7, JS8, JS9, JS10, JS11, JS12, JS13, JS14, JS15, JS16, and JS17.
All 15 suites pass. The JS17 bare-metal compile lane and strict
warning-as-error syntax lane pass, and the normal native hosted build links
successfully.

The JS17 aggregate reports 349 passed and 7 failed across 356 checks, with
all 7 JS17 hosted checks passing. This is a delta of 7 passing checks over
the recorded JS16 result of 342 passed and 7 failed. It retains the same
seven unrelated CSS failures: CSS 3C, CSS 3G, CSS 6A, CSS 6B's three checks,
and CSS 6C. No CSS repair is part of JS17.

Navigator still does not provide complete DOM Events compatibility. JS17 does
not add capture, capture listeners, event phases, `eventPhase`, `bubbles`,
`cancelable`, listener options, `{ once: true }`, passive listeners, additional
event types, coordinates, keyboard/input/change events, timers, promises,
task queues, microtasks, asynchronous dispatch, or broad DOM expansion.

## Phase JS18: bounded `{ once: true }` click listeners

JS18 adds the one supported listener option:

```javascript
element.addEventListener("click", callback, { once: true });
```

The existing two-argument form remains persistent. An ordinary object with
`once: false`, no `once` property, or `{}` is also persistent. The parser
and runtime already support object literals; the host call boundary passes the
bounded runtime object ID to the Navigator adapter, which reads only its
`once` property through the existing runtime property lookup. Unknown members
such as `banana` are ignored. Only literal Boolean `true` and `false` are
accepted for `once` (an `undefined` value is treated like a missing
property). Numbers, strings, `null`, non-object third arguments, and other
malformed options are rejected with `HostInvalidValue` before a listener slot
is allocated. Boolean capture arguments, `capture`, and `passive` are not
implemented.

Duplicate identity remains exactly the JS17 tuple (Element, `"click"`,
Function ID). A duplicate call is a no-op even when its once value differs,
so a persistent registration cannot be changed to once and a once registration
cannot be changed to persistent by a duplicate call. Different function
objects remain distinct. Removing a fired once listener is harmless; a later
registration of the same function receives a new sequence and can fire again.

The listener record stays 24 bytes: Element serial, runtime Function ID, a
32-bit flags word using one once bit, and the 64-bit registration sequence.
The fixed 64-record listener table remains 1,536 bytes, and the independent
onclick table remains 1,024 bytes. The per-node dispatch snapshot remains at
most 64 entries of 16 bytes, or 1,024 bytes of stack-local storage. No
once-specific heap allocation or per-click registration table is added.

Before invoking a snapshotted listener, dispatch validates its sequence and
Function ID. If the record is once, Navigator clears it and releases its
global slot immediately before invoking the callback. This ordering is
intentional: an error, `stopPropagation()`, `stopImmediatePropagation()`, or
self re-registration cannot restore or double-invoke the old registration.
Self re-registration therefore creates a new sequence that is excluded from
the current snapshot and can run on the next click. Earlier callbacks can
remove a later once listener, and slot reuse cannot make a replacement match
the stale snapshot identity.

`onclick` remains first, followed by registered listeners in logical
registration order. A once listener can call `preventDefault()` and later
listeners observe `event.defaultPrevented`; after it is gone, the next click
starts with a fresh default-prevention state. Propagation controls remain
independent of once removal. Path overflow happens before callback dispatch,
so it consumes neither a once listener nor default-prevention state.
Navigation, detach, generation invalidation, and realm reset clear fired and
unfired once records alike. Old Elements and retained functions remain
fail-closed at the host boundary, while JavaScript references to a function
remain valid after its listener registration is removed.

The focused proof is
`tests/navigator_javascript_js18_test.cpp`, run by
`scripts/smoke-navigator-javascript-js18.ps1`. It covers persistent two-
argument, `once:false`, missing options, once suppression and capacity
release, duplicate options, exact function identity, mixed order, onclick
precedence, removal and re-addition, self re-registration, callback errors,
propagation controls, cancellation, target/currentTarget identity, mutation
snapshots, stale-slot reuse, global and mixed capacity, overflow recovery,
navigation cleanup, unsupported events, malformed options, and 100 bounded
re-registering clicks. It reports 403 checks with 0 failures.

The authentic hosted proof is
`navigator-smoke/javascript-js18.html`, with
`navigator-smoke/javascript-js18-target.html`. The aggregate loads the page
through the production HTTP path, executes its object-literal options through
the real document realm, performs hit-tested clicks, checks onclick/once/
persistent order, once immediate-stop behavior, first-click link cancellation,
second-click navigation, and navigation cleanup.

The complete available JS1–JS18 focused set contains 16 suites: lexer, parser,
runtime, JS6, JS7, JS8, JS9, JS10, JS11, JS12, JS13, JS14, JS15, JS16, JS17,
and JS18. The JS18 bare-metal and strict warning-as-error lanes use the same
bounded sources; both focused smoke lanes pass. The native hosted build also
links successfully. The aggregate reports 355 passed and 7 failed across 362
checks, with all 6 JS18 hosted checks passing. This is a delta of 6 passing
checks and 6 total checks over the recorded JS17 aggregate. The same 7
unrelated CSS failures remain: CSS 3C, CSS 3G, CSS 6A, CSS 6B's three checks,
and CSS 6C. No CSS repair is part of JS18.

JS18 is intentionally not generic `EventListenerOptions`. It does not add
`capture`, Boolean capture semantics, passive listeners, event phases,
`eventPhase`, `bubbles`, `cancelable`, additional event types,
mouse/pointer metadata, keyboard/input events, timers, promises, task queues,
microtasks, asynchronous dispatch, or broad DOM expansion.

JS18 itself is intentionally not generic `EventListenerOptions`; the later
JS19 metadata addition is described below. Capture should follow only when it
can preserve the same fixed memory and deterministic propagation guarantees.

## Phase JS19: read-only Event metadata

JS19 adds the two read-only metadata members supported by Navigator's current
click Event model:

```javascript
event.bubbles === true
event.cancelable === true
```

Both values are ordinary JavaScript Booleans, not strings. Every supported
`"click"` dispatch reports `event.bubbles === true` because the bounded target
to-ancestor path bubbles, and reports `event.cancelable === true` because
`preventDefault()` can suppress the supported link default action.

These properties describe capability, not mutable dispatch state. Assignments
such as `event.bubbles = false` and `event.cancelable = false` are deterministic
no-ops through the existing read-only host-property semantics. Reading or
assigning either property does not stop propagation and does not cancel a
default action. Only `stopPropagation()`,
`stopImmediatePropagation()`, and `preventDefault()` retain their established
effects.

`cancelable` remains true before and after cancellation. The separate
`defaultPrevented` member starts false for each dispatch and becomes true only
after `preventDefault()`; later same-node listeners and bubbling ancestors see
both `cancelable === true` and `defaultPrevented === true`. A cancelled link
therefore remains on its page, while metadata inspection alone leaves an
uncancelled link free to navigate. `bubbles` likewise remains true after
`stopPropagation()` or `stopImmediatePropagation()`, even when that particular
dispatch no longer reaches an ancestor.

`onclick` and all registered listeners receive the same cached Event object.
The target callback, target listeners, parent listener, and grandparent
listener observe the same Boolean metadata while `target` remains the
original canonical Element and `currentTarget` tracks the node being invoked.
`{ once: true }`, listener snapshots, duplicate identity, removal, capacity,
and mutation rules are unchanged. A once callback sees the metadata before its
registration is released, and persistent listeners see it on later clicks.

### JS19 storage and lifetime

The runtime still owns one cached ordinary Event object per realm. JS19 adds
two host-created read-only `RuntimeProperty` entries to that object, increasing
the cached Event property count from seven to nine. The Event object record,
click-handler records, 64-entry listener table, dispatch snapshot, and native
function table are unchanged; native-function count does not increase. The
only fixed metadata storage is two property entries plus 17 key bytes for
`bubbles` and `cancelable`; no native metadata record or dynamic metadata table
is introduced. Exact allocator padding/capacity remains an implementation
detail of the existing property vector.

There is no per-click heap allocation for metadata. The first dispatch creates
the already-existing cached Event object and its fixed properties; subsequent
clicks only refresh target/currentTarget and default-prevention state. If
JavaScript retains the Event, later reads remain safe and deterministic within
the realm; the cached object reports the current/latest reusable metadata and
does not expose native pointers. Navigation resets the realm and clears
document-scoped listener state. Generation checks continue to make stale
Element handles fail closed, while a new document click receives fresh true
metadata and no old propagation or cancellation state.

Path construction still happens before callbacks. A path beyond the 32-node
bound returns `PropagationPathLimitExceeded` without creating the Event,
running a listener, consuming a once registration, or changing cancellation
state. A later valid path recovers normally. Callback errors remain contained,
and later eligible listeners/ancestors still see true metadata.

Unknown Event members remain ordinary missing properties (`undefined`), and
unsupported event types, invalid callbacks, and malformed listener options
remain rejected at the same host boundary. JS19 does not add capture, Boolean
capture arguments, `eventPhase`, passive listeners, additional event types,
MouseEvent/PointerEvent, keyboard/input events, timers, promises, task queues,
microtasks, asynchronous dispatch, or broad DOM expansion. These are metadata
properties for the current click model, not full DOM Events compatibility.

The focused proof is
`tests/navigator_javascript_js19_test.cpp`, run by
`scripts/smoke-navigator-javascript-js19.ps1`. It uses parsed HTML, the real
WebDocument and host adapter, canonical Element wrappers, onclick, multiple
listeners, ancestor bubbling, stopPropagation,
stopImmediatePropagation, preventDefault/defaultPrevented distinction,
read-only assignments, once removal, listener mutation, callback-error
containment, stale generation handling, navigation cleanup, 64/65 capacity,
path overflow/recovery, unknown and unsupported inputs, retained Event reads,
and 100 repeated clicks. It reports 442 checks with 0 failures.

The authentic hosted proof is
`navigator-smoke/javascript-js19.html`, with
`navigator-smoke/javascript-js19-target.html`. The aggregate loads these
files through the production HTTP path, executes the page script in the
production realm, performs hit-tested target/stop/immediate clicks, verifies
target and ancestor metadata, checks authentic once cancellation and later
uncancelled navigation, and confirms navigation cleanup.

The complete available JS1–JS19 focused set contains 17 suites: lexer, parser,
runtime, JS6, JS7, JS8, JS9, JS10, JS11, JS12, JS13, JS14, JS15, JS16, JS17,
JS18, and JS19. The JS19 bare-metal compile lane and strict warning-as-error
syntax lane use the same bounded sources; the native hosted build is validated
separately. The aggregate result is 362 passed and 7 failed out of 369 checks;
all seven added JS19 checks pass, so JS19 adds seven passing checks with no new
failure. The seven known unrelated CSS failures remain CSS 3C, CSS 3G, CSS 6A,
CSS 6B's three checks, and CSS 6C. No CSS repair is part of JS19.

## Phase JS20: bounded click capture-phase support

JS20 adds bounded capture-phase support to the existing click listener model:

```javascript
element.addEventListener("click", callback, { capture: true });
element.addEventListener("click", callback, { capture: false });
element.addEventListener("click", callback, { capture: true, once: true });
```

At the JS20 milestone the explicit object form was the supported surface;
Boolean capture shorthand was intentionally deferred to JS22. Supported
option members are `capture` and `once`; both use Boolean validation, absent
members default to false, and unknown members remain ignored. The current
JS22 extension adds the Boolean form without changing those object rules.

### JS20 dispatch order

Navigator continues to build one fixed target-to-root propagation path, with a
maximum of 32 Elements. Capture traverses the ancestor portion of that path in
reverse, root toward target. The target is then dispatched once as one target
stage, followed by the existing forward bubble traversal:

```text
capture: root -> ... -> parent
target:  child capture -> child onclick -> child non-capture listeners
bubble:  parent onclick -> parent listeners -> ... -> root onclick -> root listeners
```

Ancestor `onclick` is bubble-only. On one Element, multiple capture listeners
run in registration order, and multiple non-capture listeners run in
registration order. Capture always precedes target `onclick` and target
non-capture listeners; `onclick` still precedes non-capture listeners. Capture
and bubble registration order is phase-separated rather than interleaved.

`event.target` remains the original authentically clicked canonical Element for
all phases. `event.currentTarget` is refreshed to the canonical Element whose
callback is executing. `event.bubbles` and `event.cancelable` remain Boolean
`true` during capture, target, and bubble. JS20 intentionally does not expose
`event.eventPhase` or phase constants; those are recommended for JS21.

`stopPropagation()` finishes all eligible listeners on the current Element,
then prevents later Elements and later phases. At the target it still permits
target capture's later same-node handlers, `onclick`, and target non-capture
listeners, but prevents ancestor bubbling. `stopImmediatePropagation()` skips
later listeners on the current Element and all later Elements/phases, whether
called during capture, target, or bubble. Existing bubble propagation-control
behavior is unchanged.

`preventDefault()` during capture sets `defaultPrevented` while leaving capture,
target, and bubble traversal active. Later callbacks in every phase observe the
cancellation, and the supported link default action is suppressed only after
dispatch. Cancellation is independent from propagation control.

### JS20 listener identity, removal, and once

The duplicate-registration key is `(Element, "click", Function identity,
capture)`. `once` is not part of identity. Consequently, a capture and a
non-capture registration of the same function coexist, while repeating a
registration with the same capture bit is a no-op even if the second call uses
a different `once` value; the first call's `once` behavior is retained.

Removal uses the same key except that only the capture bit is read from the
options. The two-argument form means `capture: false` and removes only the
non-capture registration. `{ capture: true }` removes only capture. A `once`
member in removal options is ignored for matching (and, when present, follows
the same Boolean validation policy). Removal never removes both phases.

Capture `once` registrations are consumed immediately before callback
invocation, including target capture. They release their fixed listener slot
even if the callback stops propagation or throws a contained JavaScript error.

### JS20 mutation snapshots

The adapter reuses one fixed 64-entry, 16-byte listener snapshot (1,024 bytes)
sequentially for every node and phase. A capture snapshot is collected when a
node is reached during capture; the target capture snapshot and target bubble
snapshot are separate visits; each ancestor gets a fresh bubble snapshot when
the bubble reaches it. Each snapshot preserves registration-sequence order and
revalidates both sequence and function identity before invocation. Removal,
including remove-then-reuse of a physical slot, therefore cannot invoke a stale
callback.

Additions do not join the active node/phase snapshot. A capture callback can
add a capture listener for a later click, or add a non-capture listener to an
ancestor whose later bubble snapshot has not yet been collected; that new
listener may run in the same event's later bubble phase. Removal during capture
can prevent a later bubble listener from entering its snapshot. Mutations during
bubble cannot alter completed capture. A capture listener added by target
`onclick` waits for the next event because capture has already completed.

### JS20 bounded storage and cleanup

The listener record remains 24 bytes: serial (8), Function ID (4), flags (4),
and registration sequence (8). JS20 uses bit 1 of the existing flags word for
capture and bit 0 for once; no listener-record expansion or separate capture
table was added. The fixed 64-record listener table costs 1,536 bytes. The
16-byte snapshot entry is unchanged and the reused snapshot costs 1,024 bytes;
there are not simultaneous capture and bubble snapshot buffers.

The dispatch memory model is therefore fixed: 1,536 bytes of listener table,
up to 256 bytes for the 32-serial propagation path, 1,024 bytes for the reused
phase snapshot, the existing cached Event object/property storage and its
generation-scoped Element wrappers, plus the existing scalar phase-control
state. No capture-specific persistent collection, recursion, or per-click
Event growth is introduced. The listener capacity remains 64 total
`addEventListener` registrations across both phases; registration 65 fails
with `HostCallbackLimitExceeded`.

Path construction still happens before JavaScript. A path beyond 32 nodes
returns `PropagationPathLimitExceeded` before capture, target, bubble, Event
creation, once consumption, or cancellation. A later valid path recovers. A
callback error is contained according to the existing policy: the error is
reported, same-node eligible listeners and later phases continue, and
propagation/cancellation state already set by that callback remains effective.

Navigation clears capture and non-capture registrations, once state, snapshots,
and phase state. Stale Elements fail closed through generation checks. A
retained Event exposes only the existing cached safe metadata; no capture stack
or snapshot pointer is exposed.

### JS20 proof and results

The focused proof is `tests/navigator_javascript_js20_test.cpp`, run by
`scripts/smoke-navigator-javascript-js20.ps1`. It exercises parsed HTML,
real host option objects, canonical Element identity, phase-separated identity
and removal, 64/65 capacity, root-to-target capture, target ordering,
stopPropagation and stopImmediatePropagation in capture/target/bubble,
preventDefault visibility and cancellation, once capture, mutation snapshots,
stale-slot reuse, handlerless ancestors, independent branches, 32-node
overflow/recovery, contained capture errors, navigation cleanup, unsupported
inputs, and 100 repeated clicks. The JS20 focused suite reports 463 checks with
0 failures. The JS1-JS20 set contains 18 focused suites: lexer, parser,
runtime, JS6 through JS20. The final sweep passes all 18 focused scripts; the
dedicated JS20 script passes its `GXOS_BARE_METAL` compile and strict hosted
syntax lanes, and the normal `build.bat` native server build links
successfully.

The authentic hosted proof is `navigator-smoke/javascript-js20.html`, with
`navigator-smoke/javascript-js20-target.html` as its navigation target. The
aggregate loads the page through the production HTTP path, uses real layout,
hit testing, mouse down/up, and the production propagation path, then checks
capture order, target ordering, ancestor and target propagation controls,
capture `once`, capture cancellation of a real link, uncancelled navigation,
and navigation cleanup.

The JS20 aggregate adds ten Navigator assertions to the JS19 baseline of 369
total checks. The final hosted result is `372 passed, 7 failed` out of 379
checks: all ten JS20 assertions pass, and the seven known unrelated CSS
failures remain CSS 3C, CSS 3G, CSS 6A, CSS 6B's three checks, and CSS 6C. No
CSS repair is part of JS20.

Bare-metal compilation uses `GXOS_BARE_METAL` with the authoritative focused
source lane and strict `-Wall -Wextra -Werror -pedantic` syntax validation.
JS20 adds no dynamic
containers, hosted-only APIs, RTTI, exceptions, timers, promises, microtasks,
additional event types, MouseEvent/PointerEvent, or broad DOM behavior. Full
DOM Events compatibility remains incomplete.

The recommended JS21 milestone is `event.eventPhase` with standard phase
constants equivalent to `CAPTURING_PHASE = 1`, `AT_TARGET = 2`, and
`BUBBLING_PHASE = 3`. JS22 adds only the compatibility Boolean capture
shorthand; richer listener options and additional event types remain later
work.

## Phase JS21: `event.eventPhase` and Event phase constants

JS21 exposes the current phase of the existing bounded synchronous click
dispatch:

```javascript
event.eventPhase
```

The value is a host-owned, read-only number with the standard conceptual
values:

| Value | Meaning |
| ---: | --- |
| `0` | `Event.NONE`; no active dispatch, including a retained Event after dispatch |
| `1` | `Event.CAPTURING_PHASE`; an ancestor capture callback |
| `2` | `Event.AT_TARGET`; every callback on the clicked Element |
| `3` | `Event.BUBBLING_PHASE`; an ancestor bubble callback |

The target rule is important: a target capture listener reports `2`, not `1`.
Target capture, target `onclick`, and target non-capture listeners all share
the target phase. Ancestor `onclick` remains bubble-only and reports `3`.
`event.currentTarget` continues to identify the Element whose callback is
running, while `event.target` remains the original clicked Element. The
existing `bubbles === true` and `cancelable === true` metadata is unchanged in
all three active phases.

The default realm also exposes a bounded ordinary global object:

```javascript
Event.NONE === 0
Event.CAPTURING_PHASE === 1
Event.AT_TARGET === 2
Event.BUBBLING_PHASE === 3
```

`Event` is a constants namespace, not a new Event constructor. It has four
read-only numeric properties and no additional native functions. Assignments
to the constants and to `event.eventPhase` are deterministic no-ops under the
existing host-property read-only semantics. Unknown members retain normal
missing-property behavior (`undefined`). If explicitly tiny runtime limits
cannot fit the optional constants namespace, the mandatory cached Event phase
property remains bounded and the namespace is omitted rather than changing
the prior limit contract.

### JS21 dispatch state and safety

The runtime adds one `uint8_t` dispatch-scoped phase field. The adapter sets it
once before ancestor capture, once before the target stage, and once before
ancestor bubbling. It is never stored in a listener record, snapshot entry, or
propagation-path entry. `endEventDispatch()` resets it and the cached Event
property to `Event.NONE`, including after propagation control, contained
callback errors, or a recovered dispatch. A path overflow is rejected before
Event creation and callback dispatch, and therefore leaves the phase at `0`.

`stopPropagation()` and `stopImmediatePropagation()` preserve the phase seen
by the stopping callback and the existing same-node/later-node rules. A target
capture stop still allows target `onclick` and target bubble listeners, all at
phase `2`, while suppressing ancestor bubbling. `preventDefault()` remains
independent: a capture callback can set `defaultPrevented`, and target and
bubble callbacks observe it while their phase changes to `2` and `3`. Once
listeners are removed immediately before invocation and report the phase of
the dispatch stage that invoked them. The established callback-error
containment policy continues without prematurely changing phase.

Navigation resets the realm, listener tables, cached Event state, and active
phase state. Retained Event values are ordinary bounded objects with no stack,
snapshot, or path pointers; after dispatch their `eventPhase` safely reports
`0`. Stale Element handles continue to fail closed through generation checks,
and independent trees do not share sticky phase state.

### JS21 bounded memory

The cached Event gains one read-only `eventPhase` property. Compared with the
JS20 runtime baseline, the default realm also gains one cached ordinary
`Event` constants object with four read-only numeric properties. The exact
fixed accounting is:

| Resource | JS21 delta |
| --- | ---: |
| Runtime Event property entries | `+1` per cached Event; `+4` constants at realm initialization |
| Runtime objects | `+1` constants namespace at realm initialization |
| Native functions | `+0` |
| Host objects | `+0` |
| Listener records | `+0` (still 64 × 24 bytes) |
| Propagation path/snapshot | `+0` (still bounded at 32 serials and one 64-entry snapshot) |
| Per-click allocation | `0` after the existing cached Event/wrappers are established |

The dispatch phase itself costs one byte in `RuntimeContext` plus normal
compiler padding already accounted for by the containing runtime object. No
dynamic phase container, per-click Event, listener-record field, or hosted-only
dependency was added. Full DOM Events compatibility remains incomplete.

### JS21 proof and validation

The focused proof is
`tests/navigator_javascript_js21_test.cpp`, run by
`scripts/smoke-navigator-javascript-js21.ps1`. It uses the real parsed
WebDocument and host adapter to prove complete phase order, target
`AT_TARGET` semantics, read-only metadata and constants, currentTarget/target
identity, bubbles/cancelable/defaultPrevented behavior, propagation controls,
once, cross-phase mutation and removal, callback errors, overflow/reset and
recovery, retained Events, stale references, navigation cleanup, independent
trees, unsupported inputs, fixed listener capacity, and 100 repeated clicks.
The focused JS21 suite reports 512 checks with 0 failures in the current JS22
tree; the JS21 baseline's two now-obsolete Boolean-rejection assertions were
replaced by acceptance checks.

The authentic hosted proof is
`navigator-smoke/javascript-js21.html`, with
`navigator-smoke/javascript-js21-target.html` as its navigation target. The
production aggregate loads the fixture over HTTP, executes its parsed inline
script in the production realm, uses real hit-tested controls and links, and
checks phase ordering, same-callback `1,3`, target `2`, once, capture
cancellation, uncancelled navigation, and cleanup.

The complete JS1–JS21 focused set contains 19 suites: lexer, parser, runtime,
JS6 through JS21, and all 19 focused scripts pass. JS21 adds no new event
family, Boolean capture shorthand, passive listeners, constructors,
asynchronous work, timers, promises, microtasks, or broad DOM expansion. The
aggregate Navigator result is `381 passed, 7 failed` out of `388` checks: the
nine JS21 assertions pass, and the seven known unrelated aggregate CSS
failures remain CSS 3C, CSS 3G, CSS 6A, CSS 6B's three checks, and CSS 6C.

The recommended JS22 milestone is Boolean capture shorthand:

```javascript
element.addEventListener("click", handler, true);
element.removeEventListener("click", handler, true);
```

mapping `true` to capture and `false` to non-capture while preserving the
object form `{ capture: true, once: true }`.

## Phase JS22: Boolean capture shorthand for click listeners

JS22 implements the standard Boolean third argument for the existing bounded
`click` listener host methods:

```javascript
element.addEventListener("click", handler, true);  // capture
element.addEventListener("click", handler, false); // bubble
element.removeEventListener("click", handler, true);
element.removeEventListener("click", handler, false);
```

The Boolean form is an argument-normalization extension, not a new listener
kind. `true` means `capture: true`, and `false` means `capture: false`. The
existing two-argument add/remove form has the same normalized state as
`false`. The richer object form remains fully supported and is still required
for `once` or any future listener metadata:

```javascript
element.addEventListener("click", handler, {
    capture: true,
    once: true
});
```

### JS22 options normalization and identity

The runtime-aware host-call boundary inspects the existing runtime value type
and passes the same two Boolean fields to the established listener operation:

| Third argument | `capture` | `once` |
| --- | ---: | ---: |
| omitted | `false` | `false` |
| explicit `undefined` | `false` | `false` |
| Boolean `true` | `true` | `false` |
| Boolean `false` | `false` | `false` |
| options object | parsed `capture` member | parsed `once` member |

Options objects retain the JS18–JS21 validation rules. Boolean syntax does not
perform JavaScript truthiness conversion: `1`, `0`, strings, and other
primitive values remain deterministic `HostInvalidValue` failures. The
repository's existing null policy is also preserved: `null` is rejected as
`HostInvalidValue`. Unknown members on an otherwise valid options object keep
their prior ignored-member behavior.

Listener identity remains exactly `(Element, event type, Function identity,
capture bit)`. Consequently, Boolean and object syntax duplicates are no-ops,
and a duplicate does not consume a slot:

```javascript
element.addEventListener("click", handler, true);
element.addEventListener("click", handler, { capture: true }); // duplicate
```

The same callback may still have one capture and one bubble registration. A
Boolean removal supplies only the capture identity bit; it ignores `once`, so
an object registration `{ capture: true, once: true }` can be removed with
`removeEventListener("click", handler, true)`. Capture and bubble registrations
therefore remain independently removable across syntax forms.

### JS22 dispatch and regressions

No dispatch redesign is required. Boolean registrations enter the same fixed
64-entry global listener table, 24-byte listener records, registration
sequence ordering, bounded propagation path, phase-aware snapshots, stale-slot
checks, callback-error containment, once handling, navigation cleanup, and
cached Event metadata used by JS21. A target Boolean capture listener reports
`AT_TARGET` (`2`), while an ancestor Boolean capture listener reports
`CAPTURING_PHASE` (`1`) and an ancestor Boolean bubble listener reports
`BUBBLING_PHASE` (`3`). Target ordering remains capture listener, `onclick`,
then non-capture listener.

The focused proof is
`tests/navigator_javascript_js22_test.cpp`, run by
`scripts/smoke-navigator-javascript-js22.ps1`. It uses the real parser,
runtime, WebDocument, host adapter, and authentic dispatch path to cover
Boolean/object equivalence, two-argument equivalence, duplicate capacity,
capture-aware and bubble-aware removal, cross-form removal, same-callback
phases, target phases, `onclick` ordering, multiple-listener ordering,
object-only once, malformed inputs, propagation controls, cancellation and
`defaultPrevented`, mutation, callback errors, path overflow/recovery,
navigation/stale references, the 64/65 capacity boundary, and 100 mixed
shorthand clicks. The focused JS22 suite reports 308 checks with 0 failures.

The authentic hosted proof is
`navigator-smoke/javascript-js22.html`, with
`navigator-smoke/javascript-js22-target.html` as its navigation target. It
uses real parsed Boolean literals, nested layout, hit testing, capture,
target, bubbling, cross-form removal, propagation controls, cancellation, and
ordinary link navigation. The production aggregate adds 10 JS22 checks; all
10 pass, including navigation cleanup.

### JS22 bounded memory

JS22 adds no listener record, propagation-path entry, snapshot entry, Event
object, dispatch-state field, host object, or native function. The normalized
Boolean is held only in the existing call-local `capture` field. Before and
after JS22, the bounded accounting is:

| Resource | Before JS22 | After JS22 | Delta |
| --- | ---: | ---: | ---: |
| Listener records | `64 × 24 = 1536` bytes | `64 × 24 = 1536` bytes | `0` |
| Event properties | `10` cached properties | `10` cached properties | `0` |
| Host objects | existing bounded set | existing bounded set | `0` |
| Native functions | existing bounded set | existing bounded set | `0` |
| Per-click allocation | `0` after cache/wrappers | `0` after cache/wrappers | `0` |

The focused 100-click mixed stress proof confirms fixed Event, host-object,
listener-table, and once-slot behavior. No dynamic options object, temporary
STL container, RTTI, exception, or shorthand-specific callback table was
added. Full DOM Events compatibility remains incomplete.

### JS22 validation result

The JS22 focused script passes with `GXOS_BARE_METAL`, and its strict
`-Wall -Wextra -Werror -pedantic` adapter/runtime syntax validation passes.
The native hosted production build also passes through `build.bat`; the
existing hosted build emits unrelated pre-existing warnings but exits
successfully.

The complete JS1–JS22 focused set contains 20 suites: lexer, parser, runtime,
and JS6 through JS22. All 20 focused scripts pass. The aggregate Navigator
run reports `391 passed, 7 failed` out of `398` checks: all 10 JS22 aggregate
checks pass, so JS22 adds no failures. The seven known unrelated CSS failures
remain CSS 3C, CSS 3G, CSS 6A, CSS 6B's three checks, and CSS 6C.

Limitations remain intentional: only `click` is supported; richer listener
options require object syntax; `passive`, additional EventListenerOptions
fields, MouseEvent, PointerEvent, keyboard/input events, timers, promises,
microtasks, asynchronous queues, and broad DOM expansion are out of scope.
The recommended JS23 direction is a bounded keyboard event foundation,
starting with `keydown`/`keyup` and the smallest useful `key`/`code` metadata
representation.

## Phase JS23: bounded keyboard events

JS23 adds the first keyboard-event slice to the existing Event implementation:

```javascript
document.addEventListener("keydown", function (event) {
    // event.type, event.key, and event.code are available here.
});

input.addEventListener("keyup", function (event) {
    // The released key uses the same bounded key/code mapping.
});
```

The native path is the existing Navigator keyboard path: guideXOS key
messages are normalized by `Navigator::handleKeyPress`, the focused form
control is selected when one owns focus, and otherwise the document host is
used as the fallback target. The adapter then sends the event through the
same bounded listener table, propagation path, phase handling, cached Event,
and callback invocation used by the earlier event milestones. For keyboard
events the path is focused element, its DOM ancestors, then the document host,
so capture, target, and bubble listeners all observe the normal dispatch
order. JavaScript observation does not replace the existing textbox default
editing path; `preventDefault()` suppresses that default key action through
the existing synchronous dispatch result.

`event.key` is a read-only cached property for the bounded Navigator mapping:
letters (`a`/`A` through `z`/`Z`), digits, space, Enter, Escape, Backspace,
Tab, the four arrow keys, Delete, Home, End, PageUp, PageDown, Shift,
Control, and Alt. `event.code` is read-only and covers `KeyA`–`KeyZ`,
`Digit0`–`Digit9`, `Enter`, `Escape`, `Backspace`, `Tab`, `Space`, the four
arrow codes, and the bounded navigation/modifier codes. Key-up uses the same
mapping as key-down. Key and code strings are cached in the runtime string
store, so repeated hardware transitions do not create permanent per-event
strings or JavaScript objects.

This phase intentionally does not claim the full `KeyboardEvent` standard.
It does not add `keypress`, `beforeinput`, `input`, composition/IME events,
modifier properties such as `ctrlKey` or `shiftKey`, repeat/location/keyCode
metadata, a browser keyboard-layout database, or browser-perfect physical
scancode reporting. The current guideXOS message carries a virtual key code
but no left/right scancode distinction, so `Shift`, `Control`, and `Alt` use
the accurate generic bounded representation. Unknown virtual keys are not
given manufactured key/code names.

The focused proof is
`tests/navigator_javascript_js23_test.cpp`, run by
`scripts/smoke-navigator-javascript-js23.ps1`. The hosted fixture is
`navigator-smoke/javascript-js23.html`; the production smoke path injects
key-down and key-up transitions through `Navigator::SmokeKeyPress`, which
enters the same input bridge and dispatch path as native input. The fixture
also verifies focused-element targeting, document/ancestor capture and
bubble, `once`, Boolean capture shorthand, removal, propagation controls,
key identity, and ordinary text insertion. The fixed 64-registration listener
capacity and existing bounded propagation/runtime lifetime safeguards remain
unchanged.

### JS23 validation result

The dedicated JS23 suite reports 355 checks with 0 failures. Its focused
smoke script passes the `GXOS_BARE_METAL` compile and strict
`-Wall -Wextra -Werror -pedantic` runtime/adapter syntax lanes. The complete
available focused set contains 21 suites: lexer, parser, runtime, and JS6
through JS23; all 21 scripts pass. The production `build.bat` also completes
successfully.

The hosted aggregate reports `394 passed, 7 failed` out of `401` checks. Both
JS23 aggregate assertions pass; the seven failures are the established
unrelated CSS baseline checks: CSS 3C, CSS 3G, CSS 6A, CSS 6B's three checks,
and CSS 6C. The broader `build-kernel.bat` validation currently stops before
the kernel build on the repository's existing Mbed TLS configuration error
for partial ECC acceleration/ECDHE-RSA prerequisites. No QEMU JS23 proof is
claimed from that blocked kernel build.

## Phase JS24: focus, blur, focusin, and focusout events

JS24 exposes authentic focus transitions from the Navigator's existing bounded
form-focus model. It does not create a second JavaScript focus state. The
authoritative owner remains `WebDocument::formRuntimeState`, with
`Navigator::focusDocumentInput` changing the owner and
`Navigator::clearDocumentFocus` clearing it. Mouse activation of supported form
controls and labels, keyboard Tab/Shift+Tab traversal, and the existing
deactivation/navigation cleanup therefore share one event boundary. The
existing CSS `:focus` invalidation and caret/keyboard activation state remain
attached to that same transition.

For a focus change from control A to control B, the bounded event order is:

```text
A: blur, focusout
B: focus, focusin
```

The loss pair is delivered before the new owner is installed, and the gain pair
is delivered after B becomes the authoritative owner. Clearing document focus
emits `blur` then `focusout` for the old owner before the owner is cleared.
Navigation performs that loss dispatch before the old JavaScript realm is
detached, so old-document cleanup is observable while its listeners still
exist. Requesting focus for the already focused element is a strict no-op: it
does not dispatch duplicate events, change the focus origin, or consume a
listener/dispatch slot.

### JS24 event semantics

All four names use the existing generic cached `Event` object. The runtime
refreshes `type`, `target`, `currentTarget`, `eventPhase`, `bubbles`,
`cancelable`, and `defaultPrevented` for each dispatch, while the object
identity remains bounded and reusable. `focus` and `blur` use the normal
capture/target phases but have `bubbles === false`; ancestor non-capture
listeners are skipped. `focusin` and `focusout` use the same path with
`bubbles === true`, so ancestor capture and bubble listeners observe them.
Focus events are non-cancelable in this slice, and `preventDefault()` does not
alter the authoritative focus owner or the existing form default behavior.

The path is the focused form control, its structural DOM ancestors, and the
document host. Capture, target, and bubbling dispatch reuse the JS23 listener
snapshot and propagation controls, including registration order,
`stopPropagation()`, `stopImmediatePropagation()`, Boolean capture shorthand,
`once`, removal, stale-listener checks, and callback-error containment. The
single global listener table remains capped at 64 records; JS24 adds no
listener record shape or per-event allocation.

The implementation intentionally exposes only the generic Event surface. It
does not add `FocusEvent`, `relatedTarget`, programmatic `focus()` or `blur()`
methods, focus-visible styling, shadow-DOM retargeting, or asynchronous event
queues. The bubbling `focusin`/`focusout` pair is present, but broader DOM focus
management remains outside this milestone.

### JS24 validation result

The focused proof is
`tests/navigator_javascript_js24_test.cpp`, run by
`scripts/smoke-navigator-javascript-js24.ps1`. It uses the real parser,
runtime, WebDocument, host adapter, and bounded harness transition boundary to
cover non-bubbling focus/blur with ancestor capture, bubbling focusin/focusout,
target/currentTarget/phase metadata, same-element no-op behavior, A-to-B
ordering, focus clearing, cancellation semantics, registration order,
`once`, removal, Boolean capture, propagation controls, the 64/65 listener
capacity boundary, and JS23 keyboard retargeting/text editing. The focused
JS24 suite reports 214 checks with 0 failures, and its GXOS_BARE_METAL plus
strict `-Wall -Wextra -Werror -pedantic` adapter/runtime lanes pass.

The hosted fixture is
`navigator-smoke/javascript-js24.html`. The production aggregate injects
real mouse focus, key-down/key-up, A-to-B mouse transition, deactivation, and
navigation cleanup through the same Navigator input bridge used by the
application. All 7 JS24 aggregate checks pass. The complete available focused
set now contains 22 suites: lexer, parser, runtime, and JS6 through JS24; all
22 scripts pass. The hosted aggregate reports `401 passed, 7 failed` out of
`408` checks. The seven failures are unchanged unrelated CSS baseline checks:
CSS 3C, CSS 3G, CSS 6A, CSS 6B's three checks, and CSS 6C.

The native hosted build completes successfully through `build.bat`, with the
repository's existing unrelated warning volume. The bare-metal probe still
stops before kernel compilation at the existing Mbed TLS configuration errors
in `third_party/mbedtls/library/mbedtls_check_config.h`:
`Unsupported partial support for ECC curves acceleration` and
`MBEDTLS_KEY_EXCHANGE_ECDHE_RSA_ENABLED defined, but not all prerequisites`.
Because the kernel build is blocked at that dependency check, no full-kernel
or QEMU JS24 proof is claimed.

The recommended JS25 direction is a bounded programmatic focus API, starting
with `element.focus()` and `element.blur()` routed through the same authoritative
transition boundary and preserving the no-op, ordering, cancellation, and
navigation cleanup rules. A later milestone can then add `input`/`change`
events only after their default-action and mutation semantics are specified.

## Phase JS25: programmatic element focus control

JS25 adds ordinary host-backed DOM methods:

```javascript
element.focus();
element.blur();
```

These are receiver-aware methods resolved through the existing Navigator DOM
host dispatch path. The host handle's authentic element serial is mapped back
to the current Navigator document and requests the existing
`Navigator::focusDocumentInput` / `Navigator::clearDocumentFocus` transition.
There is no JavaScript-only focused flag: `WebDocument::formRuntimeState`
remains the single authoritative owner, and JS24 focus-event synthesis runs
from that state transition.

The JS24 ordering is therefore preserved. For A to B the observable order is
`A blur`, `A focusout`, `B focus`, `B focusin`; clearing B emits `B blur`, then
`B focusout`. `focus()` on the current owner is a strict no-op, and `blur()` on
an element that does not own focus is a strict no-op. The methods use the
existing void host-call convention and return no meaningful value.

Focusability is deliberately bounded by Navigator's existing form-control
eligibility rules: complete, supported, visible, enabled controls with valid
layout geometry. This includes the existing supported form-control classes
where they are represented by that model; it does not add `tabindex`, arbitrary
focusable elements, CSS focusability, or a browser-complete disabled-element
model. Non-focusable receivers are safe no-ops and cannot corrupt another
element's owner. Stale or invalid receivers continue to use the normal host
validation path.

Programmatic events reuse the JS24 generic cached `Event` object and the JS23
listener registry. `focus` and `blur` remain capture-enabled and non-bubbling;
`focusin` and `focusout` remain bubbling. Target, currentTarget, eventPhase,
bubbles, cancelable, listener ordering, Boolean capture shorthand, once,
removal, stopPropagation, stopImmediatePropagation, and non-cancelability are
unchanged. `preventDefault()` cannot block the ownership transition. The
global listener registry remains capped at 64 records and focus calls allocate
no permanent per-call listener structure.

Focus requests made from a focus/blur/focusout callback are kept synchronous
but deferred until the current event dispatch has completed. A single bounded
pending request slot is used (the last request wins), with a 16-redirect cap for
mutually recursive callbacks. This preserves the complete old transition
before a redirect starts, avoids reusing Event metadata while an outer callback
is still active, and gives deterministic, memory-safe behavior without an
asynchronous focus queue. The same post-dispatch drain handles a keyboard
callback that requests `blur()`.

JS23 keyboard targeting follows the same owner. After `input.focus()`, the
normal Navigator key transition dispatches `keydown`/`keyup` to that element
and existing text editing remains on the same input path. After
`input.blur()`, the no-focus fallback is the existing document target; JS25
does not introduce a second keyboard-target state. JS30 later exposes that
same authoritative owner through the read-only `document.activeElement`
projection.

### JS25 validation result

The focused proof is
`tests/navigator_javascript_js25_test.cpp`, run by
`scripts/smoke-navigator-javascript-js25.ps1`. It reports 219 checks with 0
failures, including direct focus/blur, all JS24 propagation and metadata
semantics, receiver identity, focusability no-ops, repeated A/B/A transfers,
re-entrant focus redirects, JS23 keyboard targeting, text editing, stale
handles, and the unchanged 64-listener bound. The GXOS_BARE_METAL and strict
`-Wall -Wextra -Werror -pedantic` adapter/runtime lanes pass.

The hosted fixture is `navigator-smoke/javascript-js25.html`; its four
aggregate checks all pass, including direct methods, A-to-B/clear event order,
capture/bubble evidence, and JS23 keyboard targeting followed by programmatic
blur. The complete available focused set now contains 23 suites: lexer, parser,
runtime, and JS6 through JS25; all 23 scripts pass. The hosted aggregate
reports `405 passed, 7 failed` out of `412` checks. The seven failures remain
the unrelated CSS baseline checks: CSS 3C, CSS 3G, CSS 6A, CSS 6B's three
checks, and CSS 6C; no JavaScript or focus-related aggregate check fails.

The normal native `build.bat` completes successfully. The current
`build-kernel.bat` lane does not reach the previously reported Mbed TLS check:
it stops earlier in the bootloader Visual Studio build with MSBuild `MSB6001`
(`Item has already been added`, keys `Path` and `PATH`) while launching
`CL.exe`. This is an existing external bootloader/environment blocker, so no
full-kernel or QEMU JS25 execution proof is claimed and no TLS configuration
was changed.

JS26 should remain narrow: add bounded `input` and `change` events only after
the existing focus ownership, keyboard/text-editing path, and DOM value
mutation seam are specified. This phase does not implement those events or
any broader focus API such as `FocusEvent`, `relatedTarget`, focus options,
`tabindex`, autofocus, or sequential Tab expansion. The later JS30
`activeElement` projection is a read-only view of the existing owner, not a
new focus subsystem.

## Phase JS26: form editing events

JS26 adds the bounded form-editing event pair:

```javascript
element.addEventListener("input", handler);
element.addEventListener("change", handler);
```

The supported controls are text-like `<input>` elements already represented by
Navigator (`text`, `password`, `search`, `email`, `url`, and `number`) plus
`<textarea>`. Checkbox, radio, select, button, submit, reset, and other
non-text controls do not gain JS26 editing events. The parser now completes the
textarea's form metadata after its logical serial is assigned, so an eligible
textarea participates in the same native focus and edit path as a text input.

Native printable-key, backspace, and delete editing still mutates the
authoritative `DocBlock::inputValue`. The event order for a changed key edit is
`keydown`, `input`, `keyup`; arrows and other caret-only keys do not emit
`input`. The `input` event is dispatched only after the value mutation succeeds,
and a listener observes the new value through `event.target.value` and the
element's `.value` getter. A failed or no-op edit emits neither event.

The host exposes `.value` for the supported text controls. Reading it returns
the current authoritative value. Assigning a primitive string, number, or
boolean updates that value and the rendered/form metadata within the existing
bounded script-mutation budget, but script assignment emits no implicit
`input` or `change`; the assignment is not treated as user editing. Values are
capped at the existing JS26 host bound of 256 UTF-8 bytes, and unsupported
receivers cannot mutate unrelated document blocks.

Each eligible control gets a small runtime edit-session baseline when it gains
focus. On focus loss, Navigator preserves the JS25 loss sequence (`blur`, then
`focusout`), compares the final authoritative value with that baseline, and
dispatches exactly one `change` when the values differ. The `change` dispatch
occurs after `focusout` and before the next control's `focus`/`focusin` pair.
Programmatic focus transfer, `blur()`, window deactivation, and navigation use
the same commit boundary. Reverting to the baseline, focusing an unchanged
control, repeating a focus request, or clearing an already-cleared focus emits
no change. A committed session becomes the next baseline, and document
replacement discards all old baselines.

Both names use the existing generic cached `Event` object. `input` and
`change` bubble through the structural ancestor path, are non-cancelable, and
there is no `InputEvent`, `data`, `inputType`, `isComposing`, `beforeinput`,
selection API, or asynchronous event queue. Existing capture/target/bubble
ordering, target/currentTarget/phase metadata, `once`, removal, Boolean
capture, stop-propagation controls, stale-listener checks, and the global
64-listener limit remain unchanged. Listener-side `.value` rewrites update the
current value without recursively synthesizing another `input`; focus redirects
requested from input/change callbacks remain bounded by the JS25 deferred
transition drain.

### JS26 validation result

The focused proof is
`tests/navigator_javascript_js26_test.cpp`, run by
`scripts/smoke-navigator-javascript-js26.ps1`. It covers input/change metadata,
native edit ordering, backspace/delete and revert behavior, `.value` getter and
setter rules, re-entrant listeners, focus redirects, propagation controls,
listener capacity, textareas, and document replacement. The hosted fixture is
`navigator-smoke/javascript-js26.html`; the production aggregate drives a real
key-down/key-up edit and a real focus transfer to verify the end-to-end
`keydown` → `input` → `keyup` and committed `change` boundaries.

The focused JS26 suite reports 497 checks with 0 failures, and its
`GXOS_BARE_METAL` plus strict `-Wall -Wextra -Werror -pedantic` adapter/runtime
lanes pass. The complete available focused set contains 24 suites: lexer,
parser, runtime, and JS6 through JS26; all 24 pass. The normal native
`build.bat` also completes successfully. The hosted aggregate reports 408
passed and 7 failed out of 415 checks: the three JS26 checks pass, and the
seven failures remain the unrelated CSS baselines CSS 3C, CSS 3G, CSS 6A,
three CSS 6B checks, and CSS 6C. The kernel retry reaches the kernel build but
remains blocked by the existing Mbed TLS configuration errors in
`third_party/mbedtls/library/mbedtls_check_config.h`; no full-kernel or QEMU
JS26 proof is claimed.

JS27 implements the first of those bounded extensions: checkbox, radio, and
single-select state plus authentic user `input`/`change` transitions. A future
phase may specify `InputEvent`/`beforeinput` and selection independently; those
semantics are still not inferred from JS27's generic `Event` contract.

## Phase JS27: checkbox, radio, and select form controls

JS27 extends the existing JS26 form-event seam to the three discrete controls
already parsed by Forms-lite:

```javascript
checkbox.checked = true;
radio.checked = true;
select.value = "two";
```

The host exposes `.checked` on checkbox and radio elements and `.value` on a
bounded single-select. Reads return the authoritative runtime projection, so
listeners see the same state through `event.target.checked` or
`event.target.value` and through the element getter. Unsupported receivers,
`<select multiple>`, unknown option values, disabled controls, and malformed
metadata fail closed without mutating an unrelated block. Select assignment is
an exact value match; an unknown value retains the current selection. The
bounded select model does not add option collections, selected-index APIs,
keyboard search, or general browser form APIs.

Script assignment is deliberately silent. Boolean conversion for `.checked`
uses the runtime's bounded primitive conversion rules, radio assignment to
`true` unchecks matching peers, and `.value` assignment updates the selected
option and rendered/form projections without synthesizing `input` or `change`.
All script writes use the existing per-document mutation budget and host value
bounds; there is no second JavaScript-side form model.

Authentic pointer and Space-key activation mutate the native runtime state
first. A changed checkbox toggle, radio selection, or single-select step then
dispatches exactly one generic `input` followed immediately by one generic
`change`. Both events use the existing cached Event object, bubble through the
structural ancestor path, and are non-cancelable. The listener-visible order is
`keydown`, state transition, `input`, `change`, `keyup` for Space activation;
pointer activation follows the existing click path and commits the state
before the form events. Re-selecting an already checked radio, a disabled
control, or a select with no enabled next option emits neither event.

Radio exclusivity uses the existing bounded native group rule: matching form
ownership, fieldset ownership, and non-empty name are required. A scripted or
user selection unchecks only matching peers, and all peer projections are
updated before the target's event dispatch. Listener-side assignments remain
silent and can safely perform bounded re-entrant normalization. Checkbox/radio
`:checked` styling, accessibility metadata, form serialization projections,
and select option selection are recomputed from the same runtime state.

The JS26 generic listener registry remains the only listener store. JS27 adds
no separate discrete-control listener list, preserves the global 64-listener
cap, and keeps existing capture/target/bubble ordering, `once`, removal,
`stopPropagation`, stale-document guards, and navigation cleanup unchanged.
The implementation remains limited to the existing fixed-capacity runtime
table and the already-supported document controls.

### JS27 validation result

The dedicated proof is
`tests/navigator_javascript_js27_test.cpp`, run by
`scripts/smoke-navigator-javascript-js27.ps1`. It covers initial checked and
selected projections, silent script assignments, checkbox transitions, radio
group exclusivity, re-entrant listeners, select stepping and exact-value
assignment, input/change metadata and propagation, listener removal and
capacity, focus redirection, and document reset. The hosted fixture is
`navigator-smoke/javascript-js27.html`; the production aggregate performs
real checkbox, radio, select, and Space-key activations, checks listener-visible
ordering, verifies the script-silent marker, and confirms navigation cleanup.

The focused JS27 suite reports 304 checks with 0 failures, including its
`GXOS_BARE_METAL` and strict `-Wall -Wextra -Werror -pedantic` adapter/runtime
compile lanes. The complete matrix contains lexer, parser, runtime, and JS6
through JS27: all 25 suites pass. The normal native `build.bat` completes
successfully. The hosted aggregate passes all JS27 checks and reports 416
passed and 7 failed out of 423 checks; the seven failures remain the unrelated
CSS baselines CSS 3C, CSS 3G, CSS 6A, three CSS 6B checks, and CSS 6C. The
kernel retry builds the bootloader and reaches the kernel, then remains blocked
by the existing Mbed TLS configuration errors in
`third_party/mbedtls/library/mbedtls_check_config.h` (partial ECC acceleration
and incomplete ECDHE-RSA prerequisites); no full-kernel or QEMU JS27 proof is
claimed.

## Phase JS28: form submission and default actions

JS28 adds the first form default-action event to the existing generic event
dispatcher:

```javascript
form.addEventListener("submit", handler);
```

The event target is the containing `<form>`. It bubbles, is cancelable, and
uses the same cached `Event` object, capture/target/bubble path, listener
registry, propagation controls, stale-document checks, and 64-registration
bound as the earlier event types. `preventDefault()` affects only the current
submission dispatch; `defaultPrevented` is observable by later listeners and
by Navigator's native activation seam. The event is not a `SubmitEvent` and
does not expose `submitter` or other expanded form-event fields.

The production path is deliberately ordered around the existing native form
implementation. A pointer activation first dispatches the existing `click`
event; a canceled click suppresses activation. An uncanceled submit control
activation then dispatches `submit` on its containing form. If that event is
canceled, Navigator records the bounded cancellation status and suppresses
the existing serializer and navigation/request path. If it is not canceled,
the pre-existing Forms-lite serializer proceeds using the current authoritative
document state. Handler-side `.value` and `.checked` changes therefore affect
the serialized result, while scripted changes remain silent and do not
synthesize `input` or `change`.

Supported activation sources are `<button type="submit">`, `<input
type="submit">`, and the existing focused-button keyboard path for Enter and
Space. `<button type="button">` and `<input type="button">` remain
activation-only and do not submit. Existing reset controls retain their
activation-only behavior; reset default semantics are deferred. Implicit
Enter submission from a text input is not inferred because no such native
Navigator path existed in the audited baseline.

Serialization remains the existing bounded GET/POST behavior: named text
inputs and textareas, checked named checkboxes/radios, and the selected value
of a single-select are URL-encoded from the current `DocBlock` and form
runtime state. Existing action resolution, method/encoding support, local
POST handling, hosted POST transport, and redirect policy are unchanged.
JS28 does not add `requestSubmit()`, `form.submit()`, HTML constraint
validation, `FormData`, successful-control edge cases beyond Forms-lite, or a
second JavaScript-side form model.

### JS28 validation result

The dedicated proof is
`tests/navigator_javascript_js28_test.cpp`, run by
`scripts/smoke-navigator-javascript-js28.ps1`. It reports 492 checks with 0
failures, including submit metadata and document/form propagation order,
cancellation, listener `once`/removal/stop controls, exact event-name
validation, non-cancelable input/change regression, authoritative handler
mutation and cross-form isolation, re-entrant focus and retained-event
lifetime, generation reset, and the unchanged 64-listener capacity.

The hosted fixture is `navigator-smoke/javascript-js28.html`; its seven
aggregate checks cover type=button non-submission, canceled submit with
complete propagation, uncanceled button serialization, input type=submit,
focused Enter activation, and navigation cleanup. All seven JS28 hosted checks
pass. The complete matrix contains lexer, parser, runtime, and JS6 through
JS28: all 26 suites pass. The normal native `build.bat` completes
successfully. The hosted aggregate reports 423 passed and 7 failed out of 430
checks; the seven failures remain the unrelated CSS baselines CSS 3C, CSS 3G,
CSS 6A, three CSS 6B checks, and CSS 6C.

The required `build-kernel.bat` retry builds the bootloader and reaches the
kernel build, but remains blocked by the existing Mbed TLS configuration
errors in `third_party/mbedtls/library/mbedtls_check_config.h` (partial ECC
acceleration and incomplete ECDHE-RSA prerequisites). QEMU was not launched
because no kernel image was produced; no TLS configuration was changed.

## Phase JS29: programmatic `Element.click()` and shared activation defaults

JS29 adds the receiver-aware `element.click()` host method for every live
element handle. It accepts zero arguments, returns `undefined`, fails closed
for stale or non-element receivers, and uses the existing bounded host-call
protocol; no parser special case or general DOM method table was added.

Programmatic activation, pointer click, and keyboard activation now share one
adapter seam. The seam dispatches a bubbling, cancelable generic `click` and
invokes the authoritative Navigator default-action callback only when the
complete path is not canceled. The click path is target → structural
ancestors → document, with the existing capture/target/bubble phases, `once`,
removal, `stopPropagation()`, `stopImmediatePropagation()`, listener-state
mutation, stale-document guards, and 64-registration cap. `preventDefault()`
and propagation stopping remain independent; `defaultPrevented` is local to
the active click and cannot leak into a later or nested dispatch.

The fixed-resource bounds are explicit: 16 nested activation requests, 16
nested cached `Event` dispatch states/objects, and 32 structural propagation
entries plus the document slot for clicks. Nested dispatch restores the outer
event object and flags after the inner click returns. Self-recursive
`element.click()` therefore fails as a bounded host re-entry instead of
allocating an unbounded event stack.

The native default actions are the existing authoritative form and navigation
paths. Uncanceled checkbox clicks toggle once and then emit `input` followed
by `change`; canceled clicks preserve state and emit neither event. Radio
activation selects the target and unchecks only the matching bounded group,
with no redundant transition when it is already selected. Submit-button and
`input type=submit` clicks dispatch `submit` on the containing form after the
click; click cancellation suppresses submit, and submit cancellation suppresses
serialization/navigation. `type=button` remains non-submitting. Existing link
navigation, label forwarding, and bounded single-select activation also pass
through the same seam. Disabled controls remain no-ops, and text controls do
not gain caret or selection behavior from `.click()`.

The keyboard path retains its existing keydown/keyup boundary and repeat and
stale-release guards, while its activation now enters the same click/default
action seam. Pointer activation likewise uses the same seam. Scripted
`.checked`, `.value`, and text `.value` assignments remain silent: they do not
synthesize click, input, or change events.

### JS29 validation result

The dedicated proof is
`tests/navigator_javascript_js29_test.cpp`, run by
`scripts/smoke-navigator-javascript-js29.ps1`. It reports 173 checks with 0
failures, including receiver validation, event metadata and propagation,
preventDefault/default-action ordering, checkbox/radio/submit matrices,
programmatic and nested activation, outer-event restoration, listener
mutation, focus redirection, recursion bounds, stale handles, and listener
capacity. The script also passes the bare-metal and strict adapter/runtime
compile lanes. The hosted fixtures are
`navigator-smoke/javascript-js29.html` and
`navigator-smoke/javascript-js29-target.html`.

The complete focused matrix contains lexer, parser, runtime, and JS6 through
JS29: all 27 suites pass. The normal native `build.bat` completes
successfully. The hosted aggregate reports 435 passed and 7 failed out of 442
checks. All 12 JS29 hosted checks pass, including authentic physical-pointer
click cancellation through the shared seam; the seven remaining failures are
the unrelated CSS baselines CSS 3C, CSS 3G, CSS 6A, three CSS 6B checks, and
CSS 6C. The kernel retry builds the bootloader and reaches the kernel, but
remains blocked by the existing Mbed TLS configuration errors in
`third_party/mbedtls/library/mbedtls_check_config.h`
(partial ECC acceleration and incomplete ECDHE-RSA prerequisites). QEMU was
not launched because no kernel image was produced; no TLS configuration was
changed.

## Phase JS30: `document.activeElement` and authoritative focus projection

JS30 adds the narrow, read-only document projection:

```javascript
document.activeElement
```

The getter reads the existing `WebDocument::formRuntimeState` owner rather than
introducing a second focus model. A focused result is valid only when the
focused logical serial, focused document generation, and current document
generation agree, and the serial still resolves to a supported, visible,
enabled form-control block. The result is the normal generation-bound Element
host value, so repeated `getElementById()` lookup and `activeElement` compare by
the existing host identity rules. With no valid focused control, navigation,
replacement, reload, deactivation, or a stale serial, the getter returns the
existing `null` sentinel. The property is read-only; assignment fails with the
existing `HostPropertyReadOnly` host error and cannot alter native focus.

Reads are side-effect free: they do not dispatch events, mutate runtime state,
allocate listener records, or synthesize focus. Native pointer focus, existing
keyboard Tab traversal, `focus()`/`blur()`, JS29 activation, input/change
commit, and bounded re-entrant focus redirects all continue to use the same
Navigator transition boundary. During a transfer, old-owner `blur`,
`focusout`, and `change` listeners observe the old active element; new-owner
`focus` and `focusin` listeners observe the new one. Focus requests made while
dispatching remain synchronous-but-deferred, last-request-wins, and bounded by
the existing 16-redirect drain. There is no `hasFocus()`, `relatedTarget`,
focus-options dictionary, `tabindex`, autofocus, or new window-focus state in
this phase.

### JS30 validation result

The focused proof is
`tests/navigator_javascript_js30_test.cpp`, run by
`scripts/smoke-navigator-javascript-js30.ps1`. It reports 137 checks with 0
failures and passes the GXOS_BARE_METAL and strict
`-Wall -Wextra -Werror -pedantic` adapter/runtime lanes. Coverage includes
identity and read-only behavior, no-focus fallback, exact old/new event
observation, all supported form-control kinds, input/change visibility,
re-entrant focus and nested activation, lifecycle replacement, stale serial
safety, and keyboard-owner projection.

The hosted fixture is `navigator-smoke/javascript-js30.html`. Its 10 JS30
aggregate checks pass: initial null, programmatic identity, A-to-B transfer,
physical pointer focus, existing Tab traversal, deferred focus redirect,
programmatic and canceled activation, physical blur to null, and navigation
reset. The complete focused matrix contains lexer, parser, runtime, and JS6
through JS30: all 28 suites pass. The normal native `build.bat` completes
successfully. The hosted aggregate reports 445 passed and 7 failed out of 452
checks; the seven failures remain unrelated CSS baselines: CSS 3C, CSS 3G,
CSS 6A, three CSS 6B checks, and CSS 6C. No JS30 hosted check fails.

The required `build-kernel.bat` retry builds the PacMan image and bootloader,
reaches kernel compilation, and stops at the existing Mbed TLS configuration
errors in `third_party/mbedtls/library/mbedtls_check_config.h`:
`Unsupported partial support for ECC curves acceleration` and
`MBEDTLS_KEY_EXCHANGE_ECDHE_RSA_ENABLED defined, but not all prerequisites`.
QEMU was not launched because no kernel image was produced; no TLS
configuration was changed.

## Phase JS31: form reset event and authoritative default actions

JS31 adds the bounded form reset path:

```javascript
form.addEventListener("reset", function (event) {
    event.preventDefault();
});
form.reset();
```

`reset` is the existing generic Event object with `bubbles === true` and
`cancelable === true`. Its target is the form, and it uses the normal
capture/target/bubble path. `preventDefault()` is inspected only after the
complete dispatch; if cancellation is present, the default action does not
touch any control state. Cancellation is dispatch-local and does not leak into
later reset, click, submit, input, or change events.

`form.reset()` is a receiver-checked host method with zero arguments. It and
`<button type="reset">` / `<input type="reset">` share one reset-request seam:
click dispatch and click cancellation happen first for a reset control, then
the form reset Event is dispatched, then the default reset action runs only if
neither boundary canceled it. `stopPropagation()` or
`stopImmediatePropagation()` alone does not cancel the reset default action.
Ordinary buttons remain activation-only and submit buttons remain submit
controls.

The default action restores the document-owned reset state already associated
with the production `WebDocument` form runtime. The bounded
`FormRuntimeControlState` retains mutable default value, default checked, and
default selected-option fields alongside the current projections. Current
text/textarea values remain on `DocBlock::inputValue`; current checkbox/radio
state remains in the runtime table; and current select selection remains on
the select block. No JavaScript-owned reset snapshot or history is created.

Supported text inputs, textareas, checkboxes, radio groups, and single-selects
restore their stored defaults silently. Reset does not synthesize `input`,
`change`, or click events. Radio restoration is group-aware and performs a
bounded all-off/first-initially-checked pass, so malformed multiple-checked
markup uses the existing document-order interpretation without claiming full
HTML parser correction semantics. A select with no explicit selected option
restores the parser-established fallback index rather than recomputing a new
JS31 rule.

Reset also synchronizes the focused text control's existing edit-session
baseline. Reset does not blur or move focus; `document.activeElement` therefore
continues to project the same native owner where the surrounding activation
path leaves it. A later blur cannot manufacture a stale `change` from the
pre-reset value. Listener mutations run before the default action: an
uncanceled reset restores defaults over those mutations, while a canceled reset
leaves the listener's mutation in place. Nested reset requests use the existing
16-level bounded event/re-entry policy and fail closed at the bound. A reset
listener that replaces the document is revalidated before any default action,
and document replacement clears the old default table with the rest of the
document state.

The focused proof is
`tests/navigator_javascript_js31_test.cpp`, run by
`scripts/smoke-navigator-javascript-js31.ps1`. It covers reset metadata,
receiver and stale-handle validation, cancellation, all supported control
families, parser/user/script mutation provenance, silent restoration, edit
baselines, focus projection, listener propagation/options, click-vs-reset
cancellation, cross-form and unowned-control isolation, nested submit/reset,
bounded recursive reset, and document replacement. `defaultValue` and
`defaultChecked` were intentionally deferred to JS33, where they can project
the same reset fields instead of creating a second default model.

The hosted fixture is `navigator-smoke/javascript-js31.html`. It exercises
mutate-then-reset, canceled reset, canceled reset-button click, input reset,
ordinary-button routing, submit-listener reset, cross-form isolation,
unowned-control isolation, silent input/change behavior, and focused reset
through the production Navigator activation seams. Its final reset-then-submit
path also checks that the existing GET serializer sees the restored text,
checkbox, radio, and select defaults. Full HTML form compliance, FormData,
requestSubmit, constraint validation, multiple select, dynamic options, and
named form collections remain deferred.

The focused JS31 suite passes all 258 checks, and the complete JS6-through-JS31
matrix passes all 26 repository scripts. The normal native `build.bat` also
completes successfully. The hosted aggregate reports 454 passed and 7 failed
out of 461 checks; all seven failures remain the unrelated CSS baselines CSS
3C, CSS 3G, CSS 6A, three CSS 6B checks, and CSS 6C. Every JS31 hosted check
passes, including reset cancellation, reset-button click cancellation, focus
preservation, cross-form isolation, silent restoration, and reset-then-submit
serialization.

The required `build-kernel.bat` retry builds the PacMan image and bootloader,
then reaches kernel compilation and stops at the existing Mbed TLS configuration
errors in `third_party/mbedtls/library/mbedtls_check_config.h`:
`Unsupported partial support for ECC curves acceleration` and
`MBEDTLS_KEY_EXCHANGE_ECDHE_RSA_ENABLED defined, but not all prerequisites`.
QEMU was not launched because no kernel image was produced; no TLS
configuration was changed.

## Phase JS32: `relatedTarget` and `document.hasFocus()`

JS32 adds two bounded focus-observation surfaces:

```javascript
event.relatedTarget
document.hasFocus()
```

`relatedTarget` is a read-only property on the existing generic Event object.
It is either the canonical generation-bound Element host object for the other
side of a focus transition or the existing `null` sentinel. The production
Navigator transition seam supplies only the logical serial of the opposite
focus owner; the adapter resolves that serial against the currently installed
document and fails closed to `null` when it is absent or stale. This preserves
the repository's existing host-object identity rule, so
`event.relatedTarget === document.getElementById("b")` is true for the same
document.

The event values follow the existing authoritative focus timing:

- The initial `focus`/`focusin` gain has `relatedTarget === null`.
- A transfer from A to B gives A's `blur`/`focusout` a related target of B and
  gives B's `focus`/`focusin` a related target of A.
- A clear has `null` on `blur`/`focusout`; there is no incoming focus event.
- Same-owner `focus()` and non-owner `blur()` remain no-ops and dispatch no
  synthetic transfer.

`relatedTarget` is included in the runtime's per-dispatch-depth Event cache.
Nested focus or click dispatch therefore receives its own Event object while
the outer listener's `target`, `currentTarget`, phase, and `relatedTarget`
remain stable before and after the nested call. Non-focus events refresh the
same cached object with `relatedTarget === null`, preventing metadata leakage
from a prior focus event. The existing 16-level dispatch/re-entry bounds still
apply.

`document.hasFocus()` is a zero-argument, read-only host method. This build has
no separate OS/window focus owner in the Navigator JavaScript contract, so its
narrow deterministic meaning is: true exactly when the current document has a
valid authoritative supported, visible, enabled form-control focus owner—the
same validated projection used by `document.activeElement`. It is false for a
new document, after focus clear, after replacement, or when the stored serial
or generation is stale. The query is pure: it does not dispatch events, mutate
focus state, allocate listeners, or synthesize focus. A stale document receiver
fails through the normal generation validation path, and extra arguments are
rejected with the existing `HostInvalidValue` error.

The implementation keeps `WebDocument::formRuntimeState` authoritative. It
does not add a second focus registry, window-focus flag, or JavaScript-owned
related-target snapshot. Production programmatic focus, pointer focus, and
existing keyboard Tab traversal all reach the same Navigator transition seam;
only the opposite logical serial is threaded into the shared event adapter.

### JS32 validation result

The focused proof is
`tests/navigator_javascript_js32_test.cpp`, run by
`scripts/smoke-navigator-javascript-js32.ps1`. It reports 273 checks with 0
failures and passes both the optimized `GXOS_BARE_METAL` harness and the
strict `-Wall -Wextra -Werror -pedantic` adapter/runtime syntax lane. Coverage
includes initial/transfer/clear related-target mappings, canonical identity,
read-only assignment, same-owner and unrelated blur no-ops, hasFocus stage
observations, non-focus cache hygiene, nested and re-entrant event lifetime,
stale related serials, stale document receivers, document replacement, and
the 64-listener capacity boundary.

The hosted fixture is `navigator-smoke/javascript-js32.html`. The production
`navigator.smoke` aggregate drives its programmatic A/B focus, clear, nested
redirect, physical pointer click, keyboard input plus Tab traversal, reset and
submit non-focus events, and navigation replacement. Its in-page state text
records deterministic `active`, `hasFocus`, related-target, and cache-hygiene
flags so the aggregate does not depend on browser-only inspection APIs.

The complete JS6-through-JS32 matrix passes all 27 repository scripts. The
normal native `build.bat` completes successfully, and the hosted aggregate
reports 464 passed and 7 failed out of 471 checks. All ten JS32 hosted checks
pass; the seven failures are the same unrelated CSS baselines CSS 3C, CSS 3G,
CSS 6A, three CSS 6B checks, and CSS 6C. The required `build-kernel.bat`
retry builds the PacMan image and bootloader, then stops at the existing Mbed
TLS configuration errors in
`third_party/mbedtls/library/mbedtls_check_config.h`:
`Unsupported partial support for ECC curves acceleration` and
`MBEDTLS_KEY_EXCHANGE_ECDHE_RSA_ENABLED defined, but not all prerequisites`.
QEMU was not launched because no kernel image was produced; no TLS
configuration was changed.

Window/OS activation state, focus options, autofocus, `tabindex` traversal
expansion, pointer hover/mouseover related-target events, page visibility,
`window.hasFocus()`, and full browser focus navigation remain deferred. This
phase also does not claim a separate browsing-context focus model.

## Phase JS33: default form-state DOM properties

JS33 exposes the existing document-owned reset state through these writable
host properties:

```javascript
input.defaultValue
textarea.defaultValue
checkbox.defaultChecked
radio.defaultChecked
select.selectedIndex
```

`defaultValue` and `defaultChecked` are distinct from current `value` and
`checked`. Their setters update only the reset defaults, are silent, and do not
dispatch `input`, `change`, `reset`, or `click`. A later uncanceled
`form.reset()` reads those same mutable fields, so defaults persist across
repeated resets. Reset listeners run first; a listener's default mutation is
therefore used by an uncanceled reset, while a canceled reset leaves both the
current value and the listener's default mutation intact.

For radios, parser-time defaults and scripted `defaultChecked = true` use the
same bounded group policy: the first checked default in document order wins.
Changing a default radio never changes the current radio selection; only reset
applies the default group. This is intentionally narrower than full browser
radio/attribute synchronization.

`select.selectedIndex` is current selection only for single-select controls and
is zero-based. `-1` clears selection and makes `value` empty; an index below
`-1` or beyond the option list is a safe no-op. A valid index updates the same
selection used by `.value`, and `.value = matchingValue` updates the index.
Assignments remain silent. The internal reset selected-option index is not
exposed as a non-standard default-selection property, and `option.defaultSelected`,
options collections, multiple select, and dynamic option mutation remain
deferred.

The focused proof is
`tests/navigator_javascript_js33_test.cpp`, run by
`scripts/smoke-navigator-javascript-js33.ps1`. It covers parser projection,
current/default separation, writable text and checkable defaults, radio-group
normalization, selected-index/value coherence and bounds, silent writes,
reset-listener timing and cancellation, focused baseline synchronization,
event metadata, bounded reset re-entry, document replacement, stale handles,
unsupported members, and read-only regression checks. The hosted fixture is
`navigator-smoke/javascript-js33.html`; its production smoke path covers
initial projection, default mutation, reset restoration, radio/select state,
silent scripted assignment, reset-then-submit, and lifecycle replacement.

The implementation keeps `WebDocument::formRuntimeState` authoritative and
does not reparse attributes or cache defaults globally. Host generation checks
make old handles fail closed after replacement or invalidation. The 64-listener
capacity, event cache, JS26–JS32 editing/focus behavior, and existing
`document.activeElement`/event read-only properties remain unchanged. This is
bounded form-state projection, not full HTML forms DOM compatibility.

## Phase JS34: bounded form/select collections

JS34 adds live, bounded host projections for the existing form controls:

```javascript
form.elements
form.length
select.options
select.length
form.elements[0]
select.options[0]
```

`form.elements` is ordered by document structure and includes supported
`input` controls (text, password, search, email, URL, number, checkbox, radio,
button, submit, and reset), `textarea`, and `select`. Hidden, unsupported, and
`option` nodes are excluded. `select.options` is ordered by the select's
bounded option vector. Both collections expose a read-only `length`, return
`undefined` for non-canonical or out-of-range numeric properties, and reject
replacement writes. The collection itself is canonical for its owner and
document generation, and each indexed element resolves to the same canonical
host object returned by ordinary DOM lookup.

The implementation does not add JavaScript-owned arrays or a second form
model. A collection host stores only its owner serial, generation, and
collection kind; each indexed read scans the bounded structural metadata or
the existing select option vector. Form membership uses parser-recorded
containment (`parentFormSerial`), so controls in another form or outside a
form cannot leak into the projection. The parser now also completes button
metadata after assigning its structural serial, allowing button, submit, and
reset controls to participate in the same collection contract.

Option hosts reuse their existing structural option serials. They expose
read-only `value`, writable `selected`, and writable `defaultSelected`:

- `selected = true` makes that option current, updates `selectedIndex` and
  `value`, and clears the other options; `selected = false` clears the
  single-select when that option is current.
- `defaultSelected = true` selects that option as the single reset default;
  setting it false clears the default only when it is the active default. The
  current selection is not changed by a default write.
- These scripted option writes are silent. They do not synthesize `input`,
  `change`, or `reset`; an uncanceled reset applies the mutable default, and
  submit reads the current selection.

All indexed collection and option operations use the normal host generation
guard. Old form, collection, select, and option handles fail closed after
document invalidation or replacement, while a new document receives
independent collections and defaults. Collection reads are bounded by the
existing structural/control/option caps and do not allocate a second
proportional snapshot.

The focused proof is
`tests/navigator_javascript_js34_test.cpp`, run by
`scripts/smoke-navigator-javascript-js34.ps1`. It reports 221 checks with 0
failures and passes both the optimized `GXOS_BARE_METAL` harness and the
strict `-Wall -Wextra -Werror -pedantic` adapter/runtime syntax lane. Coverage
includes form/select lengths, canonical indexed identity and ordering,
cross-form isolation, bounds and read-only behavior, option current/default
separation, selected-index/value coherence, silent writes, reset and submit,
focus and button activation through indexed controls, stale handles, document
replacement, and JS26/JS30/JS33 regression baselines.

The hosted fixture is `navigator-smoke/javascript-js34.html`. The production
`navigator.smoke` aggregate covers initial collection projection, exclusive
default selection, indexed current selection, focus, reset, current-value
submit, and document replacement. The full repository JavaScript matrix is 32
scripts: JS6 through JS34 plus the lexer, parser, and runtime lanes; all pass.
In the final hosted run, all seven JS34 checks passed; the aggregate reported
477 passed and 7 failed out of 484. Those seven failures are the existing
unrelated CSS 3C, CSS 3G, CSS 6A, three CSS 6B checks, and CSS 6C.
The production `build.bat` completed successfully. The required
`build-kernel.bat` retry built the PacMan image, ramdisk, and UEFI bootloader,
then stopped at the existing Mbed TLS configuration errors in
`third_party/mbedtls/library/mbedtls_check_config.h`: `Unsupported partial
support for ECC curves acceleration` and `MBEDTLS_KEY_EXCHANGE_ECDHE_RSA_ENABLED
defined, but not all prerequisites`. QEMU was not launched because no fresh
kernel image was produced; no TLS configuration was changed.

## JS35: `document.forms` and named form/control lookup

JS35 adds the next bounded projection over the JS34 form-control model:

- `document.forms` is a live, read-only collection with `.length`, canonical
  numeric index access, and exact string lookup.
- Named form lookup checks an exact `id` first, then an exact non-empty `name`;
  duplicate names return the first form in document order. Matching is case
  sensitive, does not use prefixes, and canonical numeric strings such as
  `"0"` remain indexed access.
- `form.elements` keeps the JS34 indexed projection and adds the same exact
  id-first/name-second lookup. Duplicate control names return the first
  matching supported control in document order; controls owned by another
  form, empty names, and unsupported metadata are excluded.
- Returned forms, controls, collections, selects, options, and `document`
  retain canonical identity. Every host reference carries the existing owner
  serial, document generation, and host-kind guard, so stale references fail
  closed after replacement or invalidation.
- Collection properties are read-only. JS35 deliberately does not add a
  general `HTMLCollection`, `item()`, `namedItem()`, `RadioNodeList`, dot-name
  magic, global named properties, or collection mutation semantics.

The implementation reuses the existing bounded structural `HtmlElementRef`
and `FormControlMetadata` records. Form names are retained on the existing
bounded `FormContainerMetadata` entries; no JavaScript registry or second
proportional HTML collection is introduced. Indexed and named reads use the
same bounded structural scans and the existing JS34 form/control caps.

The focused proof is
`tests/navigator_javascript_js35_test.cpp`, run by
`scripts/smoke-navigator-javascript-js35.ps1`. It reports 162 checks with 0
failures and passes the optimized bare-metal harness plus the strict adapter
and runtime syntax lane. Coverage includes live collection identity and
ordering, lengths and bounds, exact id/name precedence, duplicate and empty
name behavior, canonical numeric strings, read-only assignment failures,
cross-form isolation, text/checkbox/select/option projections, focus,
submit/reset/click integration, stale references, document replacement, and
JS33/JS34 regression baselines. The full repository JavaScript matrix is now
33 scripts (JS6 through JS35 plus lexer, parser, and runtime); all passed.

The hosted fixture is `navigator-smoke/javascript-js35.html`. The final
`navigator.smoke` run passed all seven JS35 checks: initial `document.forms`
projection, named mutation, named select/options, focus, named submit,
named reset, and document replacement. The aggregate reported 484 passed and
7 failed out of 491; the seven failures remain the unrelated CSS 3C, CSS 3G,
CSS 6A, three CSS 6B checks, and CSS 6C.

`build.bat` completed successfully. The required `build-kernel.bat` retry
successfully built the PacMan package, ramdisk staging, and UEFI bootloader,
then stopped at the existing Mbed TLS configuration errors in
`third_party/mbedtls/library/mbedtls_check_config.h`:
`Unsupported partial support for ECC curves acceleration` and
`MBEDTLS_KEY_EXCHANGE_ECDHE_RSA_ENABLED defined, but not all prerequisites`.
No QEMU run was claimed because the kernel image was not freshly produced,
and no TLS configuration was changed.

## JS36: bounded DOM selectors

JS36 projects familiar selector lookup over the existing parsed structural
metadata:

```javascript
document.querySelector("#login")
document.querySelectorAll("input")
form.querySelector(".required")
form.querySelectorAll("input")
```

The mandatory grammar is exact `#id`, `.class`, and tag matching. The bounded
parser also supports the cheap compound forms `tag.class` and `tag#id`.
Outer ASCII whitespace is trimmed. Tag comparison uses the existing parsed tag
representation with ASCII case normalization; id and class tokens are exact,
and class matching splits the existing bounded `className` text on ASCII
whitespace rather than using substring or dynamic-regex matching.

`querySelector()` returns the first valid match in structural document order or
the host null sentinel. `querySelectorAll()` returns a read-only, bounded live
collection with `length` and numeric indexed access; each read rescans the
current finite structural metadata. The collection retains only a parsed
selector descriptor, scope serial, and current host generation. Indexed misses
return `undefined`; array methods, mutation, and general NodeList semantics are
not implemented.

Element-scoped queries use the existing `parentSerial` chain and search strict
descendants only. Form ownership, focus, layout, render visibility, and ids are
not used as containment shortcuts. All returned elements are the same
generation-checked host objects used by `getElementById()`, `document.forms`,
`form.elements`, `select.options`, focus, click, reset, submit, and events.

Selectors are limited to 256 input bytes and ordinary ASCII identifier
characters (`A-Z`, `a-z`, digits, `-`, and `_`). Empty, malformed, oversized,
unsupported, descendant/combinator, list, attribute, pseudo-class,
pseudo-element, universal, and escaped CSS selectors fail closed: `querySelector`
returns `null` and `querySelectorAll` returns an empty bounded collection.
There is no DOMException or full CSS selector engine in this phase.

The focused proof is
`tests/navigator_javascript_js36_test.cpp`, run by
`scripts/smoke-navigator-javascript-js36.ps1`. It covers id/class/tag and
compound selectors, token/case behavior, document order, strict element
scoping, leaf queries, bounded empty/read-only collections, canonical form and
option identity, focus/click/submit/reset integration, re-entrant lookup, and
stale generations. It reports 99 checks with 0 failures and passes the
optimized bare-metal harness plus the strict adapter and runtime syntax lane.
The full repository JavaScript matrix is now 34 scripts (JS6 through JS36 plus
lexer, parser, and runtime); all 34 passed. The hosted fixture is
`navigator-smoke/javascript-js36.html`, and the production aggregate exercises
the same selector-returned objects through query, focus, submit, reset, and
unsupported-selector paths.

The final hosted aggregate added five JS36 checks, all of which passed:
fixture load, query/order/scope/identity/safe rejection, focus, submit, and
reset. The aggregate reported 489 passed and 7 failed out of 496; the seven
failures remain the unrelated CSS 3C, CSS 3G, CSS 6A, three CSS 6B checks, and
CSS 6C. `build.bat` also completed successfully with no JS36 diagnostics.

The requested `build-kernel.bat` retry staged the PacMan package, wallpaper
image, ramdisk, and UEFI bootloader, then stopped at the existing Mbed TLS
configuration guards in
`third_party/mbedtls/library/mbedtls_check_config.h`:
`Unsupported partial support for ECC curves acceleration` and
`MBEDTLS_KEY_EXCHANGE_ECDHE_RSA_ENABLED defined, but not all prerequisites`.
No third-party TLS configuration was changed and no QEMU result is claimed.

JS36 intentionally does not add descendant or sibling combinators, selector
lists, attribute selectors, pseudo-classes, pseudo-elements, CSS escapes,
`closest()`, live DOM mutation, general NodeLists/HTMLCollections, or array
methods. The next bounded direction is JS37: only add a narrowly justified
selector capability after preserving the same structural metadata, scope,
generation, identity, and bare-metal limits.

## JS37: `Element.matches()` and `Element.closest()`

JS37 reuses the JS36 bounded selector parser and the single
`selectorElementMatches()` predicate. It does not add a second selector engine,
DOM traversal subsystem, JavaScript-side element metadata, regular expressions,
or persistent result collections.

The supported grammar remains exactly JS36's bounded subset:
`#id`, `.class`, `tag`, `tag.class`, and `tag#id`. Input is capped at 256
bytes, outer ASCII whitespace is trimmed, tags compare ASCII
case-insensitively, and ids/classes retain exact case-sensitive semantics.
Unsupported, malformed, non-string, missing, and oversized selectors fail
closed: `matches()` returns `false` and `closest()` returns the existing null
sentinel. Full browser Selectors API behavior, DOMExceptions, descendant and
child combinators, selector lists, attributes, pseudo-classes, pseudo-elements,
and CSS escapes remain unsupported.

`element.matches(selector)` parses once and evaluates only the canonical
receiver. It does not search descendants or ancestors, allocate a selector
collection, mutate layout or form state, change focus, dispatch events, or
change document generation. `element.closest(selector)` parses once, tests the
receiver first, then walks the structural `parentSerial` chain to the nearest
matching ancestor. The walk is bounded by the live structural document-node
capacity (`min(maxDocumentNodes, structuralElements.size())`), stops safely on a
self-parent, and therefore terminates even if malformed metadata contains a
cycle. A successful ancestor result is the same generation-checked canonical
host identity returned by `querySelector()`, `document.forms`,
`form.elements`, and `select.options`.

Both methods are receiver-checked Element methods. Collections and unrelated
host object kinds cannot invoke them. Old-generation handles fail closed for
these pure predicates (`false`/`null`); other stale host operations retain the
strict existing stale-host error behavior. This keeps document replacement and
serial reuse from scanning the new document with old metadata.

The focused proof is `tests/navigator_javascript_js37_test.cpp`, run by
`scripts/smoke-navigator-javascript-js37.ps1`. It covers selector grammar and
invalid-selector consistency, self/parent/grandparent/nearest behavior,
structural option and form ancestry, querySelector receiver exclusion versus
closest self inclusion, bounded malformed cycles, stale generations, pure
no-side-effect checks, event-target delegation, focus/input/change/submit/reset
listeners, nested dispatch, and JS36/JS35 identity regression. The hosted
fixture is `navigator-smoke/javascript-js37.html`; the production aggregate
drives authentic click, focus, submit, reset, event-target `matches()`, and
event-target `closest()` paths. Final live evidence is 180/180 focused JS37
checks, 99/99 focused JS36 checks, 162/162 focused JS35 checks, 35/35 scripts
in the JavaScript smoke matrix, and 5/5 JS37 hosted aggregate checks. The
full hosted aggregate is 494 passed and 7 failed; the seven failures are the
pre-existing CSS phases 3C, 3G, 6A, 6B (three checks), and 6C.

JS37 remains intentionally narrow. It does not add new selector grammar,
`parentElement`, `parentNode`, `children`, `childNodes`, dynamic DOM mutation,
general collection APIs, or full browser compatibility. JS38 adds only the
bounded structural traversal described below; the shared selector predicate,
ancestry, event metadata, and bare-metal lanes remain unchanged.

## JS38: bounded DOM element traversal

JS38 adds read-only structural traversal over the existing parser-owned
`WebDocument::structuralElements` vector:

```javascript
element.parentElement
element.children
element.childElementCount
element.firstElementChild
element.lastElementChild
element.nextElementSibling
element.previousElementSibling
```

The vector is still the only structural representation. The implementation
uses each record's canonical `serial` and `parentSerial`, scans in bounded
document-vector order, and does not build a second tree, child cache, JavaScript
array, or mutable DOM. `parentElement` follows structural `parentSerial`; it
does not use `FormControlMetadata.parentFormSerial`, so a nested wrapper remains
the parent of a form control even when the control also appears in
`form.elements`. `select.children` and `select.options` therefore expose the
same canonical option identities in the same order.

`children` is a generation-checked, read-only `ElementChildren` collection.
Each `length` or numeric indexed read rescans the current bounded structural
records using the same direct-child eligibility rule. Indexed misses return
`undefined`, collection identity is stable for the same owner and generation,
and returned elements use the canonical Element host identity. `childElementCount`
shares that count, while `firstElementChild` and `lastElementChild` return the
corresponding canonical element or `null` for an empty collection. Sibling
properties scan only the receiver's same-parent group in vector order and return
`null` at the first/last boundary; missing, self-parent, and malformed parent
metadata fail closed.

All traversal properties and collection properties are read-only. Supported
root elements return `null` for `parentElement`; leaf elements return an empty
collection and count zero. Old-generation Element traversal returns the safe
sentinels (`null`, zero, or `undefined` as appropriate), while an old captured
`children` collection remains a strict stale-host error. Every reference keeps
the existing serial, document-generation, and host-kind guards, so document
replacement cannot resolve an old element into a new document. Traversal is
also bounded by the existing `maxDocumentNodes` limit.

The structural surface composes with JS36/JS37 selectors and the existing
event/default-action paths. The focused JS38 proof covers canonical identity,
parent/child/sibling round trips, first/last boundaries, direct-child-only
behavior, child-count walks, form-versus-structural-parent behavior,
select/options identity, `matches()`/`closest()`/`querySelector()` integration,
event target and current-target metadata, focus and `activeElement`, traversal-
returned click, submit, reset, value/default-value behavior, stale generations,
malformed metadata, read-only assignments, and listener bounds.

The parser limitation remains explicit: metadata-only head/title/meta/link/base/
style/script records are not structural elements, so only the bounded parsed
structural surface participates in traversal. JS38 does not add `parentNode`,
`childNodes`, general Node/HTMLCollection APIs, mutation, array methods,
descendant or sibling selector syntax, or a full browser DOM.

The focused proof is `tests/navigator_javascript_js38_test.cpp`, run by
`scripts/smoke-navigator-javascript-js38.ps1`. It reports 152 checks with 0
failures and passes the optimized bare-metal harness plus the strict adapter
and runtime syntax lane. The full repository JavaScript matrix is now 36/36:
JS6 through JS38 plus lexer, parser, and runtime lanes. Focused regression
evidence includes JS36 at 99/99 and JS37 at 180/180. The hosted fixture is
`navigator-smoke/javascript-js38.html`; the production aggregate drives five
checks covering fixture load, delegated traversal events, focus, submit, and
reset, all 5/5 passing. The final hosted aggregate reports 499 passed and 7
failed; the seven failures remain the pre-existing CSS phases 3C, 3G, 6A, 6B
(three checks), and 6C.

`build.bat` completed successfully and produced `guideXOSServer.exe` with no
JS38 diagnostics. The strict syntax lane passed as part of the JS38 smoke
script. `build-kernel.bat` staged PacMan, the ramdisk, and the UEFI bootloader
but stopped at the existing Mbed TLS guards in
`third_party/mbedtls/library/mbedtls_check_config.h` (partial ECC acceleration
and missing ECDHE-RSA prerequisites). No TLS configuration was changed and no
QEMU result is claimed because the kernel image was not freshly produced.

## JS39: bounded descendant and direct-child selector relationships

JS39 extends the JS36 simple-selector parser and the JS37/JS38 structural
matcher with exactly one relationship boundary:

```javascript
simple simple
simple > simple
```

Each side may be `#id`, `.class`, `tag`, `tag.class`, or `tag#id`. The parsed
descriptor is fixed and non-recursive: it contains `leftSimple`, a `relation`
(`None`, `Descendant`, or `Child`), and `rightSimple`, with both simple
selectors stored in the existing 256-byte bounded descriptor storage. The raw
selector string is not retained and no linked-list or recursive AST is
created.

Outer ASCII whitespace is trimmed. Internal ASCII spaces, tabs, carriage
returns, newlines, and form feeds between two simple selectors mean
`Descendant`; whitespace around `>` is ignored, so `A>B`, `A > B`, `A> B`, and
`A >B` all mean `Child`. A simple selector containing only outer whitespace,
such as `  input  `, remains a simple selector. Leading/trailing or repeated
relation characters, internal whitespace that would create a second relation,
lists, attributes, pseudo-classes, pseudo-elements, universal selectors,
escapes, and all multi-combinator chains fail closed. JS39 does not interpret
`A B C`, `A > B > C`, `A B > C`, or `A > B C` partially.

The right side is always tested against the candidate first. For `Descendant`,
the matcher then walks structural ancestors iteratively and accepts any
matching left side. For `Child`, it resolves exactly one immediate parent.
Both paths use the same `resolveStructuralParentSerial()` helper used by
`parentElement` and `closest()`, so `parentSerial` is the sole relationship
authority. Form ownership (`parentFormSerial`), `form.elements`, layout, and
render containment cannot turn a grandchild into a direct child.

The ancestry bound is the existing
`min(maxDocumentNodes, structuralElements.size())` limit used by `closest()`.
Invalid parents and self-parent records fail closed; longer parent cycles
terminate at that same finite bound. `closest()` tests the receiver first and
reuses the full `selectorElementMatches()` evaluator, so a button can be the
result of `button.closest("form button")` or
`button.closest(".group > button")` when it itself satisfies the whole
relationship.

`querySelector()` and `querySelectorAll()` scan the same bounded structural
vector in document order and return the same canonical, generation-checked
Element hosts as the existing APIs. Element-scoped queries retain the JS36
strict-descendant candidate boundary; relationship evaluation uses the
candidate's real `parentSerial` ancestry, including an ancestor outside the
receiver when the candidate is inside the receiver. Collections remain
read-only, live-on-read, and bounded by the existing 128-record selector
collection table. `matches()` performs no document scan or allocation, and
`closest()` performs no collection allocation.

The focused proof is
`tests/navigator_javascript_js39_test.cpp`, run by
`scripts/smoke-navigator-javascript-js39.ps1`. It covers parsing and whitespace,
compound selectors, document order, strict scoped candidates, form ownership
versus structural ancestry, select/options identity, read-only collections,
`matches()`, receiver-self and nearest `closest()`, malformed and multi-chain
rejection, stale generations, invalid/cyclic parents, selector-length limits,
event target metadata, nested selector calls, submit/reset behavior, and the
listener cap. The hosted fixture is
`navigator-smoke/javascript-js39.html`; the production aggregate exercises
relational lookup, delegated `matches()`/`closest()`, focus, submit, reset, and
event metadata.

JS39 remains deliberately short of full CSS compatibility. It does not add
`+`, `~`, selector lists, attribute selectors, pseudo-classes,
pseudo-elements, `*`, CSS escaping, multiple combinators, Node APIs, DOM
mutation, or asynchronous/browser APIs.

## JS40: bounded adjacent and general sibling selectors

JS40 extends the same fixed, one-relation descriptor with two additional
relation kinds:

```javascript
simple + simple
simple ~ simple
```

Both sides retain the JS36 simple forms: `#id`, `.class`, `tag`, `tag.class`,
and `tag#id`. The descriptor now contains `None`, `Descendant`, `Child`,
`AdjacentSibling`, or `GeneralSibling`; it still stores only the two simple
selectors in the existing 256-byte bounded storage. There is no recursive AST,
relation vector, second parser, or general CSS parser.

`A + B` tests the candidate B first and then asks the JS38
`elementSiblingAt(candidate, false, ...)` helper for exactly the immediate
previous structural element sibling. `A ~ B` also tests B first, then walks
backward through that same helper until it finds a matching A or reaches the
finite `min(maxDocumentNodes, structuralElements.size())` sibling bound. Both
relations require the same structural `parentSerial`; form ownership,
`form.elements`, `select.options` indexing, layout, visual order, and source
text do not substitute for sibling structure. Self-returning or invalid sibling
metadata fails closed, and a previous-sibling walk never considers a future
sibling or crosses a parent boundary.

Whitespace around `+` and `~` is ignored, so `A+B`, `A + B`, `A+ B`, and
`A +B` (and the corresponding `~` forms) are equivalent. The existing
descendant and child forms remain `A B` and `A > B`. Only one relation is
accepted: malformed leading/trailing/repeated relations and chains such as
`A + B + C`, `A ~ B ~ C`, `A > B + C`, and `A B ~ C` fail closed rather than
being partially parsed. Unsupported selector lists, attribute selectors,
pseudo-classes, pseudo-elements, `:not()`, `:is()`, `:where()`, `:has()`,
structural pseudo-classes, universal selectors, CSS escapes, namespaces,
mutation APIs, and asynchronous/browser APIs remain out of scope.

The shared matcher powers `querySelector()`, `querySelectorAll()`,
`matches()`, and `closest()`. Queries scan canonical structural elements in
document order; scoped queries retain the strict JS36 candidate boundary.
`matches()` uses the receiver as B without a document scan, and `closest()`
tests the receiver before walking ancestors, returning the matching B rather
than the left-side A. Selector collections remain read-only, generation-safe,
live-on-read, and capped at 128 collection records/results as established by
the earlier phases. Stale receivers return `false`/`null`, and stale sibling
collections retain the existing failure behavior. The 256-byte selector cap is
unchanged.

The focused proof is `tests/navigator_javascript_js40_test.cpp`, run by
`scripts/smoke-navigator-javascript-js40.ps1`. The hosted proof is
`navigator-smoke/javascript-js40.html` and is included in the production
hosted aggregate. It covers adjacent immediate-only matching, general
backward-only matching, same-parent enforcement, compound selectors, options
and form-ownership distinctions, query order and scope, `matches()`/`closest()`,
event delegation, nested dispatch, event metadata, stale generations,
malformed chains, and the selector/collection bounds.

The JS40 source and hosted lanes do not modify TLS configuration. In the JS40
verification run, `build-kernel.bat` reached the kernel lane but remained
blocked by the existing Mbed TLS diagnostics at
`mbedtls_check_config.h:51` (unsupported partial ECC acceleration) and
`mbedtls_check_config.h:64` (missing ECDHE-RSA prerequisites). No TLS changes
were made, and no QEMU proof is claimed because no fresh kernel image was
produced.

## JS41: bounded structural `Element.contains()`

JS41 exposes `Element.contains(otherElement)` as a pure Boolean query. It is
self-inclusive: an Element contains itself, as well as any live Element reached
by walking the candidate's structural ancestors. Direction is candidate-upward;
ancestors, siblings, and unrelated Elements are not contained. The exposed
`html` root contains its represented `body` and descendants, while Navigator
does not invent a parent above that represented root.

The adapter resolves both host values as current-generation Element references
and identifies them by the existing canonical `(document generation,
structural serial)` pair. The candidate serial is walked upward through
`resolveStructuralParentSerial()` and the shared bounded `isDescendantOrSelf()`
helper; it does not scan receiver descendants or use pointer/wrapper identity.
The exact parent-hop limit is
`min(maxDocumentNodes, structuralElements.size())`, using the configured
structural-node capacity (1024 by default). The same helper also supplies
element-scoped query ancestry. `closest()` continues to use the same one-step
parent resolver and document-node traversal bound.

Self-comparison is checked after both Elements are proven live, so
`element.contains(element)` remains true even if unrelated parent metadata is
malformed. Missing, self-parented, invalid, or cyclic ancestry fails closed for
other relationships; self-parent links are rejected by the common parent
resolver and cycles terminate at the node bound. Stale receivers return
`false`. Stale arguments are passed only to this fail-closed predicate and
return `false`; neither side can resolve a serial from an old generation into a
new document. Other argument counts, `null`, booleans, numbers, strings,
ordinary objects, collections, and non-Element host references return `false`
without WebIDL coercion or an exception. The method is exposed on Element
handles only, not on `document`, collection hosts, or Events.

Structural `parentSerial` ancestry is authoritative. Form ownership metadata
and `form.elements` remain independent projections and cannot substitute for
containment; a control whose synthetic owner metadata differs from its tree
position still follows its actual ancestors. Likewise, `select.children[i]`
and `select.options[i]` share their existing canonical option Element, and
containment follows the option's structural parent without a shortcut.
`querySelector()`/`querySelectorAll()` results, `parentElement`, `closest()`,
and sibling traversal return the same structural identities; a sibling edge
does not imply containment.

The predicate is read-only: it does not change focus, form state, document
generation, layout dirtiness/revision, selector collections, or event dispatch
state. Event delegation can therefore use
`panel.contains(event.target)`, including a click whose target is the panel
itself. Focus listeners and `document.activeElement` can use the same query;
submit/reset listeners do not affect their default-action decision. Calls are
reentrant and keep ancestry cursors on the call stack, so repeated, nested, and
nested-dispatch calls do not share mutable traversal scratch.

The JS41 focused test is `tests/navigator_javascript_js41_test.cpp`, run by
`scripts/smoke-navigator-javascript-js41.ps1`. Its coverage includes positive
and negative directionality, deep/root structure, form-owner separation,
options, selector/traversal identity, argument sentinels, generation reuse,
malformed ancestry, event metadata, focus/default actions, nested dispatch,
and purity. The hosted proof is
`navigator-smoke/javascript-js41.html`, included in the production hosted
aggregate; it drives a real descendant click, an unrelated nested click, and a
self-target event through the ordinary event path.

The JS41 retrieval audit found that tag and class matching already exist in the
JS36 simple-selector descriptor and use the existing selector collection
engine. Direct `getElementsByTagName()` and `getElementsByClassName()` surfaces
are deferred: their scoped collection methods, class-token input boundary, and
live collection lifecycle would add API behavior beyond a thin method alias in
this milestone. No second walker or collection type was added. The current
bounded selector APIs remain the supported retrieval surface.

JS41 does not add general Node containment or Node APIs such as `parentNode`,
`childNodes`, `compareDocumentPosition()`, `isSameNode()`, or `isEqualNode()`,
nor DOM mutation, named collections, asynchronous/browser APIs, or broader
selector grammar.

The final JS41 focused suite passes 220 checks. Focused JS36–JS40 regressions
pass 99, 180, 152, 218, and 155 checks, respectively. The complete JavaScript
matrix passes 39/39 lanes (lexer, parser, runtime, and JS6–JS41). The hosted
production aggregate reports 514 passed and 7 failed; every failure is in the
existing unrelated CSS 3C, 3G, 6A, CSS 6B (three checks), and 6C cases. All four
hosted JS41 containment/event checks pass.

`build.bat` and the strict adapter/runtime syntax lane pass. `build-kernel.bat`
could not launch its PowerShell Core wrapper because `pwsh.exe` is unavailable
in the environment. Running its underlying kernel make lane directly with
`mingw32-make ARCH=amd64 EXTRA_CFLAGS=` reproduced the existing Mbed TLS errors
at `mbedtls_check_config.h:51` (unsupported partial ECC acceleration) and
`:64` (missing ECDHE-RSA prerequisites). No TLS files were changed. No QEMU
proof is claimed because no fresh kernel was produced.

`ESP/ramdisk.img`, `out/wallpaper-pack/`, and
`out/wallpaper-pack/Apps/PacMan/` remained unmodified and are excluded from the
JS41 source change.

The next bounded direction is JS42: add document/element tag and single-token
class retrieval only if it remains a thin view over the JS36 matcher and
generation-safe selector collection machinery.

## JS42: bounded tag and class retrieval collections

JS42 exposes the familiar document- and Element-scoped entry points:

```javascript
document.getElementsByTagName("button");
panel.getElementsByTagName("button");
document.getElementsByClassName("action");
panel.getElementsByClassName("action");
```

These methods are projections over the existing JS36 simple-selector matcher
and selector-collection host. A method validates its one bounded string
argument, constructs a native `Tag` or `Class` simple-selector descriptor, and
passes that descriptor plus the document/Element scope to the existing
`getOrCreateSelectorCollection()` path. It does not serialize an argument into
CSS source, invoke a second parser, keep copied results, add a JS-owned DOM
registry, or create another walker. `querySelectorAll("button")` and
`getElementsByTagName("button")` use equal descriptors and the same scope,
therefore they reuse the same collection record and return equal canonical
Element handles in the same order. The corresponding property holds for
`".action"` and the one-token class retrieval method.

The matcher scans `WebDocument::structuralElements` in its existing structural
document order. A document receiver includes the full represented structural
document. An Element receiver admits strict descendants only: the receiver is
excluded, and a matching element outside its subtree cannot leak into the
result. The established selector-scope parent resolver and matcher remain the
authority. Form ownership, layout/render order, text records, and synthetic
form collection entries are not retrieval scope.

Tag arguments are individual tag-name tokens using the JS36 supported ASCII
tag-name shape (`A-Z`/`a-z` first, then letters, digits, `-`, or `_`), with a
maximum of 256 bytes. No outer whitespace is trimmed. Empty, malformed,
selector-like (`"input.field"` or `"#save"`), whitespace-containing, missing,
non-String, or oversized arguments produce an empty selector collection;
unknown but syntactically valid names also naturally have no matches. Tag
comparison is the existing ASCII case-insensitive matcher, so `"INPUT"` and
`"input"` select the same structural Elements. The browser-style universal
`"*"` audit found that the JS36 matcher intentionally treats universal
selectors as invalid and has no dedicated universal-element predicate; wildcard
lookup is **deferred** and currently returns an empty collection.

Class arguments deliberately support exactly one JS36 class identifier token
(`[A-Z]`, `[a-z]`, digits, `-`, or `_`); the `.` selector marker plus token
must fit the existing 256-byte simple-selector input bound, so the token is at
most 255 bytes. No trimming or multi-class interpretation occurs. Matching is
the JS36 exact, case-sensitive, ASCII-whitespace-delimited token comparison:
an element with `class="action primary"` matches `"action"` and `"primary"`,
but not `"act"` or `"Action"`. Empty, missing, non-String, whitespace,
multi-token (for example `"action primary"`), malformed, and oversized
arguments return an empty collection. Full browser multi-class AND behavior
is deliberately deferred.

The returned value is the existing selector-collection HostObject, retaining
only its bounded descriptor and scope serial. `length` and canonical numeric
indices are read-only; an indexed miss is `undefined`. There is no new
`HTMLCollection` type, `item()`, `namedItem()`, named lookup, iterator, or
general collection API expansion. Collection records are live-on-read within
their document generation: each `length`/index access scans current
authoritative structural storage under the existing `maxDocumentNodes` bound
(1024 by default). The fixed selector collection registry remains capped at
128 descriptors; repeated equivalent selector/scope pairs share their record,
and a new unique descriptor at capacity follows the existing
`DocumentLookupLimitExceeded` failure behavior. There is no separate 128-result
array or copied result buffer.

Element identity remains `(host generation, structural serial)`. Results
therefore compare identical to `getElementById()`, `querySelector()`,
`querySelectorAll()`, `form.elements`, `document.forms`, `select.options`,
`children`, and Event Element handles when those paths identify the same
structural Element. For the same selector and scope, retrieval and
`querySelectorAll()` equality proves count, structural order, and identity.
Scoped results are strict descendants, so `scope.contains(results[i])` is true;
their `parentElement`, `closest()`, sibling traversal, and `contains()` remain
the ordinary Element surfaces.

The distinction from form ownership is retained: `form.elements` follows
`parentFormSerial`, while `form.getElementsByTagName()` follows structural
`parentSerial`. The structural table can represent a form-owned control outside
the form subtree, and focused coverage assigns that metadata to prove the two
collections differ. Select option retrieval does not have a special path:
where options are structural children,
`select.getElementsByTagName("option")[0] === select.options[0]`.

Generation changes clear the selector record table. Old document- and
Element-scoped collections fail with the existing stale-host-object behavior;
old returned Elements remain stale even if a new document reuses their serial.
Calling either retrieval method through a stale Element receiver fails closed
with a current-generation empty selector collection and never resolves the old
scope serial into the new document. The method calls are reentrant along the
same guarded host-call path as selector lookups, so lookups in event callbacks
and nested clicks retain target/currentTarget/phase/relatedTarget/default
cancellation state. The operations do not change focus, controls/defaults,
layout state, structural metadata, document generation, listener registry, or
event dispatch state.

The focused proof is `tests/navigator_javascript_js42_test.cpp`, run by
`scripts/smoke-navigator-javascript-js42.ps1`. It covers both receiver kinds,
document/scoped ordering and `querySelectorAll()` equivalence, canonical
identity, exact class tokens, strict descendant scope, invalid/bounded
arguments, wildcard and multi-token deferral, shared collection slots,
read-only/index-miss behavior, access-time matching, generation invalidation
and serial reuse, form ownership versus structure, select/options identity,
traversal/containment, authentic and nested event callbacks, metadata, purity,
and earlier selector/form/event regressions. The hosted proof is
`navigator-smoke/javascript-js42.html`, included in the normal production
hosted aggregate; it checks the document and scoped counts, query equivalence,
outside-scope exclusion, exact class-token behavior, canonical identity,
`contains()`/traversal, and retrieval inside an authentic nested click path.

JS42 does not claim complete browser `HTMLCollection` or legacy DOM retrieval
compatibility. It does not add wildcard lookup, multi-token class matching,
named lookup, new selector grammar, DOM mutation, or async/browser APIs.
Validation results, including the complete JavaScript and hosted aggregate
counts, production and strict builds, kernel wrapper/direct-lane state, and
QEMU proof status, are recorded in the JS42 completion summary below.

### JS42 completion summary

JS42 reached **Outcome A**. The focused suite passes 229/229 checks. Focused
JS36–JS41 regressions pass 99/99, 180/180, 152/152, 218/218, 155/155, and
220/220, respectively. The complete JavaScript matrix passes **40/40 lanes**
(lexer, parser, runtime, and JS6–JS42).

The final hosted aggregate run reports **518 passed / 7 failed**. All four
hosted JS42 checks pass. The seven failures remain the existing unrelated CSS
3C, CSS 3G, CSS 6A, CSS 6B (three checks), and CSS 6C cases. One earlier
aggregate attempt also failed two JS15 link-navigation checks; those checks
passed on immediate rerun, and both later complete aggregate runs report the
expected 518/7 result.

`build.bat`, the focused `-DGXOS_BARE_METAL` JS42 executable, and the strict
warning-as-error adapter/runtime syntax lane pass. `pwsh.exe` is unavailable;
`build-kernel.bat` consequently exits 9009 before starting PowerShell Core. The
established direct lane, `mingw32-make ARCH=amd64 EXTRA_CFLAGS=` from `kernel/`,
reaches the Mbed TLS compile and stops at the existing
`mbedtls_check_config.h:51` unsupported partial ECC acceleration and `:64`
missing ECDHE-RSA prerequisites diagnostics. No TLS files were changed. No
fresh kernel image was produced, so QEMU proof is not claimed.

`git diff --check` passes. `ESP/ramdisk.img`, `out/wallpaper-pack/`, and
`out/wallpaper-pack/Apps/PacMan/` are unchanged. JS42 does not claim wildcard
tag retrieval, multi-token class lookup, or complete browser `HTMLCollection`
semantics.

## JS43: Universal selector and element matching

JS43 adds bare universal selector `*` as one more simple-selector kind in the
existing bounded descriptor:

```text
Invalid, Id, Class, Tag, TagClass, TagId, Universal
```

The parser stores `Universal` without a tag string or wildcard flag. The shared
simple matcher accepts it only for a valid current-generation structural
`HtmlElementRef`: the record has a nonzero serial and tag name and is the
canonical current record for that serial. Document, Event, collection, primitive,
and internal/non-Element host objects do not enter that matcher. Existing
receiver-generation checks happen before `matches("*")` can reach the matcher;
a stale Element therefore returns `false`, and stale `closest("*")` returns
`null`.

The same parsed descriptor now works through the existing document and scoped
selector methods and the JS42 tag retrieval constructor:

```javascript
document.querySelector("*");
document.querySelectorAll("*");
panel.querySelector("*");
panel.querySelectorAll("*");
element.matches("*");
element.closest("*");
document.getElementsByTagName("*");
panel.getElementsByTagName("*");
```

Document queries use structural document order, including represented root
elements such as `html` and `body`; no missing browser nodes are synthesized.
Matching does not consult layout visibility: hidden/non-rendered Elements still
match when their valid structural records remain in the document table.
`querySelector("*")` returns the first represented Element or `null`.
Document `querySelectorAll("*")` and `getElementsByTagName("*")` include all
exposed structural Elements in the same order. Element-scoped queries and tag
retrieval include strict descendants only: the receiver is excluded and no
outside subtree element can appear. A leaf scope returns `null` for
`querySelector("*")` and empty collections for the plural methods.

Wildcard query results and wildcard tag results keep the JS36/JS42 selector
collection representation: each collection retains the parsed descriptor and
scope serial and resolves against authoritative structural storage when read.
Equivalent selector/scope pairs use the same record and canonical host
identities, so wildcard tag retrieval and wildcard `querySelectorAll()` agree
in count, order, and element identity. Numeric indexing and read-only
`length`/indices are unchanged; an out-of-range index is `undefined`. The
document-node limit remains 1024 by default, and the fixed selector collection
registry remains capped at 128 records. Wildcard matching does not allocate a
wildcard-specific walker or result array and does not raise either bound.

For `getElementsByTagName`, `"*"` is the one exact additional supported tag
argument. Method arguments are not trimmed: `getElementsByTagName(" * ")`
remains empty, while outer whitespace in selector strings follows ordinary
selector trimming (`querySelector(" * ")` is valid). Ordinary tag-name
validation/case-insensitive behavior and single-token exact
`getElementsByClassName` behavior are unchanged. `form.getElementsByTagName("*")`
follows structural parent links and includes non-control wrappers; `form.elements`
continues to follow form ownership and returns controls only. Structural option
elements naturally compare identical to `select.options` and `select.children`
where those projections name the same Element.

The one-relation grammar automatically permits universal selectors on either
side and both sides of descendant, child, adjacent-sibling, and general-sibling
relations, including `A > *`, `* > B`, `* > *`, `A *`, `* B`, `* *`, `A + *`,
`* + B`, `* + *`, `A ~ *`, `* ~ B`, and `* ~ *`. Existing immediate-child,
immediate-adjacent, same-parent, backward-only sibling, and bounded ancestry
semantics are unchanged. More than one relation remains rejected safely; no
arbitrary combinator chains are introduced. Empty and whitespace-only
selectors, `**`, repeated/missing combinators, `*.class`, and `*#id` remain
invalid. The optional compound forms `*.class` and `*#id` were audited and
deferred: `.class` and `#id` already express those queries without adding
another parser branch. Selector input remains capped at 256 bytes.

The JS43 native proof is `tests/navigator_javascript_js43_test.cpp`, run by
`scripts/smoke-navigator-javascript-js43.ps1`. Its coverage includes document
and scoped results, order/identity, receiver exclusion, root/Element matching,
all four relations, malformed and bounded syntax, shared registry capacity,
form ownership versus structural retrieval, select/options, children,
containment, event target matching, metadata, nested dispatch, stale
collections/receivers/results, serial reuse, and earlier selector retrieval
regressions. The hosted fixture
`navigator-smoke/javascript-js43.html` is registered in the normal production
hosted aggregate and drives an authentic click callback.

JS43 does not claim full CSS selector compatibility. Selector lists, attribute
selectors, pseudo-classes/elements, escapes, namespaces, `*.class`, `*#id`, and
arbitrary relation chains remain outside the bounded grammar. It does not
change the `getElementsByClassName` token policy or add new collection APIs.

### JS43 completion summary

The focused JS43 suite passes **277/277 checks**. Focused JS36–JS42 regressions
pass **99/99, 180/180, 152/152, 218/218, 155/155, 220/220, and 232/232**.
The full JavaScript matrix passes **41/41 lanes** (lexer, parser, runtime, and
JS6–JS43). The JS43 test includes a retained `display:none` subtree and proves
that its structural Elements remain in wildcard results.

All **4/4 hosted JS43 checks** pass, including shared query/tag identity,
scoped results, form-ownership distinction, relational matching, and an
authentic click callback with preserved Event metadata. The complete hosted
aggregate reports **522 passed / 7 failed**. The seven failures are the existing
CSS 3C, CSS 3G, CSS 6A, three CSS 6B checks, and CSS 6C; no JS43 check fails and
there are no new unexplained failures.

`build.bat`, the JS43 `-DGXOS_BARE_METAL` focused executable, and the strict
warning-as-error adapter/runtime syntax lane pass. `pwsh.exe` is unavailable;
`build-kernel.bat` exits **9009** before its PowerShell wrapper can run. The
established direct kernel command `mingw32-make ARCH=amd64 EXTRA_CFLAGS=` from
`kernel/` reaches the existing Mbed TLS blockers at
`mbedtls_check_config.h:51` (partial ECC acceleration configuration) and `:64`
(missing ECDHE-RSA prerequisites). No TLS files were changed. No fresh kernel
was produced, so QEMU proof is not claimed.

`git diff --check` passes. `ESP/ramdisk.img`, `out/wallpaper-pack/`, and
`out/wallpaper-pack/Apps/PacMan/` remain unchanged and are excluded from the
source commit. The JS43 source commit hash and final branch/worktree state are
recorded in the milestone completion report.

Recommended JS44 direction: bounded multi-token class retrieval/matching, with
an explicit all-tokens policy and the same shared descriptor/collection bounds.

## JS44: bounded multi-token class retrieval

JS44 extends `document.getElementsByClassName()` and
`element.getElementsByClassName()` to parse a bounded whitespace-separated class
query. Every distinct requested token is required. Query token order does not
matter, element class-token order does not matter, and the element may have
additional class tokens. Matching still uses the existing exact,
case-sensitive, ASCII-whitespace-delimited class-token helper; each requested
token must match independently. A prefix such as `act` does not match `action`.
Repeated class tokens in an element have no multiplicity meaning.

The query parser uses the existing 256-byte retrieval argument bound. Each
token must fit the existing simple class-selector storage limit of 255 bytes,
and the raw query may contain at most eight tokens (counted before duplicate
normalization). Only the existing ASCII whitespace bytes space, tab, carriage
return, line feed, and form feed split tokens. Leading, trailing, and repeated
whitespace is ignored. Duplicate query tokens are collapsed, then the retained
tokens are sorted in the fixed descriptor so reversed and duplicate-equivalent
queries reuse one canonical collection. Empty and whitespace-only queries
produce empty collections. Missing, non-string, oversized, over-count, and
oversized-token inputs fail closed as empty collections; trailing tokens are
never silently ignored.
Each token also uses the existing selector identifier characters: ASCII
letters, digits, underscore, and hyphen.

The new multi-token representation is a fixed array of token offset/length
ranges inside the existing selector descriptor. It adds no heap-backed token
container, regex, class-state copy, DOM walker, result array, or collection
registry. Single-token class retrieval keeps the existing `Class` descriptor
and exact matcher path. Multi-token matching calls that same exact matcher for
each retained token and returns true only when every call succeeds.

Document retrieval walks the authoritative structural Element list in
document order. Element retrieval keeps JS42's strict-descendant scope: the
receiver is excluded, and Elements outside its structural subtree are
excluded. Scope does not follow `form.elements`; a form-owned control outside
the form's structural subtree is not a scoped retrieval result. Returned host
Elements preserve generation plus structural-serial identity, so equivalent
results from `querySelectorAll(".action")`, retrieval, `contains()`, and normal
traversal remain canonical.

Collections retain the parsed token descriptor and scope serial in the same
128-record registry and re-evaluate current structural class state on length or
index reads. They do not snapshot result serials. Indexed misses remain
`undefined`, writes to length or indices remain read-only, and generation
changes make held document/scoped collections and returned Elements stale
without resolving reused serials in the replacement document. Each result
matches both corresponding single-class selectors; token-order and
duplicate-query forms share identical ordered results. Retrieval does not
alter focus, form values/defaults, layout, structural records, generation, or
event metadata. Callback and nested-dispatch tests exercise retrieval through
the existing event call path.

CSS selector parsing is unchanged. `querySelectorAll(".action.primary")` and
`div.action.primary` remain unsupported unless separately implemented by a
future selector milestone. JS44 does not add `classList`, `className` mutation,
DOM mutation, or other collection APIs. This describes only the documented
bounds and behavior in the current Navigator runtime; it does not claim full
browser class or DOM compatibility.

The native suite is `tests/navigator_javascript_js44_test.cpp`, built by
`scripts/smoke-navigator-javascript-js44.ps1`. The hosted fixture is
`navigator-smoke/javascript-js44.html` and is part of the production hosted
aggregate through `server.cpp`. The final focused, aggregate, production-build,
kernel, and artifact results are recorded in the JS44 completion summary below.

### JS44 completion summary

The JS44 focused suite passes **183/183 checks**. The full JavaScript matrix
passes **42/42 lanes**, covering the lexer, parser, runtime, and JS6–JS44.
Focused JS36–JS43 regressions pass **99/99, 180/180, 152/152, 218/218,
155/155, 220/220, 235/235, and 277/277** checks respectively. JS42's earlier
single-token-only assertions now verify that outer whitespace trims and that
multi-token class retrieval requires every requested token.

The production hosted aggregate passes **526 checks** with the same **7 known
CSS failures** as JS43 (3C, 3G, 6A, three 6B checks, and 6C). All four JS44
hosted checks pass, including actual click dispatch, strict Element scope,
ordered identity, duplicate and reversed tokens, superset matching, and the
single-class `querySelectorAll()` subset relation. The trusted HTTPS GET check
accepts a nonempty loaded response because the live example.com page no longer
uses the former “Example Domain” title; a separate passing Page Info check still
verifies Schannel credentials, handshake, and certificate validation.

`build.bat` passes, and the JS44 smoke script passes its strict bare-metal
adapter/runtime `-Wall -Wextra -Werror -pedantic -fsyntax-only` lane. `pwsh.exe`
is available. `build-kernel.bat` reaches its PowerShell build script but stops
before the kernel phase when the PacMan Native ELF link reports unresolved
`pacman_audio_load_resources` and `pacman_audio_submit` symbols. The direct
`mingw32-make ARCH=amd64 EXTRA_CFLAGS=` kernel lane reaches the existing Mbed
TLS configuration errors at `mbedtls_check_config.h:51` and `:64`. No TLS
implementation files were changed. No fresh kernel was produced, so QEMU proof
is not claimed.

The wrapper's regenerated PacMan object files were restored after confirming
they were generated by this attempt. A content-hash comparison against the
preflight manifest confirms `ESP/ramdisk.img` (64 MiB), all 89 wallpaper-pack
files (58,221,367 bytes), and all 4 PacMan package files (1,303,667 bytes)
remain unchanged. These artifacts are excluded from the JS44 source commit.
`git diff --check` passes. The source commit and ending branch/worktree state
are reported in the JS44 task summary.

Recommended JS45 direction: if a focused selector extension is next, add bounded
compound class selectors such as `.action.primary` through the existing simple
selector descriptor and exact class matcher. Keep relation chains, selector
lists, attributes, and pseudo-classes deferred.

## JS45: bounded compound-class selectors

JS45 adds `.foo.bar` and `tag.foo.bar` to the existing simple-selector grammar.
Every distinct class token is required on the same Element. Requested token
order does not matter, additional Element classes are allowed, and duplicate
requirements are idempotent. Element class matching remains exact,
case-sensitive, and ASCII-whitespace-delimited; tag matching remains ASCII
case-insensitive. Repeated class tokens in an Element have no multiplicity
meaning.

The selector descriptor now stores every class selector, including a single
`.foo` or `tag.foo`, as the same fixed list of token offset/length ranges used
for bounded class retrieval. `Class` and `TagClass` descriptors use the JS44
exact token-set matcher; the old separate single-class text field is removed.
The parser accepts at most eight raw class components per simple selector,
counted before duplicate normalization, and retains the existing 256-byte
selector input cap. Empty dot-separated components and malformed class
compounds fail closed. Descriptor copies own their token text inside the
bounded selector storage, so live collections retain no parser scratch or
temporary source pointers.

`querySelector()`, `querySelectorAll()`, `matches()`, and `closest()` all use
the shared selector matcher. The existing one-relation model also accepts
compound simple selectors on either side of descendant, child, adjacent
sibling, and general sibling relations. Two-side compounds work within that
single relation. Arbitrary chains remain invalid. `.foo .bar` stays a
descendant relation and is distinct from `.foo.bar` on one Element.

`getElementsByClassName("foo bar")` remains its own retrieval API. Its bounded
descriptor uses the same token ranges and exact all-token matcher as
`.foo.bar`; document and Element-scoped retrieval agree with equivalent
`querySelectorAll()` results in order and canonical Element identity. Existing
128-record selector collection behavior is unchanged. Collections reevaluate
their retained descriptor on reads, remain read-only and generation-safe, and
old individual results still make stale `matches()` return false and stale
`closest()` return null.

At JS45, ID/class compounds such as `#id.foo` and `tag#id.foo` remained
unsupported; JS46 below adds them.
Universal-class compounds such as `*.foo` are also deferred because they add no
matching condition beyond `.foo`. Selector lists, attributes, pseudo-classes,
and longer combinator chains remain outside this bounded grammar. This does
not claim general CSS compound-selector completeness.

The JS45 native suite is `tests/navigator_javascript_js45_test.cpp`, built by
`scripts/smoke-navigator-javascript-js45.ps1`. The hosted fixture is
`navigator-smoke/javascript-js45.html`, and its checks run in the production
hosted aggregate through `server.cpp`.

The JS45 focused suite passes **184/184 checks**, including the strict bare-metal
adapter/runtime warning-as-error syntax lane. The complete JavaScript matrix
passes **43/43 lanes**: lexer, parser, runtime, and JS6–JS45. Focused JS36–JS44
regressions pass **99/99, 180/180, 152/152, 218/218, 155/155, 220/220,
235/235, 277/277, and 183/183** checks respectively.

The hosted aggregate reports **530 passed / 7 failed** out of 537 checks. All
four JS45 hosted checks pass, covering the core selectors, authentic event
delegation and nested click, ordered retrieval equivalence, scope, and
relational compounds. The seven failures remain the established CSS checks:
3C, 3G, 6A, three 6B checks, and 6C. `build.bat` passes. The JS45 smoke script's
strict bare-metal adapter/runtime lane passes.

`build-kernel.bat` reaches PacMan package linking and stops on unresolved
`pacman_audio_load_resources` and `pacman_audio_submit`. The independent direct
kernel lane, `mingw32-make ARCH=amd64 EXTRA_CFLAGS=`, stops at the existing
Mbed TLS configuration errors in `mbedtls_check_config.h:51` and `:64`. Neither
subsystem was changed. No fresh kernel was produced, so QEMU proof is not
claimed. The wrapper regenerated three tracked PacMan object files; they were
restored to their clean preflight contents. `ESP/ramdisk.img` (64 MiB), all 89
wallpaper-pack files (58,221,367 bytes), and all four PacMan package files
(1,303,667 bytes) retain their pre-attempt hashes.

The JS46 direction from the JS45 summary is implemented in the section below.

## JS46: bounded ID and class compound selectors

JS46 adds `#id.class`, `#id.class.class`, `tag#id.class`, and
`tag#id.class.class` to the same bounded simple-selector model. A present tag,
the one exact ID, and every requested class token must match the same
structural Element. Tag matching remains ASCII case-insensitive; ID and class
matching remain exact and case-sensitive. Class token order does not matter,
duplicate selector requirements are idempotent, and extra Element classes are
allowed.

The simple-selector descriptor now stores validity/universal metadata and
orthogonal optional tag and ID ranges plus the fixed class-token count and
ranges. `#id`, `.class`, `.foo.bar`, `tag`, `tag.foo.bar`, `tag#id`, and `*`
continue to use this representation. The former selector-kind cases for ID,
class, tag, tag-plus-class, and tag-plus-ID are removed. One shared matcher
checks the optional tag, exact ID, and all class tokens; ID/class selectors do
not use a separate matching path.

The canonical parser order is optional tag, optional `#id`, then zero or more
`.class` requirements. For example, `button#save.action.primary` is valid;
`.action#save`, `button.action#save`, and `.action.primary#save` are not.
Multiple IDs are rejected rather than partially accepted. Empty IDs/classes,
repeated dots, and other malformed compounds fail closed. Universal forms such
as `*#save`, `*.action`, and `*#save.action` remain deferred.

At most eight raw class requirements are accepted per simple selector, counted
before duplicate normalization. The selector input limit remains 256 bytes.
Class matching remains exact token matching over ASCII whitespace-separated
Element classes, with no substring behavior.

`querySelector()` returns the first structural match. `querySelectorAll()`
checks every structural candidate in order, including malformed markup with
duplicate IDs, and returns every match. `matches()` and `closest()` use the
same matcher. The richer simple selectors also work on either side of the
existing one-relation descendant, child, adjacent-sibling, and general-sibling
forms. Arbitrary relation chains remain unsupported.

Matching preserves canonical Element identity with `getElementById()` and
multi-token `getElementsByClassName()` results. An ID-plus-class query is a
subset of the corresponding all-class-token retrieval results. Collections
retain copied bounded tag/ID/class ranges, relation, scope, and generation
state; they do not retain parser-buffer pointers. They remain read-only and
generation-safe. Stale collections fail closed, stale individual
`matches()` returns false, and stale `closest()` returns null.

JS46 does not implement reordered ID/class compounds, Universal compounds,
selector lists, attributes, pseudo-classes, pseudo-elements, or arbitrary CSS
compound-selector completeness. The native focused suite is
`tests/navigator_javascript_js46_test.cpp`, built by
`scripts/smoke-navigator-javascript-js46.ps1`. The hosted fixture is
`navigator-smoke/javascript-js46.html`; its checks are part of the production
hosted aggregate in `server.cpp`.

The JS46 focused suite passes **220/220 checks**. The warning-as-error strict
bare-metal adapter/runtime syntax lane passes. The full JavaScript matrix passes
**44/44 lanes**: lexer, parser, runtime, and JS6–JS46. Focused JS36–JS45
regressions pass **99/99, 180/180, 152/152, 218/218, 155/155, 220/220,
235/235, 277/277, 183/183, and 184/184** checks respectively.

The production hosted aggregate reports **534 passed / 7 failed** out of 541
checks. All four JS46 hosted checks pass. The seven failures are the established
CSS checks: 3C, 3G, 6A, three 6B checks, and 6C. `build.bat` passes.

`build-kernel.bat` still stops at PacMan linking with unresolved
`pacman_audio_load_resources(gx_app_context*)` and
`pacman_audio_submit(void*, PacManSoundId)`. It regenerated the tracked
`game.o`, `main.o`, and `renderer.o` PacMan build objects; those three files
were restored to the clean preflight HEAD contents. The independent direct
kernel command `mingw32-make ARCH=amd64 EXTRA_CFLAGS=` still fails at
`mbedtls_check_config.h:51` and `:64`. No PacMan or Mbed TLS sources were
changed, and no fresh kernel was produced, so QEMU proof is not claimed.
`ESP/ramdisk.img`, all 89 `out/wallpaper-pack/` files, and all four
`out/wallpaper-pack/Apps/PacMan/` files retain their pre-attempt hashes.

Recommended JS47 direction: retain the bounded simple-selector representation
and consider only a narrowly specified selector-list feature if the existing
host and collection limits can support it without weakening fail-closed
parsing or generation-safe identity.

## JS47: bounded read-only Element attribute reads

JS47 adds `Element.getAttribute(name)` and `Element.hasAttribute(name)` through
one shared resolver over the current structural Element and form metadata.
Presence is kept separately from retained values on the parser's
`HtmlElementRef`, so empty `id`, `class`, `style`, and retained form values can
remain distinguishable from missing attributes. There is no JavaScript-side
attribute object, per-Element cache, or mutation API.

The parser has no generic attribute table. `HtmlElementRef` already retains
`id`, `class`, and inline style. The existing form model retains `name`, input
and button `type`, form-control defaults, and option values. JS47 exposes these
retained fields, plus presence for `disabled`, `checked`, and `selected` where
the parser handles those attributes. `form.name` comes from the existing
`FormContainerMetadata`. `input.getAttribute("value")` reads the form
runtime's default value; editing `.value` does not change it, and reset restores
that default. `checked` and `selected` presence is based on parse-time
structural presence, not the current `.checked` or `.selected` property.

Supported reads are `id`, `class`, `style`, form/control `name`, input/button
`type`, input/button/option `value`, `disabled` on controls and fieldsets,
`checked` on checkbox/radio inputs, and `selected` on options. Attribute names
are matched ASCII case-insensitively and accept at most 64 bytes using the
bounded ASCII form `[A-Za-z][A-Za-z0-9:_-]*`. Missing attributes, an empty or
malformed name, unsupported names, stale handles, overlong names, and
non-string arguments fail closed:
`getAttribute()` returns `null`, and `hasAttribute()` returns `false`. A
present empty retained value returns `""` while `hasAttribute()` remains true.

Attribute values are not lowercased. The parser's existing value handling is
preserved: `id`/`class`/style strings are returned as retained, form names and
control values are entity-decoded and bounded to their existing 128-byte or
256-byte limits, and input/button `type` is the parser's trimmed lowercase
form. Boolean attributes have only presence metadata, so their
`getAttribute()` value is normalized to the empty string. The host read bound
is 64 KiB; a larger retained string makes `getAttribute()` return `null` while
`hasAttribute()` still reports presence.

Arbitrary attributes are not retained. In particular `data-*`, `href`, and
`src` reads are unavailable even where other navigation/image code consumes
those attributes. This is a parser limitation, not an empty attribute value.
JS47 adds no attribute selectors or attribute mutation APIs. Attribute
methods are available on canonical Elements returned by selectors, ID lookup,
form/select collections, traversal, focus, and event targets. Reads are
synchronous and read-only; stale handles return `null`/`false` after generation
changes, including when a later document reuses their serial.

The JS47 focused suite passes **137/137 checks**, including the strict
bare-metal `-Wall -Wextra -Werror -pedantic -fsyntax-only` lane. The full
JavaScript matrix passes **45/45 lanes**: lexer, parser, runtime, and JS6–JS47.
Focused JS36–JS46 regressions pass **99/99, 180/180, 152/152, 218/218,
155/155, 220/220, 235/235, 277/277, 183/183, 184/184, and 220/220** checks
respectively.

The production hosted aggregate reports **538 passed / 7 failed** out of 545
checks. All four JS47 hosted checks pass, covering retained and missing
attributes, current versus default value, authentic event-target reads,
canonical Element identity, and nested Event metadata. The seven failures
remain the established CSS checks: phases 3C and 3G, phase 6A, three phase 6B
checks, and phase 6C. `build.bat` passes.

`build-kernel.bat` stops at PacMan linking with unresolved
`pacman_audio_load_resources(gx_app_context*)` and
`pacman_audio_submit(void*, PacManSoundId)`. It regenerated three tracked
PacMan objects, which were restored to their clean preflight contents. The
independent direct kernel command `mingw32-make ARCH=amd64 EXTRA_CFLAGS=`
stops at the existing Mbed TLS configuration errors in
`mbedtls_check_config.h:51` and `:64`. Neither subsystem was changed. No fresh
kernel was produced, so QEMU proof is not claimed. `ESP/ramdisk.img` (64 MiB),
all 89 `out/wallpaper-pack/` files (58,221,367 bytes), and all four
`out/wallpaper-pack/Apps/PacMan/` files (1,303,667 bytes) match their
pre-attempt hashes.

JS47 supports only the retained attribute subset described above; arbitrary
and `data-*` attributes remain unavailable because the parser discards them.
The shared resolver reads the current document's structural/form metadata and
returns borrowed views only for synchronous runtime copying. No JS-side
attribute map or mutation API was added.

## JS48: bounded parser-owned generic attributes

JS48 extends the HTML parser's retained attribute model so the existing
`getAttribute(name)` and `hasAttribute(name)` methods can inspect arbitrary
attributes on structurally retained Elements. This includes `data-*`,
`aria-*`, `href`, `src`, `alt`, `title`, `rel`, and custom names such as
`custom-thing`. There is still no JavaScript-side attribute dictionary and no
generic attribute mutation API.

At opening-tag handling, the parser scans attribute syntax into a fixed
scratch array of name/value views. When it registers a structural Element, it
copies retained values into the owning `WebDocument`'s packed byte pool. Each
`HtmlElementRef` stores only a byte offset and a retained-record count; its
records contain a one-byte name length, a two-byte value length, a lowercase
name, and the value bytes. Returned runtime strings copy from this document
storage, so values outlive parser scratch and remain safe across later reads.
The document pool is generation-local and discarded with its document.

The fixed retention bounds are:

| Bound | Limit |
| --- | ---: |
| Source attributes considered per Element | 16 |
| Retained attribute-name bytes | 64 |
| Retained attribute-value bytes | 256 |
| Retained records per document | 2,048 |
| Encoded name/value/header bytes per document | 65,535 |
| Structural Elements per document | 1,024, unchanged |

The 65,535-byte aggregate limit includes each record's three-byte header.
`HtmlElementRef` remains 440 bytes on the validated 64-bit build because the
new offset/count fields fit existing alignment padding. `WebDocument` grows by
48 bytes for the pool vector and counters. Its packed pool reserves at most
65,535 additional bytes after the first retained attribute; there is no
per-attribute heap allocation or per-Element container.

HTML attribute names are retained only when they fit JS47's query grammar,
`[A-Za-z][A-Za-z0-9:_-]*`, and are looked up ASCII case-insensitively. The
first exact duplicate wins, including duplicate `id`, `class`, and `data-*`
attributes. The parser's specialized extractor and generic retention use that
same first occurrence. Values preserve case. Generic values decode the
parser's supported entities; the surrounding quotes are not retained.
Present-empty and valueless attributes return `""` and remain present.
Boolean `checked`, `selected`, and `disabled` values follow JS47's empty-string
convention. The legacy `id`, `class`, and inline `style` projections keep their
existing raw-value behavior.

Overflow is deterministic and non-destructive. Only the first 16 source
attributes on an Element are considered. An overlong or invalid name and a
raw or decoded value longer than 256 bytes are dropped whole, never truncated
into a potentially ambiguous attribute. If the document record or byte budget
fills, later generic records are dropped while structural parsing continues;
already-retained records and neighboring Elements remain intact. Specialized
JS47 fields remain a fallback when a generic record was omitted, so legacy
reads such as `id`, `class`, `style`, and supported form metadata continue to
work under generic-storage pressure. Drop counters on `WebDocument` record
per-Element, name, value, duplicate, and document-budget drops.

The shared resolver consults the retained document model first for ordinary
attributes. `id`, `class`, and `style` continue to use their established
specialized projections for JS47 compatibility. Specialized `name`, `type`,
boolean, and form-value reads keep their existing behavior. For input/button
`value`, the resolver continues to read the form runtime's default-value
state, which can be changed through the existing `defaultValue` property.
Editing current `.value` does not change that state; checked and selected
reads remain parse-time presence and do not alias current properties. These
compatibility paths are the only places where the legacy projection takes
precedence over a retained generic record.

JS48 adds no `setAttribute()`, `removeAttribute()`, `toggleAttribute()`,
`attributes`, `NamedNodeMap`, `Attr`, `dataset`, `classList`, or attribute
selectors. Generic attribute mutation remains unsupported. Missing and
unsupported queries retain JS47's `null`/`false` results, malformed and
over-64-byte query names still fail closed, and stale receivers cannot read
attributes from a later document even when structural serials are reused.

The JS48 focused suite passes **136/136 checks** and its bare-metal
warning-as-error compile and strict parser/adapter/runtime syntax lane pass.
The complete JavaScript matrix passes **46/46 lanes**: lexer, parser, runtime,
and JS6–JS48. Focused JS36–JS47 regressions pass **99/99, 180/180, 152/152,
218/218, 155/155, 220/220, 235/235, 277/277, 183/183, 184/184, 220/220,
and 137/137** checks respectively. JS48's parser stress cases cover the 16
attribute-per-Element cap, 64-byte name limit, 256-byte value limit, 2,048
record cap, aggregate byte overflow, continuation to later Elements, and
first-wins duplicate behavior. Its runtime cases cover entity decoding,
empty/boolean presence, selector and form consistency, collection/traversal
access, current/default separation, stable returned strings, authentic event
targets, nested dispatch metadata, purity, and stale serial reuse.

The production hosted aggregate reports **543 passed / 7 failed** out of 550
checks. All five new JS48 hosted checks pass: generic and ARIA reads, an
authentic `data-action` event-target read, retained `href`/`src`, current versus
default input value, and nested Event metadata with canonical Element
identity. The seven failures are unchanged CSS checks: phases 3C and 3G, phase
6A, three phase 6B checks, and phase 6C. The production `build.bat` succeeds.

The kernel wrapper still stops before the kernel build at PacMan linking with
unresolved `pacman_audio_load_resources(gx_app_context*)` and
`pacman_audio_submit(void*, PacManSoundId)`. The independent
`mingw32-make ARCH=amd64 EXTRA_CFLAGS=` lane still stops at the existing Mbed
TLS configuration errors in `mbedtls_check_config.h:51` and `:64`. No fresh
kernel was produced, so QEMU proof is not claimed. The wrapper regenerated
`build/pacman-native/amd64/objects/game.o`, `main.o`, and `renderer.o`; all
three were restored to their clean preflight contents. `ESP/ramdisk.img`, all
89 `out/wallpaper-pack/` files, all four PacMan package files, and the tracked
PacMan build objects match their pre-kernel hashes. No generated artifacts
are part of the JS48 source change.

## JS49: bounded `setAttribute()` and `removeAttribute()`

JS49 exposes `Element.setAttribute(name, value)` and
`Element.removeAttribute(name)` on canonical Navigator Elements. Both methods
return `undefined`. They mutate JS48's document-owned packed attribute pool;
there is no JS-side dictionary, mutation sidecar, selector override, or
per-host-object copy of generic attributes. The packed records remain the
authoritative state read by `getAttribute()` and `hasAttribute()`.

Attribute names must be strings matching the existing ASCII query grammar
`[A-Za-z][A-Za-z0-9:_-]*`, with a 64-byte maximum. Names are stored lowercase
and matched case-insensitively, so case variants replace/remove the same
logical record. Values must be strings and are preserved byte-for-byte and
case-for-case, with a 256-byte maximum. Empty string is a present empty value.
Missing arguments, non-string arguments, invalid/overlong names, overlong
values, stale receivers, and capacity failures are safe no-ops; Navigator does
not add JavaScript coercion in this milestone. Removing an absent valid name
is also a no-op. Names and values are validated before mutation, so an
overlong replacement leaves the existing value intact.

An insertion adds one record if all limits permit. Replacement edits the
existing record and does not add a logical record. Removal erases the record,
compacts the packed bytes, updates later Element offsets, and releases one
logical record and one per-Element slot. Re-insertion uses those released
limits. The encoded record is exactly three header bytes (one name-length byte
and a two-byte little-endian value length), followed by lowercase name bytes
and exact value bytes. At the maximum name and value sizes a record is 323
bytes; no per-attribute string object or free-list entry is allocated.

| Bound/accounting | JS49 limit or cost |
| --- | ---: |
| Logical attributes per Element | 16 |
| Logical records per document | 2,048 |
| Name bytes per record | 64 |
| Value bytes per record | 256 |
| Encoded bytes per record | 4–323 |
| Aggregate retained encoded bytes | 65,535 |
| Additional per-Element metadata from JS49 | 0 bytes |
| Additional per-record metadata from JS49 | 0 bytes |
| Free-list / slot metadata | none |

Each Element continues to carry JS48's 16-bit packed-pool offset and 8-bit
record count (three logical metadata bytes; `HtmlElementRef` remains 440 bytes
on the validated 64-bit build because those fields occupy existing alignment
padding). `WebDocument` continues to own the packed vector and record/byte
counters. Mutation inserts/erases within the bounded pool and adjusts offsets
of later Elements; repeated replacement and removal therefore do not create a
one-way append budget. The first mutation in a document without a reserved
pool reserves the fixed 65,535-byte arena before changing logical state.

The existing attribute-count, record-count, and byte limits are checked before
the record bytes or projections change. Replacing at 16 attributes is allowed;
inserting a seventeenth is rejected. A full 2,048-record document rejects an
additional record, while removal allows later reuse. Aggregate byte overflow
also rejects insertion/replacement atomically. The JS49 capacity tests fill a
single Element to 16 records, reach exactly 2,048 records across 128 Elements,
and fill the byte pool to fewer than 67 bytes remaining. They verify that
replacement at capacity succeeds, rejected insertion leaves no partial
record, and a rejected replacement preserves its old value.

`id` and `class` are the reflected attributes that mutate in JS49. Their
generic packed record and specialized structural projection/presence bit are
updated together, including duplicate `documentElement`, `body`, and block
projections that carry the same Element serial. Consequently `getAttribute()`,
`hasAttribute()`, `getElementById()`, ID/class matching, compound class
matching, `getElementsByClassName()`, and held live class/query collections
observe changes immediately. Removing either attribute clears its presence
and projection. A parser-retained specialized ID/class projection can still be
updated if its generic record was omitted at a parser retention cap, without
manufacturing a seventeenth generic record. Canonical Element identity,
structural serials, tree relationships, and document generation do not
change.

Other reflected attributes are deliberately rejected on the Elements whose
runtime consumers use specialized state, preserving coherent reads and form
behavior rather than creating conflicting representations:

| Attribute | Mutation decision |
| --- | --- |
| `style` | Deferred on every Element; the CSS/style projection is not synchronized by JS49. |
| `name` | Deferred on `input`, `button`, `textarea`, `select`, and `form`; generic on other tags. |
| `type` | Deferred on `input` and `button`; generic on other tags. |
| `value` | Deferred on `input`, `button`, and `option`; generic on other tags. For controls, `getAttribute("value")` remains the markup default and `.value` remains the current runtime value. |
| `checked` | Deferred on checkbox/radio inputs; their checked/default/current state is unchanged. Generic on other tags and non-checkable inputs. |
| `selected` | Deferred on `option`; selected/default/index state is unchanged. Generic on other tags. |
| `disabled` | Deferred on `input`, `button`, `textarea`, `select`, `option`, and `fieldset`; generic on other tags. |

For deferred names, both setting and removing are no-ops and all existing
attribute/form/style views remain unchanged. This includes input reset
defaults, current values, checked state, selected options, disabled behavior,
and CSS style state. Generic/data attributes and attributes such as `href`,
`src`, and custom names remain mutable when the Element and document bounds
permit. Attribute selectors remain out of scope, so this milestone's selector
integration is the existing ID/class/tag selector model.

The host validates the current Element receiver before mutation. A stale
receiver cannot modify a replacement document even if a structural serial is
reused. Event-target Elements use the same host path: mutation is visible to
same-callback reads and selectors, and nested dispatch preserves existing
`target`, `currentTarget`, and event metadata behavior.

The JS49 focused suite passes **150/150 checks**, including argument and
capacity failure semantics, 16/2,048/65,535-byte caps, ID/class projection,
live collections, deferred form/style behavior, canonical identity, stale
serial reuse, event mutation, and nested dispatch. Its mutation stress performs
**1,000 replacements** and **1,000 remove/re-add/remove cycles**, then restores
one final value. The logical record count is unchanged from the start and the
final packed byte count equals its original value; the final value and a
neighboring Element's record remain correct. The strict
`-Wall -Wextra -Werror -pedantic` parser/adapter/runtime lane passes.

The full JavaScript matrix passes **47/47 lanes**: lexer, parser, runtime, and
JS6–JS49. Focused JS36–JS48 regressions pass **99/99, 180/180, 152/152,
218/218, 155/155, 220/220, 235/235, 277/277, 183/183, 184/184, 220/220,
137/137, and 136/136** checks respectively. `build.bat` succeeds.

The production hosted aggregate reports **548 passed / 7 failed** out of 555
checks. All five JS49 hosted checks pass, covering generic/data/custom writes,
`href`/`src`, ID and class selector/lookup/collection synchronization,
deferred default/current form and style state, and authentic event mutation
with nested dispatch metadata. The seven failures remain the established CSS
checks: phases 3C and 3G, phase 6A, three phase 6B checks, and phase 6C.

The kernel wrapper still stops before producing a kernel at PacMan linking
with unresolved `pacman_audio_load_resources(gx_app_context*)` and
`pacman_audio_submit(void*, PacManSoundId)`. Direct
`mingw32-make ARCH=amd64 EXTRA_CFLAGS=` independently stops at the existing
Mbed TLS configuration errors in `mbedtls_check_config.h:51` and `:64`. Neither
dependency was changed. No fresh kernel was produced, so QEMU proof is not
claimed. The wrapper regenerated `game.o`, `main.o`, and `renderer.o`; those
three objects were restored and all six PacMan object hashes match the
pre-wrapper snapshot. All 94 preflight generated artifact hashes and lengths
match, including `ESP/ramdisk.img` and the wallpaper/PacMan package trees.
Generated build outputs are excluded from the JS49 source change.

JS50 builds on the mutable authoritative generic/id/class state while keeping
style and form-reflected mutation deferred until their consumer projections can
be updated atomically.

## JS50: bounded attribute presence and exact-value selectors

JS50 extends the existing parsed selector descriptor and shared selector
matcher with one optional attribute predicate on each simple-selector side:

```javascript
document.querySelector("[disabled]")
document.querySelector("[data-action=save]")
document.querySelector("button[data-action=save]")
element.matches("button#save.action.primary[data-action=save]")
element.closest(".panel[data-role=panel]")
```

The canonical compound order is an optional tag, optional `#id`, zero or more
`.class` tokens, then one optional `[attribute]` or `[attribute=value]`
predicate. A simple selector can carry at most one attribute predicate. For a
one-relation selector, each side can independently carry one predicate. The
existing one-relation and strict scoped-query behavior is unchanged. Attribute
selectors are accepted on attribute-only, tag, ID, class, and full compounds;
`*[attribute]` remains unsupported.

Attribute names reuse JS49's ASCII grammar
`[A-Za-z][A-Za-z0-9:_-]*` and 64-byte limit. Name lookup remains
ASCII-case-insensitive. Unquoted exact values use the bounded token grammar
`[A-Za-z0-9:_./-]+`, with a 128-byte maximum. Single- and double-quoted values
are also accepted up to 128 bytes; quoted values may contain spaces and other
non-control bytes, but backslash escapes and the matching quote inside the
value are not supported. Quoted empty (`[name=""]`) is supported and matches
only a present empty value. Unquoted empty (`[name=]`) is rejected. Values are
compared byte-for-byte and case-sensitively, while names are looked up
case-insensitively. Presence matches valueless and empty attributes without
requiring a non-empty value.

Selectors remain capped at 256 input bytes. Parsing is manual and fixed-bound;
there is no regex, raw-HTML scan, CSS AST, selector-specific attribute cache,
or dynamic selector storage. Attribute names and values are copied into the
descriptor, not borrowed from the source selector. The current simple-selector
descriptor grows by 10 bytes; the full selector descriptor grows by 20 bytes
because it embeds two simple selectors. The 128-record collection registry
therefore grows by 2,048 bytes total. Each collection retains its parsed
descriptor and scope, and rescans the current document on each read.

Attribute matching calls the same `resolveElementAttribute()` routine used by
`getAttribute()` and `hasAttribute()`. Generic retained values and JS49
mutations are therefore visible immediately, including replacement and
removal. The resolver keeps the established id/class and supported form
projections authoritative. For input/button `value`, checked inputs, and
selected options, selectors observe the same retained/default attribute state
as `getAttribute()`, not current `.value`, `.checked`, or `.selected` state.
The JS49 deferred write policy for style and reflected form attributes is
unchanged. Existing generation validation rejects stale Element receivers
before matching; a reused serial cannot expose a replacement document's
attributes through an old handle. Selector reads do not mutate document state.

The accepted operators are only presence and exact equality. Multiple
attribute predicates on one simple selector, selector lists, universal-plus-
attribute syntax, namespace interpretation, case modifiers, CSS escapes,
and the other CSS attribute operators remain unsupported. Malformed syntax
fails closed (`querySelector()` returns null, `matches()` false, and
`querySelectorAll()` an empty bounded live collection). Entity-decoded retained
values are matched through the resolver; values with spaces require a quoted
selector value.

The JS50 focused suite in `tests/navigator_javascript_js50_test.cpp`, run by
`scripts/smoke-navigator-javascript-js50.ps1`, covers presence, exact and empty
values, name/value case behavior, generic/data/ARIA/custom/href/src attributes,
quoted entity-decoded values, tag/ID/class/full compounds, malformed syntax,
scoped queries, live collections through mutation, all existing one-relation
types, retained form/default semantics, authentic event mutation and nested
dispatch, stale serial reuse, selector purity, and 120 simultaneously retained
selector collections. The strict parser/adapter/runtime warning-as-error lane
is part of the focused script. JS50's production hosted fixture is
`navigator-smoke/javascript-js50.html` and is included in
`scripts/smoke-navigator-hosted.ps1`'s `navigator.smoke` run.

The focused JS50 suite passes **313/313 checks**, and both its optimized
bare-metal harness and strict `-Wall -Wextra -Werror -pedantic` lane pass. It
includes 300 repeated `matches()` calls, 200 repeated live collection reads,
120 distinct retained collections, and live mutation updates. Stale attribute
collections are rejected by generation validation and cannot read a later
document's collection results; stale Element `matches()`/`closest()` continue
to return false/null.

The full JavaScript matrix passes **48/48 lanes**: lexer, parser, runtime, and
JS6–JS50. The first matrix run caught a stale JS36 negative assertion that
treated the newly supported `[type=text]` form as invalid. That assertion now
uses the still-unsupported `[type^=text]` operator, and JS36 reruns at **99/99**.
JS39's malformed-operator check was similarly made explicit as `^=`. Focused
JS36–JS49 regressions pass **99/99, 180/180, 152/152, 218/218, 155/155,
220/220, 235/235, 277/277, 183/183, 184/184, 220/220, 137/137, 136/136,
and 150/150** checks, respectively. JS50 passes 313/313. `build.bat` passes.

The production hosted aggregate reports **553 passed / 7 failed** out of 560
checks. All five hosted JS50 checks pass: exact and presence queries with
scoping/compounds/relations, live set/replace/remove behavior, retained form
defaults, and authentic event mutation with nested dispatch metadata. The
seven failures remain the established CSS checks: phases 3C and 3G, phase 6A,
three phase 6B checks, and phase 6C. No JS50 hosted check failed.

The strict lane passes with no JS50 regex, dynamic CSS AST, map, unbounded
selector storage, exception, RTTI, or hosted-only matcher additions. Descriptor
sizes on the validated 64-bit toolchain are 44 to 54 bytes for a simple
selector and 348 to 368 bytes for the complete descriptor. Each of the 128
collection records rounds from 368 to 384 bytes, for a 2,048-byte registry
increase. Name and value ranges share the existing 256-byte descriptor text
buffer; the descriptor owns no pointers into parser scratch.

`build-kernel.bat` stops in the PacMan Native ELF link before invoking the
kernel build, with unresolved `pacman_audio_load_resources(gx_app_context*)`
and `pacman_audio_submit(void*, PacManSoundId)`. The independent
`mingw32-make ARCH=amd64 EXTRA_CFLAGS=` run from `kernel/` stops at
`mbedtls_check_config.h:51` (unsupported partial ECC curves acceleration) and
`:64` (ECDHE-RSA prerequisites missing). Neither dependency was changed. No
fresh kernel was produced, so QEMU proof is not claimed.

Before kernel attempts, SHA-256 and length snapshots covered 96 files: the
`ESP/ramdisk.img`, all 89 `out/wallpaper-pack/` files (including the four-file
PacMan package), six tracked PacMan object files, and the kernel output folder.
The wrapper regenerated `game.o`, `main.o`, and `renderer.o`; those three were
restored from the pre-kernel copies. All 96 snapshot files then matched their
pre-kernel hashes and lengths, with no new files in the snapshotted paths.
Generated outputs are excluded from the JS50 source commit.

`git diff --check` passes. One local source commit was created for JS50; nothing
was pushed.

## JS51: bounded selector lists

JS51 adds comma-separated selector lists to the JS50 bounded selector
grammar. A list contains one to four members and the complete input remains
limited to 256 bytes. ASCII whitespace around members is trimmed. The scanner
recognizes commas only outside attribute brackets and quoted values, so a
quoted attribute value can contain both a comma and a closing bracket. Every
member uses the existing JS50 parser and grammar, including its tag, ID,
class, one-attribute-predicate, standalone universal, and at-most-one-relation
rules. An empty, malformed, or unsupported member rejects the entire list.

`querySelector()` returns the first matching element in structural document
order, regardless of selector-member order. `querySelectorAll()` scans the
eligible document or scoped descendants once, appending an element only once
when any member matches. Held collections remain live and reflect attribute,
class, and ID mutations on each read. Scoped element queries continue to
exclude the scope element itself. `matches()` uses OR semantics; `closest()`
walks from the receiver toward the root and returns the nearest element
matching any member. Invalid lists fail closed with null, false, or an empty
live collection. Stale generation checks still prevent old Element and
collection handles from reading replacement-document state.

The parser stores no borrowed input pointers, dynamic list, or selector AST.
Four compact member descriptors share one 256-byte descriptor-owned text
buffer. This keeps the complete list descriptor at 524 bytes, compared with
368 bytes for the JS50 complete selector descriptor. A collection record is
544 bytes, compared with 384 bytes in JS50; 128 records use 69,632 bytes,
20,480 bytes more than the prior 49,152-byte registry. Four members are the
bounded list capacity for this representation and its fixed registry cost.

The focused suite is `tests/navigator_javascript_js51_test.cpp`, run by
`scripts/smoke-navigator-javascript-js51.ps1`. It covers the byte and member
limits, top-level comma scanning, all-or-nothing parsing, selector relations,
structural union order and deduplication, scope, `matches()` and nearest
`closest()`, live mutation, event dispatch, nested Event metadata, collection
capacity and read-only behavior, stale generations, serial reuse, and repeated
reads. It passes **278/278 checks**, including the strict
`-Wall -Wextra -Werror -pedantic` parser/adapter/runtime lane. The JS36
regression now also covers selector-list unions and passes **114/114**; its
earlier JS50-era count was 99. The final JavaScript matrix passes **49/49
lanes** across lexer, parser, runtime, and JS6–JS51.

The hosted fixture `navigator-smoke/javascript-js51.html` is part of
`navigator.smoke`. All six JS51 hosted checks pass, including structural
query order, scoped and matching behavior, quoted commas, whole-list failure,
live collections, and authentic delegated click dispatch with nested Event
metadata. The complete hosted aggregate reports **559 passed / 7 failed** out
of 566 checks. Its seven failures remain the existing CSS checks: phases 3C
and 3G, phase 6A, three phase 6B checks, and phase 6C. `build.bat` passes and
links `guideXOSServer.exe`.

The `build-kernel.bat` attempt stops before the kernel build at the PacMan
Native ELF link because `pacman_audio_load_resources(gx_app_context*)` and
`pacman_audio_submit(void*, PacManSoundId)` are unresolved. The separate
`mingw32-make ARCH=amd64 EXTRA_CFLAGS=` attempt from `kernel/` stops at
`mbedtls_check_config.h:51` (partial ECC curves acceleration) and `:64`
(missing ECDHE-RSA prerequisites). No fresh kernel was produced, so no QEMU
proof is claimed.

Before those attempts, a SHA-256 and length snapshot covered 308 files totaling
130,758,816 bytes across `ESP/`, `out/wallpaper-pack/`, `Apps/PacMan/`,
`build/pacman-native/amd64/`, `kernel/build/`, and both bootloader output
folders. The wrapper changed PacMan's `game.o`, `main.o`, and `renderer.o`;
all three were restored from the pre-attempt copies. Final comparison found no
changed, missing, or extra files in the snapshot paths. Generated outputs are
excluded from the source commit.

`git diff --check` and staged validation pass. One local source commit is
created for JS51; nothing is pushed.

## JS52: bounded `:checked`, `:disabled`, and `:focus`

JS52 adds three zero-argument state pseudo-classes to the existing shared
simple-selector matcher. A simple selector may contain at most one of
`:checked`, `:disabled`, or `:focus`. The accepted order is an optional tag,
optional ID, class tokens, optional one attribute predicate, then the optional
state pseudo. `:checked`, `:disabled`, and `:focus` may also stand alone, and
each supports a `*` prefix; the previously unsupported `*[attribute]` form
remains rejected. The existing one-relation selectors and one-to-four selector
lists reuse this same grammar. The complete input remains limited to 256 bytes.
Reordered suffixes, a second pseudo, functional pseudo syntax, and unknown
pseudo names fail closed. Pseudo keywords are ASCII-case-insensitive; ID and
class values keep their existing case-sensitive behavior.

The matcher reads current control and focus state directly. For checkbox and
radio inputs, `:checked` reads the document's `FormRuntimeControlState::checked`
bit, including the current radio-group winner and scripted `.checked` writes.
For options, it compares the option's index in its owning select's
`DocBlock::selectedOption`. It does not inspect retained `checked` or
`selected` attributes. Therefore `[checked]` keeps matching after current
checkbox state is cleared, and an option's retained `[selected]` may disagree
with current `:checked` after selection changes. Non-checkable inputs and
arbitrary Elements never match `:checked`.

`:focus` compares the candidate serial with the adapter's canonical
`activeElementSerial()` projection, which validates the current document focus
serial and generation and its supported focused control. It does not maintain
a selector-side focus copy. `document.activeElement` and `:focus` consequently
use the same owner. Existing focus timing is preserved: focus ownership moves
before `focus`/`focusin` handlers run; the previous owner remains current while
its `blur`/`focusout` handlers run; clearing occurs after those handlers; and
the existing deferred focus redirect drains after the active transition.

`:disabled` reflects the current Navigator runtime projection for supported
form controls: supported input types (text, password, search, email, URL,
number, checkbox, radio, button, submit, and reset), `<button>`, `<textarea>`,
`<select>`, disabled `<option>` data in the owning select block, and
`<fieldset>`. Inputs/buttons/textareas/selects use the document's
`FormRuntimeControlState::disabled`; options use the authoritative
`FormOption::disabled` field; a fieldset uses its existing parsed
`FormControlMetadata::disabled`. Unsupported and hidden input types do not
match. Existing parser fieldset inheritance is observed for child controls;
JS52 does not add or broaden that parser model. A `<div disabled>` can match
`[disabled]` but not `:disabled`. JS49's deferred disabled-attribute writes
remain no-ops and do not change pseudo state. This is the runtime's bounded
applicability model, not a claim of complete browser CSS disabled semantics.

The matcher composes each state condition with the existing tag, ID, all class
tokens, and optional retained-attribute predicate. `querySelector()`,
`querySelectorAll()`, scoped queries, `matches()`, `closest()`, one-relation
selectors, and selector-list union/deduplication all use that matcher. Held
selector collections remain live because each read rescans current Elements
and state. State matching is read-only and checks current Element membership
before accessing runtime state, so stale generations fail closed even when a
replacement document reuses a serial.

The descriptor stores only one `NavigatorScriptStatePseudo` enum value per
simple selector; pseudo names are not copied into the bounded descriptor text.
On the validated 64-bit toolchain the simple descriptor is 32 bytes, the
complete four-member selector-list descriptor is 524 bytes, and a collection
record is 544 bytes. Those sizes are unchanged from JS51: the enum occupies
existing simple-descriptor padding, so the 128-record collection registry
remains 69,632 bytes with zero net growth.

JS52 does not add `:enabled`, `:hover`, `:active`, `:visited`, `:link`,
`:focus-within`, `:focus-visible`, structural or functional pseudo-classes,
pseudo-elements, multiple state pseudos per simple selector, or additional
combinators. Unknown names fail the whole selector or selector list.

The focused suite is `tests/navigator_javascript_js52_test.cpp`, run by
`scripts/smoke-navigator-javascript-js52.ps1`. It covers exact parser bounds,
keyword casing and rejection, checkbox default/current divergence, scripted
and authentic click state changes, radio exclusivity and held collections,
option selection versus its retained attribute, non-applicable controls,
focus acquisition/transfer/blur/redirect and event timing, disabled controls
and fieldset projection, all query APIs and selector forms, nested event
metadata, purity, 1,000 checked writes, 300 held-collection reads, and stale
serial reuse for all three pseudo states. The focused suite passes **319/319
checks**, and its strict `-Wall -Wextra -Werror -pedantic` parser/adapter/runtime
lane passes.

The production hosted fixture is `navigator-smoke/javascript-js52.html` and
adds six checks to `navigator.smoke`: current checked divergence, programmatic
focus and event timing, disabled-attribute distinction, option/held-list
behavior, and nested activation with click-time focus, nested state visibility,
and Event metadata. All six pass in the final hosted run. The aggregate reports
**565 passed / 7 failed / 572 total**. Its seven failures are the existing CSS
checks for phases 3C and 3G, phase 6A, three phase 6B checks, and phase 6C; it
adds no JS52 or other unexplained failure.

The complete JavaScript matrix passes **50/50 lanes** across lexer, parser,
runtime, and JS6–JS52. The focused regression counts include JS24 214/214,
JS25 219/219, JS27 304/304, JS30 137/137, JS31 258/258, JS32 273/273,
JS33 133/133, JS36 114/114, JS37 180/180, JS38 152/152, JS39 218/218,
JS40 155/155, JS41 220/220, JS42 235/235, JS43 277/277, JS44 183/183,
JS45 184/184, JS46 220/220, JS47 137/137, JS48 136/136, JS49 150/150,
JS50 313/313, JS51 278/278, and JS52 319/319. The strict
`-Wall -Wextra -Werror -pedantic` parser/adapter/runtime lane passes. The
production `build.bat` build links `guideXOSServer.exe`.

The `build-kernel.bat` attempt still stops in the PacMan Native ELF link on
unresolved `pacman_audio_load_resources(gx_app_context*)` and
`pacman_audio_submit(void*, PacManSoundId)`. The direct
`mingw32-make ARCH=amd64 EXTRA_CFLAGS=` attempt from `kernel/` still stops at
`mbedtls_check_config.h:51` (partial ECC curves acceleration) and `:64`
(missing ECDHE-RSA prerequisites). JS52 does not modify PacMan or Mbed TLS.
No fresh kernel was produced, so no QEMU proof is claimed.

Before the kernel attempts, the SHA-256/length audit covered 308 files totaling
130,758,816 bytes across `ESP/`, `out/wallpaper-pack/`, `Apps/PacMan/`,
`build/pacman-native/amd64/`, `kernel/build/`, and both bootloader Release
folders. The wrapper regenerated `game.o`, `main.o`, and `renderer.o`; each was
restored from its pre-attempt copy. Final comparison found zero changed files,
zero missing files, and zero extras. Generated outputs are excluded from the
JS52 source commit.

## JS53: bounded structural child pseudo-classes

JS53 adds the zero-argument pseudo-classes `:first-child`, `:last-child`, and
`:only-child` to the shared selector matcher. The pseudo value is stored in the
existing `NavigatorScriptStatePseudo` enum; no second pseudo field, child
index, or selector-side cache is added. The parser compares pseudo keywords
ASCII-case-insensitively and requires an exact name, so mixed-case
`:First-Child` works while malformed hyphen spellings, functional forms,
unknown names, and a second pseudo on one simple selector fail closed.

Child position uses only the current document's represented structural
Elements. Text, comments, parser tokens, layout visibility, and form ownership
do not participate. A hidden but represented Element still occupies its
structural position. Each match first validates the current Element and a
valid, nonzero structural parent. Therefore a document/root Element with no
parent matches none of these pseudos, even when it has no sibling. A
self-parent or missing-parent serial also fails closed.

The matcher reuses `resolveStructuralParentSerial`, `elementSiblingAt`,
`elementChildAt`, and `elementChildCount`, the bounded helpers already used by
parent/sibling traversal and relation selectors:

- `:first-child` requires a valid parent, the candidate to be that parent's
  first structural Element child, and no previous Element sibling.
- `:last-child` requires a valid parent, the candidate to be that parent's
  last structural Element child, and no next Element sibling.
- `:only-child` requires a valid parent with exactly one Element child, that
  child to be the candidate, and no previous or next Element sibling.

The tree's structural order is authoritative. Thus `firstElementChild`,
`lastElementChild`, `previousElementSibling`, `nextElementSibling`,
`childElementCount`, and `children` agree with the pseudo results. A sole
structural child can match all three pseudos independently. `formControl`'s
`parentFormSerial`, form membership, and named controls never supply a
structural parent.

The existing compound-selector order and bounds remain in force: optional
tag, optional ID, class tokens, optional attribute predicate, then one
pseudo; pseudo-only and `*` plus pseudo forms are supported. The three pseudos
compose with tag, ID, class, and attribute selectors and work on either side
of the existing one-relation selector grammar. One-to-four member selector
lists retain document-order matching and deduplicate a sole child that matches
both `:first-child` and `:last-child`. `querySelector()`, live
`querySelectorAll()` collections, scoped queries, `matches()`, and `closest()`
all use the same matcher. Held collections re-read current structure on each
access; JS53 does not add public DOM insertion or removal.

Generation-checked canonical Element identity is validated before structural
lookups. Stale `matches()` returns false, stale `closest()` returns null, and
stale collections retain the existing fail-closed behavior even if a new
document reuses an old serial. Pseudo matching is read-only and does not alter
parent/sibling metadata, attributes, form state, focus, generation, or Event
state.

The focused suite is `tests/navigator_javascript_js53_test.cpp`, run with
`scripts/smoke-navigator-javascript-js53.ps1`. It covers parser rejection and
the 256-byte selector cap, all three structural rules, root/no-parent behavior,
hidden Elements, form-owner independence, traversal consistency, selector
composition and relations, list ordering/deduplication, scoped queries,
`matches()` and `closest()`, live collection reevaluation after a synthetic
parent reassignment, malformed parent metadata, 1,000 repeated matches per
pseudo, 300 held-collection rereads, nested click dispatch, Event metadata,
stale handles, serial reuse, and first/last matching among 1,000 structural
siblings within the 1,024-node bound. The focused suite passes 211/211 checks;
the strict `-Wall -Wextra -Werror -pedantic` adapter/runtime lane passes.

The production `build.bat` build passes. The full JavaScript matrix, including
lexer, parser, runtime, JS6–JS52, and JS53, passes 51/51 lanes. The JS36–JS52
focused regressions all pass; JS36–JS43 report 114/114, 180/180, 152/152,
218/218, 155/155, 220/220, 235/235, and 277/277 checks, respectively. JS44,
JS45, JS46, JS47, JS48, JS49, JS50, JS51, and JS52 report 183/183, 184/184,
220/220, 137/137, 136/136, 150/150, 313/313, 278/278, and 319/319 checks.

The production hosted aggregate passes all seven JS53 checks, including root
failure, traversal agreement, relation matching, selector-list deduplication,
and first/last Event matching through nested dispatch. The aggregate reports
572 passed / 7 failed / 579 total. Its seven failures remain CSS 3C, CSS 3G,
CSS 6A, three CSS 6B checks, and CSS 6C; there are no new JS53 failures.

The JS53 kernel wrapper stops at the existing PacMan Native ELF link errors
for `pacman_audio_load_resources(gx_app_context*)` and
`pacman_audio_submit(void*, PacManSoundId)`. The independent direct kernel lane
fails at `mbedtls_check_config.h:51` and `:64`, for partial ECC curve
acceleration and missing ECDHE-RSA prerequisites. Neither dependency was
modified. No fresh kernel was produced, so no QEMU proof is claimed.

Before those kernel attempts, the generated-output audit snapshotted 316 files
totaling 147,184,909 bytes across `ESP/`, `out/`, PacMan, the amd64 PacMan
objects, `kernel/build/`, and both bootloader Release folders. The wrapper
changed `game.o`, `renderer.o`, and `main.o`; all three were restored from the
snapshot. Final comparison found zero changed, zero missing, and zero extra
files. Generated artifacts are excluded from the JS53 source commit.

The JS53 increment did not add `:first-of-type`, `:last-of-type`, or
`:only-of-type`; JS54 adds those below. `nth-*`, `:empty`, `:root`, functional
pseudos, pseudo-elements, multiple pseudos on one simple selector, more
relation depth, and public DOM insertion/removal APIs remain outside this
bounded structural subset of CSS selectors.

## JS54: bounded structural of-type pseudo-classes

JS54 adds the zero-argument selector pseudos `:first-of-type`,
`:last-of-type`, and `:only-of-type` to the existing simple-selector pseudo
field. The optional pseudo enum remains one byte, so the descriptor does not
grow. The implementation uses the current `WebDocument::structuralElements`
records and their `parentSerial` links, requires a resolvable nonzero parent,
and scans at most `min(maxDocumentNodes, structuralElements.size())` records.
Only structural Elements with that same parent are candidates. Text, comment,
parser-token, layout, style visibility, and form-ownership state do not take
part.

“Type” uses the same `selectorTagEquals()` helper as an ordinary tag selector.
That comparison is ASCII case-insensitive under the existing selector/tag
rules. `HtmlElementRef::tagName` is a `std::string` with no separate fixed
per-tag byte limit; HTML parsing lowercases tag names before storing them and
the structural registry is capped at 1,024 Elements. The selector itself
remains subject to the 256-byte bound. Of-type evaluation compares directly
against the current structural tag and does not allocate or cache indexes.

Each pseudo requires a valid structural parent, including for `:only-of-type`;
a root Element with `parentSerial == 0`, a missing parent record, or a
self-parent fails closed. First/last scans use the structural vector's order
and ignore different-tag siblings. Only-of-type succeeds when there is no
other same-tag structural sibling. Hidden represented Elements count, and
logical form ownership does not change structural position. Class and
attribute predicates filter the matched Element, but do not change which
sibling tags count as its type. Thus a button between spans can be both
`:first-of-type` and `:last-of-type` while failing all three child pseudos;
`:class:first-of-type` still means the first sibling with that tag, not the
first sibling with that class.

Before evaluating type position, the matcher checks the bounded same-parent
sequence against each Element's `childIndex`, `previousSiblingSerial`, and
`siblingCount`, plus the parent's `childCount`. Missing or inconsistent order
metadata fails closed.

Pseudo-only, universal, tag, ID, class, attribute, and full compound forms
share the existing matcher. Pseudo names remain exact, hyphenated,
ASCII-case-insensitive keywords; functional forms, malformed spellings,
unknown names, reordered compound syntax, and a second pseudo on one simple
selector fail closed. The pseudos compose with the existing single-relation
selectors, selector lists, `querySelector()`, live `querySelectorAll()`
collections, `matches()`, and `closest()`. Selector-list results retain
structural document order and deduplicate Elements that match multiple
members. Held collections reevaluate the current parent and tag state. Stale
Elements and collections keep the existing generation checks.

The focused suite is `tests/navigator_javascript_js54_test.cpp`, run by
`scripts/smoke-navigator-javascript-js54.ps1`. It covers parser bounds,
mixed-type and same-type sibling rules, all-six-pseudo child/of-type
distinction, hidden siblings, form ownership, malformed and parentless
Elements, selector compounds and relations, list overlap, API identity, live
parent/tag reevaluation, generation reuse, event matching, purity, and
near-capacity stress. The stress fixture has 1,024 structural Elements with
1,021 siblings under one parent, including 1,019 same-tag spans. It completes
1,000 repeated matches for each of the three pseudos and 300 reread rounds for
held first/last/only collections. The focused suite passes **238/238 checks**;
its strict warning-as-error parser/adapter/runtime lane also passes.

JS54 reuses the fixed selector descriptors and collection registry. Sizes
remain **32 bytes** for a simple selector, **524 bytes** for a four-member
selector descriptor, **544 bytes** per collection record, and **69,632 bytes**
for the 128-record registry. No selector or Element cache was added.

The complete JavaScript matrix, lexer, parser, runtime, and JS6–JS54, passes
**52/52 lanes**. Focused regressions JS36–JS54 all pass: JS36–JS43 report
114/114, 180/180, 152/152, 218/218, 155/155, 220/220, 235/235, and 277/277;
JS44–JS53 report 183/183, 184/184, 220/220, 137/137, 136/136, 150/150,
313/313, 278/278, 319/319, and 211/211; JS54 reports 238/238. JS52's former
unknown-pseudo assertion now confirms that `:first-of-type` matches the
first input in its structural parent.

All seven JS54 production hosted checks pass, including mixed-type divergence,
parser and compound behavior, root/hidden/form semantics, relation and query
integration, selector-list deduplication, and nested Event matching with
metadata preservation. The hosted aggregate reports **579 passed / 7 failed /
586 total**. Its seven failures remain the existing CSS 3C, CSS 3G, CSS 6A,
three CSS 6B, and CSS 6C checks; JS54 adds no hosted failure. `build.bat`
passes.

The kernel wrapper stops at the existing PacMan Native ELF link errors for
`pacman_audio_load_resources(gx_app_context*)` and
`pacman_audio_submit(void*, PacManSoundId)`. The independent direct kernel lane
stops at the existing Mbed TLS configuration errors in
`mbedtls_check_config.h:51` and `:64`. Neither dependency was changed. No fresh
kernel was produced, so QEMU proof is not claimed.

Before the kernel attempts, the generated-output audit snapshotted 315 files
totaling 147,131,477 bytes across `ESP/`, `out/`, the PacMan source/package and
AMD64 objects, `kernel/build/`, and both BootLoader Release folders. The
wrapper changed `game.o`, `main.o`, and `renderer.o`; they were restored from
the snapshot and their lengths and SHA-256 hashes verified. Final comparison
found **zero changed, zero missing, and zero extra files**.

JS54 did not add `nth-*`, `:empty`, `:root`, functional pseudos,
pseudo-elements, multiple pseudos on one simple selector, more relation depth,
or public DOM insertion/removal. JS55 adds only the four bounded nth
structural pseudos documented below.

## JS55: bounded An+B structural pseudo-classes

JS55 adds `:nth-child()`, `:nth-last-child()`, `:nth-of-type()`, and
`:nth-last-of-type()` to the existing simple-selector parser and shared
matcher. Each simple selector still has at most one pseudo, and its position
remains after the optional tag, ID, classes, and attribute predicate. The
overall selector-list input remains capped at 256 bytes. The expression
inside an nth pseudo is capped at 32 source bytes, including whitespace.

The bounded parser accepts signed integer constants, `odd`, `even`, `n`,
signed integer coefficients such as `-2n`, and signed integer offsets such as
`2n + 1`, `n-2`, and `-n+3`. `odd` is `(2, 1)`, `even` is `(2, 0)`, and bare
`n` is `(1, 0)`. Pseudo names and the `odd`, `even`, and `n` tokens use ASCII
case folding. ASCII whitespace is trimmed around the expression and allowed
around the offset sign and its integer; whitespace inside a coefficient or
integer is rejected. Nested parentheses, empty arguments, decimals, exponent
and hexadecimal forms, multiplication, commas, `of <selector>`, trailing
tokens, and malformed signs fail closed. Zero and negative constants are
valid expressions that match no positive structural index.

The normalized coefficients are stored directly in each bounded simple
selector descriptor as signed 16-bit `A` and `B`, each limited to -32767
through 32767. Decimal accumulation is checked before every step. Matching
uses 32-bit integer deltas: for `A == 0`, the index must equal `B`; for
positive `A`, `index - B` must be nonnegative and divisible by `A`; for
negative `A`, `B - index` must be nonnegative and divisible by `-A`. Indexes
are positive and 1-based. No `n` iteration or floating-point math is used.

One shared bounded structural-index helper resolves a valid nonzero parent
and scans at most `min(maxDocumentNodes, structuralElements.size())` Elements.
It checks the candidate's membership, every same-parent child's `childIndex`,
`siblingCount`, and `previousSiblingSerial`, and the parent's `childCount`.
Any missing candidate or inconsistent order metadata fails closed. All-child
forms count every structural Element sibling. Of-type forms count siblings
whose current tags compare equal through the existing case-insensitive
`selectorTagEquals()` helper. The helper calculates reverse positions from the
validated forward counts. Roots and other parentless Elements never match.
Hidden structural Elements count, and form ownership, text, comments, and
layout order do not participate. Class and attribute predicates filter the
candidate after type position has been calculated; they do not redefine it.

The relation and selector-list scanners now recognize the bounded functional
parenthesis span so `+`, whitespace, and commas inside an nth expression do
not get mistaken for selector relations or list separators. The functional
pseudo parser still rejects nested or extra parentheses and preserves the
existing one-relation limit. Parsed `A/B` values live in the descriptor, so
held collections reevaluate the current structural tree and do not retain
caller-source pointers.

The simple descriptor grows from **32 to 36 bytes** for the two explicit
coefficients. The four-member selector descriptor grows from **524 to 556
bytes**. A selector collection record grows from **544 to 576 bytes** and the
fixed 128-record registry grows from **69,632 to 73,728 bytes**, a **4,096
byte** increase. No per-match allocation, selector-side index cache, or
unbounded expression storage was added.

The focused proof is `tests/navigator_javascript_js55_test.cpp`, run by
`scripts/smoke-navigator-javascript-js55.ps1`. It covers An+B parsing and
malformed forms, overflow and length bounds, all four index directions,
compounds, relations, selector lists, live reordering, parent and sibling
metadata corruption, hidden/form-owned Elements, events, nested dispatch,
generation reuse, purity, and near-capacity matching. The focused suite passes
**340/340 checks**, including the strict warning-as-error parser/adapter/runtime
lane. The near-capacity test uses 1,021 siblings, runs 1,000 repeated nth and
of-type matches, and rereads held nth collections 300 times.

The production hosted fixture is `navigator-smoke/javascript-js55.html` and
the seven JS55 hosted checks are part of `server.cpp`'s Navigator smoke
aggregate. They cover forward and reverse indexes, odd/even and An+B,
mixed-tag of-type behavior, JS53/JS54 equivalence, parser and compound
behavior, query/relation/list integration, and nested Event matching with
metadata preservation. All seven JS55 hosted checks pass. The aggregate
reports **586 passed / 7 failed / 593 total**; the seven failures remain CSS
3C, CSS 3G, CSS 6A, CSS 6B's three checks, and CSS 6C. `build.bat` passes.

The full live JavaScript matrix—lexer, parser, runtime, and JS6 through JS55—
passes **53/53 lanes**. JS36–JS43 report 114/114, 180/180, 152/152, 218/218,
155/155, 220/220, 235/235, and 277/277. JS44–JS53 report 183/183, 184/184,
220/220, 137/137, 136/136, 150/150, 313/313, 278/278, 319/319, and 211/211;
JS54 reports 238/238 and JS55 reports 340/340. The JS52 parser regression now
checks rejection of still-unsupported functional pseudos without expecting
`:nth-child()` to be rejected.

The production build succeeds. `build-kernel.bat` reaches the existing
PacMan Native ELF link failures for `pacman_audio_load_resources(gx_app_context*)`
and `pacman_audio_submit(void*, PacManSoundId)`. The independent
`mingw32-make ARCH=amd64 EXTRA_CFLAGS=` kernel lane reaches the existing Mbed
TLS configuration errors at `mbedtls_check_config.h:51` and `:64`. PacMan and
Mbed TLS sources were not modified. Neither attempt produced a fresh kernel,
so QEMU proof is not claimed.

Before the kernel attempts, the generated-output audit snapshotted 381 files
totaling 133,934,645 bytes across `ESP/`, `out/wallpaper-pack/`, the PacMan
package and AMD64 objects, `kernel/build/`, and the BootLoader and Server
Release outputs. The kernel attempts regenerated `game.o`, `main.o`, and
`renderer.o`; those three known outputs were restored from the snapshot and
their lengths and SHA-256 hashes verified. The final audit found **zero
changed, zero missing, and zero extra files**.

JS55 does not add `of <selector>` nth forms, other functional pseudo-classes,
multiple pseudos per simple selector, additional relation depth, or public DOM
insertion/removal APIs.

## JS56: parser-designated `:root`; `:empty` deferred

JS56 adds the zero-argument `:root` pseudo to the existing bounded selector
descriptor and shared matcher. `WebDocument::documentElement` is the parser's
authoritative root reference: handling an `<html>` start tag pushes the
structural Element, assigns its document-local serial, and stores that
reference in `documentElement`. Final structural-metadata processing refreshes
the stored reference from the canonical structural record. In the normal HTML
fixture this Element is `html`, and `document.querySelector(":root")` returns
the same serial and JavaScript host identity as `document.querySelector("html")`.

The matcher requires `hasDocumentElement`, a nonzero `documentElement.serial`,
and equality with the candidate Element serial. It does not treat a missing
parent as root evidence. The parser can retain more than one parentless
structural Element—for example, structural content accepted before a later
`html` start tag—and the JS56 test also exercises an added parentless
root-like record. Only the designated `documentElement` matches. The serial is
owned by the active `WebDocument`; no static or selector-specific root pointer
is cached. An old-generation Element cannot match the replacement document's
root even when parsing reuses the same numeric serial.

`:root` uses the existing terminal one-pseudo slot and keeps the canonical
compound order: optional tag, optional ID, classes, optional attribute, then
optional pseudo. ASCII case-insensitive names, pseudo-only and `*:root`
selectors, compound filters, selector lists, one-relation selectors, query
methods, `matches()`, and `closest()` all use the same selector path. A wrong
tag, ID, class, or attribute still fails. `:root > body` and `:root .panel`
use the ordinary relation matcher; `.panel > :root` parses and yields no match.
Selector-list results retain structural order and deduplicate the root when
multiple members match it. Root is distinct from `:first-child` and
`:only-child`: those continue to require validated parent/sibling metadata.

### Why JavaScript `:empty` remains unsupported

The parser has bounded `HtmlElementContentMetadata`, but it is a render/content
summary rather than authoritative child-node metadata. It records Element
children and non-whitespace text, while `flushText()` collapses and trims
ordinary whitespace before updating the summary. Whitespace-only raw runs can
be placed in bounded inline-layout items on some parser paths, but other paths
such as option and textarea content skip those items; the inline-item list can
also be capped or text-truncated. The summary's render flags describe content
such as a replaced Element's own rendering, which is not the same as that
Element having children. Therefore neither `childElementCount == 0` nor the
existing render summary can prove the CSS child-content rule in every case.

JS56 leaves `:empty` unsupported in the JavaScript selector parser, so
`:empty`, `:EMPTY`, and `:empty()` fail closed in `querySelector()`,
`querySelectorAll()`, and `matches()`. No text cache or scan over rendered
layout state was added. Element-child, text-only, whitespace-only, hidden
content, comment, and void-element `:empty` semantics are deferred until the
document model can provide complete structural text-child authority. This
means JS56 does not claim browser `:empty` compatibility.

The `NavigatorScriptStatePseudo` enum already uses an explicit one-byte
representation, so adding `Root` does not enlarge selector records. The simple
descriptor remains **36 bytes**, the fixed four-member selector descriptor
**556 bytes**, a selector collection record **576 bytes**, and the 128-record
registry **73,728 bytes**: zero net growth from JS55. Root matching compares
the candidate with the current document serial and introduces no per-match
allocation, root cache, text cache, regular expression, map, or dynamic
selector-side structure.

The focused proof is `tests/navigator_javascript_js56_test.cpp`, run by
`scripts/smoke-navigator-javascript-js56.ps1`. It covers canonical `html`
identity, pseudo-only/universal/tag/ID/class/attribute forms, case folding,
fail-closed functional and second-pseudo syntax, root relations and lists,
`querySelector()`/`querySelectorAll()`/`matches()`/`closest()`, multiple
parentless Elements, root-versus-child pseudos, unsupported `:empty`, 1,000
repeated matches and queries, 320 held collection rereads, purity, nested
events, Event metadata, stale generations, and serial reuse. The smoke also
runs the strict warning-as-error parser/adapter/runtime compilation lane.

The hosted JS56 fixture is `navigator-smoke/javascript-js56.html`; its six
aggregate checks cover root identity and compound filters, traversal and
relations, list order and deduplication, fail-closed `:empty`, and nested Event
matching. See the JS56 closeout below for live matrix, aggregate, build,
generated-artifact, and commit results.

### JS56 closeout (2026-10-03)

The JS56 focused suite passed **110/110**, including the strict
warning-as-error parser/adapter/runtime lane. The complete JavaScript matrix
passed **54/54 lanes**: the three base lanes plus JS6 through JS56. Focused
regressions passed with these totals:

| Phase | Checks | Phase | Checks | Phase | Checks |
| --- | ---: | --- | ---: | --- | ---: |
| JS36 | 114/114 | JS43 | 277/277 | JS50 | 313/313 |
| JS37 | 180/180 | JS44 | 183/183 | JS51 | 278/278 |
| JS38 | 152/152 | JS45 | 184/184 | JS52 | 319/319 |
| JS39 | 218/218 | JS46 | 220/220 | JS53 | 211/211 |
| JS40 | 155/155 | JS47 | 137/137 | JS54 | 238/238 |
| JS41 | 220/220 | JS48 | 136/136 | JS55 | 340/340 |
| JS42 | 235/235 | JS49 | 150/150 | JS56 | 110/110 |

All **six** new hosted JS56 checks passed. The hosted aggregate reported
**592 passed / 7 failed / 599 total**. The seven failures are the established
CSS phase 3C, CSS phase 3G, CSS phase 6A, three CSS phase 6B checks, and CSS
phase 6C; no JS56 check failed. The production `build.bat` completed
successfully after the hosted expectation string was corrected.

The kernel wrapper stopped at the existing PacMan Native ELF link errors for
`pacman_audio_load_resources(gx_app_context*)` and
`pacman_audio_submit(void*, PacManSoundId)`. The direct
`mingw32-make ARCH=amd64 EXTRA_CFLAGS=` lane independently stopped at the
existing Mbed TLS configuration errors in `mbedtls_check_config.h:51` and
`:64`. No fresh kernel was produced, `ESP/kernel.elf` is absent, and QEMU is
not installed on PATH; no QEMU proof is claimed.

The pre-kernel artifact snapshot covered **382 files / 133,988,800 bytes**.
The wrapper rebuilt three PacMan object files; those exact files were restored
from the snapshot and their hashes verified. Final artifact delta was **0
changed / 0 missing / 0 extra**. `git diff --check` passed. The source change is
Outcome B: `:root` is complete and `:empty` remains fail-closed pending
authoritative text-child state.

## JS57: authoritative direct text-child presence; `:empty` deferred

JS57 adds one parser-owned fact to the document's existing bounded
`HtmlElementContentMetadata` record:

```text
hasDirectTextChild
```

This is a presence bit only. It does not retain text, create text nodes, or add
one allocation per text run. It describes direct source character data owned
by the represented Element. The parser sets it while consuming each nonempty
character-data byte, using the current top of the open-Element stack. Repeated
text bytes and runs are idempotent. Entity spellings such as `&amp;`, `&#32;`,
and `&#x20;` therefore count before decoding, including entities that decode
to whitespace.

### Parser authority audit

`parseHtml()` is both the HTML token scan and the forgiving parser entry point
in `guide_web_html_parser.cpp`; there is no separate tokenizer object. At a
`<`, it scans a tag through an unquoted `>` and dispatches to
`handleOpenTag()` or `handleCloseTag()`. Represented start tags are committed
by `registerStructuralElement()` and pushed onto `ParserState::openElements`.
The record's `parentSerial` and the parent's structural child counter are the
Element-child authority. Finalization copies those counters to
`HtmlElementRef::childCount`.

Before JS57, text stayed in `ParserState::textBuf` until `flushText()`. That
function decodes entities and, for ordinary text, applies
`trim(collapseWs(...))`. Only a nonempty normalized result updated the old
render summary (`hasNonWhitespaceText` and bounded visible text bytes).
Whitespace-only content could be discarded there, and render output could be
omitted by special-element paths or bounded inline/layout storage. The new
presence bit is set in the source-character loop before `flushText()` can
normalize, trim, truncate, or discard that data.

The owner is exactly `openElements.back()` at the character byte. The bit is
not propagated to ancestors. Thus text under a nested `span` marks the span,
while literal whitespace between tags marks the parent when the parser stack
places that whitespace directly under it. If the represented stack has
overflowed, the helper does not guess an owner; existing metadata is already
marked incomplete for that bounded recovery path.

Full `<!-- ... -->` comments are skipped before generic tag scanning, so a `>`
inside a comment cannot leak the remainder into the text path. Unterminated
comments consume the rest of input. Declarations beginning with `<!` remain
ignored and do not create Element or text facts.

Textarea and option are represented Elements, so their source text is captured
in the same direct path while their existing default-value and option-label
projections continue through `flushText()`. Script bodies use the bounded
`scriptBuf`; style bodies use `textBuf` for CSS parsing. Script and style are
not represented structural Elements in this Navigator model, so JS57 skips
those special bodies instead of incorrectly assigning them to the containing
body. `head`, `title`, and other metadata Elements likewise have no structural
serial. This is the remaining raw-text representation limitation for any
future API that may expose those Elements.

### Element children, capacity, and memory

JS57 adds no Element-child field. Direct Element children continue to come
from the structural `parentSerial` relationship and finalized `childCount`;
the existing content summary's `hasElementChild` and `elementChildCount` are
kept in sync. The structural-cap check now happens before child counters or
the parent's Element-child summary are updated, so a child rejected at the
1,024-record limit cannot leave a false child fact. Accepted insertion and
finalized child counts remain aligned.

The new flag lives in the existing per-document vector, with one content
record for each represented structural Element and the existing 1,024-record
maximum. `HtmlElementRef` remains **440 bytes**. On the validated build,
`HtmlElementContentMetadata` remains **24 bytes** before and after JS57 because
the flag uses existing record padding. The maximum content-record storage
remains **24,576 bytes per document**; the maximum increase is **0 bytes**.
There is no per-run allocation or text buffer. Selector records also remain
unchanged: **36-byte** simple selector, **556-byte** four-member descriptor,
**576-byte** collection record, and **73,728-byte** 128-record registry.

Every `parseHtml()` call starts with a fresh `ParserState` and empty metadata
vectors. The flag is initialized false with its Element's new record and is
not changed by style, class, hidden state, layout, selector matching, focus,
or events. The future consumer contract for represented Elements is:

```text
no direct child content = no structural Element child
                        AND no direct text child
```

The following JS58 matcher requires complete metadata so structural-cap or
stack-overflow cases fail closed. At the JS57 phase boundary, JavaScript
`:empty` was still unsupported in `querySelector()`, `querySelectorAll()`, and
`matches()`; the JS58 section below records its implementation. Existing CSS
style matching keeps its prior render-summary behavior. No public JavaScript
API was added.

The native proof is `tests/navigator_javascript_js57_test.cpp`, run by
`scripts/smoke-navigator-javascript-js57.ps1`. It directly inspects the
document-owned metadata for empty, text-only, whitespace-only, entity,
Element-only, mixed, nested, hidden, comment, textarea, option, void, malformed,
long-text, capacity, and document-replacement fixtures. It also executes the
hosted regression fixture's scripts and checks existing `:root`, structural
traversal, form, Event, and the post-JS58 `:empty` compatibility behavior. The
hosted page is `navigator-smoke/javascript-js57.html`; it adds no metadata
debug property.

### JS57 closeout (2026-10-03)

The focused JS57 suite passed **269/269** checks, including the strict
warning-as-error parser/adapter/runtime lane. Record sizes were
`HtmlElementRef=440` bytes and
`HtmlElementContentMetadata=24` bytes; the bounded metadata maximum remained
**1,024 records / 24,576 bytes**, with zero additional per-document bytes.

The full JavaScript regression matrix passed **55/55 lanes**: the three base
lanes plus JS6 through JS57. Focused JS36–JS56 regressions also passed with the
counts recorded in the preceding JS56 closeout table; JS57 passed 269/269.
All **five** new hosted JS57 checks passed. The hosted aggregate reported
**597 passed / 7 failed / 604 total**. Its seven failures are the established
CSS phase 3C, CSS phase 3G, CSS phase 6A, three CSS phase 6B checks, and CSS
phase 6C; there are no new JS57 failures. The final production `build.bat`
completed successfully after the parser's O(1) metadata lookup path was in
place. The strict warning-as-error lane passed.

The kernel wrapper again stopped at the existing PacMan Native ELF link errors
for `pacman_audio_load_resources(gx_app_context*)` and
`pacman_audio_submit(void*, PacManSoundId)`. The direct
`mingw32-make ARCH=amd64 EXTRA_CFLAGS=` lane independently stopped at the
existing Mbed TLS configuration errors in `mbedtls_check_config.h:51` and
`:64`. The wrapper changed three PacMan object files; those exact files were
restored from a pre-kernel snapshot and verified. The snapshot covered **729
files / 166,655,465 bytes**; final artifact delta was **0 changed / 0 missing /
0 extra**. QEMU is not on PATH and `ESP/kernel.elf` is absent, so no QEMU proof
is claimed.

JS57 is **Outcome B (useful partial)**: ordinary represented Elements now
have authoritative direct text presence, including whitespace and entities,
before presentation normalization. The remaining limitation is that script
and style are not represented structural Elements, so their special body
buffers do not receive Element metadata. JS58 can implement `:empty` for
represented Elements using structural Element children plus direct text
presence and must continue to fail closed when metadata is incomplete. Full
raw-text Element coverage requires a future representation change. `:empty`
remains unsupported in JS57. `git diff --check` passed before the local source
commit; generated artifacts are excluded from that commit.

## JS58: authoritative `:empty` for represented Elements

JS58 enables the nonfunctional `:empty` pseudo in the JavaScript selector
parser and shared simple-selector matcher. This section supersedes the JS56 and
JS57 phase-boundary notes above, which record that those earlier milestones
left JavaScript `:empty` unsupported.

The match rule is:

```text
current represented Element
AND matching JS57 content record exists and is complete
AND HtmlElementRef::childCount == 0
AND structural parent-serial traversal finds no direct Element child
AND HtmlElementContentMetadata::hasDirectTextChild == false
```

`HtmlElementRef::childCount` is finalized from the existing structural
parent/child counters, and the matcher cross-checks it against the bounded
structural parent-serial traversal. It also validates the parallel
content-record vector, the record's serial, and its completeness bit. Missing,
mismatched, or incomplete metadata fails closed. The record's
`hasElementChild` summary is not used as a substitute for the structural
Element-child model. In particular, a text-only Element has
`childElementCount === 0` and still fails `:empty` because JS57 recorded its
direct text.

The matcher reads current document metadata for every call and stores no
selector-specific emptiness state. It does not inspect rendered text, layout,
visibility, raw HTML source, or the CSS engine's render-summary interpretation.
Space, tab, CR, LF, indentation, and entity source bytes all count as direct
text, including `&#32;` after it decodes to whitespace. Long text remains
nonempty when its presentation summary is truncated. Hidden text and hidden
Element children also disqualify emptiness. Comment-only Elements match when
there is no other child content; comments and declarations do not set the
direct-text bit. Descendant text is not copied to an ancestor: the ancestor
fails because it has a structural Element child, while the text-owning child
fails because it has direct text.

Textarea and option source text counts even after their runtime form value or
selection changes. An input value attribute or runtime value does not create
text content. Checked, selected, disabled, focus, attributes, and style state
do not affect the result. Represented void Elements such as input, img, and
hr can match when they have no represented child or direct text; attributes
such as `src` do not create children. `br` is not a represented structural
Element in the current model. Script and style are also not represented as
Elements, and their special raw-text buffers are not scanned or exposed to
invent `:empty` matches. This limitation is intentional and remains until a
separate document-model change represents those tags.

The parser adds `Empty` to the existing one-byte pseudo enum. The descriptor
layout does not grow. `:empty`, `:EMPTY`, and mixed ASCII case spellings are
accepted as nonfunctional pseudos; `:empty()` and unknown names fail closed.
The existing canonical order remains optional tag, ID, classes, one attribute
predicate, and one trailing pseudo. The one-pseudo and one-relation bounds are
unchanged, as are the 256-byte selector and four-member selector-list limits.

The same matcher serves `querySelector()`, live-on-read `querySelectorAll()`,
`matches()`, `closest()`, scoped queries, the existing one-relation grammar,
and selector lists. Results keep structural document order, selector-list
deduplication, and canonical generation/serial Element identity. Empty status
is independent of `:root`, child-position pseudos, and nth-position pseudos.
Stale Element `matches()` returns false and stale `closest()` returns null;
old-generation collection handles retain the existing stale-host validation.

Focused evidence is in `tests/navigator_javascript_js58_test.cpp` and
`scripts/smoke-navigator-javascript-js58.ps1`. The tests cover parser grammar,
all source-content categories, completeness fail-closed behavior, live
collection reevaluation under test-only structural/text metadata changes,
forms, void and unrepresented raw-text Elements, selector APIs, nested Event
dispatch, stale Element handles, long-text truncation, and the 1,024-Element
capacity boundary. Those test-only metadata changes do not add public DOM
mutation APIs. The hosted fixture is
`navigator-smoke/javascript-js58.html`; the production hosted aggregate also
checks basic content semantics, `childElementCount` divergence, relations,
selector lists, positional independence, forms, and Event metadata.

The focused JS58 test reports **153/153** checks. The descriptor sizes remain
36 bytes for a simple selector, 556 bytes for the four-member descriptor, 576
bytes per collection record, and 73,728 bytes for the 128-record collection
registry. `HtmlElementRef` remains 440 bytes; `HtmlElementContentMetadata`
remains 24 bytes, or 24,576 bytes for 1,024 records. JS58 adds zero descriptor,
content-record, or per-document metadata bytes. No selector cache or additional
dynamic storage was introduced.

### JS58 closeout (2026-10-03)

JS58 is **Outcome A: complete for represented Elements**. The focused suite
passed **153/153** checks, including the strict warning-as-error
parser/adapter/runtime lane. The full JavaScript matrix passed **56/56 lanes**
(the three base lanes plus JS6 through JS58). Focused JS36–JS57 regressions
passed **4,639 checks**; JS58 adds 153 focused checks. Stress coverage includes
1,000 repeated `matches(":empty")` calls, 1,000 repeated
`querySelector(":empty")` calls, 320 held live-collection rereads, and a
near-capacity mixed document of 961 represented Elements with 480 expected
matches.

All **five** hosted JS58 checks passed, including true-empty and comment-only
matches, text/whitespace/entity/hidden/structural-child rejection, the
`childElementCount === 0` text-only divergence, query/relation/list behavior,
form and positional independence, and nested Event metadata preservation. The
hosted aggregate reported **602 passed / 7 failed / 609 total**. Its seven
failures remain the established CSS phase 3C, CSS phase 3G, CSS phase 6A, three
CSS phase 6B checks, and CSS phase 6C; there were no new JS58 failures.
`build.bat` and the strict lane passed.

`build-kernel.bat` stopped before bootloader/kernel staging at the existing
PacMan Native ELF link errors for
`pacman_audio_load_resources(gx_app_context*)` and
`pacman_audio_submit(void*, PacManSoundId)`. The independent direct
`mingw32-make ARCH=amd64 EXTRA_CFLAGS=` run stopped at the existing Mbed TLS
configuration errors in `mbedtls_check_config.h:51` and `:64`. Neither blocker
was changed. No fresh `ESP/kernel.elf` was produced and `qemu-system-x86_64`
was unavailable, so QEMU proof is not claimed.

The generated-artifact snapshot covered **328 files / 147,249,718 bytes**
across ESP, wallpaper/package outputs, PacMan build objects, kernel outputs,
and bootloader outputs. The kernel attempts changed **3 / 0 / 0** files
(changed / missing / extra): three PacMan object files, all restored from the
snapshot. The final hash/length audit verified **0 changed / 0 missing / 0
extra** files against the snapshot. Generated outputs are excluded from the
source change. `git diff --check` passed before commit.

Script and style remain outside the selector domain because the structural
model does not represent them as Elements; no raw-source or layout fallback
was added. The next bounded milestone is **JS59: one-level `:not()` over a single supported simple selector**.

## JS59: bounded `:not()` pseudo-class

JS59 implements the nonfunctional selector pseudo `:not(argument)` for one
supported simple selector argument. The argument is parsed when the complete
selector is parsed and retained inline in the fixed-size selector descriptor.
Matching does not reparse selector text. The argument may contain the existing
optional tag or universal selector, ID, bounded class tokens, one attribute
predicate, or one supported nonfunctional state pseudo. Existing outer
compounds, one-relation selectors, and selector lists can use `:not()` within
their current bounds.

The argument cannot contain a selector list, a relation or combinator, another
`:not()`, a functional nth pseudo, an empty argument, or an unsupported
functional pseudo. The existing single trailing outer-pseudo rule remains in
force. A selector such as `button:disabled:not(.primary)` therefore remains
outside the grammar; callers can express one inner pseudo with other simple
conditions inside the negation, for example `:not(button.primary)`.
ASCII pseudo-name case handling and whitespace around the argument follow the
existing parser rules. The complete selector remains limited to 256 bytes.

The parser stores one `NavigatorScriptSimpleSelectorCoreDescriptor` beside
each complete simple-selector descriptor. The core has no nth parameters and
cannot contain another negation descriptor, so functional depth is fixed at
one. Parsing re-enters the simple-core helper once for the argument; the
`allowNot=false` guard rejects nested `:not()` and functional nth forms before
another helper re-entry. Matching does not recurse. Matching returns
`Invalid`, `NoMatch`, or `Match`; only a valid inner `NoMatch` is inverted to
`Match`. Invalid or stale Elements, incomplete `:empty` content metadata,
incoherent structural position metadata, invalid focus generations, and
missing root authority do not become matches through negation. `:not()` shares
the ordinary matcher for attributes, `:root`, `:empty`, form state, focus, and
structural position.

The same matcher serves `querySelector()`, live-on-read
`querySelectorAll()`, `matches()`, `closest()`, scoped queries, relations,
selector lists, and Event callbacks. Existing document order, list
deduplication, canonical generation/serial identity, and stale-wrapper
validation remain in force. Lists are outer syntax only; they are rejected
inside the negation argument.

The size accounting is **36 → 68 bytes** for one simple selector, **556 →
812 bytes** for the four-member selector descriptor, **576 → 832 bytes** per
collection record, and **73,728 → 106,496 bytes** for the 128-record registry.
The fixed core is 32 bytes; the registry grows by **32,768 bytes**. These
sizes are bounded by the existing four members and their eight embedded
simple-selector slots. `HtmlElementRef` remains 440 bytes and each JS57/JS58
content record remains 24 bytes. No per-document record or cache was added.

Focused implementation and parser/runtime coverage live in
`tests/navigator_javascript_js59_test.cpp` and
`scripts/smoke-navigator-javascript-js59.ps1`. The hosted page is
`navigator-smoke/javascript-js59.html`, and the production hosted aggregate
contains dedicated JS59 checks. The former JS52 assertion that `:not()` was
unsupported was narrowed to still-unsupported functional forms, so the full
historical matrix continues to verify the new shared parser behavior.

### JS59 closeout (2026-10-03)

JS59 is **Outcome A: complete for the represented selector and Element model**.
The focused suite passed **621/621** checks, including the warning-as-error
bare-metal compile and strict parser/adapter/runtime syntax lane. The parser
allows one bounded helper re-entry for the argument; nested functional input
is rejected before another re-entry, functional depth is **1**, matching does
not recurse, and match-time reparsing is **none**. The malformed sweep ran 40
repeats across its bounded invalid-input set. Stress passed 1,000 repeated
matches for class, `:empty`, and `:focus` negation, plus 320 held live-collection
rereads.

The full JavaScript matrix passed **57/57 lanes**: the three base lanes plus
JS6 through JS59. Focused JS36–JS58 regressions passed **4,792 checks**; JS59
adds 621, for **5,413 combined checks**. JS52's former `:not()` rejection
assertion now continues to cover unsupported functional forms.

All **four** new hosted JS59 checks passed. They cover basic and compound
negation, tag/attribute/state/`:empty`/`:root` pseudos, live attribute
collections, relations, selector-list order and deduplication, malformed
grammar, and nested Event dispatch. The hosted aggregate reported **606
passed / 7 failed / 613 total**. The seven failures are the existing CSS phase
3C, CSS phase 3G, CSS phase 6A, three CSS phase 6B checks, and CSS phase 6C;
there were no new JS59 failures. `build.bat` passed.

The kernel wrapper stopped at the existing PacMan Native ELF link errors for
`pacman_audio_load_resources(gx_app_context*)` and
`pacman_audio_submit(void*, PacManSoundId)`. The independent direct
`mingw32-make ARCH=amd64 EXTRA_CFLAGS=` lane stopped at the existing Mbed TLS
configuration errors in `mbedtls_check_config.h:51` and `:64`. Neither blocker
was changed. The before/after artifact audit covered **409 files / 132,423,955
bytes**; three PacMan objects were restored from the snapshot. Final hashes and
lengths verified **0 changed / 0 missing / 0 extra** files. No fresh
`ESP/kernel.elf` exists and `qemu-system-x86_64` is unavailable, so QEMU proof
is not claimed. `git diff --check` passed before commit; generated outputs are
excluded from the source commit.

## JS60: bounded `:is()` pseudo-class

JS60 adds positive `:is(argument)` matching for one bounded non-relational
simple selector. For a valid current Element `E`, `E.matches(":is(S)")` has
the same result as `E.matches("S")`; `:is(S)` and `:not(S)` are complements
for valid candidates. Stale Elements and invalid serials fail closed before
the inner selector is evaluated, so stale candidates do not satisfy either
side of the complement.

The parser reuses JS59's `NavigatorScriptSimpleSelectorCoreDescriptor` and
shared restricted-core parser. The outer descriptor stores one
`logicalSelector` core and marks the outer pseudo as `Not` or `Is`. The core
contains no nested logical pseudo or nth-expression state. The functional
argument scanner handles quoted attribute values and returns the first
unquoted closing parenthesis. Parsing trims the same ASCII whitespace as
`:not()`, parses the argument once, and retains its fields in the selector's
existing 256-byte text buffer. Matching uses the same tri-state core matcher:
`:is()` returns a valid inner result directly, while `:not()` inverts only a
valid result. Invalid or stale inner evaluation is never inverted. Matching
does not reparse source text.

The supported argument grammar is one optional tag or universal selector,
one optional ID, zero or more class tokens, one optional attribute-presence
or equality predicate, and one supported nonfunctional pseudo. The pseudo
subset is `:checked`, `:disabled`, `:focus`, `:first-child`, `:last-child`,
`:only-child`, `:first-of-type`, `:last-of-type`, `:only-of-type`, `:root`,
and `:empty`. Pseudo names are ASCII case-insensitive. This includes
`:is(:root)` and `:is(:empty)` and reads current form, focus, attribute, and
content metadata each time the selector is matched. Held collections remain
live-on-read. The existing eight-class-token cap, 64-byte attribute-name cap,
128-byte attribute-value cap, and 256-byte total selector-source cap remain
shared with `:not()`; the argument consumes the same source and descriptor
buffer.

JS60 deliberately rejects empty arguments, inner selector lists, inner
relations or descendant whitespace, nested `:is()` or `:not()`, and nth
functional pseudos inside `:is()`. Outer relations, outer selector lists,
scoped queries, `querySelector()`, `querySelectorAll()`, `matches()`, and
`closest()` use the existing matcher and preserve document order, deduplication,
and canonical Element identity. The outer simple selector still has one
pseudo slot, so a second outer pseudo is rejected. This is a bounded subset,
not full Selectors Level 4 `:is()` behavior.

Functional nesting depth remains **1**. The parser makes one bounded helper
re-entry to parse the argument, and its `allowLogicalPseudo=false` guard
rejects nested logical pseudos before another re-entry. Matching is
nonrecursive. The shared core remains 32 bytes; a complete simple selector is
68 bytes, the four-member selector descriptor is 812 bytes, and each
collection record is 832 bytes. These sizes are unchanged from JS59, so JS60
adds **0 bytes** per descriptor, collection, or registry. The 128-record
registry remains 106,496 bytes (32,768 bytes above the JS58 baseline because
of JS59's original inner core). `HtmlElementRef` remains 440 bytes and content
metadata remains 24 bytes; JS60 adds no document storage.

The focused JS60 suite passed **1,377/1,377 checks**, including malformed
input repeats, 1,000 repeated `matches(":is(.x)")`,
`matches(":is(:empty)")`, and `matches(":is(:focus)")` reads, 320 held
collection rereads, 256-byte boundary cases, valid-candidate complement
checks, stale generation and serial-reuse checks, nested Event dispatch, and
the warning-as-error strict parser/adapter/runtime lane. JS52 passed
319/319 and JS59 passed 621/621 during JS60 validation.

### JS60 closeout (2026-10-03)

The duplicate/stale gate found no authoritative phase marker and no existing
JS60 implementation or artifacts. The starting branch was
`NAVIGATOR_JAVASCRIPT_SUPPORT` at
`ffed6ecf80ea9929ed922e96fc8d0624117090f5` (`navigator: add JS59 bounded not
pseudo-class`), with a clean worktree and `1 ahead / 0 behind`
`origin/NAVIGATOR_JAVASCRIPT_SUPPORT`. The ending commit and divergence are
reported in the JS60 source commit.

The full JavaScript matrix passed **58/58 lanes**, the three base
lexer/parser/runtime lanes plus JS6 through JS60. JS36–JS59 passed **5,413
checks** across their focused suites: JS36 114, JS37 180, JS38 152, JS39 218,
JS40 155, JS41 220, JS42 235, JS43 277, JS44 183, JS45 184, JS46 220, JS47
137, JS48 136, JS49 150, JS50 313, JS51 278, JS52 319, JS53 211, JS54 238,
JS55 340, JS56 110, JS57 269, JS58 153, and JS59 621. JS60 passed
**1,377/1,377**. `git diff --check` passed.

All **four** hosted JS60 checks passed. They cover fixture evaluation,
positive semantics and JS59 complement, live collections and attribute
mutation, scoped queries, relations, outer selector lists, malformed grammar,
and Event matching with nested dispatch and metadata preservation. The hosted
aggregate reported **610 passed / 7 failed / 617 total**. The seven failures
are the existing CSS phase 3C, CSS phase 3G, CSS phase 6A, three CSS phase 6B
checks, and CSS phase 6C; no new JS60 hosted check failed. `build.bat` produced
the server executable used by the hosted run.

`build-kernel.bat` stopped at the existing PacMan Native ELF link errors for
`pacman_audio_load_resources(gx_app_context*)` and
`pacman_audio_submit(void*, PacManSoundId)`. The independent
`mingw32-make ARCH=amd64 EXTRA_CFLAGS=` lane from `kernel/` stopped at the
existing Mbed TLS errors in `mbedtls_check_config.h:51` and `:64`. Neither
blocker was changed. The generated-artifact snapshot covered **712 files /
157,343,180 bytes**. The kernel attempts changed three PacMan objects; all
three were restored from the clean starting revision. Hash and length
verification ended at **0 changed / 0 missing / 0 extra**. No fresh
`ESP/kernel.elf` exists and `qemu-system-x86_64` is unavailable, so QEMU proof
is not claimed.

The recommended next phase at JS60 was bounded `:where()` over the same
one-core grammar, adding a new logical pseudo kind without growing the
descriptor or introducing nested selector trees. Inner lists and nested
logical pseudos were deferred until a separate descriptor and memory review.
JS60 was not pushed.

## JS61: bounded `:where()` pseudo-class

JS61 adds matching for `:where(argument)` using the same fixed-capacity
logical-selector core introduced by JS59 and reused by JS60. The existing
`NavigatorScriptSimpleSelectorCoreDescriptor` remains the single 32-byte inner
representation. The outer pseudo kind is `Not`, `Is`, or `Where`; `:where()`
does not have a separate descriptor, parser, or matcher. The parser retains
the argument fields at selector-parse time, and matching reads those fields
and current document state without reparsing selector text.

For every valid, current Element `E` and supported inner selector `S`,
`E.matches(":where(S)")` equals both `E.matches(":is(S)")` and
`E.matches("S")`. The shared tri-state matcher checks candidate validity
before evaluating the inner core. `:is()` and `:where()` return a valid inner
match result; `:not()` inverts only a valid result. Stale Elements and invalid
serials therefore return false for all three forms. The valid-candidate
complement identity does not apply to stale or invalid Elements.

The one supported inner selector is a non-relational simple selector with an
optional tag or universal selector, optional ID, zero to eight class tokens,
one optional attribute-presence or equality predicate, and one optional
supported nonfunctional pseudo. The supported pseudo set is `:checked`,
`:disabled`, `:focus`, `:first-child`, `:last-child`, `:only-child`,
`:first-of-type`, `:last-of-type`, `:only-of-type`, `:root`, and `:empty`.
Pseudo names are ASCII case-insensitive. Attribute names remain capped at 64
bytes, attribute values at 128 bytes, and the complete selector including the
argument at 256 bytes. Attribute, form, focus, structural, and content state
is read at match time; held collections remain live-on-read.

Only one inner selector is accepted. Inner comma lists, combinators, empty or
whitespace-only arguments, nested `:where()`, `:is()`, or `:not()`, nested
functional nth pseudos, malformed parentheses, and a second outer pseudo are
rejected. Outer selector lists, the existing one-relation forms, outer
compounds, scoped queries, `querySelector()`, `querySelectorAll()`, `matches()`,
`closest()`, and Event callbacks use the existing selector matcher and retain
their bounds, ordering, deduplication, and canonical Element identity. The
functional argument parser makes one bounded helper re-entry; nested
functional syntax is rejected before another re-entry. Maximum functional
nesting depth remains **1**, and matching is nonrecursive.

`:where()` matching is implemented. CSS specificity behavior is not yet modeled
by Navigator and is therefore deferred. No specificity values, selector
specificity storage, cascade weighting, or CSS cascade behavior are modeled by
this phase. This is bounded matching support, not full CSS Selectors Level 4
`:where()` support.

The core remains **32 bytes**, a complete simple selector **68 bytes**, the
four-member descriptor **812 bytes**, each collection record **832 bytes**,
and the 128-record collection registry **106,496 bytes**. JS61 adds **0 bytes**
to the shared core, descriptor, collection record, or registry. `HtmlElementRef`
remains 440 bytes and content metadata remains 24 bytes. No per-document cache
or heap allocation was added.

The focused JS61 test and hosted fixture are
`tests/navigator_javascript_js61_test.cpp`,
`scripts/smoke-navigator-javascript-js61.ps1`, and
`navigator-smoke/javascript-js61.html`. The JS52 historical rejection check
now covers only still-unsupported `:checked()` function syntax. JS61's focused
suite verifies `:where(S) == :is(S) == S` for supported selectors, malformed
input, live attribute and state reads, stale generations and serial reuse,
outer relations and lists, scoped queries, Event matching, purity, stress, and
the fixed memory bounds.

The documented kernel blockers remain independent of JS61. The production
kernel wrapper fails at existing PacMan Native ELF symbols
`pacman_audio_load_resources(gx_app_context*)` and
`pacman_audio_submit(void*, PacManSoundId)`. The direct kernel lane fails at
existing Mbed TLS configuration checks in `mbedtls_check_config.h:51` and
`:64`. Neither subsystem is changed for JS61. A QEMU result is claimed only if
a fresh kernel is produced and QEMU is available.

### JS61 closeout (2026-10-04)

JS61 is **Outcome A: complete for the represented selector and Element
model**. The duplicate/stale gate found no authoritative phase marker. The
documentation identified JS60 as complete and JS61 as the recommended next
phase; no existing `:where()` implementation was present. The starting branch
was `NAVIGATOR_JAVASCRIPT_SUPPORT` at
`c7a6f44ffb5a22452f63a07870038d8b013c6359` (`navigator: add JS60 bounded is
pseudo-class`), with a clean worktree and **0 ahead / 0 behind**
`origin/NAVIGATOR_JAVASCRIPT_SUPPORT`.

The focused JS61 suite passed **2,077/2,077 checks**. It verified positive
matching and equivalence with both standalone selectors and `:is()`, the
valid-candidate complement with `:not()`, the full supported pseudo subset,
live attribute/form/focus/content reads, stale and invalid serial behavior,
serial reuse, selector composition, event matching, malformed-input repeats,
stress, and unchanged descriptor bounds. Functional depth is **1**, parsing
uses one bounded helper re-entry, matching is nonrecursive, and match-time
reparsing is **none**. The shared core is 32 bytes, the simple selector 68
bytes, the four-member descriptor 812 bytes, each collection record 832 bytes,
and the 128-record registry 106,496 bytes. JS61 adds **0 bytes**; Element and
content records remain 440 and 24 bytes.

The full JavaScript matrix passed **59/59 lanes**, including the three base
lexer/parser/runtime lanes and JS6 through JS61. JS36–JS60 passed all **25
focused regression lanes** (6,790 checks using the documented JS36–JS59 and
JS60 counts). JS52 passed 319/319, JS58 153/153, JS59 621/621, JS60 1,377/1,377,
and JS61 2,077/2,077. The JS61 warning-as-error strict parser/adapter/runtime
lane passed.

All **four JS61 hosted checks passed**, including the direct valid-candidate
`:where()`/`:not()` complement proof. The hosted aggregate reported **614
passed / 7 failed / 621 total**. The failures remain the existing CSS phase
3C, CSS phase 3G, CSS phase 6A, three CSS phase 6B checks, and CSS phase 6C;
there were no JS61 hosted failures. The production `build.bat` passed.

`build-kernel.bat` stopped at the existing undefined PacMan symbols
`pacman_audio_load_resources(gx_app_context*)` and
`pacman_audio_submit(void*, PacManSoundId)`. The independent direct
`mingw32-make ARCH=amd64 EXTRA_CFLAGS=` lane stopped at the existing Mbed TLS
configuration errors in `mbedtls_check_config.h:51` and `:64`. No PacMan or
Mbed TLS source was changed. The generated-output snapshot covered **906
files / 206,759,785 bytes**. The three changed PacMan objects
`game.o`, `main.o`, and `renderer.o` were restored; final SHA-256 and length
comparison reported **0 changed / 0 missing / 0 extra**. No fresh
`ESP/kernel.elf` was produced and `qemu-system-x86_64` is unavailable, so QEMU
proof is not claimed.

`git diff --check` passed before commit. The source commit is local and was not
pushed; generated artifacts are excluded. The recommended JS62 direction is
to keep selector grammar bounded and separately review any proposal for inner
selector lists or a real specificity/cascade model. JS61 makes no specificity
or cascade claim.

### JS62 relative selector evaluation foundation (2026-10-04)

JS62 adds an internal `NavigatorScriptRelativeSelectorRelation` and a private
`selectorRelativeElementMatchResult()` helper. It accepts a current-generation
Element handle, one relation, a pre-parsed `NavigatorScriptSimpleSelectorCoreDescriptor`,
and existing selector storage. It delegates target matching to
`selectorCoreElementMatchResult()`; traversal does not parse selector text,
allocate, cache results, or create a second tree. The helper is not connected
to the JavaScript parser, Element methods, or selector query API. `:has()` and
relative arguments such as `>`, `+`, and `~` remain unsupported, and
specificity/cascade behavior remains unmodeled.

The relation rules are:

* `Descendant` examines every represented structural Element except the anchor
  and follows each candidate's `parentSerial` chain until it reaches the
  anchor, a parentless root, or the document node bound. The anchor is strictly
  excluded as a target.
* `Child` scans the structural vector and accepts only Elements whose direct
  `parentSerial` equals the anchor serial.
* `AdjacentSibling` and `GeneralSibling` scan forward from the anchor's vector
  position, accepting only Elements with the same nonzero structural parent.
  The adjacent relation stops at the first following same-parent Element;
  general sibling continues through those following same-parent Elements.
  Earlier Elements, descendants, and Elements under other parents cannot
  satisfy either relation.

The existing `WebDocument::structuralElements` vector, serials, and
`parentSerial` remain authoritative. Text, comments, layout, and form-owner
metadata are not consulted. Hidden Elements remain eligible when present in
that vector. A parentless anchor can have children; it has no sibling relation
without a nonzero shared parent. Root and leaf behavior follows directly from
these rules. Anchor handles with an old generation, wrong host kind, missing
serial, or over-capacity document fail closed. Anchor ancestry is checked to a
parentless root before traversal. Missing parents, self-parent links, and
cycles are bounded by the structural node limit and return `Invalid`.

Traversal loops cap at `min(limits.maxDocumentNodes,
structuralElements.size())`, and the document is rejected if it exceeds either
the configured or global 1024-node cap. Descendant relation examines at most
1024 candidates and at most 1024 parent hops for each candidate. Each parent
resolution uses the existing bounded serial lookup over at most 1024 records;
therefore its conservative worst-case structural lookup bound is O(N^3), with
the ancestry walk alone making at most two 1024-record lookups per candidate
hop (2,147,483,648 record comparisons for the 1024-node cap), before
anchor/candidate validation and shared-pseudo matcher work.
Child and sibling relation scans examine at most 1024 vector
entries; their serial validation and shared matcher retain their own existing
bounds. The helper adds no persistent bytes to selector descriptors,
collections, document records, or registries, and adds no fixed scratch array.
This deliberately records the current conservative cost for future review
before any `:has()` syntax is considered.

The focused JS62 test is `tests/navigator_javascript_js62_test.cpp`, run by
`scripts/smoke-navigator-javascript-js62.ps1`. It proves descendant versus
child, forward adjacent and general sibling matches, stale-generation
rejection, and unsupported public `:has()`. The JS61 regression remains
2,077/2,077. Broader structural corruption, mutation/state pseudo cross-checks,
near-capacity stress, all older JS focused lanes, hosted checks, the production
build, and kernel/QEMU lanes are not claimed by this focused JS62 run.

JS63 may map a carefully bounded single-relative-selector `:has()` grammar to
this primitive only after reducing or explicitly accepting the descendant
lookup bound and adding parser-facing malformed-input, mutation, corruption,
and hosted coverage. No such public syntax is part of JS62.

### JS62R relative selector hardening (2026-10-04)

JS62R audits the internal evaluator before any public `:has()` work. The
original Descendant loop examined up to N structural candidates. For each
candidate it followed up to N parent links. Each parent link called
`resolveStructuralParentSerial()`, which performed a linear `findElement()`
scan for the current serial and another linear scan to validate its parent.
The concrete ancestry path was therefore candidate scan × parent hops × two
serial scans: at N=1024, up to 2,147,483,648 structural record comparisons,
before anchor, target, and selector matcher work.

The hardening change uses the parser's existing structural authority: accepted
structural Elements receive serials starting at one in insertion order and
are appended once to `structuralElements`. Each relative evaluation validates
that every represented record still has `serial == vector index + 1`; malformed
or conflicting serial metadata fails closed. It then resolves structural
parents using `serial - 1` direct indexing. No index is retained between
calls, no document structure or descriptor grows, and no heap or stack
scratch storage is added. Anchor ancestry and each candidate ancestry remain
bounded to N links, cycles and invalid parent serials return Invalid, and
Descendant still excludes its anchor.

The relation traversal bounds are now: Descendant O(N²) ancestry work across
at most N candidates; Child O(N) candidate checks; AdjacentSibling and
GeneralSibling O(N) forward vector scans. The shared selector matcher retains
its own bounded lookup work; in particular an attribute predicate may scan
the structural vector for each eligible candidate. Thus one relative call is
bounded by O(N²), while an outer selector evaluating N anchors can still do
O(N³) total work. At the 1024-record cap that multi-anchor upper bound has not
been demonstrated acceptable. JS63 remains blocked pending test-only work
counters and a measured outer-candidate simulation, broader mutation and
corruption coverage, and full regression qualification.

The new focused `tests/navigator_javascript_js62r_test.cpp`, run by
`scripts/smoke-navigator-javascript-js62r.ps1`, reaches exactly 1024 structural
records, checks a matching final descendant, rejects self-parent and
two-node-cycle corruption, and confirms all four public `:has()` forms remain
unsupported. Its current result is 10/10, including the strict
warning-as-error compilation. The original JS62 test remains 15/15.

The focused JS36–JS62 regression set passed, including JS61 at 2,077/2,077,
JS62 at 15/15, and JS62R at 10/10. The complete historical JavaScript matrix
passed 60/60 lanes (lexer, parser, runtime, and JS6–JS62). The production
`build.bat` passed. The hosted aggregate reported 614 passed and the same
seven known CSS failures (3C, 3G, 6A, three 6B checks, and 6C), with no new
failure. The strict warning-as-error lane passed as part of the JS62R smoke.

The kernel wrapper remains blocked by the existing PacMan undefined symbols
`pacman_audio_load_resources(gx_app_context*)` and
`pacman_audio_submit(void*, PacManSoundId)`. The direct amd64 make lane remains
blocked by the existing Mbed TLS configuration errors at
`mbedtls_check_config.h:51` and `:64`; neither blocker was modified. No fresh
kernel was produced, so QEMU is not claimed. Kernel attempts regenerated three
tracked PacMan object files; those were restored to their committed contents.
The post-run worktree contains only the four JS62R source/test/documentation
files described above.

The focused evidence still does not include test-only operation counters,
outer-candidate cost measurements, the requested deep/wide relation stress,
full live-state/lifecycle/purity cross-checks, or a pre-run snapshot of ignored
generated artifact trees. No public `:has()` is supported. Because total work
across many outer candidates remains O(N³) in the conservative bound and has
not been measured at capacity, JS62R is Outcome B: NOT YET SAFE TO PROCEED TO
JS63.

### JS62R2 relative selector exposure qualification (2026-10-04)

JS62R2 measured the existing JS62R evaluator without changing its algorithm.
The test-only `NavigatorScriptRelativeSelectorCounters` is caller-owned, has
eight 64-bit fields (64 bytes), and is compiled only with
`GXOS_RELATIVE_SELECTOR_DIAGNOSTICS`. Production builds have no counter type,
argument, global state, or counter storage. The test caller keeps the 64-byte
counter on its stack; production evaluator stack usage is unchanged. Counters
track relative calls, inspected structural records, parent hops, serial/index
resolutions, simple-selector evaluations, and Child, AdjacentSibling, and
GeneralSibling candidate checks. Repeated near-capacity runs produced identical
counts. A counter-enabled and ordinary call returned the same result.

The structural Element cap remains 1024. JS62's original repeated serial scans
had an O(N³) Descendant cost per anchor; JS62R reduced it to O(N²) per anchor
through one sequential-serial validation followed by direct `serial - 1`
parent lookup. JS62R2 made no further algorithm change. Final traversal bounds
remain Descendant O(N²), Child O(N), AdjacentSibling O(N), and GeneralSibling
O(N). No persistent cache, heap allocation, parser change, or public `:has()`
support was added. Production descriptor sizes remain: shared core 32 bytes,
simple selector 68 bytes, four-member selector 812 bytes, collection record
832 bytes, 128-record registry 106,496 bytes, `HtmlElementRef` 440 bytes, and
content record 24 bytes.

At the full 1024-record capacity, a single flat-tree Descendant call with the
only match at the last eligible node used 4,102 record inspections, 1,027
parent hops, 2,051 serial/index resolutions, and 1,021 selector evaluations.
The absent-target scan had the same counts. A 400-level nested chain (403
structural Elements) used 161,218 record inspections, 80,206 parent hops,
160,409 resolutions, and 400 selector evaluations for both deepest-match and
absent-target cases. The 1,021-child wide tree measured Child first/middle/last
matches at respectively 1,038/2,058/3,078 records, 3/3/3 parent hops,
7/517/1,027 resolutions, and 1/511/1,021 selector evaluations. Descendant
first/middle/last matches used 1,042/2,572/4,102 records, 7/517/1,027 hops,
11/1,031/2,051 resolutions, and 1/511/1,021 selector evaluations. The absent
Child case used 3,078 records and 1,021 selector evaluations.

The wide sibling fixture contained 1,018 direct siblings. GeneralSibling late
match and absent-target cases each used 3,068 records, 4 parent hops, 1,026
resolutions, and 1,017 selector evaluations. Adjacent first and middle
nonmatches used 1,036 and 1,545 records with one adjacent candidate check each;
the last-anchor case had no following candidate. A separate immediate-match
fixture verified both sibling relations.

The future-like simulation called Descendant matching for all 1,022 represented
Elements in a full-capacity document. Across all anchors, early-true used
7,930,708 records, 3,181,563 parent hops, 5,494,253 resolutions, and 345
selector evaluations (average 7,759.99 records and maximum 9,530 per anchor;
average 3,113.08 hops and maximum 3,743). Late-true used 7,931,740 records,
3,181,908 hops, 5,494,943 resolutions, and 687 selector evaluations (average
7,761 records and maximum 9,530 per anchor; average 3,113.41 hops and maximum
3,743). The full false case used 9,210,448 records, 3,820,245 hops, 6,598,727
resolutions, and 2,721 selector evaluations (average 9,012.18 records and
maximum 9,530 per anchor; average 3,738.01 hops and maximum 3,743). Native
timings for these runs were 17–20 ms and are secondary to the operation counts.
These measured bounded counts did not justify another optimization.

The new focused `tests/navigator_javascript_js62r2_test.cpp`, run by
`scripts/smoke-navigator-javascript-js62r2.ps1`, passes 4,081 checks and the
strict warning-as-error source lane. It covers the measurements above; a
300-document generation replacement loop; stale anchor and target handles;
true/false serial reuse; live attribute presence and equality; checked state;
focus moving outside, to two descendants, and outside again; empty and six
structural pseudos; a rich compound through all four relations; root negatives;
self-parent, two-node and three-node cycles; sequential and duplicate serial
corruption; invalid parent metadata; parentless and invalid sibling parents;
capacity rejection; hidden Elements; form-owner independence; anchor-self and
sibling-boundary exclusion; and the unsupported public boundary. Public
`:has(.x)`, `:has(> .x)`, `:has(+ .x)`, `:has(~ .x)`, and standalone leading
combinators remain unsupported.

The focused JS36–JS62 check counts were: JS36 114, JS37 180, JS38 152, JS39
218, JS40 155, JS41 220, JS42 235, JS43 277, JS44 183, JS45 184, JS46 220,
JS47 137, JS48 136, JS49 150, JS50 313, JS51 278, JS52 319, JS53 211, JS54
238, JS55 340, JS56 110, JS57 269, JS58 153, JS59 621, JS60 1,377, JS61
2,077, and JS62 15. All passed. JS62R passed 10/10 and JS62R2 passed
4,081/4,081. The full
historical JavaScript matrix passed 60/60 lanes: lexer, parser, runtime, and
JS6–JS62. The production `build.bat` passed. The hosted aggregate reproduced
614 passed and the same seven known CSS failures (3C, 3G, 6A, three 6B checks,
and 6C), with no new failures. The kernel wrapper stopped on the existing
undefined PacMan symbols `pacman_audio_load_resources(gx_app_context*)` and
`pacman_audio_submit(void*, PacManSoundId)`. Direct amd64 make stopped on the
existing Mbed TLS configuration errors at `mbedtls_check_config.h:51` and
`:64`; no fresh kernel or QEMU run resulted.

Before build attempts, JS62R2 captured SHA-256 and length for all 1,337 ignored
files (88,371,771 bytes), including hosted build outputs, PacMan objects,
kernel outputs, and wallpaper-pack files. Regenerated files were restored from
that backup; the three tracked PacMan objects changed by the kernel wrapper
were restored to HEAD. The final ignored-artifact comparison is changed=0,
missing=0, extra=0. No generated artifact is included in the source change.

JS62R2 is Outcome B. The measured bounded workload is small enough that
performance alone does not block JS63. However, qualification is incomplete:
radio exclusivity and fieldset-disabled projection were not exercised;
disabled-state freshness was checked through a native test accessor to the
canonical runtime state rather than a supported JavaScript mutation; independent
cross-checks against `contains`, `parentElement`, sibling traversal, and CSS
relation queries were not completed; and event metadata, nested dispatch,
reentrancy, and full evaluator-purity checks remain unqualified. Those are the
remaining blockers. No public `:has()` syntax was added.

**NOT YET SAFE TO PROCEED TO JS63**

### JS62R3 relative selector behavioral qualification (2026-10-05)

JS62R3 keeps the JS62R2 relative evaluator and public selector grammar
unchanged. The new native lane, `tests/navigator_javascript_js62r3_test.cpp`,
is run by `scripts/smoke-navigator-javascript-js62r3.ps1`. It passes 1,293
focused checks, including the inherited generation/corruption/attribute/focus
coverage, radio and disabled-state cases, cross-checks, and state snapshots.
The ordinary and strict warning-as-error source builds pass. All four public
`:has()` forms and standalone leading combinators remain rejected.

The radio fixture starts with an unchecked two-member `mode` group. JavaScript
sets radio A through the existing checked setter; then the normal focused
control activation path checks radio B, clears A, and leaves the independent
`other` group independent. ID-qualified relative `:checked` results follow A
and B, and replacing the document rejects the old radio handle and leaves the
replacement radio unchecked. The disabled fixture proves a disabled fieldset
projects `:disabled` to its eligible descendant, an explicitly disabled
control matches, and a control outside the fieldset does not. Repeated calls
observe native changes to canonical disabled state without a cache. Navigator
still has no supported JavaScript disabled-state setter: JavaScript `disabled`
attribute writes remain deferred no-ops under JS52, so freshness is established
through the existing native runtime authority.

The lane compares 256 unique-ID anchor/target/relation cases against separate
parent-chain and forward-sibling walks. It also compares representative
Descendant, Child, AdjacentSibling, and GeneralSibling results with ordinary
CSS combinator queries; Child with `parentElement`; AdjacentSibling with
`nextElementSibling`; GeneralSibling with a repeated `nextElementSibling`
walk; and Descendant with public `contains()` (which includes self, while
relative Descendant excludes it). These oracles are separate from the relative
helper, though the CSS engine and helper both ultimately use the same
authoritative structural Element records. The traversal-property and
parent-chain checks supplement that shared low-level representation.

Purity snapshots show no change to the full selector core bytes, structural
Element bytes/order, form runtime state (including focus/checked/disabled),
content metadata, document generation, selector collection occupancy, or
listener count. Separate caller-owned counter sets produce matching isolated
counts; a no-counter call returns the same result. The helper and production
build remain free of relative-result cache or heap scratch. Production data
sizes remain at the JS62R2 values: core 32, simple selector 68, selector 812,
collection record 832, registry 106,496, HtmlElementRef 440, content record
24 bytes; diagnostics remain 64 bytes per native caller and add zero
production storage.

The performance sanity subset reproduced the JS62R2 near-capacity late-true
and false counts exactly: 4,102 record inspections, 1,027 parent hops, 2,051
serial/index resolutions, and 1,021 selector evaluations in each call. A
64-anchor outer-candidate sample used 581,942 record inspections (about 9,093
per anchor), consistent with the JS62R2 9,012-record full-false average. The
algorithm was not changed and those counts do not justify optimization.

**Outcome B: NOT YET SAFE TO PROCEED TO JS63.** JS62R3 does not yet prove
relative evaluation from inside event handlers or nested dispatch. Event
target/currentTarget/phase/relatedTarget/defaultPrevented preservation during
relative evaluation, and reentrant relative calls with different anchors,
selectors, relations, and counter sets, remain unqualified. No test-only
JavaScript API was added to bridge that gap. Public `:has()` remains
unsupported. The direct relative evaluator has no callback/reentrant entry
point in this phase, so completing this item requires a native test seam that
can execute evaluator calls at event-handler boundaries without exposing a
JavaScript API or changing production behavior.

### JS62R4 Event and reentrancy qualification (2026-10-05)

JS62R4 adds a diagnostics-only native probe around each existing Event
listener invocation. The probe is compiled only when
`GXOS_RELATIVE_SELECTOR_DIAGNOSTICS` is defined; its callback type, adapter
fields, and invocation sites are absent from production builds. It exposes no
JavaScript function or selector syntax. Before and after each listener, the
native test invokes the private relative evaluator using caller-owned
counters and inspects the active runtime Event object.

The dispatch audit found that `dispatchEvent` keeps its propagation path,
listener snapshots, invocation arguments, saved adapter dispatch flag, and
related-target reference in dispatch-local storage. The event target is a
generation-checked reference retained for that dispatch; currentTarget and
eventPhase are updated at the relevant propagation stage. The shared Event
object is selected from a per-depth cache. `RuntimeContext` stores dispatch
phase, propagation flags, cancellation state, cancelability, and the prior
Event object in its fixed 16-entry EventDispatchState stack. Nested dispatch
restores that state on return. Listener function/environment state belongs to
the synchronous runtime call stack and bounded runtime environment store.
The adapter's `clickDispatchActive_` is one per-adapter flag, but each dispatch
saves and restores its previous value.

The relative evaluator has no static or global mutable traversal scratch,
anchor, relation, selector, cursor, or statistics pointer. Its traversal
temporaries and optional counter pointer are call-local. Diagnostic counters
remain caller-owned, independently resettable 64-byte structs; the
diagnostics-disabled call returns the same result. No traversal algorithm,
production Event structure, or persistent production storage changed.

The focused test performs 100 outer click dispatches. Each outer listener
prevents default, dispatches a nested click, and that listener dispatches a
third click (two levels of nesting). The test probe executes Descendant,
Child, AdjacentSibling, and GeneralSibling calls, then uses a different `#id`
core and `:empty` core in the same probe. It also evaluates with event.target
and event.currentTarget as anchors when their canonical Element handles are
available, compares a no-counter call, and rejects stale-generation and
invalid-serial anchors. Event wrapper fields are identity-observed before and
after each set of calls. The test separately checks the outer and inner
JavaScript Event objects after nested dispatch, resets only the inner
counter set at each inner target listener, and verifies that outer totals do
not change during those resets.

The JS62R4 lane passes **113/113** checks. It recorded 1,804 before/after
listener probes (400 outer-target, 600 inner-target, 800 depth-two-target
probes, plus four focus-event probes), with zero Event metadata changes and
zero result mismatches. Capture, target, and bubble phases were all observed.
One hundred nested dispatch cycles completed. Focusout/focusin relatedTarget
identity remained unchanged while relative calls ran. The counter isolation
and deliberate reset checks passed; selector core and selector storage
snapshots remained byte-identical. Invalid anchors and diagnostics-disabled
behavior left Event dispatch unaffected.

JS62R4 does not change the bounded traversal. The JS62R3 focused lane reruns
the near-capacity performance checks and preserves the JS62R2 values: for
both late-true and false Descendant evaluation, 4,102 structural inspections,
1,027 parent hops, 2,051 serial/index resolutions, and 1,021 selector
evaluations. The 64-anchor simulation remains 581,942 inspections. No
performance regression called for optimization.

The Event probe adds zero production bytes. The previously qualified
production sizes remain: shared selector core 32 bytes, simple selector
descriptor 68, four-member descriptor 812, collection record 832, 128-entry
collection registry 106,496, `HtmlElementRef` 440, content metadata record
24. JS62R2 counters remain 64 bytes per native test caller. No additional
production stack or local state was introduced; callback data and counters
are test-local.

Public `:has()` forms and standalone leading combinators remain unsupported.
The JS62R3 hosted fixture and native regression continue to assert those
rejections. No public relative-selector grammar, `Has` pseudo kind, heap
scratch, or persistent result cache was added.

#### JS62R4 phase completion audit

The exact historical JavaScript matrix passed **60/60** (lexer, parser,
runtime, and JS6–JS62). The focused JS36–JS62R3 regressions passed **30/30**;
the JS62R4 focused event/reentrancy lane passed **113/113**. JS62R3's
near-capacity and 64-anchor measurements matched their recorded JS62R2/R3
baselines. `build.bat` passed, as did the JS62R4 strict warning-as-error
parser/adapter/runtime lane.

The hosted aggregate reported **614 passed / 7 failed / 621 total**. The seven
failures were precisely the existing CSS 3C, CSS 3G, CSS 6A, three CSS 6B,
and CSS 6C checks. The kernel wrapper stopped on the established undefined
PacMan symbols `pacman_audio_load_resources(gx_app_context*)` and
`pacman_audio_submit(void*, PacManSoundId)`. Direct
`mingw32-make ARCH=amd64 EXTRA_CFLAGS=` from `kernel/` stopped at the existing
Mbed TLS configuration errors in `mbedtls_check_config.h:51` and `:64`.
No fresh kernel was produced, so **QEMU proof is not claimed**.

The pre-build ignored/generated snapshot contained **1,337 files / 88,371,771
bytes**. After the build and hosted smoke, it contains 1,343 files / 88,850,994
bytes: zero missing, one changed (`guideXOSServer.exe`), and six new hosted
smoke logs. The three PacMan object files modified by the kernel wrapper were
restored to their original tracked contents. The initial executable's SHA-256
was recorded, but its bytes were not backed up; no matching copy exists in
the workspace, so its original hash could not be restored. The shell policy
rejected removal of the six generated smoke logs. No generated file is staged
or committed, but the requested final artifact comparison is not clean.

The worktree started clean on `NAVIGATOR_JAVASCRIPT_SUPPORT` at
`ce6287ffb5e02d726ddc060af108214f4b8762ae` (`navigator: extend JS62
behavioral qualification`), tracking `origin/NAVIGATOR_JAVASCRIPT_SUPPORT`
at 1 ahead / 0 behind. No branch operation or push occurred. No commit was
created because the phase did not achieve a clean artifact audit.

**Outcome C: NOT YET SAFE TO PROCEED TO JS63.** Event, reentrancy, counter,
and selector-purity evidence passed. The remaining blocker is the failed
generated-artifact restoration audit: the previous ignored executable bytes
were unavailable, and the six run-generated logs remain present after shell
policy rejected their removal. Re-run the snapshot with restorable file
backups, then restore/clean those exact generated outputs before claiming
phase completion.

#### JS62R4M artifact provenance and deterministic closeout (2026-10-05)

This closeout supersedes the earlier artifact-audit statement above about the
six hosted smoke logs: all six exact logs were later removed and each path was
verified absent. Their total size was **479,223 bytes** (460,137 + 0 + 14,943
+ 0 + 4,143 + 0). The recorded inventories differ by exactly that amount:
88,850,994 - 88,371,771 = 479,223. The immediate comparison therefore
reported one changed file, zero missing files, and six extras; the executable
had no length delta at that comparison point. Its inferred historical length
is **8,398,274 bytes**.

The original per-file snapshot was recovered at
`C:\Users\guideX\AppData\Local\Temp\js62r2-artifact-baseline.csv`. It has
1,337 rows / 88,371,771 bytes and records `guideXOSServer.exe` as 8,398,274
bytes with SHA-256
`A5E439455E4D3E735DFB848EC296A3EC4B347822B21F5B1B736D51227046A971`. The
matching byte backup is
`C:\Users\guideX\AppData\Local\Temp\js62r2-artifact-backup\guideXOSServer.exe`.
Both its length and SHA-256 matched the CSV row before restoration. This is
the historical hash record; the earlier JS62R4C conclusion that the value
was unavailable is superseded.

The later timestamped Mbed TLS dependency file was
`kernel/build/amd64/obj/third_party/mbedtls/library/mbedtls_config.d`, a
4,361-byte generated dependency file with SHA-256
`F6A5E53C027870BD1711204D59619571E182109E5F65D918631764E7AD781C4F`, last
observed written 2026-10-05 06:16:55 -07:00 after the later
`mingw32-make ARCH=amd64 EXTRA_CFLAGS=` run in `kernel/`. The kernel Makefile
uses `-MMD -MP` for these generated dependency files. The recovered baseline
CSV and backup show this exact path and content were already part of the
original snapshot; its SHA-256 was unchanged. The file was temporarily removed
while classifying it, then restored from the matching snapshot backup. Its
restored write time is 2026-10-04 17:22:29 -07:00. It was a timestamp refresh,
not an extra. No Mbed TLS source, submodule metadata, or directory was removed.

Two separate, clean ordinary recovery copies were built from branch
`NAVIGATOR_JAVASCRIPT_SUPPORT`, exact HEAD
`ce6287ffb5e02d726ddc060af108214f4b8762ae` (`navigator: extend JS62
behavioral qualification`):

| Copy | Path | `build.bat` result | Length | SHA-256 |
| --- | --- | --- | ---: | --- |
| A | `D:\JS62R4-A` | success | 8,398,274 | `D0E356151E7C9F15740DB7C4E44DF352D9894080832A2811F2C11D0E0FC0AEAF` |
| B | `D:\JS62R4-B` | success | 8,398,274 | `1DAB1DB926317D4D80EC3434AE5F00AAB0D8596952E20484EA2BF8C677175EC9` |

Both copies used `C:\mingw64\bin\g++.exe` (MinGW-W64 x86_64, GCC 15.2.0),
the same production `build.bat` command and flags, the locked Mbed TLS 4.1.0
commit `0fe989b6b514192783c469039edd325fd0989806`, TF-PSA-Crypto 1.1.0
commit `29160dd877d29658279fd683b2ae57b320ddcf09`, the three repository patch
series, and stb_image v2.30 SHA-256
`1F8C1B6B408F26E3B20CBFBBD4758AFB3DC9B837FF1E17C258928F406148A87C`. Each
dependency profile verification passed. The recovery outputs were not copied
to the main executable.

The two executable lengths matched the historical length, but the hashes did
not match each other. A byte comparison found four changed bytes:
PE timestamp bytes at offsets 136–139 and the associated PE checksum at
216–219. `objdump` reported linker timestamps 2026-10-05 21:05:04 and
21:13:27 local time (503 seconds apart). No timestamp normalization or build
system changes were made. This is direct evidence that the existing build is
not byte-for-byte deterministic; neither recovery build matches the captured
historical hash. The deterministic fallback was therefore not used. The
exact hash-matching historical backup was restored to the main executable and
verified at 8,398,274 bytes / SHA-256
`A5E439455E4D3E735DFB848EC296A3EC4B347822B21F5B1B736D51227046A971`.

After restoring the historical executable and the baseline Mbed TLS
dependency file, a per-file comparison against the recovered CSV reports
**1,337 files / 88,371,771 bytes; changed=0, missing=0, extra=0**. This
comparison includes each row's path, length, and SHA-256, including the
restored executable and `.d` dependency file.

The focused `scripts/smoke-navigator-javascript-js62r4.ps1` rerun passed
**113/113**, with the strict warning-as-error lane passing. Its report showed
1,804 Event probes, zero relation failures, zero metadata failures, nested
callback totals 400/600/800 at depth two, and isolated counters 3000/32/6200.
The qualified counter isolation and selector immutability assertions remain
green. Public `:has(.x)`, `:has(> .x)`, `:has(+ .x)`, `:has(~ .x)`, and
standalone leading combinators remain unsupported. `git diff --check` passed.

No branch switch, stash, reset, `git clean`, Git worktree, or push occurred.
The main checkout remained on `NAVIGATOR_JAVASCRIPT_SUPPORT` at
`ce6287ffb5e02d726ddc060af108214f4b8762ae` during recovery. Live upstream
already contained that HEAD (0 ahead / 0 behind), although the earlier prompt
expected 1 ahead / 0 behind. No qualified source was copied from recovery
folders. No main production rebuild, kernel rerun, or QEMU run occurred.

**Outcome A — SAFE TO PROCEED TO JS63.** The historical executable hash was
recovered from the JS62R2 CSV and the exact backed-up bytes were restored. The
full ignored-artifact comparison is clean, JS62R4's narrow source checks pass,
and the original Mbed TLS dependency file is present with its baseline hash.
The build reproducibility experiment did expose PE timestamp variation, but
it is not needed to establish provenance now that the original hash and exact
matching bytes are available. JS62R4 selector and Event qualification remains
green; no selector or Event rework is indicated.

#### JS63 bounded public `:has()` implementation (2026-10-07)

JS63 follows the accepted JS62R4M closeout at the current source baseline
`fa853ed3836c69c4137f1ea25f7ed19f748ff5fb` (`navigator: qualify JS62 event
reentrancy`). The earlier JS62R notes in this file record real, intermediate
qualification blockers. They are historical checkpoints, not later phase
authority: JS62R4M subsequently closed the artifact/provenance work and marked
the relative evaluator safe for JS63. JS62-era tests that required public
`:has()` rejection were updated to check that these four arguments are accepted;
the dedicated JS63 suite now verifies their behavior and malformed-input
rejection.

Public support is implemented with the existing bounded parser and matcher.
The argument grammar is one optional leading relation plus exactly one
restricted simple-selector core:

| Public form | Evaluator relation |
| --- | --- |
| `:has(S)` | strict `Descendant` |
| `:has(> S)` | `Child` |
| `:has(+ S)` | `AdjacentSibling` |
| `:has(~ S)` | `GeneralSibling` |

The tested element is passed as the relative evaluator's anchor. The evaluator
searches only descendants or following siblings as indicated; the anchor does
not match itself. Child means direct structural Element child. Adjacent means
the immediate following structural Element sibling. General sibling means any
later structural Element sibling with the same valid, nonzero parent.

The existing shared core parser handles the inner selector's tag/universal,
ID, class tokens, attribute predicate, and supported nonfunctional state or
structural pseudo. Existing selector bounds remain in force: 256 characters
for the complete selector, at most four outer list members, eight class
tokens, 64 attribute-name bytes, and 128 attribute-value bytes. Pseudo names
are ASCII case-insensitive. Existing one-pseudo-per-simple-selector behavior
is preserved. Empty arguments, absent targets, malformed or multiple leading
combinators, inner selector lists, relation chains, nested `:has()`, nested
`:not()`/`:is()`/`:where()`, and inner functional nth pseudos are rejected
fail-closed. Outer selector lists remain supported.

The parsed inner core uses the existing fixed `logicalSelector` core slot; its
relation is represented by four `:has` variants of the existing one-byte
`statePseudo` enum. This avoids adding a relation member or growing selector
collections. There is no match-time parse, heap allocation, persistent result
cache, or global traversal scratch. Query, `matches()`, and `closest()` all
call the same outer matcher, which calls the JS62 relative evaluator.
`querySelectorAll()` retains its existing live collection behavior, document
order, and deduplication. Element-scoped queries continue to enumerate strict
descendants of the scope, so the scope itself is not returned as a query
result; relative evaluation for each result remains anchored to that result.

Memory sizes remain at the JS62R2 qualified values: shared selector core 32
bytes, simple selector descriptor 68 bytes, four-member selector descriptor
812 bytes, collection record 832 bytes, 128-record registry 106,496 bytes,
`HtmlElementRef` 440 bytes, and content metadata 24 bytes. JS63 adds zero
persistent bytes for relation storage because it uses existing pseudo enum
values; the inner core uses the pre-existing fixed core slot. No parser-local
stack measurement was added.

Focused regression results:

- JS63: 6/6 checks; all four relation semantics, positive/negative boundaries,
  strict self exclusion, compound/tag/attribute matching, outer-list ordering,
  Element-scoped sibling queries, `matches()`, `closest()`, live `:checked`
  and attribute mutation, malformed and unsupported grammar rejection, and
  Event callback matching.
- JS62: 15/15; JS62R: 10/10; JS62R2: 4081/4081; JS62R3: 1293/1293; JS62R4:
  113/113 with 1,804 Event probes and zero relation/metadata failures.
- JS61: 2077/2077; JS38 DOM/API regression: 152/152; JS36 query/DOM regression:
  114 checks, zero failures. Their strict warning-as-error lanes passed where
  provided by the focused scripts.

The qualified JS62R2 near-capacity false simulation remains 9,210,448
structural-record inspections, 3,820,245 parent hops, 6,598,727 serial/index
resolutions, and 2,721 selector evaluations across 1,022 anchors. A single
near-capacity Descendant late-true/false call remains 4,102 inspections, 1,027
parent hops, 2,051 resolutions, and 1,021 selector evaluations. JS63 adds one
outer predicate dispatch before entering this same evaluator; a separate
public-path counter build was not made, so no additional public-call count is
claimed.

The hosted Navigator smoke was attempted with the available
`guideXOSServer.exe`, which predates JS63. Its hosted JS59/JS61 selector checks
passed, while seven unrelated CSS smoke checks failed in phases 3C, 3G, 6A,
6B (three checks), and 6C. This run therefore does not qualify JS63 against a
fresh hosted binary. The 42 persistent generated fixtures (458,372 bytes)
matched their pre-run byte snapshot; all six timestamped logs created by the
run were deleted by exact path and verified absent. A production `build.bat`
rebuild and fresh hosted JS63 fixture remain unrun. No new standalone phase
marker was created because repository progression is recorded in this
chronological document.

**Outcome B — bounded public `:has()` implementation complete; production
hosted qualification pending.** The four forms are exposed through the
qualified JS62 evaluator without persistent memory growth. Close the hosted
baseline CSS failures and build a fresh server with a JS63 fixture before
claiming Outcome A. Next phase should be JS64 only after that evidence closes.

#### JS63Q2 hosted fixture and bounded artifact closeout (2026-10-10)

JS63Q2 completed the outstanding production-hosted qualification. The earlier
JS63Q attempt stopped during artifact preflight because it treated the full
ignored tree as requiring exact backups. JS63Q2 instead hashed that tree for
change detection and backed up only output paths identified by the build and
hosted-runner scripts before invoking them.

The new `navigator-smoke/javascript-js63.html` fixture is part of the existing
`navigator.smoke` aggregate. It makes 28 public API assertions covering
Descendant, Child, AdjacentSibling, and GeneralSibling `:has()` relations;
strict self exclusion; anchor identity; ordered and deduplicated selector-list
results; `querySelector`, `querySelectorAll`, `matches`, `closest`, and
Element-scoped queries; a scoped adjacent-sibling query; live attribute and
checked-state changes; disabled and empty targets; and rejection of empty,
list, chained, nested, and nested-functional arguments. It registers an Event
listener that evaluates `:has()` and checks target, currentTarget, eventPhase,
and defaultPrevented. A follow-up adds four assertions for live `:has(:focus)`
state while focus moves from outside the scope, between two in-scope controls,
and out again on blur. The hosted fixture passed 32/32 assertions, including
the Event callback. The initial fixture draft exposed two useful harness details:
the HTML script extractor stops at a literal `<` in inline script text, and a
matching wrapper `div` correctly wins earlier query order. The fixture avoids
the extractor boundary and uses a `main` scope so its `div` anchor expectations
are unambiguous.

`server.cpp` adds three checks to the existing aggregate for the JS63 fixture,
its public API results, and Event integration. The hosted runner reports both
the executable path and hash and checks the launched process path. The fresh
`build.bat` output was `guideXOSServer.exe`, 8,416,318 bytes, SHA-256
`343431D5FEAD5D9E0925E682B77E1DC47FCC6DE365CE863A9D82D587125809A9`. The
process actually launched from that same repository path. The runner does not
stage or copy the server binary.

The fresh hosted aggregate reported **617 passed / 7 failed / 624 total**.
All three JS63 aggregate checks passed. The only failures were the established
CSS baseline checks: CSS 3C; CSS 3G; CSS 6A; the three CSS 6B checks for sticky
diagnostics, local/nested scrollports, and fixed-descendant invariance; and CSS
6C. No CSS source was changed. The full JavaScript smoke-script matrix passed
**65/65**, including lexer, parser, runtime, JS6–JS63, JS62R, JS62R2, JS62R3,
and JS62R4. The strict JS63 `-Wall -Wextra -Werror -pedantic` lane passed.
JS63 passed **8/8** native checks, including a public `matches()` full-false
then late-true case, `querySelector()` anchor identity, and
`querySelectorAll()` at the 1,024-Element document limit. Required focused
regressions JS36, JS38–JS41, JS50–JS52, JS55–JS56, JS58–JS63, and JS62R–JS62R4
passed in the matrix.

The JS62 qualified sizes remain unchanged: inner core 32 bytes; simple
selector 68 bytes; four-member selector descriptor 812 bytes; collection
record 832 bytes; 128-record registry 106,496 bytes; `HtmlElementRef` 440
bytes; and content metadata 24 bytes. JS63 still stores its relation choice in
the existing one-byte pseudo enum and uses the pre-existing inner-core slot.
Parsing stores the bounded inner selector before matching; the matcher passes
that descriptor to the JS62 relative evaluator and does not parse raw
`:has()` text per anchor. The JS63 selector parse and match path adds no heap
container or persistent result cache. Live attribute and checked state are
read on each evaluation.

The 1,024-node public-path test passed its full-false and late-true cases.
JS62R2/R3 diagnostic baselines remain 4,102 structural inspections, 1,027
parent hops, 2,051 serial/index resolutions, and 1,021 simple-selector checks
for one near-capacity late-true or false relative evaluation. JS62R2's
1,022-anchor full-false simulation measured 9,210,448 inspections, 3,820,245
parent hops, 6,598,727 resolutions, and 2,721 selector evaluations in 19 ms.
JS63 adds the outer pseudo dispatch before this same evaluator; no separate
diagnostic-counter build for the public JS63 path was made.

`build-kernel.bat` stopped at the known PacMan link errors for
`pacman_audio_load_resources(gx_app_context*)` and
`pacman_audio_submit(void*, PacManSoundId)`. The direct
`mingw32-make ARCH=amd64 EXTRA_CFLAGS=` lane stopped at the known Mbed TLS
configuration errors in `mbedtls_check_config.h:51` and `:64`. Neither lane
produced a fresh kernel, so QEMU proof is not claimed. These unrelated kernel
blockers do not block JS63 qualification.

The broad initial ignored/generated audit covered 1,337 files / 88,371,771
bytes, recording SHA-256 for every file. Exact pre-run byte backups covered
351 files / 139,615,462 bytes across the server executable, ESP, PacMan
package/build outputs, bootloader outputs, AMD64 kernel build outputs,
wallpaper-pack outputs, and persistent hosted fixtures. Before restoration,
the only changed baseline artifact was `guideXOSServer.exe`; the four hosted
runs created 24 timestamped log files. The original executable bytes were
restored from their exact backup and all 24 exact log paths were removed. The
final broad audit returned to the same file count and total bytes with zero
changed, missing, or extra files. Generated outputs remain excluded from the
commit. The hosted server also rewrote `desktop.state`, `display-options.cfg`,
and `window-bounds.cfg`; the starting worktree was clean, so those three
tracked state files were restored to their exact starting `HEAD` blobs. The
kernel wrapper regenerated tracked PacMan `game.o`, `main.o`, and `renderer.o`;
all three were restored from the exact pre-run byte backups and hash-verified.

No CSS files changed. No branch or worktree topology operation, stash, reset,
or `git clean` was used, and nothing was pushed. The JS63Q2 source, runner, and
documentation changes are ready for one local qualification commit.

**Outcome A — JS63 bounded public `:has()` fully qualified.** No bounded
`:has()` qualification gap remains. Select the next JavaScript selector
feature for JS64 as a separate phase; the seven unrelated CSS baseline
failures are not JS63 follow-up work.

#### JS64 bounded two-alternative `:has()` lists (2026-10-10)

JS64 extends the qualified JS63 grammar to accept one or two independent
relative selectors in each `:has()` expression. The two results are combined
with OR, and a matching first alternative returns before the second is
expanded or evaluated. Each alternative retains JS63's optional single
relation and restricted simple-selector core. Descendant, child, adjacent
sibling, and general sibling relations all use the existing JS62 relative
evaluator. Nested functional pseudos, relation chains, empty alternatives,
and a third alternative remain rejected. The existing four-member outer
selector list remains independent of the two-alternative inner bound.

The inner comma scanner ignores commas inside quoted attribute values and
rejects nested functional syntax before splitting. Whitespace around either
alternative is trimmed; whitespace inside a selector core remains outside the
grammar. The shared parser still stores first-alternative relation and core in
the JS63 fields. A descriptor-wide fixed pool holds up to eight compact
second cores, matching the maximum two compound selectors across four outer
members. The simple selector's existing `nthA` records an explicit count of
one or two, while `nthB` packs the second relation and pool index. No pool
pointer or parser-buffer address is retained.

Each compact second core is 18 bytes. Class token ranges remain usable by the
existing evaluator: parsed class names are stored contiguously with NUL
separators, and the compact core records one bounded block. During matching,
that fixed block is expanded into the existing 32-byte JS62 core on the
stack. Matching does not reparse selector text, allocate memory, or retain
results. A first-alternative Invalid result fails closed; a Match
short-circuits; only NoMatch proceeds to the second alternative.

Memory was measured against the live JS63 layout before implementation:

| Structure | JS63 | JS64 | Delta |
| --- | ---: | ---: | ---: |
| Relative core, first argument | 32 B | 32 B | 0 B |
| Relative core, second argument | — | 18 B | 18 B per second core |
| `SimpleSelector` | 68 B | 68 B | 0 B |
| Four-member selector descriptor | 812 B | 958 B | +146 B |
| Selector collection record | 832 B | 976 B | +144 B |
| 128-record collection registry | 106,496 B | 124,928 B | +18,432 B |
| Document-owned persistent storage | — | 0 B | 0 B |

The 146-byte descriptor increase is eight 18-byte pool entries plus the pool
count and alignment. The collection record's alignment makes its measured
growth 144 bytes. The pool is fixed and bounded; it is not multiplied into
each `SimpleSelector`. Parser stack measurements and full JS64 qualification
results follow in the closeout below.

The parser stack comparison used MinGW GCC 15.2 with C++17, `-O2`, and
`-fstack-usage`, compiling the JS63 source/header from the starting `HEAD`
beside the JS64 worktree source/header. The per-function static stack frames
were: `parseBoundedSelector` 112 bytes before and after;
`parseBoundedSelectorMember` 176 bytes before and after; and
`parseSimpleSelectorCore` 336 bytes before, 432 bytes after. The direct
bounded-selector parser chain therefore measures 624 bytes before and 720
bytes after, a 96-byte increase. The 256-byte selector-input bound is
unchanged. These figures are compiler-reported static frames and do not add
the separate caller/API frame or helper frames.

#### JS64 qualification closeout (2026-10-10)

The focused JS64 native lane passed **24/24 checks**, including the strict
`-Wall -Wextra -Werror -pedantic` syntax compile. The native cases cover the
complete 16 ordered relation-pair matrix in first-only, second-only, and
false states; first-match short-circuiting; second evaluation after a
non-match; malformed and unsupported grammar rejection; all eight pool
entries across four outer members; scoped queries, `matches()`, `closest()`,
outer-list order and deduplication; live attributes, checked/radio, focus,
disabled, and empty state; stale generations; event metadata and nested
dispatch; and a 1,024-Element full-false/late-true search. A 256-character
selector was accepted and a 257-character selector rejected. Twenty
near-capacity direct calls measured 11 microseconds when the first alternative
matched and 9,612 microseconds when only the second alternative matched. The
timings are observational, not test thresholds.

JS63 remained green at **8/8 native checks**, and its hosted fixture retained
all **32/32 assertions**. JS62R4 and all earlier JavaScript lanes passed in the
full current smoke matrix: **66/66 scripts**. This includes the selector
regression lanes through JS63 and JS62R, JS62R2, JS62R3, and JS62R4. The JS64
strict compile and matrix runs used the same worktree source.

The dedicated hosted fixture is
`navigator-smoke/javascript-js64.html`. On the fresh production server it
reported **37 assertions, 0 failures**, and **48/48 relation-pair cases**.
Its initial and Event callback results both passed; the callback preserved
target/currentTarget/phase/default-prevented metadata. All three JS64 hosted
aggregate checks passed. The hosted JS63 fixture also passed all three of its
aggregate checks.

The full hosted aggregate reported **620 passed / 7 failed / 627 total**. The
seven failures exactly match the established CSS baseline: CSS 3C; CSS 3G; CSS
6A; the three CSS 6B checks for sticky diagnostics, local/nested scrollports,
and fixed-descendant invariance; and CSS 6C. The JS63 baseline had 617 passed,
7 failed, and 624 total; JS64 adds the three passing aggregate checks. No CSS
source was changed and there were no new unexplained failures.

`build.bat` succeeded from the JS64 worktree based on starting source HEAD
`3b66a4b5ee4617b98b9402ad102fe67ed7a2af32`, subject
`navigator: qualify hosted JS63 has support`. The output was
`D:\dev\guideXOSServer_NAVIGATOR_JS_SUPPORT\guideXOSServer.exe`, 8,423,787
bytes, SHA-256
`68E06AF7AA6FB14B524B21C5086C4F8553E09387055A3E376FD99FF64930ED5E`. The
hosted runner reported that same path and hash, launched process 3244 from
that path, and used fixture `javascript-js64.html`; it did not stage a copy.
The build emitted existing warnings from unrelated files but exited
successfully. After the hosted run, the original executable was restored from
its exact backup (8,398,274 bytes, SHA-256
`A5E439455E4D3E735DFB848EC296A3EC4B347822B21F5B1B736D51227046A971`).

The initial ignored/generated audit covered **1,337 files / 88,371,771
bytes**. Exact byte backups covered 46 mutable files / 8,857,239 bytes: the
server executable, `desktop.state`, `display-options.cfg`,
`window-bounds.cfg`, and all 42 existing files in
`navigator-smoke/generated`. The hosted run created six timestamped log
files; they were moved out of the repository to the run's temporary audit
directory. The three tracked state/config files and all backed-up generated
fixtures were restored. The final broad audit returned to **1,337 files /
88,371,771 bytes**, with **0 changed, 0 missing, and 0 extra** ignored files.

No kernel wrapper or direct AMD64 kernel build was run for this hosted-server
selector change, so no fresh kernel or QEMU proof is claimed. JS64 itself
passed on the fresh hosted production server. The JS63 qualification record
above preserves the previously observed unrelated PacMan link and Mbed TLS
configuration blockers; neither was changed for this phase.

**Outcome A — Bounded two-alternative `:has()` lists fully qualified.** The
current implementation remains intentionally limited to two alternatives
per `:has()` and four outer selector-list members. If later work needs a larger
inner list or relation chains, qualify its storage and near-capacity cost in
its own phase; otherwise select the next distinct selector capability from
the project roadmap without widening this grammar implicitly.

#### JS65 bounded two-step relative `:has()` chains (2026-10-10)

JS65 extends each of JS64's at most two independent `:has()` alternatives
with an optional second simple/compound selector step. The first relation is
still relative to the original subject; the optional second relation is
relative to each matching intermediate element. Descendant, child, adjacent
sibling, and general sibling are independently accepted in both positions.
The complete chain must match; an intermediate match alone is not sufficient.
Alternatives retain OR semantics and stop after the first complete match.
Outer selector lists, scoped query behavior, `matches()`, `closest()`, and
Event callback matching continue to use the shared bounded selector path.

The parser accepts one or two steps per alternative, at most two alternatives,
and keeps the 256-byte selector-source limit. It rejects a third step, a third
alternative, missing operands, invalid/repeated combinators, nested `:has()`,
and newly nested functional expressions. The four existing relationships are
all valid at either chain edge, for 16 ordered relation pairs. Chained matching
is iterative and bounded by the 1,024-node document capacity; it does not
retain parsed source pointers, recurse through selector chains, or allocate
heap memory. A fixed 128-byte stack-local bitset records the first relation's
candidate nodes once per chain evaluation, avoiding repeated whole-document
anchor-relation scans while checking terminal candidates.

JS65 keeps the existing 32-byte shared core and 68-byte simple-selector
descriptor. One 4-byte expression record stores the optional chain and
alternative metadata; a fixed pool of 24 compact 18-byte cores supports up to
three additional cores for each of eight possible `:has()` expressions across
four outer members. The measured four-member selector descriptor is 1,278 B,
the selector collection record is 1,296 B, and the 128-record collection
registry is 165,888 B. Against JS64, the descriptor and record each grow by
320 B, and the registry grows by 40,960 B. No document-owned storage was
added. GCC 15.2 C++17 `-O2 -fstack-usage` reports 112 B for
`parseBoundedSelector`, 176 B for `parseBoundedSelectorMember`, 528 B for
`parseSimpleSelectorCore`, and 208 B for the alternative parser callback. The
measured nested parser path is 1,552 B, compared with JS64's 720 B. The chain
evaluator frame is 96 B; the relation evaluator frame that owns the bounded
scratch is 464 B. These are compiler-reported static frames, not a whole-task
stack high-water measurement.

The focused JS65 native suite passed **18/18 checks**, including strict
warnings-as-errors compilation, every ordered relation pair in positive and
negative states, late candidates, unrelated-branch and wrong-side negatives,
OR behavior, invalid grammar, repeated valid/invalid input, exact 256/257-byte
limits, and 1,024-element capacity fixtures. JS63 remained **8/8** and JS64
remained **24/24**. JS65's full-false 1,024-element descendant-chain case
measured 2,374 microseconds for one direct evaluation in the final matrix run;
20 late-success chained evaluations measured 5,322 microseconds. A dedicated
optimized run measured 1,792 and 5,092 microseconds respectively. These are
observations without timing thresholds. JS64's 1,024-element second-
alternative true branch measured 27,167 microseconds for 20 calls in the
final matrix run (11,840 microseconds in the dedicated run). That JS64 case is
not a like-for-like chain workload, so these values are recorded without a
speedup claim. The new full-false chain workload was also used to identify and
remove repeated anchor ancestry scans; its initial unoptimized observation was
69,368 microseconds for one call.

The complete JavaScript smoke matrix passed **67/67 scripts**. The hosted JS65
fixture reported **17 assertions, 0 failures**, including all 16 relation
pairs in true and false states, OR/short-circuit behavior, malformed and
unsupported rejection, and repeated size-bound checks. Its three hosted
aggregate checks passed, including Event callback target/currentTarget/phase/
default-prevented metadata. The JS63 hosted fixture remained 32/32; JS64
reported 37 assertions with 0 failures and 48/48 relation-pair cases. The
hosted aggregate reported **623 passed / 7 failed / 630 total**. The same
seven known CSS baseline checks remain: CSS 3C, CSS 3G, CSS 6A, three CSS 6B
checks, and CSS 6C. The prior aggregate was 620/7/627; JS65 added three
passing checks and no new failures. No CSS source changed.

The production `build.bat` completed successfully. Its generated
`guideXOSServer.exe` was 8,435,170 bytes with SHA-256
`6F66B7127E16094CA43EF2B31FDF87F59002C3003632D191075902BC2CAAD995`; it was
restored from the pre-run snapshot after hosted qualification. The hosted
runner needed a bounded retry for `Get-Process.Path`, which can be empty just
after process launch; it now fails clearly after the retry window and stops
only its own launched app on early failure. The successful hosted aggregate
uses the exact built executable and fixture path. The generated fixture/log,
downloads, executable, and tracked desktop/config state are covered by the
pre-run artifact snapshot and were restored byte-for-byte.

The hosted selectors were exercised through Navigator's production server and
browser harness. An additional independent browser-reference comparison was
not available because local `file:` access was blocked by the computer-use
safety layer; no alternate protocol was attempted. No kernel build or QEMU
run was performed for this hosted selector change, and no guest validation is
claimed. The repository's starting HEAD and JS62–JS64 source, tests, and this
document provide phase evidence; this checkout has no `.phase` marker, and
none was created or changed.

**Outcome A — Bounded two-step relative `:has()` chains qualified.** The
supported maximum remains two steps and two alternatives per `:has()`. Keep
larger chain/list capacity out of scope; JS66 should be selected as a separate,
explicitly bounded selector capability after this implementation is
committed.

#### JS66 bounded `:scope` pseudo-class

JS66 adds the nonfunctional, ASCII case-insensitive `:scope` pseudo-class to
the existing simple-selector parser. It uses the existing one-pseudo-per-core
bound and can be combined with a tag, ID, class, attribute predicate, or
universal selector. It participates in the existing single outer child or
descendant relation and in the existing four-member selector list. The input
limit remains 256 bytes. `:scope()` and other argument-bearing spellings are
rejected. JS66 does not add outer relation chains or change any JS65 `:has()`
alternative or step limits.

The shared matcher receives a candidate Element and a separate
`NavigatorScriptSelectorMatchContext` containing the scope serial, active
document generation, and validity flag. `:scope` matches only when both
identities are valid for that generation and the candidate serial equals the
scope serial. The context is call-local and is passed through right-side,
left-side, and bounded logical-pseudo matching; it is not stored in a global
mutable slot or in each selector descriptor.

Document selector operations resolve scope only from the authoritative
`WebDocument.documentElement` serial after validating its canonical structural
record and root parent. They never infer the document root from an arbitrary
parentless Element. Element `querySelector` and `querySelectorAll` use the
calling Element serial as scope. Their existing result domain remains strict
descendants, so the scope anchor can satisfy a relationship without becoming a
returned candidate: `root.querySelector(':scope')` is null and
`root.querySelectorAll(':scope')` is empty, while `:scope > .row` returns only
direct rows and `:scope .row` can return deeper rows.

`matches()` uses its receiver as both candidate and scope. `closest()` uses a
fixed receiver scope while candidates walk from the receiver toward ancestors;
the candidate never replaces the scope anchor. Thus
`child.closest('div:scope')` does not match a `div` ancestor when `child` is
not itself a `div`. Document-level `:scope` normally identifies the same
canonical Element as `:root`, but nested Element scope and document root remain
distinct predicates.

Selector lists pass the same context to every member, then preserve the
existing structural ordering, union, deduplication, and first-result rules.
Outer relationships without `:scope` retain their previous whole-document
ancestor matching behavior. Retained query collections reuse their existing
`scopeSerial` field and rebuild a generation-checked context when read; the
descriptor and collection record do not retain a pointer to call-local state.
Collection records are cleared on document attachment or generation change.

`:not(:scope)`, `:is(:scope)`, and `:where(:scope)` reuse the existing bounded
logical core and the same context. `:scope` inside `:has()` is rejected in this
phase because the bounded relative evaluator has a distinct relative anchor;
it is deferred until that nested anchor can be represented and tested
explicitly. The JS65 limits of two `:has()` alternatives and two relative steps
per alternative remain unchanged.

The per-operation context adds no document-owned storage, selector-pool entry,
collection-record field, heap allocation, match-time reparsing, or result
cache. The existing canonical structural record is validated when a context
is created; document replacement clears retained collections and changes the
host generation, so serial reuse cannot revive an old scope handle. Event
callbacks and nested dispatch use separate stack-local contexts and preserve
the existing Event metadata contract.

The subset follows the API-defined scoping-root model in the
[DOM Standard](https://dom.spec.whatwg.org/#scope-match-a-selectors-string)
and the [`:scope` definition in Selectors Level 4](https://www.w3.org/TR/selectors-4/#scope-pseudo).
Navigator intentionally rejects the specialized relative-anchor case inside
`:has()` rather than reusing the outer operation's scope.

The focused native JS66 suite passed **24/24 checks** and its strict
`-Wall -Wextra -Werror -pedantic` compile passed. It covers document and
Element scope, strict query candidates, direct-child and descendant relations,
`matches()`, fixed-receiver `closest()`, functional pseudo propagation,
four-member selector lists, ordering and deduplication, independent retained
collections, interleaved roots, pseudo-name case, malformed forms, `:has()`
rejection, nested Event dispatch, and Event metadata. It also checks missing
and corrupted document-root authority, a parentless non-root record, document
replacement with serial reuse, and stale Element and collection handles. The
actual hosted fixture script passed natively, including all **26 initial
assertions** and its nested Event callback.

The memory audit measured a 16-byte call-local match context. The JS65
selector descriptor remains **1,278 bytes**, collection record **1,296
bytes**, and 128-record registry **165,888 bytes**. `HtmlElementRef` remains
**440 bytes** and `HtmlElementContentMetadata` **24 bytes**. No
`WebDocument` field or document-owned selector metadata was added, so the
document persistent-memory delta is **0 bytes**. Scope matching adds no heap
storage, cache, source pointer, or match-time reparsing.

GCC 15.2.0 MinGW `-O2 -fstack-usage` comparison against the JS65 starting
source measured `parseBoundedSelector` at 112 bytes,
`parseBoundedSelectorMember` at 176 bytes, and `parseSimpleSelectorCore` at
528 bytes both before and after JS66. The new scope-context factory is an
80-byte static frame. Matcher frames changed as follows: member match 176 to
192 bytes, selector match 96 to 112, `closest` match 176 to 208, collection
index match 128 to 176, collection count 128 to 176, and querySelector 1,488
to 1,536. These are compiler-reported per-function frames, not a whole-call
stack high-water measurement; the added work is bounded and introduces no
recursion. The measurement command was
`g++ -std=c++17 -O2 -DGXOS_BARE_METAL -iquote . -fstack-usage -c navigator_javascript/navigator_script_host.cpp`.

On the 1,024-Element fixture, 100 repeated iterations of `matches(':scope')`,
`closest(':scope')`, and scoped `querySelector(':scope > .row')` measured
**1,780 microseconds**. This is an observation without a timing threshold;
the selector matcher exposes no dedicated per-scope structural-inspection
counter.

The complete JavaScript smoke matrix passed **68/68 scripts**, with no missing
or duplicate lanes. This includes JS62R through JS62R4 and JS36–JS66. Focused
regressions passed: JS63 **8/8**, JS64 **24/24**, and JS65 **18/18**. The
JS66 lane includes the strict warnings-as-errors compile.

The production fixture is `navigator-smoke/javascript-js66.html`. Its 26
initial assertions reported **0 failures** on Navigator, and all three JS66
hosted aggregate checks passed. The nested click dispatched a second click
through a different scope root; the outer scope resumed correctly and
`target`, `currentTarget`, `eventPhase`, and `defaultPrevented` were preserved.
Hosted JS63, JS64, and JS65 fixtures also remained green; JS64 reported 37
assertions and 48 relation-pair cases, and JS65 reported 17 assertions and 32
relation-pair cases.

The final `scripts/smoke-navigator-hosted.ps1 -Build` run invoked
`build.bat` successfully from source HEAD
`579f349072cb4338d44cfe913c872d405b78b4ec` with the JS66 source changes in
the worktree. It built
`D:\dev\guideXOSServer_NAVIGATOR_JS_SUPPORT\guideXOSServer.exe`, **8,447,919
bytes**, SHA-256
`2F0F96E48DF2A9C7D00E19ECDCFC2B266277D0D985E2CA746B65D0577E658B71`.
The hosted runner launched PID **22044** from that exact path and reported the
same executable hash; no staged copy was involved. The aggregate reported
**626 passed / 7 failed / 633 total**, exactly three more passing checks than
the JS65 baseline. The only failures were the known CSS checks: 3C, 3G, 6A,
three 6B checks, and 6C. No CSS source changed and there were no new
unexplained failures. The build emitted existing warnings in unrelated
sources.

Before build and hosted qualification, the ignored/generated snapshot covered
**1,337 files / 88,371,771 bytes**. Exact backups covered **46 files / 8,857,239
bytes**: the server executable, three existing desktop state/config files,
and all 42 existing `navigator-smoke/generated` files. The original
executable backup was
`C:\Users\guideX\AppData\Local\Temp\guidexos-js66-artifacts-683a1adee315422292786c0fac07885b\backup\guideXOSServer.exe`
(8,398,274 bytes, SHA-256
`A5E439455E4D3E735DFB848EC296A3EC4B347822B21F5B1B736D51227046A971`). The
full exact-path backup list is in `mutable-backups.csv` in that audit folder.
All backed-up files are restored after qualification, and only logs created by
these JS66 hosted runs are removed or moved out of the repository. The final
ignored-artifact comparison returned **0 changed, 0 missing, and 0 extra**.

No kernel build or QEMU run was needed for this hosted Navigator selector
phase; no kernel or guest qualification is claimed. The checkout has no
`.phase` file, and none was created. No branch or worktree topology change,
stash, reset, or `git clean` was used, and nothing was pushed.

**Outcome A — Bounded `:scope` pseudo-class fully qualified.** Keep the
two-alternative/two-step `:has()` limits and the 256-byte selector input bound.
The next phase should add a distinct selector capability without widening
these bounds implicitly.

#### JS67 bounded `:scope` inside `:has()`

JS67 removes the JS66 parser's explicit rejection of the already-supported
nonfunctional `:scope` simple selector inside the existing bounded `:has()`
relative argument. No other parser feature was opened. The argument remains
limited to two alternatives, two relation steps per alternative, the 256-byte
selector input bound, and one supported pseudo-class per simple-selector
core. Nested functional pseudos inside `:has()`, nested `:has()`, excess
relations, and excess alternatives remain rejected.

The matcher keeps the two relevant identities separate. The call-local
`NavigatorScriptSelectorMatchContext` continues to carry the API operation's
original scope serial, generation, and validity. The candidate Element whose
`:has()` predicate is being evaluated remains the relative traversal anchor.
Relative relations start from that anchor, while an explicit `:scope` in
either relative step is tested against the unchanged API context. The relative
matcher passes the same context to first-step candidates, correlated
intermediates, and terminal candidates. It does not rewrite the context or
retain it in selector or collection storage.

This follows the current Selectors Level 4 definitions: relative selectors
are anchored against the `:has()` subject, and `:scope` represents the
scoping root supplied by the outer operation. The CSSWG resolved to remove
special `:scope` handling in `:has()` and relative selectors; the issue
discussion records that explicit `:scope` matches the same elements inside
and outside `:has()`. See the [current relational pseudo-class definition](https://drafts.csswg.org/selectors/#relational),
the [current `:scope` definition](https://drafts.csswg.org/selectors/#scope-pseudo),
the [DOM scope-match algorithm](https://dom.spec.whatwg.org/#scope-match-a-selectors-string),
and [CSSWG issue 6399](https://github.com/w3c/csswg-drafts/issues/6399).
Issue [7211](https://github.com/w3c/csswg-drafts/issues/7211) captures the
historical ambiguity that this behavior resolves. Thus the earlier JS66
statement that inner `:scope` was rejected records that phase's behavior and
is superseded here.

The context split gives the intended positive results through public APIs.
For `leaf.closest('.container:has(:scope)')`, the container is the relative
anchor and the original leaf is the strict descendant that matches the API
scope. `leaf.closest('.container:has(> :scope)')` also matches when the leaf
is a direct child. In a two-step selector such as
`leaf.closest('.panel:has(> .row > :scope)')`, the API-scoped leaf must be a
child of the same `.row` that satisfies the first relation; a leaf in an
unrelated branch does not match. `:scope` also works in the first relative
step, in compound selectors such as `span:scope`, and at either sibling
relation position. The four existing relation types and all 16 ordered
two-step relation pairs remain bounded and retain their JS65 meaning.

`matches()` keeps its strict-relative behavior: an Element does not match
`:has(:scope)` or `:has(> :scope)` when it is both the API scope and the
`:has()` subject, because it cannot be its own strict descendant or child.
Outer selectors continue to evaluate their left side with the original API
scope while the rightmost returned candidate stays in the existing query
domain. Thus `queryRoot.querySelector('.ancestor:has(> :scope) .target')`
can return a strict descendant target even though `.ancestor` is outside
`queryRoot`; `queryRoot` satisfies the inner scope test but is not itself a
query result. Document queries still use the authoritative document element
as scope, and strict descendant matching leaves document-level
`:has(:scope)` and `:has(> :scope)` without a result for a parentless document
root.

Selector-list matching retains the same API context for every member, keeps
structural order and deduplication, and preserves the two-alternative OR and
first-success behavior. Retained collections continue to store their existing
scope serial and reconstruct a generation-checked context when read. The
matcher uses no global scope slot, borrowed context pointer, heap-backed AST,
match-time reparsing, or result cache. The 1,278-byte selector descriptor,
1,296-byte collection record, 165,888-byte 128-record registry, 16-byte
match context, 440-byte `HtmlElementRef`, and 24-byte content record are
unchanged; the document-owned memory delta is zero.

The new native suite is `tests/navigator_javascript_js67_test.cpp`, run by
`scripts/smoke-navigator-javascript-js67.ps1`. It checks parser acceptance
and rejections, single-step and two-step scope, compound scope, document
negatives, `matches()` self-exclusion, both positive `closest()` cases,
same-intermediate correlation, unrelated-branch rejection, sibling
relations, all 16 scope-terminal relation pairs, outer query/matches
integration, strict query result domains, selector-list order/deduplication,
alternative order, independent retained collections, generation replacement
with serial reuse, nested Event dispatch and metadata, malformed structure,
fixed memory sizes, and the 1,024-Element near-capacity case. The native lane
passed **44/44 checks** and its `-Wall -Wextra -Werror -pedantic` compilation
passed. It also verifies that the HTML parser preserves the hosted fixture's
complete inline script before executing it. The 1,024-node query kept the API
scope at serial 995, evaluated 15 outer candidates and both alternatives, and measured about **19 ms** in the
optimized native run. A direct late-scope two-step evaluation inspected
2,030 structural records, took 3 parent hops, and performed 996 simple
selector checks; these are workload observations, not timing thresholds.
GCC 15.2.0 `-O2 -fstack-usage` reports 112 B for `parseBoundedSelector`,
176 B for `parseBoundedSelectorMember`, 528 B for `parseSimpleSelectorCore`,
480 B for the relative evaluator (down from 496 B), 96 B for the relative
chain wrapper, and 256 B for the shared simple matcher. The parser frames
remain unchanged, and the evaluator remains iterative and bounded.

The hosted JS67 fixture is `navigator-smoke/javascript-js67.html`. Its 34
initial assertions cover document and Element scoping, closest/matches,
correlated chains, outer qS/qSA and candidate-domain behavior, alternatives,
sibling relations, live checked/focus/attribute state in a scoped relative
chain, logical pseudo compatibility, and malformed syntax. Its nested click
adds three assertions for independently scoped selectors and stable Event
metadata. Hosted JS63–JS66 fixtures and the complete JavaScript
matrix are qualified in the JS67 run record below; no prior phase results are
removed.

The JS67 grammar still defers nested functional pseudo-classes in `:has()`,
third alternatives, third relation steps, and additional outer relation
chains. Keep those as separate bounded proposals. The JS66 limits of four
outer selector-list members and the existing scope-aware API behavior remain
unchanged.

#### JS67 final qualification record

The starting checkout was `D:\dev\guideXOSServer_NAVIGATOR_JS_SUPPORT` on
`NAVIGATOR_JAVASCRIPT_SUPPORT`, at JS66 HEAD
`9349955c88bfee277d09a5d37e0d156ab3b4ed7a` (`navigator: add JS66 bounded
scope pseudo-class`), one commit ahead and zero behind upstream, with a clean
worktree. The duplicate/stale gate found no JS67 or later implementation; the
existing `:scope` rejection inside `:has()` was present. `.phase` was absent
and its absence did not block this work.

Final native qualification passed **44/44 JS67 checks** with strict
`-std=c++17 -O2 -Wall -Wextra -Werror -pedantic` compilation. JS63, JS64,
JS65, and JS66 focused regressions passed **8/8**, **24/24**, **18/18**, and
**24/24**; JS62R through JS62R4 passed **10/10**, **4,081/4,081**,
**1,293/1,293**, and **113/113**. The complete 69-script
`smoke-navigator-javascript-*.ps1` matrix passed **69/69**, with no nonzero
script exits. The JS67 test added the HTML-parser source-integrity assertion
after the first hosted attempt exposed that a literal `<` in the fixture's
JavaScript loop condition was interpreted as HTML markup. Replacing the loop
condition with an equivalent `!==` comparison kept the fixture within the
existing HTML subset; the hosted script is now checked byte-for-byte before
execution in the native fixture lane.

The final hosted run used the fresh executable produced by
`scripts/smoke-navigator-hosted.ps1 -Build`, which invoked `build.bat` from
JS67 dirty source based on HEAD
`9349955c88bfee277d09a5d37e0d156ab3b4ed7a`. Build succeeded. The executable
path was
`D:\dev\guideXOSServer_NAVIGATOR_JS_SUPPORT\guideXOSServer.exe`, size
**8,442,463 bytes**, SHA-256
`782A271D5F135A981051609234F0C25D3407443F6ED2AB9CB9DA09A1804B3C4B`.
Hosted process PID **16004** reported that exact path and hash. The final
hosted JS67 marker was `initial:assertions=34:failures=0:malformed=true`;
after nested Event dispatch it was
`event=true:nested=true:metadata=true:assertions=37:failures=0`. The scoped
relative chain also observed checked, focused, and attribute state live.
Hosted JS63, JS64, JS65, and JS66 markers also passed, including their Event
checks.

The complete hosted aggregate reported **629 passed / 7 failed / 636 total**.
Its seven failures are the established unrelated CSS baseline: 3C, 3G, 6A,
three 6B checks, and 6C. This is three more passing checks than the JS66
aggregate (626/7/633), and there were no new unexplained failures. The initial
hosted attempt did not qualify because the HTML subset truncated the fixture
at its JavaScript `<` comparison; it was corrected, and the final run above
used the newly rebuilt, path-and-hash-verified JS67 executable.

Memory stayed at the JS66 baseline: match context **16 B**, selector
descriptor **1,278 B**, collection record **1,296 B**, 128-record registry
**165,888 B**, `HtmlElementRef` **440 B**, and content metadata **24 B**;
document persistent-memory delta was zero. GCC 15.2.0 stack-usage comparison
against JS66 showed `parseBoundedSelector` **112 B** (unchanged),
`parseBoundedSelectorMember` **176 B** (unchanged),
`parseSimpleSelectorCore` **528 B** (unchanged), the alternative parser lambda
**208 B** (up from 192 B), and the relative evaluator **480 B** (down from
496 B). The relative chain wrapper stayed at **96 B** and the shared simple
matcher at **256 B**. The 1,024-Element near-capacity workload used scope
serial 995 and 15 outer candidates; the observed optimized query workload was
**18,953 us**. Its late-scope relative evaluation inspected 2,030 structural
records, made 3 parent hops, and ran 996 simple-selector tests. These are
workload observations, not imposed timing thresholds.

An independent standards-browser comparison was unavailable: the headless
browser attempt did not return DOM results because it attached to an existing
browser session. No local-file workaround was used. The interpretation was
checked against the current Selectors Level 4, DOM scoping algorithm, and the
CSSWG resolution and issue discussion linked above.

The ignored-artifact snapshot before the hosted build covered **1,337 files /
88,371,771 bytes**. Exact backups covered **46 files / 8,857,239 bytes**:
`guideXOSServer.exe`, `desktop.state`, `display-options.cfg`,
`window-bounds.cfg`, and 42 existing files under `navigator-smoke/generated`.
The executable backup is
`C:\Users\guideX\AppData\Local\Temp\js67-evidence-6ab6abe7d35f4429818ca6844d5cf23f\guideXOSServer.exe.baseline`;
the exact mutable path/hash/backup list is
`C:\Users\guideX\AppData\Local\Temp\js67-evidence-6ab6abe7d35f4429818ca6844d5cf23f\mutable-backups.csv`.
All 46 files were restored and hash-verified; the tracked desktop state/config
files match their initially clean contents. Newly created hosted logs were
archived outside the checkout. The final ignored-artifact comparison returned
**0 changed / 0 missing / 0 extra**, with the baseline count and byte total
restored. No kernel build or QEMU run was attempted, and the unrelated
QEMU/QMP processes were left untouched.

**Outcome A — JS67 context-correct `:scope` inside `:has()` fully qualified.**
The original API scope remains distinct from the changing `:has()` relative
anchor through `closest()`, outer relational selectors, retained collections,
and nested Event dispatch. There was no branch switch, creation, rename,
worktree topology change, stash, reset, clean, upstream fetch, or push; the
current branch advanced only for this local qualification commit.

#### JS68 bounded logical pseudos inside `:has()`

JS68 allows one `:is(S)`, `:where(S)`, or `:not(S)` in either relative simple
selector core of either existing `:has()` alternative. Each `S` is exactly one
JS59–JS61 restricted, nonfunctional simple-selector core. Existing bounded tag,
ID, class, attribute, and supported state-pseudo matching is reused. A logical
pseudo consumes that core's one functional slot: lists, stacked functions,
nested `:has()`, and recursive logical arguments remain rejected. In
particular, a comma inside a logical argument is not treated as an outer
`:has()` alternative separator. The 256-byte selector limit, two alternatives,
two correlated steps, and four-member outer list remain in force.

`:is(S)` and `:where(S)` test the actual relative candidate against `S` and
have identical Boolean behavior in this matcher; specificity and cascade are
outside Navigator's JavaScript matching behavior. `:not(S)` inverts only after
the candidate's handle, generation, structural position, and inner selector
are valid. The enclosing `:has()` still requires a structurally related
candidate, so zero candidates cannot become a match. This is existential
`:has(:not(S))` behavior, distinct from `:not(:has(S))`.

Two-step selectors retain JS65 correlation: the second step is searched only
within the first step candidate's structural relationship. Logical predicates
are checked at the relative step where they appear; the first step and target
do not drift into unrelated branches. JS67's explicit API scope remains
separate from the changing `:has()` anchor, including when a logical predicate
contains `:scope`. Both alternatives still use OR semantics and stop after a
successful alternative. Invalid inner arguments return `Invalid` before any
`:not()` inversion.

The representation is fixed and nonrecursive. Each 76-byte
`NavigatorScriptHasExpressionDescriptor` has four expression-local compact
argument slots, one per possible relative core (two steps by two alternatives).
Each compact argument is 18 bytes and points into the existing selector text
buffer. The existing 24-slot relative-core pool remains unchanged; JS68 does
not allocate a selector AST, cache results, or reparse during matching. The
descriptor grows from 1,278 to 1,856 bytes, the collection record from 1,296
to 1,872 bytes, and the 128-record registry from 165,888 to 239,616 bytes;
the match context (16 B), `HtmlElementRef` (440 B), and content metadata (24 B)
are unchanged. This is bounded registry growth of 73,728 bytes, measured and
covered by static assertions.

The permanent native suite is `tests/navigator_javascript_js68_test.cpp`,
run by `scripts/smoke-navigator-javascript-js68.ps1`. It exercises all three
logical pseudos in both relative steps and alternative positions, inner-core
restrictions, existential negation and zero candidates, stale/reused handles,
structural corruption, correlated relations, all 16 ordered pairs of the four
relations, `:scope` API propagation, query and collection paths, live state,
Event reentrancy and metadata, alternative short-circuiting, parser bounds,
and the 1,024-Element near-capacity workload. The hosted fixture is
`navigator-smoke/javascript-js68.html`; its initial and nested Event assertions
are integrated into `server.cpp`. Final native, matrix, production, hosted,
memory, stack, and artifact qualification evidence is recorded in the JS68
qualification entry below.

#### JS68 final qualification record

The qualification started in `D:\dev\guideXOSServer_NAVIGATOR_JS_SUPPORT`
on branch `NAVIGATOR_JAVASCRIPT_SUPPORT`, at clean HEAD
`abe22a61e0a5bdff2552de6b119e8adfb0e6568f` (`navigator: add JS67 scoped
relative selector support`). The live upstream divergence was **0 ahead / 0
behind**; this live value was used instead of the prompt's expected 2 ahead /
0 behind. No JS68 or later implementation was present. `.phase` was absent,
which is normal for this checkout, and was not created. No branch or worktree
topology change, fetch, reset, stash, clean, or push was made.

The grammar is one `:is(S)`, `:where(S)`, or `:not(S)` in each relative
simple-selector core, with at most two steps and two alternatives per `:has()`.
`S` is one trimmed, nonfunctional JS59–JS61 restricted core. Leading and
trailing ASCII whitespace is accepted; whitespace-separated compounds,
top-level commas, multiple functions in a core, recursive logical nesting,
nested `:has()`, and logical argument lists are rejected. Existing tag, ID,
class, attribute, `:scope`, and supported live state predicates remain
available inside `S`. No attribute operators or unrelated selector primitives
were added. The outer selector remains limited to 256 bytes and four list
members.

`:is(S)` and `:where(S)` have the same Boolean result for the actual relative
candidate. `:not(S)` first requires a valid, generation-matched candidate that
satisfies the structural relationship and its nonlogical compound conditions;
an invalid inner result propagates as `Invalid` and is never inverted. The
enclosing `:has()` still requires an existing related candidate, so zero
related Elements cannot satisfy `:not()`. This implements existential
`:has(:not(S))`; it does not implement `:not(:has(S))`. The second step stays
correlated to its first-step intermediate, and `:scope` continues to use the
outer API scope independently of the changing relative anchor. The two
alternatives retain OR behavior and first-success short-circuiting.

Each 76-byte `NavigatorScriptHasExpressionDescriptor` stores four fixed
18-byte compact arguments, corresponding to each possible core in the two
steps and two alternatives. There are at most eight `:has()` expression slots
per selector (four outer members times two sides), hence at most 32
expression-local logical argument slots. The existing 24-entry compact
relative-core pool is unchanged. The arguments are parsed once into fixed
storage; there is no recursive AST, match-time reparsing, cache, or dynamic
allocation. The 256-byte input, two-alternative, two-step, and four-member
bounds are enforced before matching. Failed parsing leaves an empty selector
descriptor.

Measured memory changed as follows: match context **16 B** (unchanged),
selector descriptor **1,278 -> 1,856 B** (+578 B), collection record
**1,296 -> 1,872 B** (+576 B), and 128-record registry **165,888 -> 239,616
B** (+73,728 B). `HtmlElementRef` stayed **440 B**, content metadata stayed
**24 B**, and the relative-core pool capacity stayed 24. The per-expression
argument slots account for the bounded registry growth; compile-time size
assertions cover the resulting records. The extra descriptor storage does
not change document content records.

GCC 15.2.0 `-O2 -fstack-usage` measured `parseBoundedSelector` **112 B**,
`parseBoundedSelectorMember` **176 B**, `parseSimpleSelectorCore` **336 B**,
`parseHasPseudo` **592 B** (its alternative parser lambda is **208 B**), the
relative evaluator **496 B**, the chain wrapper **112 B**, and the shared
simple matcher **352 B**. Public `querySelectorAll` and
`getElementsBySimpleSelector` frames measured **2,064 B** and **2,176 B**.
The reset reconstructs the fixed descriptor in place, avoiding the 1,856-byte
temporary that the ordinary assignment form introduced. The matcher remains
iterative and fixed-depth.

The 1,024-Element native workload used scope serial **995** and 15 outer
candidates; its optimized query workload took **31,098 us**. Diagnostic
counters below compare early exits and late/full-false paths. Each row is one
relative evaluation; `early exits` records successful short-circuit exits.

| Workload | Records | Parent hops | Serial/index resolutions | Simple tests | Logical tests | Early exits |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Correlated two-step, late `:scope` | 2,030 | 3 | 8 | 2,988 | 996 | 1 |
| Early `:is()` | 1,043 | 7 | 12 | 2 | 1 | 1 |
| Late `:where()` | 5,008 | 1,494 | 2,986 | 1,984 | 992 | 1 |
| Early existential `:not()` | 1,048 | 9 | 16 | 4 | 2 | 1 |
| Full-false `:not()` | 5,183 | 1,567 | 3,132 | 2,042 | 1,021 | 0 |
| `:not()` with zero matching targets | 11,281 | 4,630 | 8,237 | 0 | 0 | 0 |

`tests/navigator_javascript_js68_test.cpp` passed **69/69** checks. Its
`-Wall -Wextra -Werror -pedantic` native compilation and strict production
parser/adapter/runtime syntax compilation passed. The suite checks all three
logical pseudos, their positions in both relative steps and alternatives,
quoted commas and spaces in attributes, inner-list and recursive-depth
rejection, existential negation, no-candidate false results, invalid and stale
generation contexts, reused serials, invalid structural indices and parent or
sibling links, parser limits and failure clearing, all 16 ordered two-relation
pairs, API scope, collections, live checked/focus/attribute state, Event
reentrancy and metadata, and first-alternative short-circuiting. It also
verifies that the HTML parser preserves the hosted inline script before
execution.

The focused historical gates passed: JS59 **621/621**, JS60 **1,377/1,377**,
JS61 **2,077/2,077**, JS62 **15/15**, JS62R **10/10**, JS62R2 **4,081/4,081**,
JS62R3 **1,293/1,293**, JS62R4 **113/113**, JS63 **8/8**, JS64 **24/24**,
JS65 **18/18**, JS66 **24/24**, and JS67 **44/44**. The updated legacy
rejection cases still reject recursive functional nesting, nested `:has()`,
and inner logical lists while permitting the new JS68 one-level form. JS63,
JS64, JS65, and JS67 hosted fixtures remain in the aggregate and passed; their
malformed-input lists were adjusted to express the still-unsupported grammar.
The complete `smoke-navigator-javascript-*.ps1` matrix passed **70/70**,
including the lexer, parser, and runtime smoke lanes.

The production build ran through
`scripts/smoke-navigator-hosted.ps1 -Build`, which invoked `build.bat` on the
JS68 worktree based on the starting JS67 HEAD. Build succeeded. The fresh
executable was `D:\dev\guideXOSServer_NAVIGATOR_JS_SUPPORT\guideXOSServer.exe`,
**8,462,812 bytes**, SHA-256
`73554A502C596FD77A02218222590F4E26B6C663F661DF25C4F569DE51E0D179`. Hosted
process PID **24684** reported the same executable path and SHA-256. The
JS68 initial marker was `JS68:initial:assertions=39:failures=0:malformed=true`;
after nested Event dispatch it was
`JS68:event=true:nested=true:metadata=true:assertions=42:failures=0`. JS63,
JS64, JS65, JS66, and JS67 hosted markers and Event checks also passed.

The full hosted aggregate recorded **632 passed / 7 failed / 639 total**.
The seven failures are the established unrelated CSS baseline: 3C, 3G, 6A,
three 6B checks, and 6C. There were no JavaScript fixture failures. The
hosted wrapper returned nonzero because it marks any aggregate failure as
`NAVIGATOR_SMOKE_RESULT: FAIL`; under the phase's stated baseline rule, this
same seven-CSS set does not block JS68 Outcome A.

Before building, the ignored-artifact snapshot contained **1,337 files /
88,371,771 bytes**. Exact backups covered **46 files / 8,857,239 bytes**:
`guideXOSServer.exe`, desktop state/configuration, and all 42 existing files
under `navigator-smoke/generated`. All 46 were restored and SHA-256 checked.
The new executable was archived outside the checkout at
`C:\Users\guideX\AppData\Local\Temp\js68-evidence-20261011\guideXOSServer-js68-built.exe`;
the six hosted logs were archived outside the checkout. The final ignored
manifest returned **0 changed / 0 missing / 0 extra**, with the original file
and byte totals. No kernel build or QEMU run was needed or attempted.

**Outcome A — JS68 bounded logical pseudos inside `:has()` fully qualified.**
`:is(S)`, `:where(S)`, and `:not(S)` work in the bounded relative grammar,
including correlated chains and existing API scope context. Recursive logical
forms, nested `:has()`, inner selector lists, third steps, and third
alternatives remain deferred. The seven historical CSS failures are unchanged.

#### JS69 bounded logical selector lists inside `:has()`

JS69 extends the JS68 functional predicate in each supported relative simple
core from one restricted argument to one or two comma-separated arguments:

```css
:has(:is(.warning, .error))
:has(> :where(.ready, .selected))
:has(.row > :not(.disabled, .hidden))
```

The grammar is limited to `:is(A)`, `:is(A, B)`, `:where(A)`,
`:where(A, B)`, `:not(A)`, and `:not(A, B)` inside the already bounded
`:has()` alternatives. Each member is independently parsed with the existing
restricted nonfunctional core grammar; list members cannot contain relation
chains or another functional selector. The 256-byte input bound, four outer
selector-list members, two `:has()` alternatives, two relative steps, and one
functional nesting level remain unchanged. Empty entries, a third logical
member, nested functions, nested `:has()`, malformed grouping, and unsupported
relations remain rejected.

The parser assigns commas at three levels. The outer selector parser splits
only at top level while tracking functional depth, attributes, and quoted
attribute strings. `parseHasPseudo` then splits its one or two relative
alternatives only outside an attribute or functional argument. The logical
core parser splits at most one comma outside attributes and quoted values;
`findLogicalArgumentClose` rejects nested parentheses and malformed closing
brackets before forgiving member handling. Empty and excess entries reject
the whole selector. Thus an outer list, a `:has()` alternative list, and an
inner logical list can coexist without a naïve comma split.

`:is(A, B)` and `:where(A, B)` match when either valid member matches and
short-circuit on the first match. Within this supported subset their
JavaScript Boolean behavior is identical; CSS specificity is unchanged and
outside scope. These two functions use forgiving handling only for an
individually invalid member whose boundary is safe: that member is marked
invalid and ignored, while the other member can still match. If both are
invalid, the predicate matches no candidate. Empty entries, excess members,
unbalanced brackets or quotes, nested functions, and other structural syntax
errors reject the selector rather than being discarded. `:not(A, B)` is
strict: either invalid member rejects the selector, and a valid candidate
matches only if neither member matches (`NOT (A OR B)`).

Candidate and relation validation still precede logical evaluation and
negation. The candidate must have a valid current handle and generation, pass
the enclosing relative relationship, and satisfy the nonlogical compound
conditions before `:not()` can return true. The surrounding `:has()` remains
existential, so no related candidate is always false, including for
`:has(:not(...))`. The second step remains correlated to its first-step
intermediate. `:scope` inside either logical list member keeps the original
public API scope instead of rebinding to the subject or visited candidate.
The two relative alternatives retain isolated OR evaluation and successful
short-circuiting.

Logical storage remains fixed and nonrecursive. Each 116-byte
`NavigatorScriptHasExpressionDescriptor` contains eight 14-byte compact
argument records: two members for each of its four possible relative cores.
One selector descriptor has eight expression records across four outer
members and two sides, so the maximum is 64 fixed compact logical arguments.
The existing 24-entry relative-core pool is unchanged, and matching expands
arguments into bounded core storage rather than allocating an AST, reparsing,
or caching results. Compared with JS68, the selector descriptor grows from
1,856 to 2,176 bytes (+320), a selector collection record from 1,872 to 2,192
bytes (+320), and the 128-record registry from 239,616 to 280,576 bytes
(+40,960). The 16-byte match context, 32-byte core, 68-byte simple selector,
440-byte `HtmlElementRef`, and 24-byte content metadata remain unchanged.

The native test harness now owns its fixed-size `NavigatorScriptHostAdapter`
through a `unique_ptr`. This keeps the approximately 280 KiB adapter record
array off test-call stacks that previously overflowed in an older JS26 test.
This test-only ownership change does not add a production allocation: the
production adapter remains statically allocated. GCC 15.2.0 with `-O2
-fstack-usage` reports a 944-byte `parseSimpleSelectorCore` frame (with the
bounded `:has()` parse inlined), 112 bytes for `parseBoundedSelector`, 528
bytes for the relative element evaluator, 128 bytes for the correlated-chain
evaluator, and 240 bytes for the simple-element wrapper. The
`parseHasPseudo` alternative-parsing lambda reports 224 bytes. These are
per-function frame measurements; the grammar still has a fixed maximum parse
depth, and matching is iterative.

The JS69 native suite is `tests/navigator_javascript_js69_test.cpp`, invoked
by `scripts/smoke-navigator-javascript-js69.ps1`. It covers one-member
backward compatibility, OR/NONE results, first and second member evaluation,
forgiving and strict invalid members, empty/excess/malformed/nested syntax,
all three comma levels, quoted commas, all four relations, two-step
correlation, alternatives, scope, public query APIs, ordering and
deduplication, generation replacement, live state, nested Event dispatch,
fixed-pool bounds, parser atomicity, and 1,024-Element near-capacity counts.
The hosted fixture is `navigator-smoke/javascript-js69.html`; `server.cpp`
checks its initial and nested Event result markers against the production
Navigator executable.

#### JS69 final qualification record

The qualification started in `D:\dev\guideXOSServer_NAVIGATOR_JS_SUPPORT`
on branch `NAVIGATOR_JAVASCRIPT_SUPPORT`, at clean HEAD
`6fc3963711cad5b8fc499eafb84b3447ab8d804c` (`navigator: add JS68 bounded
logical pseudos inside has`). The live upstream divergence was **0 ahead / 0
behind** at start; the prompt's expected divergence differed, so the live
repository state was used. No JS69 or later implementation was present.
`.phase` was absent as expected for this checkout and was not created. The
branch and worktree topology were preserved; no fetch, reset, clean, stash, or
push was used.

The parser accepts one or two restricted members for each of `:is()`,
`:where()`, and `:not()` within either relative step and either `:has()`
alternative. One-member JS68 selectors retain their behavior. The native
suite proves OR behavior for `:is()` and `:where()`, NONE behavior for
`:not()`, first and second member matches, duplicates, order reversal,
forgiving invalid members for `:is()`/`:where()`, and strict invalid-member
rejection for `:not()`. Both invalid forgiving members produce no match.
Candidate validity, including generation, and structural eligibility are
validated before negation. `:has(:not(...))` remains false when no related
candidate exists.

The delimiter scanners keep outer selector commas, relative-alternative
commas, and logical-list commas at their owning grammar level, respecting
attributes and quoted commas. Empty members, third members, malformed
quotes/brackets/parentheses, unmatched closing brackets, nested functions,
nested `:has()`, and relation chains within a logical member reject the
selector. A safely delimited unsupported simple member can be discarded only
by forgiving `:is()`/`:where()` handling. Two-step candidates remain
correlated; `:scope` uses the original API root; alternatives remain
independent. Public query, collection ordering and identity, deduplication,
live attributes/checked/focus, generation replacement, and nested Event
dispatch passed.

The 14-byte compact argument format stores eight logical slots in each
116-byte `NavigatorScriptHasExpressionDescriptor`; those slots cover both
members for the four possible relative cores. Across the selector's eight
expression records, the maximum is 64 compact arguments. The 24-entry
relative-core pool is unchanged. JS68-to-JS69 measured sizes are: descriptor
**1,856 -> 2,176 B**, collection record **1,872 -> 2,192 B**, and the
128-record registry **239,616 -> 280,576 B**. The match context (**16 B**),
simple-selector core (**32 B**), simple-selector descriptor (**68 B**),
`HtmlElementRef` (**440 B**), and content metadata (**24 B**) are unchanged.

GCC 15.2.0 `-O2 -fstack-usage` measured these per-function frames: bounded
selector member **192 B** (JS68: 176 B), bounded selector **112 B** (112 B),
`parseSimpleSelectorCore` **944 B** (336 B; the bounded `:has()` parse is
inlined), the `parseHasPseudo` alternative-parser lambda **224 B** (208 B),
relative element evaluation **528 B** (496 B), correlated-chain evaluation
**128 B** (112 B), `querySelectorAll` **2,384 B** (2,064 B), and
`getElementsBySimpleSelector` **2,496 B** (2,176 B). The shared simple-match
lambda remained **352 B**. These are compiler per-function stack reports;
the selector parse depth remains fixed and relative matching is iterative.
The native test harness holds its fixed adapter through `unique_ptr` so its
roughly 280 KiB collection array does not occupy test-call stack space. This
test-only change adds no production allocation: the production adapter stays
statically allocated.

Across two optimized 1,024-Element query runs, measured workload time ranged
from **116,796 to 200,904 us** under concurrent build load; there is no timing
threshold. The deterministic late two-step `:scope` case inspected 2,030
records with 3 parent hops, 8 serial/index resolutions, 3,984 simple tests,
1,992 logical tests, and one early exit. A late successful `:is()` case
inspected 5,008 records with 1,494 parent hops and 2,986 resolutions. A
full-false `:not()` case inspected 5,183 records; the zero-target case
performed no logical or simple predicate tests and returned false.

`tests/navigator_javascript_js69_test.cpp` passed **72/72** with strict
`-Wall -Wextra -Werror -pedantic` compilation; the production parser,
adapter, and runtime strict syntax build passed. Focused historical results
were JS63 **8/8**, JS64 **24/24**, JS65 **18/18**, JS66 **24/24**, JS67
**44/44**, and JS68 **69/69**. The refreshed JavaScript matrix passed
**71/71 scripts**, including JS59–JS69, the JS62R hardening lanes, and the
lexer, parser, and runtime lanes. The first hosted aggregate exposed a stale
JS64 HTML negative list that still rejected two-member logical selectors. It
was replaced with three-member and nested-function rejection cases; the
rerun's JS64 fixture and Event markers passed.

`build.bat` succeeded from the JS69 source state. The fresh executable was
`D:\dev\guideXOSServer_NAVIGATOR_JS_SUPPORT\guideXOSServer.exe`, **8,469,180
bytes**, SHA-256
`A35DEE4B1EF9FE1449A572BE22385F878AE2E4DB04683CF28C2A048A2D1254A8`. Hosted
process PID **7992** reported the same executable path and SHA-256. The JS69
initial marker was `JS69:initial:assertions=74:failures=0:malformed=true`; the
nested Event marker was
`JS69:event=true:nested=true:metadata=true:assertions=77:failures=0`.

The final hosted aggregate recorded **635 passed / 7 failed / 642 total**.
There were no JavaScript failures. The seven failures were the existing CSS
baseline: CSS 3C, CSS 3G, CSS 6A, three CSS 6B checks, and CSS 6C. No CSS code
was changed.

The starting ignored-artifact manifest contained **1,337 files / 88,371,771
bytes**. Exact backups covered **46 mutable files / 8,857,239 bytes**: the
existing executable, three desktop state/configuration files, and all 42
existing `navigator-smoke/generated` files. The fresh executable was archived
outside the checkout; logs from both hosted runs were archived outside the
checkout, and only those 12 run-created log files were removed. All 46
baseline files were restored and SHA-256 checked. The final ignored manifest
was **1,337 files / 88,371,771 bytes**, with **0 changed / 0 missing / 0
extra**. The hosted smoke's downloads snapshot was restored by its wrapper.
No kernel build or QEMU run was attempted because neither is required for
hosted Navigator qualification.

**Outcome A — JS69 bounded logical selector lists inside `:has()` fully
qualified.** Two-member Boolean behavior, forgiving versus strict handling,
candidate validation, comma ownership, correlation, scope, generation safety,
public APIs, Event reentrancy, bounded memory, strict native compilation,
historical regressions, fresh production provenance, hosted behavior, the CSS
baseline, and exact artifact restoration all passed their stated gates.

Recommended next bounded capability: support one parsed `:nth-child(An+B)`
predicate on a `:has()` relative core, without an `of <selector-list>` clause,
using a compact fixed representation and explicit stack and pool measurements.

## JS70 selector descriptor storage optimization

JS70 audits and compacts the fixed selector storage introduced through JS69.
The task began on `NAVIGATOR_JAVASCRIPT_SUPPORT` at
`40c72a5a3724bb4a2b971172c4f965a0ff3b2afa`; the live branch had no commits
ahead of or behind its configured upstream. The worktree was clean. No
`.phase` file existed, as expected for this subsystem. No later JS70 work was
present in history or the checkout. Measurements below use GCC 15.2.0 on
64-bit Windows with C++17; layout diagnostics and compile-time assertions
confirm the reported values.

### Initial layout and storage ownership

Before editing, the JS69 descriptor had this layout:

| Structure | Size / alignment | Relevant layout |
|---|---:|---|
| Restricted selector core | 32 B / 2 | `valid` 0, `universal` 1, tag offset/length 2/4, id 6/1, class count 8, eight class ranges 9–24, attribute fields 25–30, pseudo 31 |
| Simple selector | 68 B / 2 | 32-byte core, `nthA` at 32, `nthB` at 34, nested core at 36 |
| Compact relative core | 18 B / 2 | validity and universal flags plus tag/id/class/attribute ranges and state pseudo |
| Compact logical argument | 14 B / 2 | flags, tag range, id, class block, attribute ranges, separate pseudo byte |
| `:has()` expression | 116 B / 2 | four relation/index bytes and eight 14-byte logical slots |
| Outer selector member | 138 B / 2 | relation at 0; simple selectors at 2 and 70 |
| Selector descriptor | 2,176 B / 2 | 256-byte text, four members at 260, 24 relative cores at 812, eight expressions at 1,246 |
| Collection record | 2,192 B / 8 | active at 0, scope at 8, descriptor at 16 |
| 128-record collection array | 280,576 B | 128 fixed collection records |
| Scope context | 16 B / 8 | scope serial, generation, validity |
| `HtmlElementRef` | 440 B / 8 | unchanged document element representation |
| Content metadata | 24 B / 8 | unchanged retained-content metadata |

The simple-selector core is always present. Its nth formula fields and nested
logical core are present in every simple selector even though the pseudo kind
selects only one of those representations. Every collection record owns one
complete selector descriptor and its original scope serial. The selector
text buffer is shared by offsets inside that descriptor; no persistent field
borrows a parse-buffer pointer. Alignment in the nested selector descriptor
and its collection record contributes to the final sizes, so all savings were
checked with compiler offsets and `sizeof`, not inferred from field totals.

### Relative-core pool capacity

Each of four outer members can contain two eligible simple-selector
`:has()` owners, for eight expression records. The first step of the first
alternative is stored inline in that owner's simple-selector payload. Each
expression can additionally need exactly three pool slots: the first
alternative's second step, the second alternative's first step, and that
alternative's second step. Thus `8 * 3 = 24` simultaneously live pool slots
are reachable under the existing limits. No slot can be shared between
exclusive alternatives because both alternatives remain part of one valid
descriptor. The 24-entry bound therefore was retained.

The JS70 native capacity fixture parses four outer members with two
two-step `:has()` expressions each, then repeats the parse and query. It
asserts four outer members, eight expressions, all 24 owned pool entries, and
no match-result corruption. A second descriptor fills all eight logical
argument slots of one expression across its four relative cores. A separate
selector of exactly 256 bytes is accepted; the same form at 257 bytes is
rejected. The parser publishes pool indices only after complete core
compaction. Indices are checked against both the descriptor's published count
and the fixed array size; a failed parse cannot leave a published partial
descriptor. Copies keep their own text, indices, pool, expressions, and scope
in the owning record.

### Compaction choices

The implementation keeps every existing selector and collection limit and
uses only fixed, descriptor-owned byte storage:

* **Tagged simple-selector payload:** the `statePseudo` discriminator already
  makes `:has()`, logical pseudos, and nth pseudos mutually exclusive. The
  former 32-byte nested core and two 16-bit nth values are replaced by one
  17-byte payload. `:has()` uses 16 bytes for its compact first core and one
  expression index; logical pseudos use 13 bytes; nth pseudos use four bytes.
  The active bytes are copied with `memcpy`; there is no union, pointer, or
  active-lifetime ambiguity.
* **Relative-core validity:** a pool entry becomes visible only after a valid
  compact core has been appended. The descriptor's bounded published count
  therefore supplies the validity/ownership check. Removing the repeated
  `valid` byte and ordering fields to avoid alignment gaps reduces each core
  from 18 to 16 bytes.
* **Logical arguments:** each 14-byte slot becomes 13 bytes. The pseudo kind
  shares the flag byte, the 16-bit tag length is encoded as two explicit
  bytes, attribute presence follows a nonempty attribute-name length, and a
  universal argument is reconstructed when it has no other predicate. The
  sole-`*` case is preserved explicitly by that reconstruction rule.
* **Expression metadata:** relation kinds, alternative count, and validity
  remain in the existing four flag/index bytes. Eight compact 13-byte slots
  make each expression 108 bytes instead of 116.
* **Outer members and collection records:** each fixed outer member embeds
  two simple-selector descriptors, so its size falls from 138 to 102 bytes.
  Collection records continue to own their descriptor and scope. Sharing
  records across collection objects would weaken identity and lifetime
  guarantees, so the 128-record fixed registry remains in place.
* **Alignment and scratch:** fields are reordered without packed structs.
  Relative pool entries expand into the established restricted core on the
  stack for the existing matcher. This keeps the matching grammar and
  evaluator unchanged and introduces no heap storage, cache, or reparse.

The outer member array remains four entries; selector text remains 256 bytes;
the expression array remains eight entries; the relative-core array remains
24 entries; each expression keeps eight logical slots; the collection array
remains 128 records; and the structural document limit remains 1,024
Elements. Public JavaScript API signatures and accepted selector syntax did
not change. All compact structures remain trivially copyable and contain no
internal pointers.

| Structure | JS69 | JS70 | Saved |
|---|---:|---:|---:|
| Simple selector | 68 B | 50 B | 18 B |
| Compact relative core | 18 B | 16 B | 2 B |
| Compact logical argument | 14 B | 13 B | 1 B |
| `:has()` expression | 116 B | 108 B | 8 B |
| Outer selector member | 138 B | 102 B | 36 B |
| Selector descriptor | 2,176 B | 1,918 B | 258 B |
| Collection record | 2,192 B | 1,936 B | 256 B |
| 128-record registry | 280,576 B | 247,808 B | 32,768 B |

The field ordering and compact encodings are checked by static assertions
and the native layout diagnostic. The memory result is a 1,918-byte selector
descriptor and a 1,936-byte collection record. The record's eight-byte
alignment rounds the 258-byte descriptor decrease to a 256-byte per-record
decrease. The resulting fixed registry is 247,808 bytes, a 32,768-byte
reduction (11.6788%) from JS69. The standalone descriptor reduction is 258
bytes (11.8566%). Scope context, base simple-selector core, `HtmlElementRef`,
and content metadata retain their sizes.

### Native verification and JS69 parity

`tests/navigator_javascript_js70_test.cpp` keeps the JS69 Boolean, parsing,
query, `:scope`, correlated two-step, invalid-candidate, generation, and Event
checks, and adds storage-specific coverage. It verifies type copyability,
descriptor and registry sizes, the 24-slot/8-expression maximum, the eight
logical slots in one expression, 256-byte acceptance and 257-byte rejection,
failed-selector rejection, repeated descriptor queries, and 128 collection
records with the same bounded 129th-record failure. It also checks empty,
disabled-only, hidden-only, and valid candidates under two-member `:not()`,
out-of-range serial and parent indices, document replacement and serial reuse,
scope ownership in retained collections, and nested Event dispatch.

The JS70 native executable passed **77/77** checks with
`-Wall -Wextra -Werror -pedantic`; the production parser/adapter/runtime
passed the corresponding strict syntax build. Its deterministic 1,024-Element
operation counters match the JS69 baseline counters for every retained
near-capacity logical case, including the 2,030-record correlated late-scope
query, the 5,008-record late `:is()` match, and the 5,183-record full-false
`:not()` case. The zero-target `:not()` case still performs no logical or
simple-predicate tests. The JS70 native suite executes the hosted fixture's
script and validates its assertion markers before production hosting.

Focused historical suites passed JS64 **24/24**, JS65 **18/18**, JS66
**24/24**, JS67 **44/44**, JS68 **69/69**, and JS69 **72/72**. Earlier JS63
coverage remains **8/8**. The changed historical tests only update
compile-time storage expectations affected by the new layout; selector
results and grammar expectations are retained. JS69's native test ran at the
original 2,176-byte descriptor and again after compaction, with the same
72/72 results and the same operation counters, providing a direct parity
check against the starting source.

### Controlled performance and stack comparison

The same C++ benchmark source was compiled once against the captured JS69
header/adapter and once against JS70. Both used GCC 15.2.0, C++17, `-O2`, the
same warning flags, public query APIs, identical generated 1,024-Element
fixtures, 20 repeated queries per sample, and seven samples per workload. The
128-record saturation case used 128 selector queries per sample; the nested
Event case used 40 dispatches. The runs were serial, with no competing
compiler processes. Timings include script execution and query work, so they
compare the same harness end to end. Native diagnostic counters were
collected separately with the same test-only instrumentation on JS69 and
JS70; every recorded operation count matched.

| Workload | JS69 median (range) µs | JS70 median (range) µs | Median change |
|---|---:|---:|---:|
| Simple tag | 35 (30–152) | 40 (32–118) | +14.3% |
| Simple class | 4,677 (4,600–4,799) | 4,464 (4,405–4,972) | −4.6% |
| Four-member outer list | 19,652 (19,324–21,714) | 19,506 (18,313–25,340) | −0.7% |
| Single-step `:has()` | 7,933 (7,856–8,582) | 4,403 (4,396–4,488) | −44.5% |
| Two-alternative `:has()` | 17,068 (11,849–17,621) | 9,157 (9,002–9,204) | −46.3% |
| Two-step chain | 99 (84–142) | 145 (113–170) | +46.5% |
| Explicit `:scope` | 4,773 (4,716–5,834) | 4,539 (4,481–4,914) | −4.9% |
| `:scope` in `:has()` | 14,710 (14,216–15,095) | 13,034 (12,995–13,501) | −11.4% |
| Nested logical predicate | 14,618 (13,789–20,757) | 12,920 (12,759–13,261) | −11.6% |
| Two-member `:not()` | 117 (93–192) | 138 (95–268) | +17.9% |
| Large `querySelectorAll()` | 5,056 (4,901–5,419) | 4,740 (4,641–5,037) | −6.2% |
| 1,024-Element false case | 5,328 (4,969–6,293) | 4,687 (4,637–4,891) | −12.0% |
| 1,024-Element late success | 4,496 (4,391–4,993) | 4,330 (4,308–4,655) | −3.7% |
| 128-collection saturation | 500 (490–538) | 502 (498–568) | +0.4% |
| Nested Event dispatch | 294 (285–311) | 295 (294–316) | +0.3% |

The single-step and two-alternative `:has()` medians improved by 44.5% and
46.3%; their observed ranges do not overlap. The 1,024-Element false case
improved by 12.0%, and other large-query and scope cases were lower by
0.7–11.6%. The simple-tag, two-step, two-member `:not()`, collection, and
Event timings are under 200 microseconds or have overlapping ranges and are
treated as noise-limited, not regressions. There is no timing threshold.
Together with identical operation counters and no sustained slow case, the
measured tradeoff is acceptable for this fixed-memory reduction.

GCC `-O2 -fstack-usage` reports these relevant frames:

| Function/frame | JS69 | JS70 | Change |
|---|---:|---:|---:|
| `parseBoundedSelectorMember` | 192 B | 144 B | −48 B |
| `parseBoundedSelector` | 112 B | 112 B | unchanged |
| `parseSimpleSelectorCore` | 944 B | no separate frame | inlined by optimizer |
| `:has()` alternative-parser lambda | 224 B | 224 B | unchanged |
| Relative element evaluator | 544 B | 528 B | −16 B |
| Correlated-chain evaluator | 144 B | 128 B | −16 B |
| Simple-selector matcher | 240 B | 288 B | +48 B |
| Simple-match predicate lambda | 400 B | 368 B | −32 B |
| `querySelectorAll` | 2,384 B | 2,128 B | −256 B |
| `getElementsBySimpleSelector` | 2,496 B | 2,240 B | −256 B |

The parser has no recursive selector descent; relative matching remains
iterative. The simple-selector function has a slightly larger reported frame
for lazy compact decoding, while its inner predicate lambda and large
collection-query frames are smaller. The no-frame parser entry is omitted by
the compiler after inlining, not treated as zero stack for its callers. The
standalone optimized adapter object changed from **249,540 B** to **249,668
B** (+128 B, about 0.05%); code footprint is effectively unchanged while the
per-adapter collection storage falls by 32 KiB.

### Full matrix, production build, and hosted qualification

The complete Navigator JavaScript smoke matrix finished **72/72** after
rerunning the JS70 lane. All historical JavaScript and DOM lanes passed,
including JS59–JS61, JS62 and JS62R–JS62R4, JS63–JS69, and the lexer, parser,
and runtime lanes. The first sweep reported one JS70 lane failure because a
test-source quote was removed while trimming trailing whitespace; that line
was fixed, and the JS70 strict runner then passed 77/77. No historical lane
failed.

`build.bat` completed successfully from the JS70 source state. The production
executable was
`D:\dev\guideXOSServer_NAVIGATOR_JS_SUPPORT\guideXOSServer.exe`, **8,475,734
bytes**, SHA-256
`912A4E5F699976476A2BEBAA9A9881F08FE6ED0C21988BD2A849884FF0EBBEC9`. The
hosted runner launched PID **19688** from that same path and verified the
process image had the same SHA-256.

The JS69 qualification records its fresh `build.bat` executable at **8,469,180
bytes**, SHA-256
`A35DEE4B1EF9FE1449A572BE22385F878AE2E4DB04683CF28C2A048A2D1254A8`. The
JS70 server image is 6,554 bytes larger (0.08%), including the new hosted
fixture integration. The focused adapter object changed by only 128 bytes.

The fresh hosted run passed the JS70 fixture: the initial marker was
`JS70:initial:assertions=76:failures=0:malformed=true`; after nested Event
dispatch it was
`JS70:event=true:nested=true:metadata=true:assertions=79:failures=0`. JS63,
JS64, JS65, JS66, JS67, JS68, and JS69 hosted initial/Event markers also
passed.

The hosted aggregate recorded **638 passed / 7 failed / 645 total**, compared
with the JS69 baseline of **635 passed / 7 failed / 642 total**. JS70 added
three passing hosted checks and no new failures. The seven failures remain
the known CSS baseline: CSS 3C, CSS 3G, CSS 6A, three CSS 6B checks, and CSS
6C. No CSS implementation or fixture was changed for JS70. The hosted runner
therefore printed `NAVIGATOR_SMOKE_RESULT: FAIL` for those same seven known
CSS checks; the Navigator JavaScript fixtures, including JS70, passed.

### Artifact restoration and outcome

Before the build, the ignored/generated baseline contained **1,337 files /
88,371,771 bytes**. Exact byte backups covered the existing executable and
all 42 existing `navigator-smoke/generated` files. The pre-existing
executable was **8,398,274 bytes**, SHA-256
`A5E439455E4D3E735DFB848EC296A3EC4B347822B21F5B1B736D51227046A971`; the
fresh JS70 executable was copied outside the checkout before hosting. The
hosted runner's downloads snapshot was restored by its wrapper. Six newly
created hosted logs were archived outside the checkout and removed; all 1,019
pre-existing logs remained byte-identical. The executable and all 42
generated files were restored from their exact backups and SHA-256 checked.
The final ignored manifest again contained **1,337 files / 88,371,771 bytes**
with **0 changed / 0 missing / 0 extra**.

No kernel build or QEMU run was needed for this hosted Navigator phase.
**Outcome A — JS70 selector storage optimization fully qualified.** The
registry is measurably smaller; selector syntax and all parser, pool,
collection, generation, scope, and structural capacities remain unchanged;
the JS69 result corpus and operation counts match; native, strict, matrix,
fresh-build, hosted JS70, and artifact-restoration gates passed. The seven
hosted CSS failures are unchanged from JS69.

One remaining fixed component of the registry is the 256-byte selector text
buffer in each collection record. A JS71 audit could examine safe
bounded storage for repeated selector text while retaining 128 independently
owned collection descriptors and all current lifetimes. Any shared fixed
pool would need explicit slot ownership, reuse, generation, and reentrancy
proof before replacing the current self-contained records.

## JS71 bounded filtered positional selectors

JS71 adds the bounded Selectors Level 4 forms
`:nth-child(An+B of S)` and `:nth-last-child(An+B of S)`. The formula ranks
only the sibling Elements matching `S`; the candidate must match `S` as well.
For example, `:nth-child(2 of .selected)` returns the second selected sibling
even when unselected Elements appear before or between selected siblings.
The reverse form counts the same filtered sequence from its end. Ordinary
`.selected:nth-child(2)` retains its prior meaning: the candidate is selected
and is the second Element child overall.

### Bounded grammar and parsing

`S` is exactly one restricted simple-selector core supported by the existing
Navigator matcher. It can use supported tag, ID, class, attribute, and
nonfunctional state predicates. The `of` delimiter is recognized as a
case-insensitive keyword only between ASCII whitespace at the functional
pseudo's top level; brackets and quoted attribute values are tracked so text
such as `color of sky` remains part of the value. The existing bounded JS55
`An+B` parser handles the formula, including `odd`, `even`, positive and
negative steps, fixed offsets, and the existing signed 16-bit coefficient
limit.

Selector lists, combinators, `:has()`, logical functional pseudos, nested
positional pseudos, a second `of` clause, and `of S` on `:nth-of-type()` or
`:nth-last-of-type()` remain unsupported and fail closed. This is a deliberate
Navigator subset; it is not unrestricted Selectors Level 4 support. The
maximum 256-byte selector length and all existing selector, collection,
document, and pool capacities remain unchanged.

### Storage and matching

The existing positional payload keeps the two signed 16-bit formula values
and uses its next byte for a compact-core pool index (`0xFF` means no filter).
The filter core is stored by value in the descriptor's existing 16-byte
relative-core pool. No parser input pointer or heap allocation is retained.
At most eight outer simple-selector positions exist across a four-member
selector list. Each position is either a `:has()` pseudo using at most three
pool cores or a filtered positional pseudo using one, so the existing
24-entry pool remains sufficient at maximum capacity. Descriptor copies and
retained query collections keep their existing ownership behavior.

For each candidate, the matcher validates the candidate and generation,
resolves its real parent, then scans the bounded structural Element records
in document order. Only records with that same parent contribute. It checks
canonical serial identity, child index, sibling count, previous-sibling
linkage, and the parent's child count while scanning. Every actual sibling is
tested against `S` using the original scope context. If the candidate does
not match, the predicate is false. Otherwise the matcher computes its
one-based rank among matching siblings from the front or back and applies the
shared JS55 `An+B` evaluator. Text nodes and records from other parents never
contribute; invalid structure and stale generations fail closed.

Optional native diagnostics receive a caller-owned counter pointer at the
test matcher call. No mutable global counter is used, and the selector match
context remains **16 bytes** in both production and diagnostic builds.

| Fixed structure | JS70 | JS71 |
|---|---:|---:|
| Selector descriptor | 1,918 B | 1,918 B |
| Collection record | 1,936 B | 1,936 B |
| 128-record registry | 247,808 B | 247,808 B |
| Compact relative-core pool | 24 entries | 24 entries |
| Structural Element | 440 B | 440 B |
| Element content metadata | 24 B | 24 B |

### JS71 native coverage and bounded workload

`tests/navigator_javascript_js71_test.cpp` covers filtered forward and reverse
positions, candidate membership, the distinction from ordinary `nth-child`,
all-selected and empty filters, odd/even and signed formulas, mixed tags,
separate parents, `:scope`, public query/matches/closest APIs, quoted `of`
text, malformed and oversized inputs, live attributes and checked state,
retained collections, stale generations, invalid structural parents, nested
Event dispatch, and the full existing collection/pool capacities. Its hosted
fixture source is also executed by the native harness. The focused JS71
strict runner passed **110/110** checks.

At the 1,024-Element boundary, the 1,021-sibling all-selected query performed
1,045,504 structural inspections, 1,042,441 same-parent sibling inspections
and simple-filter evaluations, 1,021 parent resolutions, and 1,021 formula
evaluations. The none-match variant performed zero formula evaluations; the
one-early and one-late variants each performed one. The fixed workspace is
bounded by the existing 1,024-Element limit. GCC `-fstack-usage` on the
focused adapter reported 224 bytes for the filtered sibling resolver, 288
bytes for the simple matcher, and 912 bytes for the restricted-core parser;
the larger document-load frames are pre-existing harness work.

The JS71 hosted page starts with **43 assertions** and adds three assertions
after nested Event dispatch, for **46 total**. The server checks the exact
initial and Event result markers. Fresh production-hosted and aggregate
results, full-matrix reruns, exact executable provenance, and final artifact
comparison are recorded in the JS71 qualification closeout below.

### JS71 qualification closeout

The duplicate/stale gate found JS71 outstanding. The live repository was on
`NAVIGATOR_JAVASCRIPT_SUPPORT` at `81d67f73c170bc3999e724a40c23af7ec501dd9b`
(`navigator: optimize JS70 selector descriptor storage`), with a clean
worktree and **0 ahead / 0 behind** its upstream. The prompt's expected
divergence was 1 ahead / 0 behind; the live repository state was used. No
`.phase` file was present, which is normal for this subsystem. No branch,
worktree, remote, or commit topology was changed during qualification.

The native JS71 focused runner passed **110/110**, including strict
`-Wall -Wextra -Werror -pedantic` compilation. The established matrix contains
**73 scripts** here: all 72 historical JavaScript lanes plus JS71. The final
results were **73/73 passed**. JS55's former negative expectation for
`:nth-child(2 of .entry)` was updated to assert the newly supported syntax;
its rerun passed **343/343**. JS67 was rerun after keeping the diagnostic
context at its historic 16-byte size and passed **44/44**. JS70 passed
**77/77**. The JS70 benchmark executable also completed all 15 workload
groups, including 1,024-Element queries, 128-collection saturation, and
nested Event dispatch. The final JS71 and JS67 diagnostic outputs both report
the 16-byte match context and unchanged fixed storage sizes.

`build.bat` completed successfully from the final JS71 source. The fresh
`D:\dev\guideXOSServer_NAVIGATOR_JS_SUPPORT\guideXOSServer.exe` was
**8,484,988 bytes**, SHA-256
`9A7BF046FB8B9E03700248B4F0DE40E2859C45E8B5F3EE815CBF0BC807966EC7`. The
hosted runner launched PID **17940** from that same executable path and
verified the same hash. The fresh executable was copied to
`C:\Users\guideX\AppData\Local\Temp\guidex-js71-fresh-production.exe`
before artifact restoration.

The production-hosted JS71 fixture passed all **43 initial assertions** and
all three post-click nested-Event assertions, for **46 total**. Its exact
markers were
`JS71:initial:assertions=43:failures=0:malformed=true` and
`JS71:event=true:nested=true:metadata=true:assertions=46:failures=0`.
The aggregate recorded **641 passed / 7 failed / 648 total**. The seven
failures are unchanged CSS baselines: CSS 3C, CSS 3G, CSS 6A, three CSS 6B
checks, and CSS 6C. JS63 through JS71 hosted initial/Event markers passed.
No CSS source or fixture was changed, and no new hosted failure appeared.

Before the build and hosted run, the ignored baseline was **1,337 files /
88,371,771 bytes**. Exact backups covered the original **8,398,274-byte**
server executable (SHA-256
`A5E439455E4D3E735DFB848EC296A3EC4B347822B21F5B1B736D51227046A971`) and all
42 pre-existing generated smoke files. The hosted runner restored `downloads`
using its own snapshot. Six run-created HTTP, HTTPS, and smoke logs were
archived at
`C:\Users\guideX\AppData\Local\Temp\guidex-js71-hosted-evidence-20261011-175904`
and removed from the checkout. The original executable and all 42 generated
files were restored and hash checked. Final ignored-state comparison was
**0 changed / 0 missing / 0 extra**, with the same 1,337 files and
88,371,771 bytes.

No kernel build or QEMU run was needed for this hosted Navigator feature.
**Outcome A — JS71 filtered positional selectors fully qualified.** The
implementation adds the restricted filtered-sibling forms without changing
the JS70 descriptor, collection, registry, context, structural-record, or
pool sizes; native, historical matrix, fresh production build, hosted JS71,
aggregate, and artifact-restoration gates passed.

The next bounded Navigator review can examine fixed selector-text sharing in
the 128 independently owned collection records, as proposed in the JS70
record. It must preserve per-collection lifetime and generation behavior and
must not weaken the ownership guarantees retained by JS71's descriptor-owned
filter cores.

## JS72 filtered positional selector performance

### Profile and baseline method

JS71 resolves a candidate's parent, validates its direct-sibling chain, and
walks that chain to run the restricted `of S` matcher and derive the candidate
ordinal. `querySelectorAll()` invokes the same positional matcher for each
outer candidate. With a common parent this repeated structural and filter
work grows with the number of candidates times the sibling count; the
restricted selector descriptor copy was not the source of the measured cost.
The original JS71 implementation counted 1,045,504 structural inspections,
1,042,441 same-parent sibling inspections/filter evaluations, 1,021 parent
resolutions, and 1,021 formula evaluations for the 1,024-Element
same-parent workload.

For a comparable timing baseline, the original JS71 header and implementation
were exported from commit `7fc8f6b01a688926905408d369bdad198f50412f` and
compiled with the same JS72 test fixture, compiler options, and benchmark
queries as the optimized version. The baseline binary is outside the
checkout at `%TEMP%\guidex-navigator-javascript-js72-test\navigator_javascript_js72_js71_baseline.exe`;
its source snapshot is at `%TEMP%\guidex-js72-js71-baseline-source`. Each
scenario was run five times, serially, with no competing benchmark process.
The optimized executable used the same C++17 `g++ -O2` harness and fixture.
Times below are elapsed microseconds for the complete measured operation,
with all scenario-specific query repetitions held constant between builds.

| Workload | JS71 median (min–max) | JS72 median (min–max) | Median change |
|---|---:|---:|---:|
| 1,024 siblings, all selected | 463,128 (454,396–507,450) | 86 (80–194) | −99.98% |
| 1,024 siblings, alternating filter | 483,685 (478,901–510,989) | 87 (76–96) | −99.98% |
| 1,024 siblings, no selected | 458,238 (453,942–519,140) | 144 (140–997) | −99.97% |
| Late matching sibling | 1,000,762 (967,580–1,033,524) | 314 (266–341) | −99.97% |
| 1,024 Elements across many parents | 119,329 (112,204–156,052) | 499 (439–560) | −99.58% |
| One direct child | 7 (6–25) | 9 (7–19) | +2 μs |
| Repeated `querySelectorAll()` | 1,014,837 (955,540–1,042,901) | 160 (154–204) | −99.98% |
| `matches()` on one Element | 466 (459–480) | 462 (458–483) | −1% |
| `closest()` | 450 (445–471) | 469 (458–504) | +4% |
| `:nth-last-child()` reverse ranking | 512,984 (498,389–515,134) | 86 (82–112) | −99.98% |
| Four-member outer selector list | 3,495,826 (3,296,528–4,791,701) | 1,268 (1,236–1,368) | −99.96% |

The large-query reduction is also reflected in the counters, so the result is
not attributed only to instruction-level savings. The 1,024-Element
same-parent alternating-filter qSA recorded 2,048 structural inspections,
2,046 sibling-link inspections, 1,023 filter evaluations, three parent
resolutions, and 511 formula evaluations. It initialized and built one rank
cache and recorded 1,023 candidate cache hits. JS71's corresponding
same-parent count is 1,045,504 structural inspections, 1,042,441 sibling
inspections/filter evaluations, 1,021 parent resolutions, and 1,021 formula
evaluations. JS72 therefore reduced those measured quantities by about
99.8%, 99.9%, and 99.7% respectively; formula calls also stop for candidates
that fail the `of S` membership requirement.

The 1,024-Element many-parent case recorded 513 parent resolutions, 2,048
structural inspections, 2,046 sibling-link inspections, 1,023 filter
evaluations, 255 matching siblings and formula evaluations, and one cache
initialization/build with 1,023 candidate cache hits. The table's JS72 times
come from a standalone `--bench-only` run after the matrix completed, with
five serial repetitions per workload and no concurrent compiler or
benchmark process. One of the five no-match samples was a 997 μs outlier, so
its median is 144 μs; the reported range preserves that sample.

### Bounded invocation scratch

The selected approach keeps a `FilteredNthQueryScratch` on the filtered
matcher call stack. It has fixed arrays for 1,024 structural-group states,
group counts, and eight descriptor-specific rank maps of 1,024 entries,
alongside bounded selector-slot metadata. The exact object size is 19,576
bytes. There is no heap allocation, global mutable storage, document-owned
cache, descriptor growth, or retained collection cache. Scratch is created
for a filtered query collection read and destroyed before that public read
returns; a later read of the live collection rebuilds ranks and observes
current attributes and checked/focus state. `matches()` and `closest()` keep
the direct single-candidate path and do not initialize the scratch.

Repeated per-candidate sibling scans were retained only as the direct
single-candidate fallback. A document-persistent cache was rejected because
attribute/state mutation, scope changes, document generations, and nested
Event dispatch would require a broader invalidation and ownership protocol.
The chosen implementation first validates each direct-child group once per
read, then builds one rank table per outer-member/position-descriptor slot.
Known canonical Elements use their verified document slot during core
filtering, avoiding a repeated linear `findElement()` lookup for every
sibling; attribute and state predicates are still evaluated for the current
read, so this shortcut does not retain mutable filter results.

Each rank slot belongs to one outer selector member and one positional
descriptor within it. A slot binds the current document, generation, API
scope, and descriptor identity. A different parent group is validated and
counted independently; a malformed group is rejected without invalidating
other parents. The pass checks canonical serial mappings, parent identity,
child indexes, sibling counts, previous-sibling links, and parent child
counts before filter evaluation. A failed validation is fail-closed for that
group. Only structurally valid siblings are tested against the restricted
core. Forward ordinals are recorded once; reverse ordinals are derived from
the same matching total. Candidate membership in `S` remains mandatory
before the cached rank can satisfy the positional formula.

The scratch is invocation-local, so nested Event callbacks obtain an
independent object. A callback's nested qSA completes and releases its
scratch before dispatch resumes. Generation replacement, stale collection
reads, different scopes, outer-list members, filters, and parent groups do
not share rank state. The tests exercise nested dispatch, attribute mutation
between retained-collection reads, same-serial generation replacement,
invalid sibling structures followed by repair, separate parents, and
different filters.

The per-candidate helper's GCC `-fstack-usage` frame is 19,648 bytes for
filtered count and 19,680 bytes for filtered index; rank preparation/build
and cache lookup frames are 128/176/256 bytes. Review of the call paths
confirmed that filtered count and index use distinct public-call frames and
that Event dispatch does not nest a live scratch frame around the callback's
separate query. The small-document workload measured 7 μs at baseline and
9 μs after optimization (five runs: 6–25 μs and 7–19 μs), a 2 μs difference
at a very short duration. `matches()` and `closest()` median changes are
−4 μs and +19 μs respectively; both run distributions overlap. The
optimization is scoped to filtered collection reads.

### Memory, capacities, and correctness coverage

JS72 leaves the selector descriptor at 1,918 bytes, collection record at
1,936 bytes, 128-record registry at 247,808 bytes, canonical `HtmlElementRef`
at 440 bytes, and content metadata at 24 bytes. The 19,576-byte scratch is
temporary stack storage. All existing limits remain: 256 selector bytes,
four outer-list members, two `:has()` alternatives and relative steps, two
nested logical members, 24 relative-core entries, 128 collections, and 1,024
structural Elements. No syntax or public API was added.

The JS72 native harness checks forward and reverse positions, `An+B`,
candidate membership, filters and parents in isolation, scope, matches and
closest, live collection reads, checked/focus state, malformed structures,
generation replacement, Event reentrancy, four-member list identity/order,
and maximum-capacity one-parent and many-parent documents. The focused run
passed **55/55** checks and strict `-Wall -Wextra -Werror -pedantic`
compilation. The JS71 focused suite passed **110/110** after the change.
The dedicated runner repeats all eleven timing workloads five times; its
timings and diagnostic counts above were captured serially. Full JavaScript
matrix, production-hosted JS72, aggregate, and artifact-restoration results
are recorded in the qualification closeout below.

### JS72 qualification closeout

The JS72 hosted fixture starts with 23 assertions and runs three additional
assertions after Event dispatch. The server checks its exact markers:
`JS72:initial:assertions=23:failures=0:malformed=true` and
`JS72:event=true:nested=true:metadata=true:assertions=26:failures=0`.

The complete native JavaScript matrix passed **74/74 lanes** and reported
**13,219/13,219 assertions across 32 lanes**. This includes the parser,
runtime, JS6–JS72, and all four JS62 revision lanes. The updated JS72 focused
runner passed **55/55** checks with strict compilation; the JS71 focused
regression passed **110/110**.

`build.bat` succeeded from the JS72 source. The fresh
`D:\dev\guideXOSServer_NAVIGATOR_JS_SUPPORT\guideXOSServer.exe` was
**8,498,327 bytes**, SHA-256
`9AF5D9E2C6ED89A5C84888894C01AF0FEFFC3841D0525AD304147EE3060E2D3A`. The
successful hosted retry launched PID **21860** from that same path and
verified the same SHA-256. The first hosted attempt exposed a fixture setup
issue: its event buttons had no visible content for the hosted click helper.
Adding visible labels fixed the fixture; all three JS72 hosted checks then
passed against the same fresh executable.

The hosted aggregate recorded **644 passed / 7 failed / 651 total**. All
three new JS72 checks passed. Its only failures are the established CSS
baseline: CSS 3C, CSS 3G, CSS 6A, three CSS 6B checks, and CSS 6C. The
JavaScript hosted fixtures passed, including the JS72 initial and nested
Event markers above. No CSS implementation or fixture was changed.

Before build and hosted execution, the ignored file manifest held **1,337
files / 88,371,771 bytes**. The original **8,398,274-byte** executable was
backed up with SHA-256
`A5E439455E4D3E735DFB848EC296A3EC4B347822B21F5B1B736D51227046A971` and
restored and hash-verified. The 42 pre-existing generated fixtures had no
content or size differences from the manifest after the run; the hosted
runner also restored `downloads`. Twelve run-created HTTP, HTTPS, and smoke
logs totaling **979,069 bytes** were copied to
`C:\Users\guideX\AppData\Local\Temp\guidex-js72-hosted-evidence-20261012`.
Windows command policy rejected removal of those exact archived log files.
The final comparison therefore has **0 changed / 0 missing / 12 extra**
files, with **1,349 files / 89,350,840 bytes**. The remaining extras are
only the two attempts' identified logs; no baseline file changed.

At task start the worktree was clean on
`NAVIGATOR_JAVASCRIPT_SUPPORT`, HEAD
`7fc8f6b01a688926905408d369bdad198f50412f`, with the recorded divergence
1 ahead / 0 behind. During qualification, the local origin-tracking ref
advanced to that same JS71 commit (`origin/NAVIGATOR_JAVASCRIPT_SUPPORT`
reflog: update by push at 2026-10-12 05:54 -0700); no fetch or push was run
for this task. Before the JS72 commit, the live divergence was 0 ahead / 0
behind. `.phase` is absent as expected. No kernel or QEMU test was needed.

The large shared-parent and many-parent performance gains, semantics,
capacity bounds, strict compilation, native matrix, fresh build, hosted JS72,
and CSS-baseline classification are qualified. **Outcome B — useful JS72
performance work with artifact cleanup incomplete:** exact ignored-state
restoration is the sole open acceptance item because command policy rejected
deleting the archived run logs. The 19,576-byte per-read scratch and
per-index live collection rebuilds were recorded as follow-up opportunities;
the JS73 qualification below evaluates both while preserving mutation, scope,
generation, and Event isolation.

## JS73 retained collection indexed reads and scratch reduction

### Objective and retained collection contract

JS73 investigates the cost of independently reading retained selector
collections and reduces temporary filtered-rank scratch. The work changes no
selector grammar and adds no public JavaScript API.

The bridge supports `querySelectorAll()`, a live-on-read `length` property,
and numeric bracket reads such as `rows[0]`. `.item()` and iteration are not
implemented and are not used by these tests. The collection retains a fixed
selector descriptor and its query-root serial; it does not retain a result
snapshot. Each `length` call computes the current complete count. Each
numeric read resolves the current element at that index, returns `undefined`
when out of range, and returns the canonical Element for a valid serial.
Results retain structural order and selector-list deduplication.

Collection records are limited to 128. A record is tied to the current
document generation and query root. Replacing the document invalidates old
collection and Element handles even if the replacement reuses structural
serials. Query scratch binds the current document pointer, generation, scope,
and descriptor for one operation; no part of that scratch survives the read.
The JS73 tests exercise distinct scopes and filters, interleaved reads,
attribute/class mutations, checked and focus changes, radio exclusivity,
statically disabled state, invalid sibling structure and repair, and stale
collection access after same-layout document replacement.

### Indexed-read audit and measured behavior

An indexed read does not first materialize the entire matching collection or
call the full-count path. The existing indexed traversal examines structural
candidates in order and stops after it finds the requested matching index;
an out-of-range request necessarily scans all candidates. A `length` read
must inspect the full candidate range. The JS72 source already had the
indexed stop-at-target path, so JS73 preserves and measures it rather than
claiming a new per-read latency improvement.

Every independent read reevaluates selection and filtered sibling positions.
That is required by the established live-on-read behavior: class/attribute,
checked, focus, radio, or document-generation changes between reads must be
visible on the next operation. The repository has no single authoritative
invalidation mechanism covering all those changes, so a cross-call match or
rank cache would risk returning stale identity, order, count, or filtered
position. No persistent result cache or mutation epoch was added. The
invocation-local rank pass still avoids JS71's repeated per-candidate sibling
filter scans during a single full enumeration.

The JS72-source baseline and JS73 build used the same C++17 `g++ -O2`
benchmark harness, deterministic fixtures and arguments, five serial runs
per case, and diagnostic counters. The source baseline was exported before
the implementation change. These timings are complete-operation medians in
microseconds; short-read differences overlap the observed run-to-run spread.

| Indexed workload | JS72-source median (min–max) | JS73 median (min–max) |
|---|---:|---:|
| One matching Element, first index | 12 (9–19) | 14 (9–24) |
| 32 sequential indexed reads | 220 (206–237) | 212 (173–247) |
| 512 sequential indexed reads | 14,438 (14,229–21,301) | 14,100 (13,649–14,291) |
| 1,024-element collection, repeated first index | 2,497 (2,418–2,525) | 2,439 (2,325–4,243) |
| Repeated middle index | 3,653 (3,599–3,692) | 3,540 (3,466–3,575) |
| Repeated final valid index | 5,037 (4,946–5,207) | 4,893 (4,768–4,979) |
| Repeated out-of-range index | 4,779 (4,720–4,983) | 4,719 (4,693–5,782) |
| Deterministic random valid indices | 3,707 (3,522–3,784) | 3,615 (3,576–3,845) |

These distributions do not establish a material indexed-latency change. The
reason for not adding cross-call reuse is semantic, not a claim that all
remaining per-read work is optimal. In four independent reads at indices 0,
512, 1,020, and out of range, counters record 2,568 query-candidate scans,
8,192 structural inspections, 12 parent resolutions, 8,184 sibling
inspections, 4,092 filter evaluations, four rank builds, and 2,564 rank
lookups. Each operation creates its own rank state, then discards it. The
maximum-capacity bulk query still performs 2,048 structural inspections,
2,046 sibling inspections, 1,023 filter evaluations, three parent
resolutions, one rank build, and 1,023 rank hits. This preserves JS72's
bounded near-linear shared-parent work instead of returning to roughly one
million repeated sibling/filter evaluations.

The expanded harness also measures mutation and scope workloads after JS73:
32 alternating reads between two independently scoped collections had a
913 μs median (871–967); retained indexed reads around class mutation had a
145 μs median (134–195); and retained checked/focus reads had a 71 μs median
(59–78). These cases verify that the next independent access observes the
current DOM state. They are characterization measurements, not before/after
speedup claims.

### Selected scratch representation and designs evaluated

JS72 allocated eight 1,024-entry `uint16_t` rank arrays for every filtered
read, even when the descriptor had only one filtered positional slot. JS73
maps the active filtered positional descriptors to dense physical slots and
uses a compile-time-sized invocation object containing only that many rank
arrays. The logical descriptor slots remain independent across outer selector
members and their positional descriptors. Arrays are still bounded by the
1,024-Element structural capacity. A small fixed switch selects one through
eight physical slots; invalid slot mapping fails closed.

The per-parent state and count arrays are packed into one `uint16_t` per
structural Element: two high bits hold the four validation states and 14 low
bits hold the bounded child or matching count. That combines 1,024 bytes of
state and 2,048 bytes of counts into 2,048 bytes. The invocation object also
stores only slot metadata and references. Its maximum eight-slot size is
18,568 bytes; the one-slot and four-slot forms are 4,232 and 10,376 bytes.
There is no new static or registry storage and no heap allocation.

| Active filtered-rank slots | Old fixed scratch | JS73 scratch | Reduction |
|---:|---:|---:|---:|
| 1 | 19,576 B | 4,232 B | 78.4% |
| 4 | 19,576 B | 10,376 B | 47.0% |
| 8, maximum | 19,576 B | 18,568 B | 5.1% |

The maximum-capacity registry remains 247,808 bytes. Descriptor and
collection sizes remain 1,918 and 1,936 bytes. Structural Element and content
metadata sizes remain 440 and 24 bytes. The 11-bit packed-rank experiment was
rejected: it reduced storage but made the 512-index and repeated-index timing
cases about 5–17% slower, so JS73 keeps two-byte ranks. Persistent per-record
index tables were rejected because they would multiply 1,024-entry storage
by the 128-record collection capacity and would snapshot live results.

### Scratch lifetime, stack, and reentrancy

The scratch object is created inside the filtered count or indexed-read
operation, binds its document/generation/scope/selector, and is destroyed when
that operation returns. Nested handlers receive independent calls and
invocation-local storage. Tests retain collection handles across nested
dispatch, read both collections in the nested callback, then read the outer
collection again and verify canonical identity, order, target,
currentTarget, and eventPhase. The existing JS48/JS49 Event regression lanes
also cover `relatedTarget` and `defaultPrevented` across nested dispatch.

GCC `-O2 -fstack-usage` reports the following filtered-call frames, including
diagnostic-build parameters:

| Active slots | Count frame | Indexed-read frame |
|---:|---:|---:|
| 1 | 4,336 B | 4,352 B |
| 4 | 10,480 B | 10,496 B |
| 8, maximum | 18,672 B | 18,688 B |

The JS72 source reported 19,648-byte count and 19,680-byte indexed-read
frames. Helper frames also occur (the generated report records the
group-preparation/build and lookup frames at 112, 208, and 256 bytes), so a
single `-fstack-usage` number is not a whole call-chain high-water mark. The
measured top-level frame improvement is 78% for a one-slot filtered selector
and 5% at the eight-slot maximum. No constrained-runtime stack budget or
complete bare-metal call-chain high-water measurement is available; this
report does not claim kernel or bare-metal stack safety. Host focused suites
and nested Event dispatch passed.

### JS73 native and hosted qualification

The new native test and runner are `tests/navigator_javascript_js73_test.cpp`
and `scripts/smoke-navigator-javascript-js73.ps1`. The focused run passed
**77/77** checks and compiled the parser, adapter, and runtime with strict
`-Wall -Wextra -Werror -pedantic` flags. Tests cover JS72 golden-result
parity, live length/index reads, first/middle/last and out-of-range indices,
repeated and sequential reads, independent scope/filter collections,
mutation visibility, checked/focus/radio state, disabled matching, stale
generations and serial reuse, forward/reverse ranks, invalid structural
groups and repair, sparse/dense and no-result filters, maximum capacity,
scratch-slot bounds, per-read counters, and nested Event queries.

The hosted fixture is `navigator-smoke/javascript-js73.html`. It exercises
the supported collection read surface, identity/order, out-of-range
behavior, filtered positions, live class/checked/focus changes, distinct
query roots, radio ownership, and nested click dispatch. The server checks
explicit `JS73:initial` and `JS73:event` markers, plus source-integrity
coverage for literal `<` in inline JavaScript. The final hosted and aggregate
results, fresh executable provenance, full-matrix totals, and JS73 artifact
closeout are recorded in the final qualification closeout below.

### JS73 final qualification closeout

The final native JavaScript matrix passed **75/75 lanes** and reported
**13,296/13,296 assertions across 33 lanes**. The standalone JS71 regression
passed **110/110**, JS72 passed **55/55**, and the JS73 focused suite passed
**77/77** with strict `-Wall -Wextra -Werror -pedantic` compilation. JS73
source integrity, maximum-capacity counters, and all five-repeat indexed
benchmark cases completed from the same source state.

`build.bat` succeeded. The fresh production executable was
`D:\dev\guideXOSServer_NAVIGATOR_JS_SUPPORT\guideXOSServer.exe`,
**8,518,202 bytes**, SHA-256
`2479D06B423560F2888F7018C2588D6E7DC8BAB0D643EB3E119AF96915A44308`. The
hosted aggregate launched PID **34696** from that exact path and verified the
same executable hash. All three hosted JS73 checks passed: the initial
marker was `JS73:initial:assertions=35:failures=0:malformed=true`, and nested
dispatch ended with `JS73:event=true:nested=true:metadata=true:assertions=38:failures=0`.
The hosted aggregate reported **647 passed / 7 failed / 654 total**. Its only
failures were the known CSS baseline: CSS 3C, CSS 3G, CSS 6A, three CSS 6B
checks, and CSS 6C. It returned a failing aggregate status for those seven
checks; no JS73 or other new failure occurred.

Before building, the actual ignored/generated baseline held **1,349 files /
89,350,840 bytes**, including the 12 pre-existing JS72 hosted logs totaling
**979,069 bytes**. They were preserved. The original executable was backed up
at `%TEMP%\guidex-js73-evidence-20261012\mutable-backups\guideXOSServer.exe`
(8,398,274 bytes, SHA-256
`A5E439455E4D3E735DFB848EC296A3EC4B347822B21F5B1B736D51227046A971`). The
42 mutable generated fixtures were backed up in the adjacent
`navigator-smoke-generated` directory. The fresh production binary was
archived outside the checkout at
`%TEMP%\guidex-js73-evidence-20261012\mutable-backups\guideXOSServer-js73-final-20261012.exe`
with its build hash. After the hosted
run, the baseline executable was restored and hash-verified; all 42 fixture
files matched their backup.

The two earlier JS73 hosted qualification attempts created 12 logs totaling
**980,286 bytes**; those exact files were archived under
`%TEMP%\guidex-js73-evidence-20261012\hosted-created-logs` and removed from
the checkout. The final production-hosted run created six logs totaling
**490,429 bytes**; their verified copies and manifest are in
`hosted-created-logs\run-20261012-235323`, and those exact six files were
removed. The final ignored-state comparison against the JS73 starting
manifest was **0 changed / 0 missing / 0 extra**, with the same 1,349 files
and 89,350,840 bytes. This count includes and preserves all 12 JS72 logs.

Before the final JS73 commit, the worktree was on
`NAVIGATOR_JAVASCRIPT_SUPPORT` at HEAD
`d623cf54295fbda02e466777ddf99179c4c15bff`, subject
`navigator: investigate JS72 filtered sibling performance`. The live
upstream tracking ref already pointed to that same commit at preflight
(0 ahead / 0 behind); its reflog records an upstream push update at
2026-10-12 21:16 -0700, before the JS73 artifact manifest was captured. No
fetch or push was run for JS73. `.phase` was absent as expected. `git diff
--check` passed. A kernel or QEMU run was not needed for the hosted Navigator
change; constrained-runtime stack budget and whole-chain bare-metal stack
high-water remain unverified as described above.

The implementation preserves existing capacities and semantics, materially
reduces the common one-slot invocation frame, retains JS72's bulk-query
operation counts, and accounts for all generated artifacts. **Outcome A —
JS73 indexed collection and scratch optimization fully qualified.** A useful
next performance investigation is a lazy per-parent rank build for one
requested index. It should compare with the existing bounded one-pass build
without adding result reuse across live reads.

#### JS74 selector stack safety and worst-case qualification (2026-10-13)

JS74 audited the JS73 invocation-local selector-rank scratch against selector
capacities and nested Event dispatch. It preserves the production selector
limits: 256 input bytes, four outer selector-list members, 24 relative
`:has()` cores, eight simultaneously active filtered-rank slots, 1,024
document nodes, and 128 retained query-collection records. The JS74 native
suite passed the inherited JS73 checks (77/77) and its new checks (25/25).
It accepts an exactly 256-byte selector and rejects 257 bytes, exercises
one-, four-, and eight-slot queries, nested two-step `:has()` with logical
alternatives, and sweeps nested Event dispatch from depth 1 through the
runtime's fixed limit of 16. Every depth dispatched and returned with all
selector, retained-collection, object-identity, Event target/currentTarget,
eventPhase, and defaultPrevented checks intact.
The inherited 1,024-Element diagnostics retain JS72's 2,048 structural
inspections, 1,023 filter evaluations, and one rank build; the indexed probe
retains four builds for four independent reads. A JS74 boundary case creates
128 distinct collections with distinct results and verifies every final
indexed value, filling the existing registry without growth.

No production selector source or storage layout changes were needed. The
selector descriptor and query-collection record remain 1,918 and 1,936 bytes;
the 128-record collection registry is 247,808 bytes. The largest scratch
object remains invocation-local and grows only with the number of active
filtered-rank slots:

| Active slots | Scratch object | Count frame, production | Indexed-read frame, production |
|---:|---:|---:|---:|
| 1 | 4,232 B | 4,304 B | 4,336 B |
| 4 | 10,376 B | 10,448 B | 10,480 B |
| 8, maximum | 18,568 B | 18,640 B | 18,672 B |

The native diagnostic build, which includes its counter pointer in scratch,
reports the same slot scaling at 4,336/4,352 B (one slot), 10,480/10,496 B
(four), and 18,672/18,688 B (eight) for count/index frames. Against JS72's
fixed diagnostic frames of 19,648/19,680 B, that means the actual one-slot
frames are 78% smaller, four-slot frames 47% smaller, and eight-slot frames
about 5% smaller. The eight-slot maximum retains JS72's bulk-query path while
the common case now reserves only its own specialization. JS74 itself makes
no selector frame change; these are the qualified JS73 layout and frame
sizes, remeasured under both production and native flags.

The scratch audit found one 2,048-byte `uint16_t` group-metadata array (two
state bits and a 14-bit bounded child/match count per structural parent),
eight one-byte logical-to-physical slot indices, eight selector pointers, and
eight built flags. The rank table is exactly 2,048 bytes per active slot
(1,024 `uint16_t` ranks); document, descriptor, generation, and scope
references are shared once per call rather than repeated per slot. Element
serial-to-index resolution uses the already-validated serial ordering, so no
second 1,024-entry mapping table is needed. No rank data escapes the stack
call, and no global, heap, or persistent cache was introduced.

The reachable worst-case test corpus has separate inputs because the bounded
grammar and 256-byte input limit do not allow every independent maximum in
one selector. The four-member eight-slot selector is 215 bytes and places two
filtered positional expressions in each member. A separate 256-byte class
selector checks the exact accepted input boundary and its 257-byte neighbor.
A 48-byte `:has()` selector covers two correlated relative steps and a
two-member nested `:is()` list. The 128-collection stress fills every retained
record with a distinct selector/result. These inputs cover the largest
simultaneous rank occupancy, input bytes, relative traversal/logical depth,
collection occupancy, and the 1,024-Element document bound without claiming
an impossible selector that combines all maxima.

Optimization candidates were reviewed against these measurements. Narrowing
the 16-bit rank to an 11-bit packed representation was previously measured
in JS73 and rejected after 5–17% regressions in repeated indexed reads. The
current template specializations already compile a genuinely smaller frame
for one and four active slots; the runtime samples track the 6,144-byte
one-to-four scratch delta exactly. Reducing the 14-bit group count would
require packing counts across word boundaries; 14 bits already covers the
1,024-node bound and leaves simple state operations. Slot metadata is small
and carries per-slot selector identity and build state needed to reject
cross-slot reuse. The compact descriptor is copied once into the retained
record by design and is not copied into scratch on collection reads. A
bitset/checkpoint representation would add lookup work to the hot path, and
heap/global scratch would break the existing ownership and reentrancy model.
With the measured 1 MiB native stack margin, none of these changes is
justified for JS74.

These are GCC 15.2.0 x86_64 MinGW `-O2 -fstack-usage` static frame sizes
compiled with the production flags (`-std=c++17 -Wall -O2 -iquote .`). In
the native diagnostic build, the count frames add 32 bytes and the indexed
frames add 16 bytes. All reported
translation-unit frames had the `static` classification; the compiler did
not report a dynamic or unbounded frame. Relevant production caller frames
are `querySelector` 2,160 B, `querySelectorAll` 2,128 B,
`getProperty` 272 B, filtered-count dispatch 48 B, filtered-index dispatch
64 B, `dispatchEvent` 2,544 B, `callInternal` 2,224 B, `RuntimeContext::execute`
608 B, `Evaluator::invokeFunction` 528 B, and `Parser::parse` 560 B (the
lexer-result overload is 80 B). A frame-size report is not itself a whole
call-chain high-water measurement: caller/callee frames coexist only along
the active path, and unrelated alternatives must not be added together.

The bounded selector parser has separate frames: `parseBoundedSelector`
112 B, `parseBoundedSelectorMember` 144 B, and `parseSimpleSelector` 304 B.
The parser is entered under the 2,160 B `querySelector` or 2,128 B
`querySelectorAll` frame, which owns the 1,918 B descriptor. `querySelector`
matches immediately; `querySelectorAll` copies that descriptor into its
retained record and returns. The `Parser::parse` 560 B value above belongs to
the JavaScript source parser, not this selector parser.

The maximum eight-slot retained-`length` rank-building path has a
conservative adapter-only frame sum of **20,704 B**: `getProperty` 272,
count dispatch 48, filtered-slot dispatch 48, the eight-slot count frame
18,640, internal scan 192, selector/member/simple matching frames 128 + 176 +
80 + 320, filtered-index resolution 256, rank build 176, and restricted-core
matching 144 + 224. The equivalent indexed-read adapter path sums to
**20,784 B**, substituting the indexed dispatch (80 + 64), indexed frame
18,672, and indexed scan (192) for the count dispatch/frame/scan. These are
deliberately conservative sums of listed out-of-line frame sizes along the
source call path; compiler inlining, tail calls, and mutually exclusive
branches can make the actual chain smaller. They exclude the JavaScript
evaluator frames and platform entry baseline. The runtime watermark below
samples the complete tested path, including those callers. Count and indexed
paths are separate operations and never coexist.

The relevant call paths are:

```text
JS execution / callback
  -> querySelector / querySelectorAll
       -> bounded selector parser -> local descriptor
       -> querySelector: scan and return one Element
       -> querySelectorAll: copy descriptor to retained record and return
  -> retained collection property read
       -> getProperty -> count/index dispatcher
       -> one-, four-, or eight-slot specialization
       -> local scratch -> structural scan -> restricted-core matcher
       -> build the requested rank cache -> release scratch on return

matches / closest
  -> callInternal -> bounded selector parser -> direct Element matcher
  -> bounded ancestry walk for closest; no full rank scratch

nested Event A -> callback A -> query/read scratch A (returns and is released)
               -> nested Event B -> callback B -> query/read scratch B (released)
               -> callback A resumes -> retained A is read and rebuilt
```

The source call graph shows at most **one live filtered-rank scratch object**
at a time. Selector evaluation is synchronous host code and does not call
script callbacks. A `length` or index read has returned and released its
local scratch before JavaScript can dispatch the next Event.
Collection records and callback activations remain live, but those are not
rank-array scratch instances. The fixed runtime limits are 16 Event dispatch
levels and 16 script activations. The depth-16 test exercises the event bound;
it does not claim support for unbounded recursive JavaScript.

The public query calls parse the bounded selector into a descriptor in their
own frame. `querySelectorAll` registers a compact collection record, then
returns; later `length` and numeric property reads evaluate the retained live
query. A filtered `length` read reaches the one-, four-, or eight-slot count
specialization. An indexed read reaches its matching indexed specialization.
Each specialization constructs its scratch on that call's stack and releases
it on return. `matches()` and `closest()` use direct matching and do not
construct the full rank scratch. Repeated and nested reads therefore rebuild
from live DOM state and cannot alias a previous or outer invocation's rank
arrays. Nested synchronous Event dispatch does retain the bounded JavaScript
and dispatch frames; selector scratch is not live across a callback unless
the callback itself is currently evaluating a collection read.

To sample the complete native call path, the JS74 runner built a separate
`-finstrument-functions` executable on a dedicated Windows thread with a 1 MiB
stack reserve. A `uintptr_t` marker in the entry hook records the lowest
function-entry stack address for each workload. Disassembly verified that the
largest specialized frame runs `___chkstk_ms` and reserves its frame before
the compiler-generated entry hook. The probe also reads the owning thread's
`NT_TIB.StackBase` and `StackLimit` after each workload. The minimum hook
address gives a per-workload watermark; the TEB limit gives a cumulative,
page-rounded upper bound on committed stack and covers transient frames in
non-instrumented library calls. It reads no arbitrary stack memory and uses
integer address arithmetic. The stack limit is monotonic on this thread, so
the maximum committed value is reported once for the full workload sequence.

The worst per-workload watermark was the 16-level nested Event case with
selector reads at every level: **328,993 B observed**. The TEB committed-stack
high-water was **331,776 B out of 1,048,576 B**, leaving **716,800 B** of
reserve. Initial commitment was 8,192 B. The TEB result is a conservative
page-rounded upper bound for this entire native test thread, which also
includes test-harness setup. The separate function-entry samples were
190,433 B for an eight-slot retained `length` read and 175,537 B for a
one-slot indexed read. These are runtime results for the instrumented native
test executable, not exact production stack consumption; the instrumentation
and harness add frames. The freshly built hosted executable's PE stack
reserve is recorded in the final build/hosted closeout. No constrained-runtime
or kernel stack budget is inferred from this Windows measurement.

Five-repeat native timing medians and ranges cover representative selector
paths. In the final focused run, `matches()` took 9 us (6–28); one-slot
forward/reverse qSA took 234 us (232–255) / 242 us (236–274); four-slot qSA
took 257 us (252–279); eight-slot qSA took 541 us (538–580); dense/sparse
filters took 195 us (192–214) / 134 us (130–204); three repeated indexed
reads took 258 us (213–436); and the depth-16 Event workload took 629 us
(625–669). The broad repeated-index range includes scheduling noise, so no
speedup is inferred from these host timings. The figures are qualification
samples, not performance guarantees.
The rejected
11-bit rank representation remains out of scope: JS73 found a 5–17% slowdown
for the repeated-index cases, and JS74 found no stack-safety need that would
justify that tradeoff.

The hosted fixture `navigator-smoke/javascript-js74.html` adds initial
selector/capacity and nested-event coverage to the existing hosted aggregate.
`server.cpp` adds three aggregate checks for the initial marker, selector
coverage, and nested Event marker. Its visible `<h1>` also supplies the title
text required by the hosted document-text smoke API. The fixture keeps literal
`<` out of its inline JavaScript because the HTML subset parser treats that
byte as the end of script source.

**Decision:** the existing bounded per-call scratch is safe for the measured
native worst case and the fixed 16-level Event regression. It preserves exact
slot-scaled allocation, live-query semantics, and all selector capacities;
this audit requires no production logic or layout change.

##### JS74 final qualification closeout

The full JavaScript matrix completed **76/76 lanes** and reported
**13,321/13,321 assertions across 34 lanes**. This includes the inherited
JS71/JS72/JS73 focused suites at 110/110, 55/55, and 77/77, plus the JS74
suite at 25/25. JS74's strict compile passed with GCC 15.2.0 targeting
`x86_64-w64-mingw32`; the 16 nested Event-depth cases all passed. The final
stack probe reported 328,993 B maximum instrumented watermark and 331,776 B
maximum TEB committed stack against its dedicated 1 MiB reserve.

The full matrix preceded the visible-heading fixture correction below. That
change did not alter inline script bytes; the focused JS74 run was repeated
afterward and again passed 25/25, including its source-integrity check. The
stack and timing numbers here are from that final focused run.

`build.bat` completed successfully with the configured MinGW compiler. The
fresh `guideXOSServer.exe` was 8,523,834 bytes with SHA-256
`FC5ABEEDC056AF0CB4DA707FB955B0DC058BEDF666B9BE5CF4021930C494F504`.
`objdump -p` reported a PE stack reserve of 2,097,152 bytes and initial commit
of 4,096 bytes. The hosted runner confirmed the exact path and hash for the
launched server process (PID 22168). That aggregate reported **650 passed,
7 failed, 657 total**. All three JS74 checks passed. The seven failures were
the pre-existing CSS 3C, 3G, 6A, three 6B, and 6C checks; the JS72 hosted
baseline had the same seven CSS failures (647/7/654 before JS74's three
checks). The first hosted attempt exposed that the smoke document-text API
does not include `<title>`; adding a visible heading and rerunning produced
the passing JS74 checks above. The overall hosted command therefore still
exits with the known CSS failures.

The original local server executable was restored after hosted verification.
All 42 files (458,372 bytes) in `navigator-smoke/generated` and the backed-up
`desktop.state`, `display-options.cfg`, and `window-bounds.cfg` were restored.
The before/after ignored-file audit matched every original file hash and
length: **1,349 files / 89,350,840 bytes; 0 changed, 0 missing, 0 extra**.
This includes the 12 preserved JS72 logs. Newly created hosted stdout and
stderr logs were archived outside the repository at
`C:\Users\guideX\AppData\Local\Temp\guidex-js74-evidence-20261013`; the
same folder contains the baseline manifest, executable backups, and stack
reports. No existing ignored outputs were removed. `.phase` remained absent.

Pre-commit, the branch was still `NAVIGATOR_JAVASCRIPT_SUPPORT` at starting
HEAD `8842b21db0c9952dc1c26913dc884c6b5d000226` (one commit ahead of its
already-known upstream tracking ref). No fetch, push, reset, checkout, clean,
stash, or worktree operation was used. The JS74 work is committed locally;
the branch is left unpushed. No kernel or QEMU run was performed, so this
Windows hosted/native qualification does not establish a constrained-runtime
stack budget. A follow-up should measure the target's real thread reserve and
repeat the maximum selector and Event inputs there before making a
bare-metal stack-safety claim.

## JS75 retained collection `item(index)` and indexed-access consistency

JS75 adds a callable `item(index)` method to the existing retained
`querySelectorAll()` collection. Both document and Element-scoped results use
the same method and collection record. The operation accepts zero-based
indices. A found index returns the existing canonical Element host handle;
an absent index returns JavaScript `null`. Numeric bracket reads keep their
prior behavior and return `undefined` when absent. `length` remains live on
each read, and item and bracket reads re-evaluate the retained selector against
current document state. These Navigator collections are live-on-read; they are
not browser-style static `NodeList` snapshots.

The existing `getElementsByTagName()` and `getElementsByClassName()` adapters
reuse the same retained collection object when scope and selector descriptors
match `querySelectorAll()`. Those aliases therefore expose the same `item()`
method while preserving their existing strict identity and registry-slot reuse.
JS42 now checks that shared surface, canonical Element results, `null` misses,
and the unchanged bracket `undefined` miss.

The native method is returned through the established receiver-aware host
method mechanism. Its runtime method value is shared across collection
receivers, while invocation still uses and validates the actual receiver. A
detached method call fails with `InvalidReceiver`; an Element or unrelated
object cannot redirect a method call to another collection. Repeated reads do
not register a collection or copy its descriptor. No iterator, `forEach()`,
snapshot, or persistent result cache is added. This JavaScript subset does not
implement the `typeof` operator or `Function.prototype.call`/`apply`, so method
presence and invocation are verified through direct property access, repeated
method identity, and successful calls.

The `SelectorCollectionLookup` result contains a found flag and serial. The
numeric property path and `item()` both call this shared adapter helper, which
delegates to the existing bounded `selectorMatchAt()` traversal. The helper
does not parse a selector or implement a second matcher. The existing path
retains ordering, de-duplication, strict scoped descendant rules, generation
validation, structural bounds, live mutation visibility, and JS71–JS74
filtered positional optimizations. Both paths create the canonical Element
handle from the same generation and serial.

### `item()` argument conversion

One supplied primitive argument is converted by the runtime's existing
primitive number parser. Numbers and booleans convert to numbers; `null`
converts to zero; explicitly supplied `undefined` and invalid numeric strings
convert to `NaN`, which becomes zero under Web IDL unsigned-long conversion.
The supported numeric string grammar is the runtime's existing bounded parser
subset. The resulting number is truncated toward zero, reduced modulo 2^32,
and only then cast to an unsigned 32-bit index. Non-finite values and either
zero sign produce index zero. This avoids out-of-range floating-to-integer
casts and does not clamp or wrap a large index into a valid collection slot.
Extra arguments are ignored after the required first argument.

Calling `item()` with no argument reports `RuntimeErrorCode::TypeError` and
stops that script execution. The runtime has no catchable JavaScript error
objects or `try`/`catch`, so the TypeError classification is observable through
the host runtime result and smoke diagnostic, not through JavaScript exception
handling. ToPrimitive for object, function, and host-object arguments is not
implemented by this runtime; those arguments fail closed with
`HostInvalidValue`. Those limitations are not claimed as full Web IDL
conversion conformance.

### Live, scoped, and reentrant behavior

The JS75 fixture checks first, middle, last, one-past-end, large, empty, and
single-result indices; item/bracket `null` versus `undefined`; canonical
identity with both bracket access and `querySelector()`; ordinary numeric and
primitive conversions; separate Element scopes; filtered positional selectors;
class and supported attribute mutations; checked state and radio exclusivity;
and focus changes. The retained collections are read again after each state
change. The nested Event fixture reads scoped collections through `item()` at
three levels, mutates each matching class, and checks the event target,
currentTarget, phase, and default-prevented state as callbacks unwind.

Generation replacement invalidates an old collection before a method can
reuse its structural serial in the new document. A new collection reads the
replacement document normally. The method uses the established collection
capacity and selector limits and introduces no per-collection storage. The
descriptor remains 1,918 bytes, each collection record 1,936 bytes, and the
128-record registry 247,808 bytes. No selector scratch escapes the current
lookup call, so Event reentrancy keeps invocation-local scratch ownership.

### JS75 qualification

The focused JS75 native suite inherits JS73's collection, scratch, and
generation regressions and adds tests for native method invocation, argument
conversion, missing arguments, receiver validation, replacement generations,
hosted fixture integrity, and indexed-read timing. Strict compilation uses
`-Wall -Wextra -Werror -pedantic`. Production `-O2 -fstack-usage` reports
the added method dispatch and the same shared indexed lookup frames. These
static frames do not establish whole-chain or bare-metal stack safety.

The JS75 hosted fixture is `navigator-smoke/javascript-js75.html`; it uses the
existing aggregate runner and visible buttons for its nested Event checks. A
small negative fixture verifies the missing-argument TypeError because this
runtime cannot catch JavaScript exceptions. The inline script keeps literal
`<` out of its source to preserve the hosted HTML-subset script integrity
behavior.

### JS75 final qualification closeout

The duplicate/stale check started on branch `NAVIGATOR_JAVASCRIPT_SUPPORT`,
at `9c6075c65fedd38dc0147f0be72cff66b0fdffd0` (`navigator: qualify JS74
selector stack and scratch safety`). The live local tracking refs showed
0 ahead / 0 behind at task start; the pasted prompt's reported 2-ahead value
was stale. The tracked worktree was clean, no JS75 implementation was already
present, and `.phase` was absent and left absent. The preflight ignored-file
manifest contained 1,349 files totaling 89,350,840 bytes. Exact backups of the
executable, four desktop settings, and 42 pre-existing generated fixtures were
saved outside the repository before builds and hosted runs.

The full JavaScript runner passed **77/77 lanes**. The accepted JS74 matrix
reported 13,321 assertions; JS75 adds 61 and the compatibility expectation
updates preserve their assertion counts, for **13,382/13,382 reported
assertions**. The final continuation itself passed 35/35 lanes and reported
13,199/13,199 assertions across 34 lanes. JS42, JS43, and JS44 were rerun
separately after the shared collection surface was updated; their results were
235/235, 277/277, and 183/183. JS45 and JS46 passed 184/184 and 220/220.
Focused regressions passed: JS73 77/77 and JS74 25/25. JS75 passed 61/61 and
strict compilation with `-Wall -Wextra -Werror -pedantic`.

Five-repetition median timings in microseconds (bracket → `item()`):

| Workload | Bracket | `item()` |
|---|---:|---:|
| First index | 24 | 27 |
| Index 255 of 511 | 887 | 917 |
| Index 510 of 511 | 4,058 | 4,402 |
| One past end (511) | 1,941 | 2,023 |
| Filtered index 255 | 1,061 | 1,076 |
| Sixteen sequential reads | 50 | 50 |
| Many parents, index 127 | 886 | 870 |
| Nested Event depth 3 | 29 | 28 |

The additional cost is the host method dispatch; both accesses use the same
indexed traversal and keep the existing targeted and filtered lookup paths.
The JS73 indexed counter workload remained at 8,192 structural inspections,
4,092 filter evaluations, 4 rank builds, 12 parent resolutions, and 2,564
rank lookups for four reads. It still builds one invocation-local rank set per
read rather than restoring quadratic repeated sibling matching. Timings are
hosted microbenchmarks and vary between runs.

The JS75 standalone Windows stack probe observed 332,465 B at nested Event
depth 16, with 335,872 B committed from a 1,048,576 B reserve and 712,704 B
headroom. This is 3,472 B more observed watermark than the JS74 baseline and
remains within the hosted thread reserve. `callInternal` grew from 2,224 B to
2,240 B static frame usage; the shared lookup adapter is 80 B,
`selectorMatchAtInternal` is 192 B, and the eight-slot filtered frame remains
18,672 B. Descriptor, collection-record, and 128-record registry sizes stayed
at 1,918 B, 1,936 B, and 247,808 B. Invocation-local scratch remains bounded
at 4,232 / 10,376 / 18,568 B for one / four / eight active rank slots. These
are hosted Windows measurements, not bare-metal stack qualification.

`build.bat` succeeded. The fresh production executable was
`D:\dev\guideXOSServer_NAVIGATOR_JS_SUPPORT\guideXOSServer.exe`, 8,534,462
bytes, SHA-256
`3187A722DFE27EA7CF9F7D7568863A63E6547ABC4D16A4819B5FC39D5ABE1D09`. The
final hosted process used that exact path and hash (PID 31476). All four JS75
hosted checks passed: fixture results, callable conversion/null/identity/live
semantics, nested Event reads, and omitted-argument TypeError. The run is
recorded in the archived app log
`C:\Users\guideX\AppData\Local\Temp\guidex-js75-evidence-20261013-1930\hosted-log-archive\navigator-hosted-smoke-20261013-195824.log`.

The final hosted aggregate passed 654/661 checks. Its seven failures were the
known CSS baseline: CSS 3C, 3G, 6A, three CSS 6B checks, and 6C. The exact
backed-up pre-JS75 executable reproduced those same seven CSS failures. One
early aggregate attempt on the fresh build also had 13 transient JS9/JS10
listener failures; repeated fresh-build runs passed those checks and returned
to only the seven CSS failures. No CSS source was changed.

After qualification, the executable, desktop settings, and all 42 original
generated fixtures were restored byte-for-byte. All 30 logs from the five
JS75 hosted attempts were archived outside the repository. Comparing the
final ignored-file manifest with the preflight manifest yielded **0 changed,
0 missing, 0 extra**.
No kernel or QEMU run was performed; the requested hosted/native qualification
does not establish bare-metal stack safety.

The result is **Outcome A — JS75 collection item(index) support fully
qualified**. This record is committed locally as
`navigator: add JS75 collection item method`; it was not pushed. JS76 builds
on that retained collection API with bounded `forEach()` callback support;
JS75 itself adds no iterator or broader collection API.

### JS76 bounded collection `forEach()`

JS76 adds the synchronous method `collection.forEach(callback[, thisArg])` to
the retained selector collection used by `querySelectorAll()` and the
`getElementsByTagName()` / `getElementsByClassName()` adapters when they reuse
that collection. The callback receives the canonical live Element at the
current index, the zero-based index, and the original retained collection.
Callback return values are discarded; successful traversal returns JavaScript
`undefined`. Missing or non-callable callbacks report `TypeError` through the
runtime result. Since this interpreter has no `try`/`catch` or catchable error
objects, those errors stop script execution rather than becoming catchable
JavaScript exceptions.

The host method dispatch receives a short-lived `HostFunctionInvoker`
capability from the active evaluator. It validates and invokes the existing
runtime function value in that same evaluator and realm. The callback path
does not build source strings, reparse JavaScript, create a second
interpreter, or keep callback state in a global. Invocation state and the
callback argument array are local to each call. The Element argument resolves
to the same generation-checked canonical host handle used by bracket and
`item()` reads. The third argument is the same collection receiver, so its
`length`, bracket, and `item()` reads keep their established live behavior.

The runtime forwards a supplied `thisArg` as the callback's `this` value.
When the argument is omitted, it forwards `undefined`. The fixture verifies
both cases, including an explicit object receiver. This does not add browser
strict/sloppy `this` normalization, receiver coercion, or general object
coercion beyond the runtime's existing value conversions; it is the documented
Navigator subset rather than full `NodeList.prototype.forEach()` conformance.

At entry, `forEach()` reads the current live collection length once and uses
that value as the fixed upper bound, limited by the existing 1,024-Element
structural capacity. For each index below that bound it resolves the
collection again through the existing generation-safe indexed lookup. If an
index no longer resolves after mutation, the loop skips it and continues to
the next bounded index; it does not fabricate a `null` or `undefined`
callback Element, and it does not early-stop on a missing index. Each lookup
sees current selector and structural state. Growth can therefore change what
later bounded indices identify, but never adds indices past the entry bound.
Removal or reordering can cause an original Element to be skipped or another
current Element to be visited; the method does not promise snapshot identity
or exactly-once delivery of the entry Elements. A later independent read sees
all current matches.

After every callback, including the last one, the implementation validates
the collection generation before it can report success. The JS76 native probe
replaces the document from the first callback with a document that reuses
Element serials. The outer `forEach()` stops with the runtime's stale-host
error after one callback; it neither restarts on the new document nor passes a
reused serial as an old-generation Element. Separate scoped collections keep
their original scope. Nested query and nested `forEach()` tests confirm that
the outer callback index, collection handle, and live selector scratch remain
independent.

The selector limits remain unchanged: 256-byte selector input, four outer
selector members, 24 relative-core entries, two `:has()` alternatives, two
relative steps, two logical-list members, a 128-record collection registry,
and 1,024 structural Elements. Selector descriptor, collection record, and
full registry sizes remain 1,918 B, 1,936 B, and 247,808 B. JS76 traverses a
full 1,024-result collection and also fills the collection registry, then
checks traversal and preserved handles at saturation. It adds no per-callback
collection record, persistent result cache, or heap-backed selector storage.
The bounded JavaScript environment and host-object pools are increased to
2,048 so a full callback pass can retain call environments and canonical
Element handles without exhausting the prior 256-environment / 1,024-host
object limits.

The fixture `navigator-smoke/javascript-js76.html` checks callback execution,
arguments, identity, `thisArg`, `undefined`, empty/single/multiple results,
attribute mutation, live removal, repeated growth under a fixed bound,
shrinkage, nested traversal, Element scopes, filtered positional selectors,
tag/class adapters, and nested Event dispatch with target, currentTarget,
phase, and default-prevented metadata. Visible buttons drive the nested Event
case. The invalid-callback fixture checks hosted `TypeError`. Both inline
scripts avoid a literal `<` in JavaScript source to preserve the hosted
HTML-subset integrity safeguard.

The focused native suite includes the JS75 compatibility checks and adds
invalid callbacks, ignored callback returns, callback error containment,
in-callback document replacement, 128-record registry saturation, maximum
capacity, hosted fixture integrity, and five-sample timing runs. Strict
compilation uses `-Wall -Wextra -Werror -pedantic`. The selected JS76 timing
workload performs one initial length read and one live indexed lookup per
permitted index; filtered positional lookup rebuilds its invocation-local
rank scratch for each index read, preserving JS72–JS73 behavior. Measurements
and the final matrix, hosted aggregate, fresh executable identity, stack
frames/high-water, and artifact manifest are recorded in the JS76 closeout
below after qualification.

### JS76 native and regression closeout

The JS76 focused runner passed **106/106 checks**, including its inherited
JS75 compatibility run (**39/39**), strict compilation with
`-Wall -Wextra -Werror -pedantic`, the 1,024-Element traversal, registry
saturation, callback error containment, and document replacement during the
callback boundary. The same-run JS75 focused lane passed **61/61**; the focused
JS73 and JS74 lanes passed **77/77** and **25/25**. The full JavaScript matrix
passed **78/78 lanes** and reported **13,527/13,527 assertions across 37
assertion-reporting lanes**. The JS75 script's static `.su` compile loop now
temporarily handles compiler warnings as non-terminating PowerShell output
while still failing on a nonzero compiler exit code; no assertions or test
expectations changed.

JS76's five-run median timings from the final native matrix run were:

| Selector / matching count | `forEach()` median |
|---|---:|
| `.absent` / 0 | 334 µs |
| `.r1` / 1 | 269 µs |
| `.r16` / 16 | 490 µs |
| `.r128` / 128 | 865 µs |
| `.r512` / 512 | 13,154 µs |
| `.r1024` / 1,024 | 95,932 µs |
| Filtered positional / 257 | 14,733 µs |

A separate same-harness comparison captured the length once and made one live
indexed read per index in both cases. At 1,024 ordinary matches,
`forEach()` measured 88,168 µs versus 107,252 µs for the `.item()` loop. At
257 filtered matches, `forEach()` measured 15,420 µs versus 25,899 µs for the
`.item()` loop. Each result is the median of five runs; system load caused
noticeable variation between runs, so these are local comparison evidence,
not stable performance promises. The 1,024-element check visited exactly
1,024 callbacks through index 1,023, retained one collection record, and
allocated no collection record per callback. JS73's filtered-selector
operation counters remained unchanged at 8,192 structural inspections,
4,092 filter evaluations, four rank builds, 12 parent resolutions, and 2,564
rank lookups for four indexed reads.

The instrumented JS76 stack run passed ordinary, filtered, nested
`forEach()`, mutation plus nested query, `forEach()` from an Event, a
depth-16 nested Event chain with one `forEach()` in each handler, and
1,024-result capacity workloads. Their maximum observed stack watermark was
**331,729 B**, with **335,872 B committed** from the **1,048,576 B** hosted
reserve and **712,704 B reserve headroom**. The depth-16 chain measures Event
reentrancy with `forEach()` in each event handler; it is not a qualification
for 16 recursively nested `forEach()` calls. These Windows measurements do
not establish bare-metal stack safety.

Production `-O2 -fstack-usage` reports: `selectorCollectionForEach()` 8 B,
`Evaluator::invoke()` 368 B, `Evaluator::invokeFunction()` 528 B,
`RuntimeContext::invokeHostMethod()` 576 B, and
`selectorCollectionLookup()` 80 B. The existing one/four/eight-slot filtered
lookup frames remain 4,336 / 10,480 / 18,672 B. The JS76 selector descriptor,
collection record, and 128-record registry remain 1,918 B, 1,936 B, and
247,808 B. The runtime's bounded environment pool is now 2,048 (was 256),
and its bounded host-object pool is 2,048 (was 1,024), to hold escaping
callback environments and canonical Element references through a maximum
traversal.

The fresh production build and final hosted/artifact qualification follow.

### JS76 hosted, artifact, and repository closeout

The duplicate/stale gate found no existing JS76 implementation. Work began on
branch `NAVIGATOR_JAVASCRIPT_SUPPORT` at
`69876f0743fc5cec0b42e5cdd12025e52afed0e7`
(`navigator: add JS75 collection item method`) with a clean tracked worktree;
the live local tracking refs showed 0 ahead / 0 behind. `.phase` was absent
and left absent.

The production command `build.bat` succeeded from that source revision. The
fresh executable was
`D:\dev\guideXOSServer_NAVIGATOR_JS_SUPPORT\guideXOSServer.exe`, 8,548,902
bytes, SHA-256
`42B8AC522CF70B16FAF111C630C64645BE1710724AC9981B53F9D33E4D977F58`. The
final hosted process was PID 16816 and ran that exact executable path and
hash. All JS9 checks passed; all four JS76 hosted checks passed, covering
callback behavior, mutations and arguments, collection scopes and adapters,
nested Event metadata, and invalid-callback TypeError.

The final hosted aggregate reported **658 passed / 7 failed / 665 total**.
Its only failures match the historical CSS baseline: CSS 3C, CSS 3G, CSS 6A,
three CSS 6B checks, and CSS 6C. No new JavaScript failure was observed. The
raw output is
`C:\Users\guideX\AppData\Local\Temp\guidex-js76-hosted-final.log`.

The final ignored-file manifest matched the JS76 preflight manifest exactly:
**1,349 files / 89,350,840 bytes; 0 changed, 0 missing, 0 extra**. The
pre-existing executable, four desktop settings files, and 42 generated
fixtures were restored byte-for-byte. The 18 logs created by JS76 hosted runs
were archived outside the checkout at
`C:\Users\guideX\AppData\Local\Temp\guidex-js76-hosted-log-archive-20261016`;
pre-existing logs were preserved. The hosted evidence came from the fresh
build before the original ignored executable was restored. No kernel or QEMU
run was performed because this is hosted runtime qualification; the Windows
stack measurements above do not establish bare-metal safety.

The source and qualification record passed `git diff --check`. This result is
**Outcome A — JS76 bounded collection forEach support fully qualified**. It
was committed locally as `navigator: add JS76 bounded collection forEach`
and was not pushed. A focused next phase could qualify one short-circuiting
predicate such as `some()`, including its early-termination and mutation
contract. Iterator protocols, `Symbol.iterator`, and `for...of` remain
outside the supported collection API.

### JS77 bounded collection `some()`

JS77 adds `some(callback[, thisArg])` to Navigator's retained selector
collections, including the shared retained collections returned by the
supported tag-name and class-name adapters. This is a guideXOS Navigator
extension analogous to `Array.prototype.some()`. It does not claim that
browser `NodeList` or `HTMLCollection` objects support this method, and it
does not add Array methods, iterator protocols, `Symbol.iterator`, `for...of`,
`every()`, or `filter()`.

The method reuses JS76's active-evaluator callback capability and the same
bounded traversal helper as `forEach()`. Callback invocation remains in the
current runtime and realm; the implementation does not stringify or reparse
the function, create another interpreter, or keep a global callback registry.
It passes the canonical generation-checked Element, the requested live
zero-based index, and the original retained collection. A supplied `thisArg`
is forwarded as the callback receiver; omission remains `undefined`, subject
to the Navigator runtime's existing receiver semantics. No `Function.call`
or `Function.apply` support was added.

After each callback, the runtime applies its existing JavaScript truthiness
conversion. `some()` returns a JavaScript Boolean: `true` at the first truthy
result and `false` after all eligible indices return falsy. It accepts any
callback result and does not require literal Booleans. The existing supported
values were checked: false/true, zero/negative zero/nonzero/NaN, empty and
nonempty strings, null/undefined, an object value, a function value, and a
canonical host Element. Object and function values are truthy; host Elements
are tested through the same runtime conversion. This phase adds no new
literal, coercion, or type-inspection syntax. Empty collections return false
without invoking the callback. Missing or non-callable callbacks use the
existing native TypeError result path; because this runtime has no
`try`/`catch`, those errors terminate the script rather than becoming
catchable exceptions. Callback runtime errors propagate and unwind callback
frames; they are not converted to false.

Traversal captures `collection.length` once after validating the collection
and callback, then uses that entry length as its fixed upper bound, capped by
the existing 1,024-Element structural capacity. Each permitted index is
looked up live, with the existing scope and generation checks. An index that
no longer resolves after mutation is skipped; later indices retain their
original index values. Growth may change which current Element is found at a
later in-bound index, but cannot extend the bound. Removing, adding, or
reordering matches therefore follows JS76's live-read behavior rather than a
snapshot or exactly-once promise. Short-circuiting occurs immediately after
the first truthy callback, so no later index is looked up or invoked. The
initial length computation still counts the collection before traversal; an
early success limits callback and indexed-lookup work, but does not make the
entire invocation O(1). For `n` permitted callbacks in a document with `m`
structural Elements, the initial count is O(m), while each live indexed
lookup may inspect up to O(m) Elements. The worst-case all-false traversal is
therefore O(m + n·m) selector work (quadratic when a dense collection has
`n` near `m`), plus callback execution. This preserves the established
live-on-read semantics. Filtered positional reads continue through the
JS72/JS73 rank evaluator and its invocation-local scratch; JS77 does not
replace it with sibling rescanning. Existing capacities bound `m` at 1,024.

For the native capacity fixtures, the initial collection count inspects all
1,024 structural positions, including the empty-selector case. Each simple
`.sN` selector matches a contiguous prefix, so resolving requested index `i`
inspects exactly `i + 1` structural positions. The native counts therefore
imply 1,025 total structural inspections for index-zero success, 1,034 for
the required 100-match/index-three success, 132,865 for index-512 success in
the 1,024-match case, and 525,824 for final-index success or an all-false
1,024-match pass. The latter performs 1,024 callbacks and indexed lookups.
These simple-class workloads perform zero filtered-selector evaluations,
rank builds, and parent resolutions. JS77's separate filtered positional
mutation case visits two callbacks and two indices; JS72/JS73's established
diagnostic comparison remains 8,192 structural inspections, 4,092 filter
evaluations, four rank builds, 12 parent resolutions, and 2,564 rank lookups
for four reads.

Generation replacement during a callback is revalidated before a result is
reported or the next lookup begins. The native probe replaces the document
and reuses an Element serial after callback zero; the old collection fails
with `StaleHostObject` after exactly one callback and one indexed lookup. It
does not restart in the replacement document or pass the reused serial to a
callback. A truthy result instead returns immediately after its normal
generation validation. Separate scoped collections preserve their query
roots and callback collection identities. The retained tag/class adapters
use the same implementation, without per-adapter callback code.

Nested `some()` and `forEach()` calls keep independent bounds, callback
results, indices, and collection identities. A three-level Event sequence
dispatches Event B from a callback in Event A, then Event C from Event B;
the nested handlers combine `some()` and `forEach()` while checking target,
currentTarget, phase, default-prevented state, and distinct Event identity.
All active callback frames return to zero. The JS76 reentry policy remains in
place: `querySelectorAll(...).item(0)` is used where nested reads are needed,
and `getElementById()` reentrancy is not broadened. The JS9 hosted Event lane
is included in the full regression gate.

The focused JS77 native suite passed **135/135 checks** and strict
compilation with `-Wall -Wextra -Werror -pedantic` using MinGW GCC 15.2,
target `x86_64-w64-mingw32`. It covers callback conversion and arguments,
invalid callbacks and cleanup, callback errors, early stop, live mutation,
bounded growth, shrinking, empty and full collections, scoped roots,
filtered positional selectors, tag/class adapters, nested callbacks and
Events, generation/serial reuse, registry saturation, pool bounds, repeated
calls, and timing workloads. A 100-match collection returning true at index
3 invoked four callbacks and made exactly four indexed lookups. A 1,024-match
collection returning true at index 0 invoked one callback and made one
indexed lookup; an all-false pass invoked 1,024 callbacks and made 1,024
lookups. The test also checks success at the first, middle, and final valid
indices and a full false traversal for sizes 1, 16, 100, 128, 512, and 1,024;
an empty collection performs no callback or indexed lookup.

The full 1,024-callback false traversal used 1,025 of 2,048 bounded
environments and 1,026 of 2,048 host-object slots. The existing descriptor,
collection record, registry, collection count, and structural capacity
remain 1,918 B, 1,936 B, 247,808 B, 128 records, and 1,024 Elements. Repeated
early-success calls reused the one retained collection record; no callback
record or result cache was introduced. The near-capacity per-index lookup
counts matched the number of callbacks. Filtered positional selectors still
use the JS72/JS73 invocation-local rank path and preserve its established
diagnostic workload: 8,192 structural inspections, 4,092 filter evaluations,
four rank builds, 12 parent resolutions, and 2,564 rank lookups for four
reads.

Three-run native medians from the final matrix run were 283 µs
(276–311 µs) for 1,024-element early success, 106,341 µs
(98,985–108,933 µs) for all-false `some()`, 98,104 µs
(96,212–114,522 µs) for all-false `forEach()`, and 138,644 µs
(116,335–174,735 µs) for an equivalent `item()` loop. These local
measurements time interpreter execution on a freshly loaded harness; fixture
parsing occurs before the timer. They vary with system load and are
comparisons rather than performance promises.
An all-false call remains a full bounded traversal; its small Boolean result
handling does not change the live indexed lookup mechanism.

The instrumented Windows stack cases passed for empty, early-true, all-false,
filtered, nested `some()`, `some()` inside `forEach()`, `forEach()` inside
`some()`, and nested Event dispatch. The maximum observed watermark was
117,233 B for the nested Event case; the runner reported 229,376 B committed
from a 1,048,576 B reserve. Production `-O2 -fstack-usage` reported 80 B for
`selectorCollectionLookup`, 80 B for `selectorCollectionSome`, 544 B for the
shared callback traversal, 528 B for evaluator function invocation, 368 B
for evaluator callback invocation, and 576 B for runtime host-method
invocation. The filtered lookup frames remain 4,336 / 10,480 / 18,672 B for
one / four / eight rank slots. These Windows measurements do not establish
bare-metal stack safety.

The complete JavaScript matrix passed **79/79 lanes** with
**13,662/13,662 assertions across 38 assertion-reporting lanes**. This
includes JS75 (**61/61**), JS76 (**106/106**, with JS75 compatibility
**39/39**), and JS77 (**135/135**), all under strict compilation; JS74 and
JS76 stack/reentrancy lanes passed as well. JS77 used MinGW GCC 15.2,
`x86_64-w64-mingw32`; its maximum observed hosted stack watermark was
117,233 B and maximum TEB commitment was 229,376 B of the 1,048,576 B
reserve. The fresh production build, hosted aggregate, exact binary
provenance, and restored artifact comparison are recorded in the JS77
closeout below.

### JS77 build, hosted, and repository closeout

The duplicate/stale gate found no existing JS77 implementation or later
superseding work. The repository was
`D:\dev\guideXOSServer_NAVIGATOR_JS_SUPPORT`, branch
`NAVIGATOR_JAVASCRIPT_SUPPORT`, starting at
`e8bc221786139b034347e0cbd464e79937042236` (`navigator: add JS76 bounded
collection forEach`). The live upstream divergence was 0 ahead / 0 behind;
the prompt's expected 1-ahead count differed from live Git state, which was
left authoritative. Tracked and untracked work was clean before JS77. `.phase`
was absent and remains absent. No branches, refs, or remotes were changed.

`build.bat` succeeded at source revision
`e8bc221786139b034347e0cbd464e79937042236`. The fresh executable was
`D:\dev\guideXOSServer_NAVIGATOR_JS_SUPPORT\guideXOSServer.exe`, 8,558,769
bytes, SHA-256
`4807F4DE1D0BB8285C085A0709AF954E037FD4D52522A4809A01780AE739E633`.
Hosted process PID 13456 ran that same path and hash. The normal application
build emitted existing warnings in unrelated desktop, IPC, and UI sources;
it completed successfully.

The complete hosted aggregate returned exit code 1 and reported 662 passed /
7 failed / 669 total. The only failures were the established CSS baseline:
CSS 3C, CSS 3G, CSS 6A, three CSS 6B checks, and CSS 6C. All five JS9 hosted
checks passed, as did all four JS77 hosted checks for the fixture, conversion
and early-stop counts, nested Events, and invalid-callback TypeError. Thus
there were no new JavaScript or JS9 failures. The archived output is
`C:\Users\guideX\AppData\Local\Temp\guidex-js77-hosted-log-archive-20261017-170617\navigator-hosted-smoke-20261017-170617.log`.

Before mutation, the ignored-file manifest contained 1,349 files / 89,350,840
bytes. The original ignored executable (8,398,274 bytes,
`A5E439455E4D3E735DFB848EC296A3EC4B347822B21F5B1B736D51227046A971`) and all
42 generated fixtures were backed up externally and restored byte-for-byte.
Five settings files were also backed up and verified unchanged:
`desktop.json`, `desktop.json.phase2.bak`, `desktop.state`,
`display-options.cfg`, and `window-bounds.cfg`. Six JS77 hosted logs were
copied and SHA-256 verified in the external archive directory above. Their
individual files remain in the checkout because the Windows command policy
rejected the exact `Remove-Item -LiteralPath` cleanup command as blocked by
policy. The requested cleanup was not bypassed. The final ignored manifest
therefore contains 1,355 files / 89,847,082 bytes: **0 changed, 0 missing,
6 extra**, all six extra files being those JS77 logs. Pre-existing logs and
other ignored files remain intact.

The source, native tests, fixtures, and documentation pass `git diff
--check`. The implementation and qualification evidence are useful and
complete, but the six policy-blocked log removals prevent exact ignored-file
restoration. The result is therefore **Outcome B — useful partial JS77
bounded collection `some()` support** under the artifact-restoration gate,
not a claim of full Outcome A. The prescribed partial-work commit is
`navigator: add JS77 collection predicate foundation`; it is local only and
was not pushed. The final branch is one commit ahead / zero behind once
committed. No kernel or QEMU run was performed; hosted Windows stack evidence
does not establish bare-metal stack safety.

A sensible separate JS78 phase is bounded collection `every(callback[,
thisArg])`, with first-falsy short-circuit tests and its own mutation, scope,
generation, and Event qualification. Iterator protocols remain deferred.
