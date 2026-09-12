# guideXOS Developer Studio — Phase 28I Selected-Frame Watches

## Scope and outcome

Phase 28I adds a bounded, read-only watch-expression evaluator to the
authoritative developer-studio server/kernel path. This is Outcome B,
Level 2: the evaluator is implemented and proven against the existing
Phase 28H selected-frame variable snapshot, while the standalone
`D:\dev\guideXOS_Developer_Studio` UI repository is intentionally left
unchanged. No new public debugger request or host-call slot is introduced.

The evaluator consumes a `NativeDebugWatchFrame`. The frame is assembled from
the authenticated Phase 28H query result, including the selected frame index,
RIP/RBP identity, session generation, stop generation, and the bounded variable
snapshot. It does not accept an arbitrary target address or perform a second
target-memory lookup.

## Public ABI compatibility

The public debugger ABI remains at its Phase 28H shape:

- `gx_development_debug_request` is 104 bytes.
- `gx_host_calls` has 10 slots and is 432 bytes.
- The existing `INSPECT_VARIABLES` command remains the source of selected-frame
  data.
- `auxiliaryAddress` continues to carry the selected call-stack frame index
  for the existing debugger queries.

Expression text, result buffers, and evaluator metadata are private to the
bounded evaluator and its tests. They are not added to the wire request or
host table.

## Evaluation contract

Evaluation is allowed only when the process is paused, the selected frame
index is within `GX_DEVELOPMENT_DEBUG_MAX_CALL_STACK_FRAMES`, the selected
frame has nonzero instruction/frame pointers, the session and stop
generations are nonzero and match the variable snapshot, and the snapshot is
`SUCCESS` or `TRUNCATED` with a valid bounded size and variable count.

Any generation mismatch returns `StaleGeneration`; a running process returns
`Running`; an invalid selected frame returns `InvalidSelectedFrame`. Only
variables marked validated, live, value-valid, and available can be resolved.
Unavailable values return `NotLive`, unsupported metadata types return
`UnsupportedType`, and missing names return `UnknownIdentifier`.

The evaluator uses fixed-size local storage and explicit limits:

- expression: 256 bytes;
- tokens: 96;
- AST nodes: 64;
- parse depth: 16;
- operators: 32;
- identifiers: 64 bytes;
- numeric literals: 32 bytes;
- formatted result: 64 bytes;
- diagnostic: 128 bytes.

The parser supports decimal and hexadecimal integer literals, parentheses,
unary `+`, unary `-`, logical-not `!`, and bitwise-not `~`. Binary operators
are multiplication, division, modulo, addition, subtraction, shifts,
relational comparisons, equality comparisons, bitwise `&`, `^`, `|`, and
logical `&&`, `||`, with conventional C-like precedence. Logical operators
short-circuit. Assignment, calls, increment/decrement, dereference, and
address-of are rejected; the evaluator never executes user code.

Supported scalar inputs are signed 32-bit integers and pointers. Results are
signed 32-bit integers, pointers, or booleans. Pointer operations are limited
to pointer equality/inequality and comparison with the integer zero; pointer
arithmetic, ordering, and dereference are rejected. Arithmetic is checked
for signed 32-bit overflow, division by zero, the `INT32_MIN / -1` case, and
invalid shift counts. Each failure has a deterministic status and bounded
diagnostic text.

The evaluator does not read target memory, change registers, change the
selected frame, resume or stop execution, or mutate the query snapshot. A
watch is therefore a pure re-evaluation of the authenticated paused snapshot.

## Runtime fixture and evidence

The Phase 28H nested-helper fixture supplies three selected frames:

- frame 0: `tail_value` evaluates to 33;
- frame 1: `adjusted` evaluates to 14;
- frame 2: `root_value` evaluates to 100.

The smoke also proves cross-frame isolation, arithmetic precedence,
parentheses, boolean output, pointer inequality using a fresh authenticated
one-entry snapshot, invalid-frame rejection, running-state rejection,
stale-stop-generation rejection, read-only stack/register stability, and
re-evaluation after selected-frame changes. The focused QEMU script emits
`DEVELOPER_STUDIO_PHASE28I_*_PASS` markers and finishes with
`DEVELOPER_STUDIO_PHASE28I_PASS`; the Phase 28H prerequisite finishes with
`DEVELOPER_STUDIO_PHASE28H_PASS`.

## Standalone Studio status

The standalone Studio repository already contains its existing Watches model
and pane. It was not modified in this phase. Its current hosted backend and
parser limits are therefore not silently presented as server-backed
expression evaluation. Integrating the pane with this evaluator remains a
follow-up that can be done without changing the public debug ABI.
