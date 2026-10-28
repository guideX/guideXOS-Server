# Developer Studio COMP4J — Typed Unsigned-Byte Pointers

**Status: `COMP4J_HOSTED_ACCEPTED`**

**Repository:** `D:\dev\guideXOSServerV0.5_DEVELOPER_STUDIO`

**Branch:** `v0.5_DEVELOPER_STUDIO`

**Starting HEAD:** `f9b46384375cc2169fbe664ed48b5230841c9442`

**Ending implementation HEAD:** `bb8837bd682478025ba404426aea48e29d208ea5` (COMP4J implementation commit).

**Final documentation closeout HEAD:** reported in the task closeout.

**Qualification:** hosted only; no QEMU launch and no QAC admission.

COMP4J adds a bounded `unsigned char*` subset to the production guest-native compiler. It reuses the compiler's descriptor-backed pointer model and keeps unsigned-byte pointers distinct from `int*`, struct pointers, and the existing narrow string `char*` parameter form.

## Existing pointer architecture

The compiler already supports bounded `int*` and typed struct pointers. A local pointer value occupies a 32-byte frame descriptor containing the current address, source-object base, byte extent, element size, provenance kind, and initialized marker. Pointer parameters are passed through the guideXOS four-register convention as 64-bit descriptor addresses and copied into callee-owned descriptors. The language does not expose these descriptors as integers or allow arbitrary casts.

Before COMP4J, the parser and backend had no unsigned-byte pointer value kind. Unsigned-byte scalar and array storage, direct indexed access, function byte parameters, and structure byte fields already used distinct signedness-aware metadata. COMP4I intentionally rejected unsigned-byte pointers.

## New supported operations

The compiler accepts local declarations and initializers such as:

```c
unsigned char value = 255;
unsigned char bytes[4];
unsigned char* scalar = &value;
unsigned char* data = bytes;
```

The supported subset includes:

- Address-of a local `unsigned char` scalar or local unsigned-byte array element.
- Controlled decay of a local `unsigned char` array to a matching `unsigned char*`.
- `*p` reads and `*p = int_expression` writes.
- `p[index]` reads and writes for constant or variable integer indexes.
- `p + integer` and `p - integer`, scaled by one byte. One-past positions may be formed but cannot be dereferenced.
- Up to four typed function parameters, nested calls, and cross-module declarations using `unsigned char*`.
- Byte-value promotion to signed 32-bit `int` after zero-extension, and narrowing stores that retain the low eight bits.

The frame, local-array, expression, temporary, four-parameter, and generated-code limits remain the existing limits. Global pointer variables, pointer returns, pointer casts, arbitrary pointer construction, signed `char*` loads/stores, and general array decay remain unsupported.

## Bounds and pointer contract

For pointer locals whose origin and current offset remain statically known, constant subscripts that fall outside the source scalar or array are rejected during parsing. The parser drops that compile-time fact across `if` and `while` control flow or when a pointer is assigned an unknown value; this avoids treating a parse-time path as a proof about runtime control flow.

Every supported runtime pointer access validates the descriptor's initialized marker, element size, current address, source base, and byte extent before touching memory. Unknown variable indexes and pointer parameters therefore receive runtime bounds checks against the carried source extent. Pointer arithmetic checks signed offsets, address wraparound, and one-past limits. An invalid dereference exits through the existing runtime status path; hosted tests do not execute invalid host memory accesses.

These checks apply to the compiler's supported descriptor-backed subset. They do not establish full C pointer semantics or a general memory-safety guarantee for arbitrary addresses. The source language cannot cast an integer or raw address into this pointer type.

## AMD64 code generation

Unsigned-byte pointer reads use a one-byte memory load followed by zero-extension (`MOVZX EAX, byte ptr [RDX]`, bytes `0F B6 02`). Indirect writes use a byte-width store (`MOV byte ptr [RDX], AL`, bytes `88 02`), preserving adjacent bytes. Typed pointer arithmetic multiplies offsets by element size; the hosted suite inspects the byte-pointer scale-one instruction and executes the emitted code.

`char*` continues to use its existing string-literal representation. `char*`, `int*`, `unsigned char*`, and struct-pointer parameters have distinct `ParameterKind` values. Assignment, initialization, direct calls, declarations, object exports/imports, and cross-module linking reject mismatched pointer types.

## Compiler object ABI

The public object format remains version 2. The internal compiler object ABI advances from 14 to 15 because function export/import signatures now serialize a distinct unsigned-byte pointer parameter kind. The object reader accepts the new kind, and the existing ABI identity check rejects cached ABI-14 objects. The hosted suite verifies deterministic object serialization, signature round-trip, current format/ABI values, and rejection of a checksum-valid ABI-14 object.

The public Build API and NativeElf host-call layout are unchanged.

## Hosted qualification evidence

`tests/compiler_unsigned_byte_pointers_host_test.cpp` is the dedicated COMP4J production-compiler suite; run it with `scripts/run-compiler-unsigned-byte-pointers-host-test.ps1`. It parses through the production lexer/parser, emits through the production AMD64 backend, inspects the emitted byte instructions, and executes valid generated-code fixtures on the host.

Coverage includes local declarations, scalar address-of, array decay, dereference and assignment, pointer indexing and byte scaling, zero-extension, byte truncation, neighboring integer sentinels, repeated variable-index reads/writes in a loop, nested pointer calls, callee writes through copied descriptors into the caller's buffer, signed-byte versus unsigned-byte behavior, cross-module linking and signature mismatch rejection, object round-trip and ABI rejection, static constant bounds rejection, runtime invalid indexes, uninitialized descriptors, and pointer arithmetic bounds failures.

The suite passed **30/30 assertions**, including the 64-byte local-array limit, unsupported pointer conversions, mismatched cross-module signatures, and runtime invalid accesses. Existing COMP4A–COMP4I hosted regressions also passed: arrays, bootstrap, Build job manager, cooperative Build lifecycle, character bytes, character functions, driver diagnostics, functions, globals, multifile linking, object serialization, existing pointer behavior, signed arithmetic, struct arrays, struct byte fields, structs, and COMP4I unsigned-byte semantics. The stack-frame bound suite, NativeElf ABI layout test, and QAC acceptance-helper suite passed as well. The cooperative Build suite covers failed-build artifact preservation, cancellation, publication/rollback, and Run-generation ownership. A concurrent struct-array runner invocation returned nonzero; its binary passed directly and the complete runner passed again in an isolated output directory. The earlier COMP4E/COMP4G diagnostics were refreshed only where their old expectations treated the now-supported unsigned-byte pointer forms as unsupported; their replacement cases still verify unsupported conversions and signature mismatches.

All phase-specific outputs were written under `out/comp4j-validation/`. QAC helper verification used its hosted fixtures only; no QEMU launch or QAC admission occurred. COMP4D's prior intermittent VFS observation remains preserved and was not speculatively repaired in COMP4J.

## Guest qualification and next phase

`COMP4J_HOSTED_ACCEPTED` is a hosted result only. COMP4F remains `HOSTED_READY_GUEST_PENDING`; COMP4H and COMP4I guest qualification also remain pending. COMP4J guest qualification is added to the consolidated compiler guest-acceptance backlog. A future guest run must rebuild fixtures and cached objects with the current object ABI 15. No QEMU was launched and no QAC admission was requested because .NET Support retains guest-testing priority.

Recommended COMP4K scope: wait for the compiler guest backlog to receive capacity, then qualify the current typed byte-pointer fixtures through guest Build/Run with freshly rebuilt ABI-15 objects. After that gate, consider only a small read-only byte-view extension if a concrete language use requires it; keep unrestricted casts and general pointer escape out of scope.
