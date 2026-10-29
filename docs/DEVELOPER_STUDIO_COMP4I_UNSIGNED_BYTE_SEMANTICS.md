# Developer Studio COMP4I — Unsigned Byte Semantics

**Outcome: `COMP4I_HOSTED_ACCEPTED`**

**Qualification: hosted only**
**Date: 2026-10-28**

## Revision record

- Starting checkout: `D:\dev\guideXOSServerV0.5_DEVELOPER_STUDIO`
- Branch: `v0.5_DEVELOPER_STUDIO`
- Starting HEAD: `2f7e85d6f8cf3f1c20cf5ebdf912365da14d786d`
- Ending implementation HEAD: `6c6a27edc4e4aaefaf9a9df93a30a766dd04ee2a`
- Upstream divergence at start: 0 behind, 6 ahead of `origin/v0.5_DEVELOPER_STUDIO`

The implementation is committed locally. This report is committed separately as documentation closeout; the final repository HEAD is recorded in the task closeout.

## Signedness and representation

Before COMP4I, signedness was inferred from the storage or signature kind. Plain `char` used one-byte scalar, array, parameter, return, and structure-field kinds. A signed 32-bit `int` was the ordinary expression type. Character loads sign-extended to `int`; assignments stored the low byte. Expressions therefore did not carry a separate signedness bit, and byte types promoted at expression reads.

COMP4I preserves that design and adds distinct unsigned-byte kinds for scalar and array storage, structure fields, parameters, and returns. Indexed byte expressions now carry an element-kind discriminator so the backend can choose extension based on the declared array type. Signed and unsigned structure fields share one-byte layout but contribute different kinds to structure identity. Typed pointers remain the existing `int*` and struct-pointer subset; character pointers do not provide general byte access.

## Language and conversion behavior

Supported forms include local `unsigned char` scalars and fixed local arrays, unsigned-byte fields in supported structures and structure arrays, and unsigned-byte function parameters, prototypes, and returns. A byte occupies one byte. Narrowing an integer expression to `unsigned char` stores its low eight bits: `127 → 127`, `128 → 128`, `255 → 255`, `256 → 0`, `300 → 44`, and `-1 → 255`. Reading it promotes to signed 32-bit `int` with a value in the range 0–255. Plain `char` remains signed and promotes by sign extension. `int` remains signed 32-bit; arithmetic and comparisons continue to use the existing signed integer rules after promotion.

Array indexing retains the existing bounds checks and frame limits. Array stores write one byte, preserving adjacent elements. Structure layout keeps the COMP4H rule: each field is aligned to its width and the final size is padded to the largest field alignment. An unsigned byte does not change layout: byte fields are one byte, `int` fields are four bytes. Signed and unsigned byte fields can coexist, and their loads retain their respective extension behavior.

## AMD64 and function behavior

The backend uses zero-extending byte loads (`MOVZX`) for unsigned scalar locals, indexed array elements, structure fields, and the normalized unsigned-byte return in EAX. Existing signed `char` loads remain sign-extending (`MOVSX`). Both byte kinds store AL to one-byte storage. The parameter prologue stores the low argument-register byte; reads from an unsigned-byte parameter zero-extend it. Call expressions are integer expressions, so values are promoted after the callee returns. Argument truncation occurs at the parameter's byte-storage boundary, including nested and cross-module calls.

These signatures use the existing guideXOS four-register calling convention. COMP4I adds no stack arguments and makes no claim of general external x64 ABI interoperability. Signed `char`, `unsigned char`, and `int` remain distinct parameter and return kinds during declaration matching, object serialization, and linking.

## Object compatibility

Compiler object ABI advanced from 13 to **14**; object format remains version 2. The object metadata now admits appended unsigned-byte structure-field, parameter, and return kinds. Since the new kinds change object-producing semantics and serialized signatures, ABI 13 cached objects are rejected instead of interpreted under ABI 14. No public Build API, NativeElf host-call layout, or object format version changed.

## Hosted evidence

The dedicated runner is `scripts/run-compiler-unsigned-character-host-test.ps1`. It ran with output under `out/comp4i-validation/` and passed all three production-compiler fixtures:

- Byte storage and arrays: boundary conversions, scalar and indexed `MOVZX`, signed `MOVSX`, byte stores, adjacent element integrity, execution of emitted AMD64, array bounds and frame limits, and rejection of unsupported declarations.
- Function ABI and objects: 32 assertions covering parameter/return truncation and promotion, mixed signed/unsigned signatures, nested calls, emitted operations, cross-module serialization/linking/execution, signature mismatch rejection, and ABI metadata.
- Structure fields and layout: 41 assertions covering mixed-width offsets and size, signed/unsigned loads, neighboring-field integrity, multiple instances and struct arrays, cross-module field identity, and rejection of ABI 13 cached objects.

The complete COMP4A–COMP4H hosted regression set passed: driver diagnostics, bootstrap, Build job manager, cooperative Build service and lifecycle, arrays, functions, globals, pointers, signed arithmetic, stack-frame bounds, structures, structure arrays, multifile compilation, object serialization, and native ABI layout. This includes Build failure preservation, cancellation, generation ownership, publication, and rollback coverage in the cooperative lifecycle suite. The QAC acceptance-helper checks also passed, including their fake loopback QMP fixture. No QEMU was launched and no QAC admission was requested.

All new validation outputs were written under `out/comp4i-validation/`. Existing unrelated untracked validation artifacts were preserved. `git diff --check` passed.

## Remaining limits and consolidated guest backlog

COMP4I did not add `unsigned int`, wider integer types, general unsigned arithmetic, casts, bitwise operations, standalone character globals, or general character-pointer loads and stores. It did not add general C array decay or initializers. These forms remain outside the bounded language subset.

Guest qualification remains pending for COMP4F character/byte behavior (`HOSTED_READY_GUEST_PENDING`), COMP4G character function ABI, COMP4H mixed-width structure fields, COMP4I unsigned-byte semantics, and COMP4J typed unsigned-byte pointers. COMP4J is hosted accepted; its detailed implementation and proof record is [DEVELOPER_STUDIO_COMP4J_UNSIGNED_BYTE_POINTERS.md](DEVELOPER_STUDIO_COMP4J_UNSIGNED_BYTE_POINTERS.md). No QEMU/QAC guest result is claimed for these phases. Any combined guest acceptance must rebuild fixtures and cached compiler objects using the current internal compiler object ABI 15 and must not reuse objects from ABIs 13 or 14.

COMP4K has completed hosted preparation as `COMP4K_HOSTED_READY_GUEST_PENDING`. Its consolidated `/P27W` guest Build/Run fixtures cover character/byte behavior, signed and unsigned function ABIs, mixed-width structure arrays, bounded unsigned-byte pointers, incompatible signatures, failed-Build preservation, fresh ABI-15 objects, and Run identity rejection. The kernel harness compiled and the hosted matrix passed, but guest Build/Run evidence is still pending. .NET Support retains QEMU priority; no QAC admission or QEMU launch is authorized until an explicit baton release. See [DEVELOPER_STUDIO_COMP4K_CONSOLIDATED_GUEST_COMPILER_QUALIFICATION.md](DEVELOPER_STUDIO_COMP4K_CONSOLIDATED_GUEST_COMPILER_QUALIFICATION.md) for the exact preparation results and pending guest gates.

## Recommended COMP4K scope

Do not expand the language before the pending COMP4F/G/H/I/J guest backlog receives qualification capacity and COMP4K's authorized consolidated guest run completes. Then consider only a small read-only byte-view extension if a concrete language use requires it. Keep unrestricted casts and general pointer escape out of scope.
