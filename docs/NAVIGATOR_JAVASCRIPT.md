# guideXOS Navigator JavaScript

## Purpose and current boundary

Navigator currently has a reusable HTML document/parser layer in
`guide_web_document.*` and `guide_web_html_parser.*`, with Navigator-specific
navigation and rendering in `navigator.*`. The repository audit found no
JavaScript lexer, parser, runtime, DOM abstraction, event scripting system, or
`<script>` execution path. The existing HTML parser intentionally strips
`<script>` content.

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
No parser, AST, runtime values, scopes, functions, objects, arrays, built-ins,
DOM, events, timers, or web APIs exist in this phase.

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

### Validation tiers

Tier 1 is the mandatory isolated JavaScript-engine gate for JS2: focused
structural AST tests, MinGW/g++ tests, MSVC `/W4 /WX` tests, deterministic
limits and malformed-input coverage, source/build-list integration, and
`git diff --check`. Tier 2 is the Navigator integration gate: the full guideXOS
build, hosted Navigator regression, bare-metal/QEMU regression, layout/image/
resource regression, and HTTP/HTTPS/TLS regression. Tier 2 is attempted when
practical and becomes mandatory before JavaScript affects page execution or
browser behavior. Existing Mbed TLS dependency/profile and QEMU/full-project
environment failures remain inherited integration blockers for this isolated
phase; they do not weaken or bypass TLS/security requirements.

**Navigator still does not execute JavaScript after Phase JS2.** There is no
`<script>` execution, `window`, `document`, DOM binding, events, timers,
networking API, or page-loading hook.

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

The next milestone is **Phase JS3 — JavaScript Runtime Values & Execution Core
Foundation**: values, environments/scopes, expression evaluation, and bounded
execution, still without DOM integration. Full ECMAScript compatibility is not
promised.
