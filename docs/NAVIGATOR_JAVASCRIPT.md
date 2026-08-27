# guideXOS Navigator JavaScript

## Purpose and current boundary

Navigator currently has a reusable HTML document/parser layer in
`guide_web_document.*` and `guide_web_html_parser.*`, with Navigator-specific
navigation and rendering in `navigator.*`. The independent JavaScript
subsystem now contains a bounded lexer, parser/AST, runtime core, and the JS8
controlled document bridge described below. It has no general event system,
but JS9–JS12 provide a deliberately bounded inline-script and synchronous
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
expansion remain outside this milestone. The recommended next step is JS13:
basic click bubbling and propagation-path construction, with the child as
`event.target` and each executing Element as `event.currentTarget`.
