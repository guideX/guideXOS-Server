# Phase 27U fixture

This three-file project is the bare-metal arrays-of-structs proof. It uses a
three-field `Point` (12-byte stride), traverses three independent local
elements through `Point*`, mutates the middle element through `->`, calls a
cross-file struct-pointer helper, and returns 1594.

The project deliberately has no aggregate initializer, headers, or by-value
struct ABI. Global struct-array and persistent-object checks are exercised by
the in-kernel smoke harness.
