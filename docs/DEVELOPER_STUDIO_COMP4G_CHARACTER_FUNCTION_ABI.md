# Developer Studio COMP4G — Native Character Function Parameters and Returns

**Outcome:** `COMP4G_HOSTED_ACCEPTED`

**Branch:** `v0.5_DEVELOPER_STUDIO`

- **Starting HEAD:** `d9226bb4d91837b0bbdf3bc8c3161fdaef5a8058`
- **Starting upstream divergence:** 0 behind / 3 ahead
- **Ending state:** committed locally as the COMP4G closeout commit; the final commit SHA and divergence are in the closeout response.
- **Guest qualification:** Pending from COMP4F. This phase used hosted production compiler components only. No QEMU was launched and no QAC admission was requested.

## Preflight and scope

The repository, branch, and starting commit matched the task. The tracked worktree was clean. Existing untracked outputs under `out/` and `kernel/out/` were preserved; COMP4G runner outputs were directed to `out/comp4g-validation/`. No branch switch, reset, stash, clean, rebase, push, or change to the Debugging or Tools worktrees was made. No `AGENTS.md` was present.

The COMP4A–COMP4F reports and the current parser, code generator, module/object serializer, linker, Build service, NativeElf entry wrapper, and host test runners were reviewed. COMP4E supplied signed byte storage for local `char` values and arrays. Before COMP4G, ordinary functions accepted no scalar `char` parameter and functions could return only `int`.

The previously documented COMP4D observation—one non-reproduced `VFS_ERR_NOT_SUPPORTED` while creating `/P27W/build/obj/amd64`—is retained without speculative VFS repair. COMP4F guest acceptance remains pending; this work did not change its guest fixture or claim guest results.

## Bounded function type support

Ordinary functions can now declare scalar signed-byte parameters and `char` return types within the existing grammar. For example:

```c
char increment(char value) { return value + 1; }
int combine(char value, int amount) { return value + amount; }
char narrow(int value) { return value; }
```

The source expression language continues to represent scalar arithmetic as signed 32-bit `int`. An expression passed to a `char` parameter is converted when the callee stores the incoming value: only the low 8 bits are stored. Reading that parameter sign-extends the stored byte to EAX, so `128` reads as `-128`, `255` reads as `-1`, and `258` reads as `2`. Character parameters can be assigned within the function, and those writes update only their owned byte.

A `char` return accepts a supported `int` expression. At each return, the callee takes the low byte from AL and executes `movsx eax, al` (`0F BE C0`). The caller therefore receives the signed, promoted value in EAX with no stale upper bits. It can use that result in integer arithmetic/comparisons or pass it onward as another `char` argument, where the next callee again narrows to a byte.

Mixed signatures are supported, including multiple `char` parameters, `int` parameters with `char` returns, and `char` parameters with `int` returns. `gx_main` remains an `int` entry point. Character globals and fields, character-pointer returns, `char**`, unrestricted pointer access, arrays as parameters, variadic functions, and general C/C++ conversions remain unsupported. Existing bounded `char*` parameter behavior is preserved.

## Actual calling convention and frame ownership

The compiler does not implement the full System V AMD64 ABI or full Microsoft x64 ABI. Its bounded `guidexos-c-abi-v1` generated-call convention uses the first four argument registers in this order:

| Argument | Register |
|---|---|
| 1 | ECX / RCX |
| 2 | EDX / RDX |
| 3 | R8D / R8 |
| 4 | R9D / R9 |

There are no stack-passed arguments; declarations and calls are limited to four parameters. The caller evaluates arguments in source order and spills their values to compiler-owned temporary slots before loading the argument registers. Calls reserve the 32-byte home area plus the correction needed to keep RSP 16-byte aligned at the call boundary (32 or 40 bytes under the bounded expression stack states). The hosted code-generation test checks the home-area reservation/release instructions, and nested generated calls execute successfully.

The callee owns frame storage for its parameters. A scalar `char` occupies one byte; an `int` occupies four bytes; a raw `char*` occupies eight bytes; and the current integer/struct pointer descriptor forms retain their existing aligned 32-byte storage. Each parameter records its byte offset, including required alignment padding. Prologue code stores character arguments from CL, DL, R8B, or R9B into those byte slots. Character reads use signed byte loads. Integer parameter storage remains dword-sized, and pointer parameter lowering keeps its previous representation.

`gx_main(gx_app_context*)` remains the NativeElf entry contract: the NativeElf wrapper invokes a Microsoft-ABI entry function with the context pointer in RCX and consumes the signed 32-bit result in EAX. Generated internal calls follow the bounded convention above and preserve the existing runtime call-depth/failure tracking. This describes the compiler/runtime subset; it does not promise interoperability with arbitrary external C or C++ functions.

## Prototypes, objects, and cross-module linking

Named and unnamed prototypes retain parameter kinds, and declaration matching now includes the return kind as well as parameter count, parameter kinds, struct identities, and context form. `extern char name(char)` declarations use the same signature checks. Same-translation-unit conflicts are diagnosed by the parser; import/export records carry character parameter and return kinds, and the production linker rejects mismatched cross-module return signatures.

The object file's physical format remains version 2: the serialized import and export records keep their existing widths, with the previously reserved 16-bit field carrying the return kind in its low byte and a zero high byte. The compiler object ABI advances from 11 to 12 because the signature semantics and parameter representation changed. The deserializer rejects objects with an incompatible ABI, so cached ABI-11 objects cannot be reused under this compiler. Existing object-version migration and rejection regressions pass.

The public Build snapshot, Build API, NativeElf host-call table, NativeElf entry layout, and object format version are unchanged. Character parameters are not currently emitted as debugger variables because the existing debug-variable type model has no signed 8-bit entry; that metadata limitation does not affect compilation or runtime behavior.

## Hosted evidence

The new `compiler_character_functions_host_test` passed **21 assertions**. It compiles through the production lexer, parser, IR, and AMD64 backend; executes emitted AMD64 on the Windows host; and checks:

- one and multiple character parameters, mixed character/integer parameters, integer returns from character parameters, and character returns from integer expressions;
- signed-byte boundaries, low-byte truncation, promotion, mutable parameters, repeated calls, nested calls, and division/remainder using character-returning calls;
- byte-sized parameter stores, signed parameter loads, ECX/R8B argument-register handling, `movsx eax, al` return normalization, byte offsets/alignment, and the aligned home-space reservation;
- a production cross-module path with an `extern char` prototype: compile provider and consumer modules, serialize and reopen both ELF objects, verify ABI-12 signatures, link them, write and validate final NativeElf bytes, then execute the linked AMD64 call;
- located diagnostics for conflicting signatures, return-type conflicts across modules, unsupported pointer forms, invalid returns, malformed declarations, unsupported arguments, and parameter/call resource limits.

The cooperative Build-service fixture now compiles and links a two-module `char increment_character(char)` call. Its existing hosted lifecycle checks also cover failed Build artifact identity, preservation of the prior artifact, cancellation boundaries, publication rollback, and recovery. This uses the production compiler with the existing fake VFS; it is not a guest Build or Run.

All **16 existing COMP4A–COMP4F hosted suites passed**:

1. Driver diagnostics, source mapping, incremental cache, and linker stages.
2. Build-job manager ownership and generation.
3. Compiler bootstrap.
4. Integer arrays.
5. Functions and recursion stack accounting.
6. Globals.
7. Pointers and pointer safety.
8. Structures and typed pointers.
9. Structure arrays and native execution.
10. Compiler object serialization, migration, and rejection.
11. Multi-file linking.
12. Compiler stack-frame bounds.
13. Cooperative Build lifecycle, cancellation, artifact preservation, rollback, and recovery.
14. Signed division and remainder code generation/execution.
15. Character literals, byte storage, and character arrays.
16. Native ABI layout.

The Developer Studio QAC acceptance helper passed **32 source/helper assertions**. It exercised its local command-line and QMP fixtures only; it did not start QEMU, contact QAC, or change scheduler state. The 16 existing suites plus the new COMP4G suite are hosted evidence and do not establish guest execution.

The COMP4G-specific compiler build emitted no warnings. The cooperative Build-service host target emitted two existing `-Wunused-function` warnings for `diagnostic_for` and `build` in the included `compiler_driver_diagnostic_host_test.cpp`; the test passed. No other COMP4G output contained compiler warnings.

## Remaining qualification and COMP4H scope

Actual guideXOS guest behavior remains unqualified. COMP4F's guest Build/Run acceptance is still pending, and hosted instruction execution does not prove the booted guideXOS compiler, object cache, or NativeElf runtime. No QEMU reservation was requested because .NET Support retains primary guest-testing priority.

After COMP4F guest acceptance is separately authorized and QAC priority is available, a future guest-backlog extension may add a small provider/consumer character-function fixture to the pending Build/Run acceptance path and verify signed boundaries, cross-module signatures, artifact preservation, and the actual guest return value. Do not infer those results from COMP4G. A broader COMP4H compiler expansion should first decide whether to add signed-byte debugger variable metadata; it should not expand into general C conversion or pointer semantics without a separate contract.

## Git closeout

The implementation, dedicated hosted suite, Build-service fixture extension, maintained capability matrix, and this report are committed locally. No push was performed. Existing untracked outputs were preserved.
