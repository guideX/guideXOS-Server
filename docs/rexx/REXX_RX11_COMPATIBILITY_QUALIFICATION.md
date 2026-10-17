# guideXOS Server REXX RX11 — Compatibility Qualification

## Outcome

**Outcome A — RX11 qualification infrastructure accepted.** The hosted regression suites pass after the RX11 runtime corrections, the new corpus is reproducible, both sanitizer workflows pass, a full AMD64 freestanding kernel build/link succeeds, and the changed behavior plus script recovery pass in an isolated QEMU guest. Regina was not installed, so all 78 independent comparisons are `NOT_RUN`; none are counted as matches or evidence of conformance.

This outcome qualifies the testing and safety infrastructure. It does not claim complete ANSI/classic REXX compatibility.

## Qualification identity and scope

- Repository: `D:\dev\guideXOSServer_REXX`
- Required branch: `REXX_SUPPORT`
- Starting HEAD: `7fc1b0a073d3ba32072f02bd6c8a9c41728f42e8`
- Ending branch: `REXX_SUPPORT`; the accepted RX11 work is committed locally (the final commit SHA is reported in the delivery summary).
- The branch was not switched; no fetch, pull, push, rebase, merge, reset, clean, stash, or worktree operation was performed.
- Root `.phase` remained absent.
- No production ESP or disk image was modified. QEMU used a copied ESP in `tmp/` and `-net none`.
- An ownership census found an unrelated QEMU process using `out/font11-validation`; it was left running and its files were not used.
- RX11 remained a qualification phase. Runtime corrections were classic logical-operator execution and safe lifetime of the active instruction record during dynamic INTERPRET completion; no large built-in family was added.

## Accepted RX10 baseline and post-change regression

The accepted RX10 design is a bounded lexer → parser/AST → compiler/bytecode → VM pipeline. RX10 `INTERPRET` compiles into a dynamic continuation on the existing VM, with limits for source size, nesting, aggregate generated storage, instruction count, translation work, dispatch, and numeric work. See [REXX_RX10_INTERPRET.md](REXX_RX10_INTERPRET.md) for the existing ownership model and budgets.

The complete hosted runner passed before RX11 edits and again after the edits. The post-change run used `tests/run_rexx_tests.ps1`, with GCC 15.2.0 (MinGW-W64 x86_64, Brecht Sanders build). Current results:

| Suite | Post-change result |
| --- | ---: |
| RX1 runtime | 11/11 |
| RX1 hosted CLI | 20/20 |
| RX2 VFS/output | 45 assertions |
| RX2 freestanding numeric adapter | 18 assertions |
| RX2 shell bridge | 7 assertions |
| RX3 decimal | 26 assertions; 13/13 fixtures |
| RX3 allocation failure | 570 checks |
| RX4 arithmetic | 53 assertions; 11/11 fixtures |
| RX5 numeric context | 35/35 assertions; 13/13 fixtures |
| RX6 PARSE allocation failure/resource | 439 checks; 25/25 fixtures |
| RX7 routines | 13 assertions; 33/33 fixtures; allocator cases 46/28/111 |
| RX8 semantics/recovery | 279/279 assertions; 26/26 fixtures; allocator cases 38/21/50 |
| RX9 conditions | 467 assertions; 24/24 fixtures; allocator cases 41/37/78 |
| RX10 INTERPRET | 257 assertions; 19/19 fixtures; allocator failpoints VM nested=33, VM SYNTAX=14, parser=8, compiler=11 |

All suites completed with exit status 0. Regina-dependent checks in the inherited runner remain 0-run because Regina is unavailable.

## Regina discovery and differential harness

Discovery was non-destructive: PATH, conventional install locations, development-tool directories, and project-local tool locations were checked. No verified `regina` or `regina.exe` was found. A generic executable named `rexx.exe` was not assumed to be Regina. No interpreter was installed or downloaded. Regina 3.9.7 was considered as the requested distribution candidate, but no executable version was recorded because none was available.

The runner is [run_differential.py](../../tests/rexx_compat/run_differential.py). It builds the hosted CLI with GCC/`CC` unless `--cli` supplies one, verifies Regina identity with `-v`, then runs a harmless marker in restricted `-r` mode. `--regina` and `GUIDEXOS_REGINA` configure a known executable. Each side has a five-second timeout and one MiB stdout/stderr limit. The scripts are corpus-controlled, stdin is closed, shell execution is disabled, and process cleanup is limited to the launched process tree. Raw stdout/stderr bytes, argv, cwd, status, timeout state, output digests, and reproduction commands are saved per case.

Only line endings are normalized (CRLF or lone CR to LF). No trimming, case conversion, numeric reformatting, whitespace removal, status normalization, stderr suppression, or output reordering is done. The portable corpus contains no `ADDRESS` host commands, shell execution, file mutation, or networking.

Post-change command:

```powershell
python tests/rexx_compat/run_differential.py --results-dir tmp/rexx-rx11-differential-final-20261017
```

| Category | Cases |
| --- | ---: |
| Core language | 17 |
| Decimal arithmetic | 14 |
| PARSE | 12 |
| Routines | 10 |
| Conditions | 11 |
| INTERPRET | 14 |
| **Total** | **78** |

| Independent result | Count |
| --- | ---: |
| MATCH | 0 |
| MISMATCH | 0 |
| GUIDEXOS_UNSUPPORTED | 0 |
| REFERENCE_UNSUPPORTED | 0 |
| HOST_SPECIFIC (in the portable run) | 0 |
| INCONCLUSIVE | 0 |
| NOT_RUN (Regina unavailable) | 78 |

The separate guideXOS execution column records 78/78 expected process statuses, including the logical NOT additions to the core boolean case. `EXIT 5` is a declared expected result for its deterministic fixture, not an accidental runner failure. Local execution and external comparison are reported separately.

The reusable command, configuration, limitations, and single-case reproduction pattern are in [tests/rexx_compat/README.md](../../tests/rexx_compat/README.md). The baseline per-case report is preserved under [evidence/rx11/differential](evidence/rx11/differential/); the final post-NOT report and raw outputs are under [evidence/rx11/differential-final-not](evidence/rx11/differential-final-not/).

## Confirmed defects and focused fixes

### Classic boolean operators

The compiler accepted logical bytecodes but VM dispatch did not implement `OP_LOGICAL_AND`, `OP_LOGICAL_OR`, `OP_LOGICAL_XOR`, or `OP_LOGICAL_NOT`. The RX11 handlers convert operands to exact decimal values, accept only 0 or 1, charge numeric work, and return normalized 0/1 values. Invalid operands produce the runtime numeric error path and can be handled by a SYNTAX trap. The corpus checks every AND/OR/XOR truth-table combination, both unary NOT values, and a trapped invalid operand.

This correction follows the traditional REXX/VM definitions for `&` AND, `|` OR, `&&` exclusive OR, and prefix `\` NOT. See the [IBM z/VM 7.4 REXX/VM Reference](https://www.vm.ibm.com/library/740pdfs/74631400.pdf). No Regina behavior was used as the compatibility authority.

### INTERPRET instruction lifetime

The first real ASan run found a heap-use-after-free in RX10 dynamic INTERPRET completion. `OP_HALT` could pop the last interpretation context and free its bytecode chunk before the dispatch loop performed post-dispatch checks using the current `RexxInstruction *`. The VM now copies that instruction record into the dispatch stack frame before execution. The ownership of the active chunk and continuation is otherwise unchanged. The RX10 C test and the new completed-fragment corpus case pass under post-fix ASan and UBSan.

The pre-fix finding is preserved in [asan-initial-rx10-uaf.stderr.log](evidence/rx11/sanitizers/asan-initial-rx10-uaf.stderr.log); it is not a post-fix failure.

## Hosted ASan/UBSan and allocation-failure qualification

The earlier MinGW GCC installation (GCC 15.2.0) accepted neither sanitizer as a usable runtime: `-fsanitize=address` failed to link with missing `-lasan`, and `-fsanitize=undefined` failed to link with missing `-lubsan`. These attempted commands and failures are kept in the sanitizer evidence.

Local installed toolchains did support both sanitizers:

- MSVC x64 AddressSanitizer with the Visual Studio x64 runtime.
- LLVM/Clang `clang-cl` UBSan linked with the Visual Studio bundled x64 UBSan runtime.
- Minimal intentional-fault smoke programs were used to confirm the runtimes produced diagnostics, not merely accepted the compiler flags.

The reproducible script is `tests/run_rexx_sanitizers.ps1`; its Visual Studio lookup captures `vswhere`'s process result before processing output so repeated Address/Undefined runs work reliably in one PowerShell session. Command:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests/run_rexx_sanitizers.ps1 -Sanitizer Both
```

The final combined run completed both stages successfully: for each sanitizer, 13 hosted C executables passed and all 78 corpus scripts returned their declared guideXOS statuses. The final AddressSanitizer run had zero findings; UBSan reported zero findings. An earlier ASan run had the pre-fix finding described above. Final combined-run logs and per-case results are preserved under [evidence/rx11/sanitizers-final](evidence/rx11/sanitizers-final/), alongside the earlier evidence under [evidence/rx11/sanitizers](evidence/rx11/sanitizers/). The sanitizer matrix covers the REXX C translation units and C test executables. The C++ shell bridge and freestanding-only numeric adapter are covered by the ordinary hosted runner, not the sanitizer matrix. No sanitizer flag or runtime was added to the freestanding kernel.

RX3–RX10 allocation-failure hooks were exercised by the full post-change runner; all current failpoint suites passed with the counts recorded above.

The inherited bounded robustness tests also remained green: RX7 recursive routine depth and recovery; RX8 subsidiary EXPOSE bounds and cleanup; RX3/RX4 large numeric inputs and work limits; RX6 parser/template resource limits; and RX10 nested INTERPRET depth, generated-source/bytecode and shared instruction budgets, repeated compilation, and VM reuse after failure. The accepted RX10 resource limits were not changed. The RX10 design report lists the specific bound table and tests.

## Built-in function inventory

The 75-row [CSV inventory](REXX_RX11_BUILTIN_INVENTORY.csv) is based on the IBM [z/OS REXX built-in function reference](https://www.ibm.com/docs/en/zos/3.1.0?topic=functions-built-in) and actual guideXOS direct dispatch and VM special-function dispatch. IBM identifies six additional TSO/E built-ins—EXTERNALS, FIND, INDEX, JUSTIFY, LINESIZE, USERID—as non-SAA; these are separately marked and are not part of the classic portable target.

The inventory lists 67 classic/SAA candidate functions, with 11 fully implemented entries and one partial entry (ARG). The implemented names are LENGTH, SUBSTR, WORD, WORDS, STRIP, POS, TRANSLATE, ARG, CONDITION, DIGITS, FUZZ, and FORM. ARG supports E/O presence queries but currently rejects the standard N option. Direct string functions operate on byte strings; Unicode/DBCS compatibility is not claimed. The remaining 55 classic candidates are not implemented in the inspected function dispatch. The file records call shape, status, argument/option coverage, limitations, known tests, and priority for each.

No Regina extension was imported into the classic feature set because Regina could not be run. Regina-only optional extensions and future guideXOS-specific functions are separate inventory rows; no future guideXOS names are proposed in RX11.

## Compatibility scorecard and priorities

The machine-readable scorecard is [REXX_RX11_SCORECARD.json](REXX_RX11_SCORECARD.json). Each feature independently records syntax implementation, internal verification, independent differential result, RX11-specific native guest verification, and known limits. No ANSI conformance percentage is produced.

- **P0:** no known safety defect remains from the RX11 investigations; the ASan-discovered use-after-free is fixed and the post-fix instrumented suites pass.
- **P1:** the classic interpreter needs independent reference execution across its implemented syntax and semantics; this evidence gap remains because Regina is unavailable.
- **P2:** the most valuable next language work is common classic function families, beginning with DATATYPE, COMPARE, VERIFY, SPACE, COPIES, and WORDPOS; complete ARG option N and add independent edge tests.
- **P3:** separately decide whether IBM TSO/E non-SAA and Regina-only extensions belong in any later target.
- **Future:** define guideXOS OS-service functions only as part of a later host-integration phase, outside the portable classic core.

Recommended RX12 focus: add a bounded, tested group of common pure string/classification BIFs (DATATYPE, COMPARE, VERIFY, SPACE, COPIES, WORDPOS), close the ARG `N` option gap, and establish Regina on a controlled qualification host so differential outcomes can be reviewed. Do not begin a broad architecture rewrite or host integration in that phase.

## AMD64 freestanding build and isolated guest

Runtime source changed in RX11, so the complete forced AMD64 kernel build was run in the isolated output directory `kernel/build/rexx-rx11-native-final-20261017`, preserving the existing `kernel/build` outputs. The full-build command, linker output, and compiler diagnostics are preserved in [evidence/rx11/native-build-final.log](evidence/rx11/native-build-final.log). The build linked both PE and ELF images with the existing freestanding flags; it uses no C++ standard library, exceptions, RTTI, hosted-only runtime, Regina, or sanitizer dependency.

| Image | SHA-256 | Bytes |
| --- | --- | ---: |
| `kernel.elf` | `9C557F4CE875DFEB6F9A7D504D64910CE191F9F5EFCA11321F042EC47CB6F3B8` | 2,808,840 |
| `kernel.pe` | `3043C6A423E62C8797478E5EA817F1A09C0DB11A1BCD18163067E30F4E395F08` | 2,918,475 |
| `BOOTX64.EFI` (MSBuild-isolated bootloader output) | `964CB594260193FFB827B4BF94747C4569CC57A7061A36C18B98C5F5FACF927C` | 56,832 |

The root production `ESP` was not used as a QEMU drive. The bootloader was built with MSBuild overrides directing both output and intermediate files into `tmp/rexx-rx11-bootloader`, then paired with a copy of `ESP` and the RX11 kernel in a new disposable directory. The final guest used QEMU 11.0.0-12122-ga4bb4b10c9 with `-net none`; ownership censuses were saved before launch and unrelated QEMU processes were left untouched. The final guest ran the AND/OR/XOR truth-table tail, both NOT values (1 for `\ 0`, 0 for `\ 1`), trapped invalid logical input and then ran the recovery script. The final screen visibly shows the NOT results, `RX11_NATIVE_SYNTAX_RECOVERED`, and `RX11_NATIVE_RECOVERY_SCRIPT_OK`. The invocation, QMP transcript, serial boot log, scripts, raw PPM capture, and PNG preview are in [evidence/rx11/native-guest-final/capture4](evidence/rx11/native-guest-final/capture4/).

## Evidence layout

- `evidence/rx11/differential/` and `evidence/rx11/differential-final-not/` — baseline and final 78-case JSON results, summaries, and raw output bytes.
- `evidence/rx11/sanitizers/` and `evidence/rx11/sanitizers-final/` — initial ASan finding, final ASan/UBSan logs, corpus reports, compiler identities, and success manifests.
- `evidence/rx11/native-build-final.log` — complete forced AMD64 freestanding rebuild and link transcript.
- `evidence/rx11/native-guest-final/capture4/` — isolated guest invocation, pre-launch census, QMP transcript, serial log, scripts, raw PPM, and PNG preview.
- `evidence/rx11/hosted-final-not-20261017.log` — RX1–RX10 hosted regression transcript after the final runtime correction.

## External references

- [IBM z/OS 3.1 REXX built-in functions](https://www.ibm.com/docs/en/zos/3.1.0?topic=functions-built-in)
- [IBM z/VM 7.4 REXX/VM Reference](https://www.vm.ibm.com/library/740pdfs/74631400.pdf)
- [Regina REXX documentation index](https://regina-rexx.sourceforge.io/doc/)
