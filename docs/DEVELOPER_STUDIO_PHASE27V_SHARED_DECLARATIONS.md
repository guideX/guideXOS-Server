# Developer Studio Phase 27V — Shared Declarations and Header Dependencies

Phase 27U proved structs, arrays of structs, typed struct pointers, pointer
arithmetic, persistent ET_REL objects, incremental reuse, and NativeElf
execution. Its cross-file example still repeated a compatible `struct Point`
definition in each translation unit. Phase 27V adds the first bounded shared
declaration boundary without turning the compiler into a C preprocessor.

## Architecture

The production incremental driver recognizes only quoted local includes:

```c
#include "point.h"
```

The driver reads the source and recursively loads declaration text through the
guideXOS VFS. It expands only declaration-file include directives into the
existing parser input; it does not create a second header type system. Headers
can contain the existing struct definitions, scalar declarations, and function
prototypes, including:

```c
struct Point { int x; int y; int tag; };
extern int point_value(struct Point* p);
```

`extern int f(...);` function declarations are accepted by the same function
declaration path as ordinary prototypes. Header function declarations emit no
body or storage. Struct definitions describe layout only.

`#define`, conditionals, angle-bracket includes, pragmas, and other
preprocessor directives are rejected with a bounded diagnostic. This is a
declaration loader, not macro preprocessing.

## Resolution, duplicates, and cycles

The compiler derives the project root from the actual VFS source path and its
project-relative source identity. A requested header is tried in this order:

1. the including declaration's project-relative directory;
2. the project root;
3. `projectRoot/include/`.

Separators are normalized to `/`; absolute paths, empty components, `.` and
`..` components are rejected. The resulting path must remain project-relative.
Canonical path text is the dependency identity. A header is loaded once per
translation unit even if multiple include edges reach it. An active canonical
path encountered again is an include-cycle error. Nested includes are
supported to the bounded depth.

## Bounds

The implementation rejects rather than truncates when limits are exceeded:

* 32 declaration dependencies per object;
* 64 KiB total expanded declaration/source input;
* depth 8 nested includes;
* 16 KiB per declaration file;
* 16 KiB dependency metadata payload;
* existing source, token, object, diagnostic, struct, and ABI-contract limits.

## Type identity and ABI validation

`StructTypeIR::identity` is a deterministic FNV-1a fingerprint independent of
translation-unit table indexes or addresses. It incorporates the tag, field
count, ordered field names, field kind/element count, field offsets, field
sizes, field alignment, total size, and struct alignment. Thus field order,
field type, pointer/aggregate shape, and computed layout are part of the
contract. Same-name structs with different fingerprints are rejected; the
linker does not use last-declaration-wins semantics.

Supported function import/export contracts persist parameter count, parameter
kind, and struct identity for struct-pointer parameters. The linker compares
return/function kind as represented by the current compiler, parameter count,
parameter kinds, and parameter struct identities before relocation and final
ELF publication. `point_value(struct Point*)` therefore cannot resolve to
`point_value(int)` or to a definition using an incompatible `Point`.

External scalar data already retains the existing bounded data signature. No
new aggregate-global ABI is invented in this phase; function pointer-to-struct
contracts are the required cross-file proof.

## Persistent dependency metadata

The object format remains standard ELF64 `ET_REL`, `EM_X86_64`, with compiler
metadata in `.gx.meta`. The format remains 2 and the compiler-object ABI is
bumped from 6 to 7 because ABI-6 objects cannot prove declaration dependency
validity. The `.gx.meta` header is 104 bytes in ABI 7 and records dependency
count and dependency metadata bytes in addition to the existing bounded
metadata.

Each dependency record contains:

* canonical project-relative path;
* exact file byte count;
* FNV-1a content hash.

Records are sorted by canonical path before serialization. On a later build,
the driver reopens the object, validates the ELF and metadata bounds/checksum,
derives the same project root, reads every dependency through VFS, and
rehashes it. Cache validity is source identity plus source hash plus compiler
object ABI plus every persisted dependency path/size/hash. Because the full
transitive set is persisted, a change to a nested header invalidates every
dependent object even when its immediate include file is unchanged.

Malformed metadata, old ABI objects, missing dependencies, and hash mismatches
reject the cache entry and cause a source rebuild attempt. Publication uses the
existing serialize/deserialize and temporary-sibling path checks; failed
compilation or linking does not replace the previous authoritative ELF.

## Proof project and results

The focused project is staged as `/P27V` with `include/point.h`, nested
`include/common.h`, and `src/main.cpp`, `src/math.cpp`, and `src/util.cpp`.
`main.cpp` uses `Point points[3]`, indexed field stores, `&points[0]`, and
scaled `Point*` traversal. `math.cpp` consumes the same header and defines
`point_value(Point*)`; `util.cpp` provides `shared_bias()` without including
the Point header.

The in-OS sequence proves:

* clean: 3 compiled, 0 reused, objects closed/reopened, native result 235;
* warm: 0 compiled, 3 reused, native result 235;
* deterministic object regeneration: dependent `.gxo` bytes match;
* header edit adding `bonus`: main/math rebuilt, util reused, native result 245;
* header/source restoration: main/math rebuilt, util reused, result 235;
* build-service execution followed by a second service invocation: 0 compiled,
  3 reused, result 235;
* missing header, malformed header, corrupt object, include cycle, and path
  escape all fail safely with the prior valid artifact still runnable;
* incompatible `Point` layout fails before link publication;
* `point_value(int)` against the shared `Point*` declaration fails ABI
  validation before link publication.

The QEMU marker is emitted only after all of these checks pass:
`DEVELOPER_STUDIO_PHASE27V_PASS`.

## Validation

Focused host results:

* `compiler_object_host_test`: PASS, including ABI-7 dependency metadata
  round-trip and deterministic object bytes;
* `compiler_multifile_host_test`: PASS, including `extern Point*`, compatible
  cross-file linking, struct mismatch rejection, and function ABI mismatch;
* Phase 27T structs host test: PASS;
* Phase 27U struct-array host test: PASS;
* Phase 27U pointer host test: PASS.

The existing Phase 27U QEMU proof was rerun after the driver root-derivation
compatibility fix and passed during implementation. The existing Phase 27P
host/build-service regression remains covered by its production persistent
object smoke; Phase 27V uses the same incremental driver and adds its own
build-service recreation check.

## Limitations and Phase 27W recommendation

This phase intentionally does not add macros, conditionals, system headers,
aggregate initialization, pointer indexing, struct-by-value ABI, unions,
bitfields, optimization, parallel builds, or a general project build system.
The low-level `compile_module_from_source` API still expects already-expanded
source; header discovery is part of the production incremental/build-service
path. Diagnostics for include directives use the bounded driver location
because the current parser consumes the expanded declaration stream.

Phase 27W should add source/header location mapping for diagnostics and, if
needed by the product, a small explicit header-aware parser API. It should not
expand the include facility into macro preprocessing or weaken the persisted
object contract.
