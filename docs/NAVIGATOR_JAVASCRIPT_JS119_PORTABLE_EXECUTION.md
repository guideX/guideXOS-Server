# Navigator JavaScript JS119 — Portable Execution Core

**Phase:** JS119
**Target outcome:** `JS119_PORTABLE_EXECUTION_CORE_VERIFIED`
**Starting HEAD:** `60023951028cfef195fd30254d0e45950b444885`
**Branch:** `NAVIGATOR_JAVASCRIPT_SUPPORT`
**Qualification:** Hosted/native execution and AMD64 freestanding compile/link; no guest execution.

## Starting architecture and decision

JS118 supplied the shared 16-byte `Value`, bounded runtime string storage, and
generation-checked lexical environment storage. Hosted `RuntimeContext` already
used the same environment and string stores through a thin `Environment`
adapter. The hosted evaluator still owned its statement and expression
dispatch, function records, and call behavior.

JS119 keeps one AST and one set of runtime records. `PortableExecutionCore` is
an exception-free evaluator for a deliberately narrow AST subset. Normal
`RuntimeContext::execute()` dispatches supported programs through that core.
Programs outside the subset continue through the established hosted
`Evaluator`, preserving the accepted Windows behavior. The portable core uses
the same `Value`, `Ast`, `EnvironmentStore`, `RuntimeStringStore`, function
record layout, and error categories; it does not create a second value model or
binding registry.

The header-defined core keeps its actual methods available to both the hosted
runtime and the freestanding probe without adding a new implementation object
to existing script source lists. The freestanding anchor calls the real lexer,
parser, stores, and execution core.

## Shared execution and storage

`PortableExecutionCore::supports()` checks the complete program before
execution. Unsupported syntax stays on the hosted evaluator. The core then
predeclares the relevant global and lexical bindings, creates function records,
and executes statements using explicit `PortableExecutionResult` and
`PortableExecutionError` values. It does not throw or allocate.

`FunctionRecord` moved to `function_storage.h` and is shared by the hosted
evaluator and portable store. It retains the function kind, AST declaration
ID, closure environment handle, native/host metadata, arrow flag, and lexical
`this`. A caller-backed append-only `FunctionStore` returns generation-checked
handles. Its configurable slot base lets `RuntimeContext` retain its existing
native/host prefix while the portable store appends later records. Function
handles remain indexes, not pointers into movable storage.

The portable core receives caller-owned `PortableCallFrame[]` and a flat
`Value[]` argument arena. Both have hard capacities. The arena holds arguments
for nested calls only while those calls are active, rather than reserving an
argument array in every frame. Function and environment records are retained
for the realm lifetime so escaped closures keep stable references; reset
advances the generation and invalidates old handles.

Portable execution creates environment records directly in the shared
`EnvironmentStore`. The hosted adapter now lazily materializes facades by
stable slot when the legacy evaluator later reaches one. This lets a portable
closure survive a same-realm source append and be invoked through the existing
hosted evaluator without a duplicate environment model.

## Supported portable subset

The core handles:

- Number, string, Boolean, `null`, `undefined`, and `this` values.
- Identifier reads, unary `!`, unary plus/minus, numeric arithmetic,
  comparisons, equality, and short-circuit `&&` / `||`.
- Identifier assignment and supported compound arithmetic assignment.
- Expression statements, `let`/`const` declarations, blocks, and
  function `return`.
- Lexical resolution, parent lookup, shadowing, temporal dead zone,
  immutable binding errors, and missing-name errors.
- Simple ordinary functions, simple-parameter arrows, nested calls, returned
  functions, and closures retaining their defining environments.

Parser operator precedence and evaluation order are preserved because the
core walks the accepted AST. Arguments bind in parameter order; omitted
parameters become `undefined`, and excess arguments are ignored as in ordinary
JavaScript calls. Arrow metadata and lexical `this` are retained. The focused
suite checks independent captures, nested calls, recursive depth limits,
error return, stale handles after reset, and a valid next program after a
runtime error.

The dispatch preflight accepts identifier calls only when the name resolves
statically to supported user-function syntax. It rejects names that may be
shadowed by a parameter or a nonfunction local and recognizes a factory result
only when the named declaration directly returns a function expression or
arrow. Calls through function-valued parameters, native globals, and host
callbacks stay on the hosted evaluator.

Portable invocation currently requires simple identifier parameters. Defaults,
rest parameters, member-call receivers, host callbacks, and object/Array
operations remain on the hosted evaluator. Programs using control flow such
as `if`, loops, updates, or unsupported syntax also fall back. The core
conservatively falls back when a program combines a string literal with an
addition operator because shared number-to-string coercion is not implemented.
String literal storage and decoding are portable; coercive string addition is
not claimed.

## Errors, budgets, and lifetime

The portable result distinguishes unsupported syntax, invalid AST, missing
and uninitialized names, duplicate and immutable bindings, invalid assignment
or invocation, stale references, environment/binding/function/string/argument
capacity exhaustion, execution-step exhaustion, call-depth exhaustion,
allocation failure, illegal return, and invariant failure. The hosted boundary
maps these to established `RuntimeErrorCode` values, including
`UnknownIdentifier`, `UninitializedBinding`, `TypeError`,
`EnvironmentLimitExceeded`, `FunctionLimitExceeded`,
`StringLimitExceeded`, `PortableCapacityExceeded`, `ExecutionBudgetExceeded`,
and `CallDepthExceeded`.

The existing defaults remain in force: 100,000 execution steps, 64 call
frames, 64 arguments per call, 2,048 retained environments, 16,384 binding
records, 4,096 user functions, and 256 levels of accepted expression nesting.
The portable argument arena defaults to 512 live `Value` entries. Capacity
failure reports an error and never truncates an argument list. Core execution
uses no allocator, so there is no heap allocation partway through invocation;
its failure points are explicit store or scratch capacities. The hosted
runtime may allocate the bounded backing vectors before dispatch and falls
back to its established evaluator if optional execution scratch cannot be
provisioned. If a function record is appended but its declaration binding
cannot be published, the core removes that newest unpublished record before
returning the binding-capacity error.

Function and environment records are append-only until realm reset; there is
no garbage collection. This makes closure lifetime predictable but retains
unreachable function records and environments until reset. Same-realm source
append keeps AST node IDs stable and retains source bytes for prior function
spans.

## Memory accounting

JS118 portable runtime backing remains 2,080,768 bytes: 2,048 environment
records, 16,384 binding records, 1 MiB of copied binding names, 4,096 string
records, and 256 KiB of string bytes. JS119 adds these default execution
capacities:

| Storage | Capacity | Element size | Maximum backing |
| --- | ---: | ---: | ---: |
| Function records | 4,224 total values across user/native/host-method caps | 48 bytes | 202,752 bytes |
| Call frames | 64 | 72 bytes | 4,608 bytes |
| Live argument values | 512 | 16 bytes | 8,192 bytes |
| **JS119 execution backing** |  |  | **215,552 bytes** |

The function store's actual configured capacity is computed from the
remaining user, native, and host-method limits after the hosted prefix; 4,224
is the combined default upper bound. The execution scratch is provisioned
when a supported program selects the portable path. The `PortableExecutionCore`
metadata object is 296 bytes on the qualified AMD64 target.

In the independent freestanding probe, frontend arrays, source, JS118 stores,
and JS119 execution storage are caller-provided and live together. At all
configured maxima, their accounted backing is 6,490,688 bytes: 3,145,792
bytes of JS116 frontend storage, 2,080,768 bytes of JS118 stores, 215,552
bytes of JS119 execution storage, and up to 1 MiB of source. This sum excludes
the probe's small metadata objects, vector/host allocator overhead in
`RuntimeContext`, hosted AST/source containers, native runtime objects,
properties, and process state. It is a simultaneous caller-backed maximum, not
a statement that every hosted script allocates all pools.

The representative portable test peak was 5 environment records, 9 bindings,
1 runtime string using 3 bytes, 3 function records, 3 nested calls, and 3 live
argument values. These are observed test high-water values, not production
workload estimates.

## Freestanding AMD64 qualification

The qualification script compiles the actual translation units:

```text
navigator_javascript/lexer.cpp
navigator_javascript/ast.cpp
navigator_javascript/parser.cpp
navigator_javascript/value.cpp
navigator_javascript/environment.cpp
tests/navigator_javascript_js119_execution_anchor.cpp
```

The anchor invokes real parse and execution operations with caller-provided
storage. It is not a dummy implementation. The compiler is
`C:\mingw64\bin\x86_64-w64-mingw32-g++.exe`, GCC 15.2.0, target
`x86_64-w64-mingw32`. Each translation unit uses this C++17 profile:

```text
-std=c++17 -ffreestanding -O2 -Wall -Wextra -Werror -pedantic
-fno-exceptions -fno-rtti -nostdlib -nostdinc++ -fno-builtin
-DGXOS_BARE_METAL -DGXOS_NAVIGATOR_FRONTEND_FREESTANDING
-m64 -mcmodel=large -mno-red-zone -mno-mmx -mno-sse -mno-sse2
-fno-pic -fno-pie -fno-PIC -fno-PIE -mno-stack-arg-probe
-fno-stack-check -fno-stack-protector -fstack-usage -D__x86_64__
-isystem C:\mingw64\include\c++\15.2.0
-isystem C:\mingw64\include\c++\15.2.0\x86_64-w64-mingw32
-iquote .
```

The script compiles each listed source with `-c`, combines the actual objects
with `ld -r --no-undefined`, archives the result with `ar`, and checks object
format, sections, definitions, and undefined symbols with `objdump` and `nm`.

The relocatable object is AMD64 PE/COFF. `nm -u -C` reports no undefined
symbols for the component or archive. Symbol audits show no standard-library,
exception-runtime, allocator, or platform-import dependencies. `objdump`
reports 128 bytes of `.data`, zero `.bss`, no TLS, and no constructor
sections. This closes dependencies
for the selected portable execution component only; `runtime.cpp`, the
complete hosted interpreter, Navigator host bindings, and the production
kernel are not part of this freestanding component.

The target stack report records these notable static frames: the C ABI probe
1,504 bytes; `invokeFunction` 384; `evalBinary` 368; `evalExpression` 352;
`instantiateFunctions` 304; `instantiateLexicalBindings` 272;
`executeStatement` 208; and `supportsExpression`/`supportsStatement` 160
bytes each. Parser frames include `parseInto` 688 and
`parseAssignmentExpression` 1,328 bytes. Call depth is capped at 64 and AST
walk depth at 256, but recursive C++ evaluator/parser calls can multiply these
per-frame numbers. No aggregate stack maximum was measured or enforced. The
engine is not called from the 16 KiB boot stack; this report does not establish
boot-stack safety, guest stack safety, or guest execution.

## Qualification results

### E0–E6 gates

| Gate | Result | Evidence |
| --- | --- | --- |
| E0 — Architecture verified | PASS | Existing evaluator, shared stores, function identity, and closure lifetimes inspected and documented. |
| E1 — Portable execution dispatcher | PASS | Shared core runs bounded statements, expressions, and bindings; focused harness passes 54/54. |
| E2 — Function invocation | PASS | Ordinary/arrow calls, returns, nesting, independent closures, and hosted interop regression pass. |
| E3 — Freestanding compilation | PASS | Six real translation units compile and relocatable-link for AMD64 COFF. |
| E4 — Dependency closure | PASS | No undefined symbols or runtime imports; 128 bytes `.data`, zero `.bss`, no TLS or constructor sections. |
| E5 — Hosted semantic parity | PASS | Full matrix and hosted aggregate retain their established results. |
| E6 — Optional kernel-format integration | NOT RUN | Optional; no kernel link/conversion or guest execution was attempted. |

The standalone portable harness passes **54/54**. JS115 frontend and JS118
Value/Environment qualification pass. The final full JavaScript matrix passes
**112/112 lanes** and **17,710/17,710 reported assertions** across the
assertion-reporting lanes. JS104/JS105 parameter and function regressions,
JS106–JS108 DOM suites, JS109 JSON, JS110 reviver, and the existing closure and
Array callback tests are included.

The fresh production hosted build succeeded at 9,261,097 bytes with SHA-256
`83CA60CC4517F82953F4CC0FFB75EAB2BBDB82FDAF23B4D80E8CF349AA7112FD`.
Hosted JS109 and JS110 pages each returned HTTP 200 with no JavaScript runtime
error. The hosted aggregate reports **767/774**: its seven failures are the
accepted CSS checks in phases 3C, 3G, 6A, three checks in 6B, and 6C; no
additional JavaScript failures are present.

The protected original `guideXOSServer.exe` was verified before the production
build at 8,398,274 bytes and SHA-256
`A5E439455E4D3E735DFB848EC296A3EC4B347822B21F5B1B736D51227046A971`, then
restored byte-for-byte and rechecked. All 42 generated fixtures (458,372
bytes) still match the saved JS118 manifest; the hosted aggregate changed no
fixture contents. `.phase` remains absent. No QEMU, QAC,
guest page loading, kernel boot hook, or NavigatorApp integration was used.

Evidence is stored under
`E:\\CodexArtifacts\\guideXOSServer_NAVIGATOR_JS_SUPPORT\\JS119`, including
preflight hashes, target compiler logs, object/archive audits, stack usage,
qualification metadata, and hosted build/page/aggregate logs. The final hosted
run is `run-20261114-08\\hosted`; it keeps the fresh executable candidate
separate from the restored repository executable.

## Remaining dependencies and JS120

Only the selected AST slice is freestanding. The full `RuntimeContext`, its
hosted evaluator fallback, objects/Arrays/properties, JSON, host adapters,
Navigator DOM bindings, and page-loading integration still use hosted
containers, allocators, or exception boundaries. Defaults/rest parameters,
dynamic member receivers, loops, and coercive string addition are also outside
this portable slice.

JS120 should first replace or further constrain recursive portable
dispatch so a complete aggregate stack budget can be proven for the intended
target task stack. Then it can add the smallest shared control-flow and
coercion operations while keeping the hosted evaluator as the compatibility
reference. Guest integration should wait until the aggregate stack, storage,
and lifetime bounds are measured for the actual target context.
