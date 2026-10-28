# Developer Studio COMP4E — Character and Byte Support

**Outcome:** `COMP4E_HOSTED_ACCEPTED`
**Branch:** `v0.5_DEVELOPER_STUDIO`
**Starting HEAD:** `471458208dd9a754fe340d360cb85fe666a7ed58`
**Qualification:** Hosted production-compiler tests only. No QEMU reservation or guest qualification was used.

## Preflight and baseline

The required repository and branch were present, and the starting HEAD matched the requested value. At preflight the branch was zero commits behind and one commit ahead of `origin/v0.5_DEVELOPER_STUDIO`; the ahead commit was the expected COMP4D guest Build/Run qualification. There were no tracked or staged edits. Existing untracked validation directories under `out/` and `kernel/out/` were preserved. No `AGENTS.md` or separate phase-marker file was found. No other checkout was modified.

The COMP4A–COMP4D reports and implementation were reviewed. Before implementation, all 14 COMP4A–COMP4D hosted compiler suites passed, as did the native ABI layout test and QAC acceptance source checks.

## Type-system audit before COMP4E

- The lexer produced integer and string-literal tokens. `char` was recognized only as a narrow `char*` function-parameter spelling for existing host-call signatures; a quote-delimited character literal was not a value.
- The expression IR represented signed 32-bit integers, integer pointers, struct values/pointers, and string pointers. Locals supported 32-bit integers, integer arrays, pointer descriptors, and structs. Structure fields were 32-bit integers.
- Integer parameters and returns used the existing 32-bit guideXOS C ABI. Existing `char*` host-call parameters used a raw 64-bit pointer. The language had no character parameter or return type.
- Local frame placement and scalar loads/stores used four-byte slots and dword operations. Arrays carried an element size for address calculation and bounds checks, but accepted integer elements and struct elements only; indexed load/store operations were dword-width.
- String literals were stored as bounded, NUL-terminated read-only data. A string-literal expression produced the existing string-pointer value for supported host calls. The compiler did not provide general byte access through those pointers.

These facilities made the existing bounded frame accounting, array checks, and AMD64 emitter reusable. The missing pieces were literal tokenization, byte-sized local layout, type-directed byte loads/stores, and validation that kept character arrays out of integer-pointer paths.

## Character-literal contract

An ordinary character literal contains one printable ASCII character and has the compiler's ordinary integer-literal type, signed `int`. The accepted escapes are `\n`, `\r`, `\t`, `\\`, `\'`, and `\0`. They produce the corresponding integer values. Empty and unterminated literals, invalid escapes, multicharacter constants, raw non-ASCII encodings, and `L`, `u`, `U`, or `u8` encoded character forms are rejected with the literal's source location.

This is a bounded subset, not a complete C character-constant grammar. In particular, octal and hexadecimal escapes, universal character names, wide characters, and multibyte encodings are unsupported.

## Character storage and conversions

Local `char` scalars and fixed-size local `char` arrays occupy one byte per scalar or array element. Integer and struct locals retain four-byte alignment, pointer descriptors retain eight-byte alignment, and character storage can pack into the intervening bytes. Frame size accounting uses the real byte count plus required alignment padding, within the existing 256-byte local-storage cap.

The implementation defines plain `char` as signed 8-bit two's-complement storage. Assigning an `int` to `char` retains its low eight bits, then a later read interprets those bits as the signed range -128 through 127. Reading a character value sign-extends it to `int` before arithmetic, comparison, calls to existing `int` parameters, or an `int` return. Character literals themselves already have type `int`. This makes boundary results deterministic: storing 128 reads as -128, storing 255 reads as -1, and storing 258 reads as 2.

Scalar initialization and assignment accept supported `int` expressions. Character arrays support direct indexed reads and writes; array initializers are rejected, and dynamic indices retain the existing negative and upper-bound checks. Local array length is bounded to 64 elements, and aggregate local storage remains subject to the existing frame limit. Character arrays do not decay to `int*` and their elements cannot be addressed as `int*`.

Global characters, structure character fields, by-value `char` parameters, `char` return declarations, local `char*` variables, general pointer casts/arithmetic, and reads through string-literal `char*` values remain unsupported. Existing string-literal bytes, NUL termination, and host-call ABI representation were not changed.

## AMD64 lowering and ABI

The production AMD64 backend emits `mov byte ptr [rbp+disp32], al` for scalar stores (`88 85` plus displacement) and `movsx eax, byte ptr [rbp+disp32]` for scalar reads (`0F BE 85`). Indexed character arrays use byte stores (`88 02`) and signed byte loads (`0F BE 02`) through the already bounds-checked effective address. Integer and struct operations keep their existing widths. No dword store is used for a character scalar or array element.

Each local symbol now has an identity separate from its byte offset. Its byte offset records the actual end of its allocation below RBP, so adjacent character locals may share a former dword without sharing storage. Integer and structure locals remain aligned; arrays scale indices by their real element size. The backend continues to use its existing volatile register set and frame planner, preserving 16-byte call alignment and keeping byte operations in AL/EAX without clobbering adjacent storage.

No NativeElf Build API, snapshot, host-call table, or Run ABI layout changed. `COMPILER_OBJECT_ABI_VERSION` advanced from 10 to 11 so persistent object-cache entries cannot be reused across the changed code-generation contract.

## Diagnostics and bounds

Malformed and unsupported character literals fail in the lexer with lexical or unsupported diagnostics at the opening quote/prefix. Invalid scalar initializers, character-array initializers, and character-array element types are explicitly rejected. Unsupported character pointer/global forms are explicitly rejected. Local array lengths above the existing element cap use the resource-limit diagnostic category. The focused test checks the reported byte offset, line, and column.

The COMP4E production Build-service regression compiles a scalar character and a two-element character array. A subsequent unterminated-character Build fails with the lexical category, publishes no artifact identity, removes its temporary artifact, and preserves the previously successful artifact bytes. Cooperative polling/cancellation, generation ownership, sidecar publication/rollback, and stale-artifact Run gating were not changed.

## Hosted qualification

The new `run-compiler-character-bytes-host-test.ps1` test uses the production lexer, parser, and AMD64 backend. It inspects emitted AMD64 byte opcodes and executes generated code on the Windows host. Execution covers character initialization and reassignment, signed and truncating boundaries, integer promotion to an existing `int` parameter, comparisons, escape values, adjacent integer and character locals, four-byte character-array indexing, and runtime array-bound failure.

The COMP4B cooperative Build-service hosted fixture now includes character scalar/array source and failed-Build artifact-preservation checks. All 14 existing COMP4A–COMP4D hosted compiler suites passed again, along with the new character/byte suite. The native ABI layout test and QAC acceptance source checks passed. These are hosted results; no guest machine code was run in this phase.

## Remaining limits and follow-up

This remains a bounded C-like language, not general C or C++. Character globals, structure fields, character-typed function signatures, arbitrary byte pointers, general string indexing, wide/encoded literals, standard headers, and runtime string operations remain out of scope. Guest behavior for character and byte-array code is **unverified** pending a later guest acceptance phase.

The COMP4D report's VFS observation remains a follow-up: one intermediate diagnostic boot reported `VFS_ERR_NOT_SUPPORTED` while creating `/P27W/build/obj/amd64`; it did not recur in subsequent boots, including the accepted boot. COMP4E did not change VFS code. Revisit that issue only if it can be reproduced with concrete evidence.

**Recommended COMP4F:** perform the genuine guest Build/Run acceptance for this hosted-qualified character subset. Verify scalar signedness, byte-array bounds and adjacency, malformed-literal Build rejection, prior-artifact preservation, and the existing COMP4A–COMP4D guest behaviors. Keep the language scope bounded; consider global/field or general character-pointer support only as separately justified work. Do not start COMP4F as part of this closeout.
