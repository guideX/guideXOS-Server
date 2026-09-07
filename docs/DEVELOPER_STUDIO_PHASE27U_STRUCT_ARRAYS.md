# Developer Studio Phase 27U — Arrays of Structs and Typed Struct Pointers

Phase 27U extends the Phase 27T struct model with bounded arrays of structs,
addressable elements and fields, typed struct pointers, and scaled pointer
arithmetic. The implementation is production compiler/linker/backend code and
is exercised both by a host suite and by the in-kernel compiler plus NativeElf
QEMU proof.

## Boundary and representation

The language remains the bounded bootstrap C-like language from Phase 27T:
integer fields only, no preprocessor or headers, no by-value struct parameters,
and AMD64 only. A declaration such as:

```text
struct Point { int x; int y; int tag; };
struct Point points[3];
```

uses `StorageKind::ArrayStruct`. A scalar struct continues to use
`StorageKind::Struct`; the distinction is explicit in IR so a bare array value
cannot accidentally be used as a scalar or pointer.

Struct layout is deterministic and currently follows the existing integer
layout rules: fields are 4-byte aligned, fields are placed in declaration
order, and the total size is the checked sum of field sizes rounded to the
struct alignment. Therefore the three-field `Point` is 12 bytes and an array
of three has 36 bytes. The array stride is exactly the checked struct size;
there is no hard-coded 4-byte array stride.

Local arrays are allocated as one contiguous frame region. The local symbol
records element count, element size, total size, and the highest logical stack
word used by the existing frame layout. Zero initialization covers the whole
aggregate. The existing bounds remain active: at most 64 local array elements,
256 bytes of local storage, 16 fields per struct, and 256 bytes per struct.
Global struct arrays are mutable `DataStruct` symbols with the same count,
stride, total-size, and struct identity metadata. Their storage is zero-filled
and bounded by the existing 8192-byte linked mutable-data limit.

## Expressions and type safety

The parser now composes the following lvalue and address forms:

```text
points[1].x
&points[1]
&points[1].x
p->x
p = &points[1]
p = p + 1
```

`AddressOfIndexed` carries the struct type index, element count, and element
size. `AddressOfField` preserves the field subobject extent and integer
pointer type. `LoadStructPointer` and `StoreStructPointer` are explicit IR
operations, so `.` and `->` cannot silently interchange. A struct pointer is
not an untyped integer: its descriptor carries current address, base, extent,
element size, provenance, and validity, while the compile-time IR carries the
struct type index.

The parser rejects invalid arrow operands, missing fields, pointer/integer
mixing, constant out-of-range indexes, pointer-array declarations, and
oversized or overflowing struct arrays. Struct arrays do not decay implicitly
as a new language rule; an indexed element is the addressable unit in this
phase.

## Backend and runtime safety

The AMD64 backend computes indexed addresses as:

```text
element_address = array_base + index * struct_size
```

and scales typed struct-pointer addition/subtraction by the same compile-time
element size. The generated code uses a dedicated multiply path for strides
other than 4; the Phase 27U fixture proves the `imul ..., 12` sequence for the
12-byte `Point`. No ARM64 implementation is added.

Before a struct-pointer field access, NativeElf code validates the descriptor,
struct identity, 4-byte alignment, nonzero stride, and that the current
address is inside the represented element range. The one-past position is
representable for arithmetic but is rejected for field access. Addition and
subtraction check overflow and cannot escape the descriptor extent. Existing
integer-pointer provenance and bounds checks remain unchanged.

The feature uses the existing relocation forms. A global array element or
field still becomes the standard 64-bit global-data relocation; no new
relocation type or loader policy is required.

## Object and incremental build behavior

Phase 27P already persisted struct descriptors, global metadata, element
counts, element sizes, and total sizes in the ELF64 ET_REL `.gx.meta` payload.
Phase 27U validates and round-trips the new `ArrayStruct` metadata through the
same object path, so the object format and compiler-object ABI remain:

```text
COMPILER_OBJECT_FORMAT_VERSION = 2
COMPILER_OBJECT_ABI_VERSION    = 6
```

No bump was necessary because no serialized field or code-generation contract
was added beyond metadata already represented by the Phase 27P object
contract. Any future incompatible layout or backend change must bump the
appropriate version in `compiler_ir.h`.

The focused in-kernel build uses three translation units (`main.cpp`,
`math.cpp`, and `types.cpp`) and the persistent object service:

| Transition | Compiled | Reused | Result |
| --- | ---: | ---: | --- |
| clean | 3 | 0 | 40 |
| warm/reopen | 0 | 3 | 40 |
| deterministic regeneration | 3 | 0 | identical objects and ELF hash |
| `math.cpp` edit | 1 | 2 | 41 |
| syntax failure | 0 | 2 before failure | prior 41 artifact survives |
| source restore | 1 | 2 | 40 |

The objects are closed, reopened, deserialized, linked, and executed through
NativeElf. The native proof also compiles and runs nested field-address and
global struct-array programs, and emits the final ELF for an independent
host-side ELF header/program-header audit.

## Verification

Host verification:

```text
scripts/run-compiler-struct-arrays-host-test.ps1
DEVELOPER_STUDIO_PHASE27U_HOST_PASS
```

The host test checks 12-byte stride layout, indexed loads/stores, pointer
aliasing, `&array[i].field`, mutation and element isolation, cross-file typed
struct-pointer linking, deterministic persistent objects, object reopen, and
negative diagnostics.

Authoritative native verification:

```powershell
.\scripts\smoke-compiler-bootstrap.ps1 -Phase27UOnly -BootCount 1
```

The run passed the Phase 27U markers, the exact central marker
`DEVELOPER_STUDIO_PHASE27U_PASS`, native return values 40/41/99/40,
object reopen, failure recovery, teardown, and external ELF inspection. The
existing Phase 27B/27C/27D route also remained green in that boot.

## Scope limits and next phase

Deferred items are pointer indexing syntax (`p[1]`), aggregate initializers,
struct-valued returns or assignments, nested/non-integer field types, headers,
preprocessor/dependency discovery, and ARM64. The exact recommended Phase 27V
scope is the declaration/header dependency model: add normalized header-like
shared declarations, dependency fingerprints, and cross-file struct identity
validation while preserving the current object ABI and bounded language. Do
not combine that work with pointer indexing or general aggregate initialization.
