# guideXOS Server Developer Studio — Phase 28L

Phase 28L adds bounded hit-count policies and non-breaking source logpoints on top of the Phase 28K persistent source-breakpoint manager.

## Result

The feature is implemented in the bare-metal Native ELF run service. A LOG entry remains an ordinary persistent INT3 user breakpoint. It uses the same original-byte ownership, displaced-instruction TF/#DB servicing, exact rearm, generation identity, and temporary-breakpoint collision rules as a BREAK entry.

The policy order is:

1. accept the exact owned INT3 and restore its byte;
2. increment that entry's `rawHitCount`;
3. evaluate its Phase 28J condition at frame 0;
4. evaluate its hit-count policy;
5. combine the gates with logical AND;
6. pause for BREAK, or render/enqueue for LOG;
7. execute the displaced instruction once and rearm through the existing TF/#DB path.

Condition-false and hit-policy-false hits are transparent: they do not pause or log, but they still increment the raw counter, execute once, rearm, and continue.

## Actions and policies

Public actions are `BREAK` and `LOG`. `NONE` in the action field is accepted as the legacy/default BREAK action. No arbitrary action scripting is provided.

The closed hit-policy set is:

| Policy | Threshold | Eligibility |
| --- | --- | --- |
| `NONE` | ignored and normalized to zero | every otherwise-valid hit |
| `EQUAL` | `N > 0` | `rawHitCount == N` |
| `MULTIPLE` | `N > 0` | `rawHitCount % N == 0` |
| `AT_LEAST` | `N > 0` | `rawHitCount >= N` |

Zero thresholds for EQUAL, MULTIPLE, or AT_LEAST and unknown policy values are rejected without division by zero. `rawHitCount` is a monotonic `uint64_t` and saturates at `UINT64_MAX`.

Policy updates use command 27 and preserve the breakpoint ID, source/address identity, installed patch, and raw counter. Enable/disable also preserves the counter; a disabled entry does not receive hits. Remove destroys the entry's count and template, and a re-add receives a new ID with count zero. A new debug generation starts with an empty manager and output queue.

## Logpoint templates

Command 22 can supply policy fields at add time, and command 27 can change them without changing the machine patch. A LOG template is bounded to 256 source bytes, has at most four `{expression}` placeholders, and renders into a fixed 512-byte record buffer.

The parser accepts literal text and `{ expression }`. Expressions are delegated to the existing Phase 28I evaluator; there is no second expression grammar. Evaluation always uses execution frame 0 and the current authenticated paused trap context. Integers render as signed decimal and pointers as bounded hexadecimal (`0x...`) without dereference. Format specifiers, nested braces, and literal brace escaping are not supported; unmatched or malformed braces produce a bounded template-syntax error record.

A template placeholder error is distinct from a condition error. The output record contains the evaluator error category and a marker such as `value=<error:UNKNOWN_IDENTIFIER>`, then execution continues without a user pause. In contrast, a condition-evaluation error retains Phase 28J safety behavior and pauses even when the entry action is LOG.

## Output queue

The run service owns a fixed FIFO of 32 `gx_development_debug_output_record` values. Each record carries the breakpoint ID, generation, target address, source location, raw hit count, error category, source path, and rendered text. Records are returned in accepted-hit order and are never sorted by ID.

Command 28 drains the queue into the snapshot and advances the consumer. A drain does not execute or stop the target. Empty drains succeed with zero records. The dropped count is cumulative for the current generation and is reported with every drain. On overflow, the newest record is dropped, the dropped count saturates at `UINT32_MAX`, and target execution continues without blocking or pausing. New-generation initialization clears records and the dropped count.

Records produced before normal completion remain available while the debug handle is alive, so the harness drains the Phase 28L template-error records after completion and before release.

## Append-only ABI

The legacy request prefix through `expression` remains 104 bytes. Phase 28K's breakpoint request prefix through `sourceCondition` remains 136 bytes. Phase 28L appends action, policy, threshold, and `logTemplate`, making the policy request 160 bytes. Services gate the new fields by `request.size`, so older callers retain legacy BREAK/NONE behavior.

The public breakpoint record grows from 304 to 336 bytes by appending `rawHitCount`, action, policy, threshold, and template-present metadata. The snapshot appends output count/capacity/drop/status and 32 output records. The output record is 720 bytes; the full snapshot is 27,384 bytes. Existing object ABI layout and ABI table slots are unchanged.

## QEMU proof

The focused harness is:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\smoke-compiler-bootstrap.ps1 `
  -Phase28LOnly -BootCount 3 -TimeoutSeconds 180
```

The Phase 28L fixture runs three calls through `phase28l_helper` and renders `28L GUI 9`.

* A: `src/helper.cpp:4`, conditional LOG, condition `input - 1`, template `input={input} doubled={input * 2}`. The first raw hit is false; hits two and three emit `input=2 doubled=4`, then `input=3 doubled=6`.
* B: `src/helper.cpp:5`, BREAK with EQUAL 3. Hits one and two continue; hit three is the first user-visible pause, with raw count 3.
* C: `src/main.cpp:12`, ordinary BREAK. It remains active and pauses after B resumes.

The proof checks three active entries, no pause for successful LOG hits, FIFO order and values, hit-count gating, INT3 rearm, LOG + BREAK coexistence, GUI completion, output-template errors, clean teardown, and a second generation whose new ID starts with raw count zero and an empty queue. The host policy contract covers NONE/EQUAL/MULTIPLE/AT_LEAST, invalid zero thresholds, composition primitives, and saturating increment. The host output contract covers two independent record payloads, FIFO order, deterministic drop-new overflow, cumulative drops, and clear behavior.

The GUI assertion uses the existing deterministic runtime close automation; Phase 28L does not claim mouse-driven interaction. No standalone Developer Studio repository files are modified.
