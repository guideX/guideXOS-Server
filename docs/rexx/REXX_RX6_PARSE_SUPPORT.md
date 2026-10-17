# RX6 Classic `PARSE` Qualification

**Acceptance: A — tested and accepted, including native guest verification.**

RX6 adds `PARSE ARG`, `PARSE VAR`, and `PARSE VALUE` to the hosted REXX
interpreter and the freestanding guideXOS runtime. This qualification preserves
the RX6 implementation and records the hosted, allocation-failure, kernel, and
isolated guest results below.

## Repository and build record

- Branch: `REXX_SUPPORT`.
- Starting HEAD before RX6 acceptance work: `8332ca450bc21d24e6b5fef9e35e6ce3e5ff8719`.
- RX5 baseline: `8332ca450bc21d24e6b5fef9e35e6ce3e5ff8719`; it is an ancestor of
  the qualified tree. The qualification began at that HEAD; no branch switch,
  reset, merge, rebase, cherry-pick, or push was performed.
- Root `.phase`: unchanged.
- Hosted test runner: `tests/run_rexx_tests.ps1`, including the legacy Windows
  PowerShell argument-quoting fallback and the RX6 fixtures.
- Native build: AMD64 `kernel.elf` linked successfully in
  `kernel/build/rexx-rx6-qualification-20261016-c3d306ae565a4937a052ad6d18395725-amd64/bin/`.
  SHA-256: `8481ABE3413C35186BC07C71103FD3065FE5FA106980AD40474CB2CED35C17D4`.
- Native build log:
  `tmp/rexx-rx6-qualification-20261016-c3d306ae565a4937a052ad6d18395725/native-kernel-build.log`.

## Supported syntax and behavior

RX6 accepts these forms:

```rexx
parse [upper] arg template-list
parse [upper] var source-variable template-list
parse [upper] value expression with template-list
```

Templates support blank-delimited word fields, a final remainder field,
placeholders (`.`), quoted string patterns, absolute numeric positions,
signed relative positions (`+n` and `-n`), and variable patterns such as
`(delimiter)`, `=(position)`, `+(position)`, and `-(position)`. Variables in
the template are resolved as parsing reaches them, so a variable assigned by an
earlier field can supply a later dynamic pattern. Comma-separated templates
are parsed in order. The CLI provides one invocation string; later template
groups therefore parse empty strings until routine-call argument frames are
available.

`PARSE ARG` preserves input case. `PARSE UPPER ARG`, `PARSE UPPER VAR`, and
`PARSE UPPER VALUE` uppercase the copied source before field assignment.
`PARSE VAR` reads a snapshot of its source string: assignments to a target with
the same name, including a compound variable target, do not change what later
fields read from that same `PARSE` instruction. `PARSE VALUE` evaluates its
expression once before parsing.

The CLI joins arguments following the script path with one blank into the
program invocation string. A native shell command such as
`rexx /rx6arg.rex MiXeD two WORDS` consequently gives `PARSE ARG` the string
`MiXeD two WORDS`. Hosted CLI tests use the process argument-list API where
available and a Windows command-line quoting fallback on legacy PowerShell;
the regression includes repeated blanks inside one argument and an embedded
quote so quoting does not silently alter the REXX input.

Absolute positions are one-based and positions beyond the string are bounded
to its end. Relative patterns move from the current pattern position, including
backward movement. When a backward or equal position moves to or before the
current field segment, the intervening fields receive the remaining source
text, matching the IBM examples exercised below. Empty string patterns match
the end of the source. Repeated literal delimiters and blank spacing are
preserved according to the field boundaries rather than normalized.

## Compatibility matrix and IBM evidence

RX6 fixture expectations are checked byte-for-byte by the hosted test runner.
The rules and published examples were checked against IBM's REXX references:

- [IBM REXX/VM Reference, Chapter 4 “Parsing”](https://www.ibm.com/support/pages/zvm/library/710pdfs/71631400.pdf)
  describes word fields, string and positional patterns, placeholders,
  one-based absolute positions, signed relative positions, and variable
  patterns.
- [IBM parsing instructions](https://www.ibm.com/docs/en/cics-ts/6.x?topic=data-parsing-instructions)
  documents the case-preserving behavior of `PARSE ARG`, `PARSE UPPER ARG`,
  `PARSE VAR`, and `PARSE VALUE`.
- [IBM: combining string and positional patterns](https://www.ibm.com/docs/en/zvm/7.2?topic=parsing-combining-string-positional-patterns-special-case)
  documents the cursor rule for a string pattern followed by a variable and a
  relative positional pattern.
- [IBM: parsing multiple strings](https://www.ibm.com/docs/en/zos/2.5.0?topic=parsing-multiple-strings)
  documents comma-separated templates and the single invocation argument
  string when a program starts as a command.

| Behavior | RX6 fixture or guest script | Expected result checked |
| --- | --- | --- |
| `PARSE ARG` preserves mixed case and joins CLI arguments | `parse_arg_case_and_shell_tokenization`; guest `rx6arg.rex` | `[MiXeD  Two Tail"Quoted]` in hosted CLI; `[MiXeD two WORDS]` in guest |
| `PARSE UPPER ARG` translates before assignment | `parse_upper_arg`; guest `rx6upper.rex` | `MIXED CASE` |
| Ordinary and compound `PARSE VAR` fields | `parse_var_fields`, `parse_compound_variables`; guest `rx6var.rex` | Word fields and final remainder retain the specified blanks |
| Source variable aliases a target | `parse_source_alias`, `parse_compound_source_alias` | `[alpha][beta]` and `[first][ second]` from the original source snapshot |
| `PARSE VALUE` evaluates expressions and honors quoted delimiters | `parse_value_expression`, `parse_literal_delimiters`; guest `rx6value.rex` | `[alpha][beta]`; `[Smith][John][Jr]` |
| Placeholders, empty strings, repeated delimiters, whitespace, and missing matches | `parse_placeholders`, `parse_empty_source`, `parse_empty_pattern`, `parse_repeated_delimiters`, `parse_single_field_whitespace`, `parse_missing_match`, `parse_unmatched_then_fields`, `parse_arg_missing` | Exact `.out` fixtures; empty argument strings assign empty values, and missing literal matches leave the remaining source in the preceding field |
| Absolute positions and backward/equal movement | `parse_absolute_positions`, `parse_absolute_backtracking`; guest `rx6position.rex` | IBM example: `[cdefghij][abcdefghij][abcde][fghij][bcdefghij]` |
| Signed relative positions | `parse_relative_positions`, `parse_relative_backtracking` | IBM example output: `stars` |
| String pattern then relative position cursor special case | `parse_string_relative_special_case` | IBM example output: `REXX` |
| Dynamic string and position patterns | `parse_dynamic_string_pattern`, `parse_dynamic_patterns` | `[11][/][15][90]`; `[Samuel Clemens][Mark Twain]` |
| Invalid template diagnostic and recovery | `invalid_template`; guest `rx6invalid.rex` | Source path, line, column, and parser message; next shell command succeeds |

The exact cursor examples are significant: the classic relative example parses
`astronomers` with `2 var1 +2 -3 var2 +1 +2 var3 +1 +6 var4` and produces
`stars`. The IBM absolute/backward example parses `abcdefghij` with
`3 p1 -5 p2 +0 p3 6 p4 =2 p5` and produces
`[cdefghij][abcdefghij][abcde][fghij][bcdefghij]`. The published
string/relative special case concatenates to `REXX`. RX6 has a fixture for each
example. Regina was not installed, so independent Regina differential testing
was not performed.

## Test and safety results

Final `tests/run_rexx_tests.ps1` result: exit code 0, no failed assertions.

| Gate | Result |
| --- | ---: |
| RX1 hosted runtime integration cases | 11/11 |
| Existing RX1 hosted CLI cases | 15/15 |
| RX6 hosted CLI argument cases | 3/3 |
| CLI source-size resource check | 1/1 |
| Hosted CLI total, including RX6 and source-size checks | 19/19 |
| RX2 hosted VFS/output assertions | 45/45 |
| RX2 freestanding numeric adapter assertions | 18/18 |
| RX2 shell bridge assertions | 7/7 |
| RX3 decimal assertions | 26/26 |
| RX3 allocation-failure assertions | 570/570 |
| RX3 compatibility fixtures | 13/13 |
| RX4 arithmetic assertions | 53/53 |
| RX4 compatibility fixtures | 11/11 |
| RX5 numeric-context assertions | 35/35 |
| RX5 compatibility fixtures | 13/13 |
| RX6 parser/compiler/runtime allocation-failure assertions | 403/403 |
| RX6 compatibility and resource fixtures | 25/25 |
| Regina differential cases | 0 run; Regina unavailable |

The RX6 allocation-failure harness injects deterministic failures through
parser AST, compiler bytecode/pool, VM dynamic-pattern, and argument-string
allocations, then checks cleanup and successful reuse. Resource tests cover the
parser AST node limit, a bounded 16 MiB pattern-comparison work budget, and the
16 MiB source/invocation-string limit. Parse plans own only bounded arrays of
indices and values; literal patterns remain in the bytecode constant pool,
dynamic names use the symbol table, and runtime source and pattern copies are
freed on both success and failure. The `PARSE VAR` copy makes target/source
aliasing safe. Test allocators are enabled only in hosted qualification builds;
the AMD64 kernel link passed with the freestanding runtime configuration.

Static review of the REXX paths found no STL, `std::`, exception, RTTI, or
direct C++ `new` dependency. The parser, compiler, and VM use the existing C
allocation entry points, which resolve to the kernel allocator in the
freestanding build. The native build confirmed the test-only failpoint hooks do
not enter the bare-metal kernel build. Bounds checks cover template slices,
symbol and constant indices, source lengths, positions, and pattern-match work.

## Native guest qualification

The actual AMD64 kernel was booted in QEMU from a newly copied, isolated RX5
test ESP represented as a writable FAT directory. The guest had no network
device. The QEMU process, QMP port, ESP path, and kernel hash were checked
before guest input; no production ESP or disk image was attached. The guest
kernel hash matched the built kernel hash above.

Through the guideXOS Shell Console, the guest ran:

- `rexx /rx6arg.rex MiXeD two WORDS` → `RX6 ARG=[MiXeD two WORDS]`
- `rexx /rx6var.rex` → `RX6 VAR=[first][second][  third]`
- `rexx /rx6value.rex` → `RX6 VALUE=[Smith][Jane][Captain]`
- `rexx /rx6position.rex` → the absolute and backward outputs shown above
- `rexx /rx6upper.rex mixed Case` → `RX6 UPPER=[MIXED CASE]`
- `rexx /rx6invalid.rex` → `syntax error: PARSE positional pattern requires a whole number or variable pattern`
- `echo $?` after invalid input → `1`; after successful REXX input → `0`
- `echo rx6recovered` → `rx6recovered`; serial heartbeats continued afterward

The serial log records UEFI boot, the running kernel, VFS opens for the RX6
scripts, and continuing heartbeat ticks. The host and guest kernel identity
record is `tmp/rexx-rx6-qualification-20261016-c3d306ae565a4937a052ad6d18395725/guest-media-manifest.txt`.
Graphical captures are in that same scratch directory: `accept-arg-case.png`,
`accept-var.png`, `accept-value.png`, `accept-position.png`,
`accept-upper-lower.png`, `accept-invalid.png`, `accept-exit-status.png`,
`accept-success-status.png`, and `accept-recovery.png`. The complete serial
capture is `serial.log`.

## Limitations and RX7 handoff

- Only `ARG`, `VAR`, and `VALUE` are implemented as `PARSE` sources. `PULL`,
  external input sources, `ARG` as a separate instruction, and the full REXX
  condition and trapping behavior remain outside RX6.
- Program invocation supplies one argument string. True per-argument strings
  for internal routine calls require routine frames; extra comma templates
  currently receive empty strings.
- Positions and delimiter matching operate on byte strings. `UPPER` currently
  applies ASCII case conversion; DBCS and locale-specific case behavior were
  not qualified.
- Source and invocation strings are limited to 16 MiB; template ASTs are
  bounded by the parser's 65,536-node limit; pattern comparisons are bounded by
  16 MiB of byte comparisons per `PARSE` instruction.
- `PARSE VALUE` expressions are limited to the expression syntax already
  supported by the interpreter.

RX7 can add routine frames and multiple true argument strings while retaining
this matrix. Keep the `PARSE VAR` source snapshot, the IBM relative-pattern
cursor cases, the dynamic-pattern evaluation order, the parser/runtime bounds,
and failpoint cleanup in the RX7 regression handoff. RX7 was not started as
part of this qualification.
