# Developer Studio Phase 28G — Arguments and Locals Inspection

Phase 28G adds compiler-owned persisted variable metadata and a read-only
NativeElf inspection operation for the paused top user frame. Hosted and
bare-metal execution use the same public result shape and the same final-ELF
metadata rules.

## Contract

The new debug command is `GX_DEVELOPMENT_DEBUG_INSPECT_VARIABLES` (command
20). It is accepted only for the current paused session, thread, stop
generation, artifact identity, and native runtime. It rejects running,
stale, stepping, internal-trap, missing-metadata, invalid-frame, and invalid
location requests without changing registers, memory, breakpoints, or run
state.

The result is bounded to 32 variables. Each entry carries a fixed-size name,
argument/local kind, signed-32-bit or pointer type, declaration source
location, stable frame offset, live range, availability, validation flags,
and a typed value when the slot is readable. Truncation is explicit. There
are no watches, arbitrary expressions, dereference chains, or user-provided
addresses in this phase.

## Compiler and object format

The compiler owns the records. Supported records are parameters and scalar
locals whose storage is stable in an AMD64 frame-pointer (`RBP`) slot. The
initial implementation persists signed 32-bit integers and pointers, records
declaration locations, marks initialized locals, and emits conservative
function live ranges. Unsupported types and optimized-away values are not
invented by the debugger.

The compiler object ABI advances from 9 to 10. The `.gx.meta` header now
stores a bounded debug-variable count and byte length, followed by fixed-size
records. Serialization, deserialization, validation, checksum coverage, and
linking all preserve the records and their final function/source identity.
Old ABI-9 objects are rejected by the existing object-cache compatibility
checks.

## Final ELF metadata

The deterministic `GXSM` trailer advances to version 2 when variable records
are present. Version 1 remains readable for existing source-map-only images.
Version 2 keeps the source/function map and appends a bounded variable-record
area with checksum coverage. The ELF writer rejects malformed counts, sizes,
names, enum values, ranges, locations, and source/function references. Runtime
resolution accepts only records whose function identity, source identity, and
live range match the current paused top frame.

## Frame and value policy

The debugger inspects only the paused top frame. It first proves the frame
shape and stack bounds, then validates the shared final-ELF record, computes
the requested `RBP + frameOffset` address with overflow-safe checks, and reads
only the declared 4-byte or 8-byte slot. The hosted path uses the runtime's
owned stack image; the bare-metal path uses the same bounded stack ownership
rules. A failed read reports unavailable rather than guessing a value.

Source stepping can change a local's value while preserving its identity and
slot. The Phase 28G fixture observes `adjusted` as 14 before the assignment
and 15 after stepping into the next source location. Stepping out changes the
top frame to `gx_main`; stale-generation and running-state queries remain
rejected.

## ABI and implementation surface

The new hosted and bare-metal callback slots are appended after the Phase 28F
call-stack slots. Existing slots remain unchanged. The implementation covers
the SDK header, hosted runtime table, bare-metal runtime table, development
run services, compiler IR/parser/backend/module/linker, object format, ELF
writer, and both debugger implementations.

## Tests and smoke proof

Host coverage includes compiler-object metadata round-trip and link/ELF
resolution, public ABI layout assertions, hosted runtime callback routing,
native ELF runtime compilation, and the existing compiler regression matrix.
The Phase 28G fixture is under
`scripts/fixtures/phase28g/`. Its smoke sequence performs a warm-cache build,
entry pause, call-stack regression, argument/local query, repeated-query
read-only comparison, source-step value change, live-range check, step-out,
stale rejection, resume rejection, GUI rendering, cleanup, and a normal-run
regression. The bootstrap harness supports `-Phase28GOnly`, fresh per-boot
ESP copies, serial artifact export, and external ELF inspection.

The QEMU proof is driven by the existing serial harness and its compiler-built
GUI path; it does not add a separate standalone Developer Studio UI. The
standalone repository is intentionally outside this phase and remains
unchanged. On hosts where the existing MSVC SDK include-name collision is
present, the authoritative compiler/runtime checks use the repository's
documented GNU include ordering and preserve the pre-existing warning set.
