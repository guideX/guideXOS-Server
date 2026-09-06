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
does not introduce a second keyboard-target state. `document.activeElement` was
audited but intentionally deferred: Navigator has no existing tiny direct
projection, and adding it would expand the bounded DOM object-model scope.

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
any broader focus API such as `FocusEvent`, `relatedTarget`, `activeElement`,
focus options, `tabindex`, autofocus, or sequential Tab expansion.

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

The recommended JS27 direction is to choose one bounded extension explicitly:
either checkbox/radio/select user-value transitions, or a separately specified
`InputEvent`/`beforeinput` and selection model. Those should not be inferred
from JS26's generic Event contract.
