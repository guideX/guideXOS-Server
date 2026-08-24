# guideXOS Navigator JavaScript

## Purpose and current boundary

Navigator currently has a reusable HTML document/parser layer in
`guide_web_document.*` and `guide_web_html_parser.*`, with Navigator-specific
navigation and rendering in `navigator.*`. The repository audit found no
JavaScript lexer, parser, runtime, DOM abstraction, event scripting system, or
`<script>` execution path. The existing HTML parser intentionally strips
`<script>` content.

JavaScript is therefore a separate subsystem. It must not become an implicit
part of HTML parsing or page loading. Phase JS1 only accepts source text and
produces a deterministic token stream. It has no connection to document
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

## Roadmap

```text
JS lexer
  -> parser / AST
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

The next milestone is **Phase JS2 — bounded parser / AST foundation**. It
should consume only successful JS1 tokens, retain explicit bounds and source
locations, and remain execution-free until a later phase has a reviewed
runtime and explicit host capability boundary. Full ECMAScript compatibility is
not promised.
