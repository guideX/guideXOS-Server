# Developer Studio COMP4F — Character and Byte Guest Qualification

**Outcome:** `COMP4F_HOSTED_READY_GUEST_PENDING`

**Branch:** `v0.5_DEVELOPER_STUDIO`

- **Starting HEAD:** `b143df3b3a0ee1c5e9f88ed592ff37b086ec1481`
- **Starting upstream divergence:** 0 behind / 2 ahead
- **Ending HEAD:** COMP4F closeout commit; the exact SHA is included in the task closeout response.
- **Guest acceptance:** Pending. No QEMU was launched and no guest test is claimed as passed.

## Preflight

The required repository and branch matched the brief. The tracked worktree was clean at the start. There was no `AGENTS.md`. Existing untracked validation folders under `out/` and `kernel/out/` were present and were not removed. No branch switch, reset, stash, rebase, clean, push, or change to the Debugging or Tools worktrees was made.

The COMP4A–COMP4E reports, the compiler test scripts, the NativeElf Build/Run smoke, and the QAC admission helper were reviewed. COMP4E was hosted-only. COMP4D's single `/P27W/build/obj/amd64` directory-creation failure remained a non-reproduced VFS observation; the accepted COMP4D boot and later runs succeeded without a VFS change.

## Hosted qualification

All 16 existing COMP4A–COMP4E compiler and ABI suites passed:

1. Driver diagnostics and source mapping.
2. Build job manager ownership and generation.
3. Compiler bootstrap.
4. Integer arrays.
5. Functions and stack accounting.
6. Globals.
7. Pointers.
8. Structures.
9. Structure arrays.
10. Persistent compiler objects.
11. Multi-file linking.
12. Compiler stack-frame bounds.
13. Cooperative Build service, cancellation, and artifact lifecycle.
14. Signed arithmetic code generation and hosted execution.
15. Character and byte code generation and hosted execution.
16. Native ABI layout.

The Developer Studio QAC acceptance helper passed 32 assertions. The smoke PowerShell file parsed successfully, `git diff --check` passed, and the COMP4F kernel image compiled successfully without being booted. Its final focused kernel compile emitted 27 warnings and zero errors; they are existing unused-parameter and smoke-helper warnings, including `kernel_app.h` defaults and helpers omitted by the Phase 27W-focused configuration. The generated kernel is retained at `kernel/out/comp4f-preparation/bin/kernel.elf`.

The hosted character suite exercised supported literals, signed and truncating boundaries, integer promotion, byte loads/stores, adjacent storage, and bounds failures by executing generated AMD64 code on Windows. The cooperative Build service suite exercised character scalar/array compilation, lexical failure, cancellation boundaries, and previous-artifact preservation. These checks use production compiler code hosted with a fake VFS; they are not guest execution evidence.

`COMPILER_OBJECT_ABI_VERSION` is 11 in `compiler_ir.h`. The serializer writes the version into object metadata, and object deserialization rejects a different version. The persistent-object suite passed, including version migration/rejection coverage. The COMP4F guest harness also inspects the ABI version of a real object produced by a guest Build, but that check has not yet run.

The existing `gx_build_snapshot` layout and NativeElf host-call ABI remain the COMP4D layouts. COMP4F changes only smoke/test code and QAC marker selection; it does not change the Build snapshot, host-call table, compiler language ABI, Run identity, or compiler implementation.

## Prepared guest fixtures

The new focused mode reuses the existing `/P27W` NativeElf project, production Build service, and Run service. It writes bounded source text through guest VFS and calls the authentic guideXOS-native compiler. Each successful case checks the Build progress stages, two generated and linked translation units, a generation-bearing handle, a validated artifact identity, the ABI version in `src/value.o`, and the real Run exit code plus deterministic log marker.

The source fixtures are:

- **Character literal:** `phase27w_value()` returns `'A'`; Run must return 65 and log `COMP4F CHAR 65`.
- **Signed scalar:** local `char` values cover `'A'`, 127, 128, and 255. The program verifies 128 promotes as -128 and 255 as -1, then returns 66.
- **Local byte array:** a four-element `char` array stores 65, 127, 128, and 255. It checks signed promotion and integer sentinels around the array, then returns 67.
- **Arithmetic:** promoted `char` values participate in addition, multiplication, signed division, and remainder. The program verifies 64, -5, -3, and -2, then returns 68.
- **Failed Build:** after a successful executable is saved, an unterminated character literal is compiled. The required checks are a lexical diagnostic at `value.cpp` line 1, an empty failed-build artifact identity, preserved prior ELF bytes, and Run rejection of the failed generation.
- **Repeated Build:** successive programs return `'R'` (82) and `'S'` (83). They must have different handles and artifact hashes, and each Run must emit its matching marker.

The focused guest path also retains COMP4B queued cancellation and COMP4D partial-ELF Run rejection/preservation checks. The compiler stage remains non-preemptible; the harness does not claim that cancellation interrupts lexer, parser, code generation, linking, or VFS work.

## Guest Build/Run and QAC evidence

No guest fixture was attempted. No guest source-loading, compiler invocation, object/ELF publication, Run launch, runtime value, guest diagnostic, QEMU exit code, reservation grant, or reservation release is claimed.

The one live QAC status/list/queue snapshot reported the coordinator online, zero QEMU processes, zero managed sessions, zero queued reservations, and zero blocked reservations. The task assigns .NET Support first priority, and QAC's empty queue does not show whether that track has released its priority. No reservation was submitted and no boot was launched, so the COMP4F work did not take QEMU capacity from that track. QAC grant and release evidence remains pending.

Hosted observations predict the signed `char` results (128 → -128, 255 → -1) and the expected byte-array values, but actual character and array behavior inside guideXOS is still unverified. The diagnostic and artifact-preservation outcomes above are hosted only until the new guest markers are observed under QAC.

## VFS observation and remaining limits

COMP4D recorded one intermediate `VFS_ERR_NOT_SUPPORTED` while creating `/P27W/build/obj/amd64`; it did not recur in the accepted run. COMP4F made no VFS change and has no new guest VFS observation.

The compiler remains the bounded COMP4E subset. Character globals and fields, `char` parameters and returns, general character pointers, string indexing, wide or encoded character forms, preprocessing, and standard-library support remain outside scope. COMP4F adds no language feature.

## Qualification records

The baseline scripts were invoked at their repository defaults. They rebuilt generated test binaries under the existing untracked `out/comp3-validation/` directory; the directory and other existing untracked artifacts were left in place. Additional default output directories `out/comp3-stack-frame-validation/` and `out/comp4b-validation/` were created by their test scripts. No generated output was added to Git.

## Recommended COMP4G scope

After .NET Support releases QEMU priority, run the prepared `-BootCount 1 -Comp4FNativeBuildRunOnly -QacDeveloperStudioAcceptance` path using the documented QAC wrapper. Collect one-boot serial evidence for all required markers, confirm the actual guest return values and lexical diagnostic, verify QEMU exit 0 and managed release, and preserve the complete evidence directory. If guest results uncover a compiler defect, reproduce and fix only the narrowest issue, then rerun affected hosted and QAC checks. Do not expand the language until the COMP4F guest gate is accepted.

No COMP4G work was started.

## Git closeout

The COMP4F guest preparation, QAC marker gates, hosted checks, and this report are committed locally. No push was performed. Existing untracked validation artifacts were not staged or deleted.
