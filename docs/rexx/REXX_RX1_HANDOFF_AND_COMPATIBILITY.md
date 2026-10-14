# guideXOS Server REXX — RX1 Handoff and Compatibility

**Outcome: A — RX1 accepted.** The inherited implementation was audited, the hosted runtime was stabilized in the critical areas, and hosted output/status regression coverage was added. Native kernel readiness was assessed separately; a complete kernel link or guest run is not claimed.

## Evidence and starting state

- Repository: `D:\dev\guideXOSServer_REXX`
- Starting branch and HEAD: `REXX_SUPPORT`, `ae204156a1dbb3263d221aafcf794ba7709d2466`
- The reviewed remote commit `ae204156` was present locally and was the starting HEAD.
- Starting worktree: clean; no untracked files; no root `.phase` marker.
- No previous `docs/rexx` RX1 handoff or dedicated REXX test files existed.
- Source history shows an incremental lexer, parser/compiler, VM, hosted pipeline, CLI, and shell bridge, committed on 2026-10-11 and 2026-10-12. The source is a modular continuation of that implementation, not a replacement. The old suite had four integration cases and did not expose the missing EXIT handler, ADDRESS stack leak, or incomplete command execution.

Evidence labels used below:

- **Source inspection** means behavior is visible in code and was not necessarily executed.
- **Hosted execution** means the C11 hosted executable was compiled and run on Windows.
- **Cross-compile probe** means an isolated object compile used the installed `i686-elf` toolchain; it is not a kernel link.
- **Guest runtime** means an actual guideXOS boot/QEMU run. RX1 produced no guest runtime evidence.

## Architecture

| Component | Responsibility and ownership |
|---|---|
| `rexx_lexer.c/.h` | Allocation-free lexer. Tokens are borrowed slices into the caller's source. It tracks byte line/column positions, ASCII-folds keyword matching, and scans comments, quoted strings, numbers, and X/B encoded strings. The source must outlive the AST and compilation. |
| `rexx_parser.c/.h` | Recursive-descent parser. AST nodes are separately allocated with private allocation headers. Failed parses reclaim the allocation list; successful parses transfer ownership to the AST, released by `rexx_parser_free_ast`. Nesting, expression-node, and AST-node counts are bounded. |
| `rexx_compiler.c/.h` | Emits fixed-size bytecode records and copied constant/symbol pools. Branches use absolute instruction indexes. Compilation has instruction, pool, and nesting limits; a failed compile frees its partial chunk. |
| `rexx_vm.c/.h` | Owns string values, evaluation stack, local/global tables, stem table, current ADDRESS environment, command status, and EXIT result. Callbacks are per VM. Most tables use linear lookup. Execution now has a configurable per-call instruction budget. |
| `rexx_builtin.c/.h` | Case-insensitive built-in lookup and dispatch. The registered set is `LENGTH`, `SUBSTR`, `WORD`, `WORDS`, `STRIP`, `POS`, and `TRANSLATE`. |
| `rexx_cli.c/.h` | Reads one file through `rexx_os_io`, rejects NUL-containing source, runs lexer/parser/compiler/VM, reports diagnostics to `stderr`, and maps an integer EXIT result to the shell/process status. |
| `rexx_os_io.c/.h` | Hosted stdio file adapter or bare-metal VFS adapter. Source size is capped at 16 MiB. Bare-metal calls the C ABI implemented in `kernel/core/vfs.cpp`. VFS and address-dispatch registrations are process-global statics. |
| `rexx_main.c` | Hosted integration driver and test suite, not the shell CLI entry point. |
| `kernel/shell/rexx_shell_cmd.cpp/.h` | Tokenizes `rexx` command arguments and calls `guide_rexx_main` when linked. It preserves a process-style return code for the shell. |

### Ownership, failures, and quality findings

- **Lexer lifetime:** safe by contract; tokens never own text. The CLI retains the source buffer until AST and bytecode cleanup is complete.
- **Parser ownership:** all AST allocations are tracked until successful transfer. Syntax, lexer, allocation, and parser resource errors have distinct statuses and source coordinates where available.
- **Compiler ownership:** literals and symbol names are copied into the bytecode chunk. Compiler diagnostics distinguish malformed/unsupported AST, out-of-memory, and resource-limit cases.
- **VM ownership:** values are copied into locals/globals/stems and moved into/out of the evaluation stack. CLI cleanup releases the VM, bytecode, AST, and source on all normal error paths. Execution errors now clear live stack values before returning.
- **Error propagation:** the CLI returns 1 for file, syntax, compile, runtime, output, and unrepresentable EXIT-value failures; 2 for usage errors; and the numeric EXIT result when it is an integral `int32_t`. The VM's command RC remains separate from its interpreter status.
- **Limits:** parser AST/expression/nesting, compiler instruction/pool/nesting, VM stack/table, source-file size, and instruction execution are bounded. Allocation failure paths are source-inspected but no allocator fault-injection mechanism exists, so OOM was not deterministically injected.
- **Host dispatch:** VM callbacks are per-instance. The OS adapter's callback registrations are global and unsynchronized, which is a future multi-instance/threading constraint.
- **Unsupported bytecode:** unimplemented operations return `REXX_VM_STATUS_UNSUPPORTED_OPCODE` with a stable diagnostic. They do not silently succeed.

## Opcode coverage matrix

The enum in `rexx_compiler.h` defines 55 opcodes. The table reports the VM behavior after RX1; “default error” means execution returns `REXX_VM_STATUS_UNSUPPORTED_OPCODE`.

| Opcode | VM implementation | Status and limit |
|---|---|---|
| `OP_HALT` | Stops execution and clears the stack. | Implemented |
| `OP_NOP` | No operation. | Implemented |
| `OP_LOAD_CONST` | Decodes number, quoted string, hex, or binary constant and pushes it. | Implemented; numeric value is still `double` |
| `OP_LOAD_VAR` | Loads local/global value; an unset simple variable evaluates to its uppercase name. | Implemented subset |
| `OP_STORE_VAR` | Stores a copied local value. | Implemented subset |
| `OP_LOAD_STEM` | Loads exact stem/tail or the empty-tail default. | Partial; compound-tail substitution is absent |
| `OP_STORE_STEM` | Stores copied stem/tail value. | Partial; compound-tail substitution is absent |
| `OP_POP` | Pops and releases one value. | Implemented |
| `OP_POSITIVE` | Default error. | Unsupported |
| `OP_NEGATE` | Default error. | Unsupported |
| `OP_LOGICAL_NOT` | Default error. | Unsupported |
| `OP_POWER` | Default error. | Unsupported |
| `OP_MULTIPLY` | Binary numeric operation through `double`. | Partial REXX arithmetic |
| `OP_DIVIDE` | Default error. | Unsupported |
| `OP_INTEGER_DIVIDE` | Default error. | Unsupported |
| `OP_REMAINDER` | Default error. | Unsupported |
| `OP_ADD` | Binary numeric operation through `double`. | Partial REXX arithmetic |
| `OP_SUB` | Binary numeric operation through `double`. | Partial REXX arithmetic |
| `OP_CONCAT` | Converts both operands to strings and joins without a separator. | Implemented subset |
| `OP_CONCAT_BLANK` | Joins with one blank when both operands are nonempty. | Implemented subset |
| `OP_CONCAT_ABUTTAL` | Joins without a separator. | Implemented subset |
| `OP_EQUAL` | Default error. | Unsupported |
| `OP_STRICT_EQUAL` | Default error. | Unsupported |
| `OP_NOT_EQUAL` | Default error. | Unsupported |
| `OP_STRICT_NOT_EQUAL` | Default error. | Unsupported |
| `OP_GREATER` | Numeric comparison when both parse as numbers, otherwise byte-string ordering. | Partial; not full classic padding/decimal rules |
| `OP_LESS` | Default error. | Unsupported |
| `OP_GREATER_EQUAL` | Default error. | Unsupported |
| `OP_LESS_EQUAL` | Default error. | Unsupported |
| `OP_NOT_GREATER` | Default error. | Unsupported |
| `OP_NOT_LESS` | Default error. | Unsupported |
| `OP_STRICT_GREATER` | Default error. | Unsupported |
| `OP_STRICT_LESS` | Default error. | Unsupported |
| `OP_STRICT_GREATER_EQUAL` | Default error. | Unsupported |
| `OP_STRICT_LESS_EQUAL` | Default error. | Unsupported |
| `OP_STRICT_NOT_GREATER` | Default error. | Unsupported |
| `OP_STRICT_NOT_LESS` | Default error. | Unsupported |
| `OP_LOGICAL_AND` | Default error. | Unsupported |
| `OP_LOGICAL_OR` | Default error. | Unsupported |
| `OP_LOGICAL_XOR` | Default error. | Unsupported |
| `OP_JUMP` | Validates and sets the absolute instruction index. | Implemented |
| `OP_JUMP_IF_FALSE` | Consumes a value and branches using VM truthiness. | Partial; classic condition rules differ |
| `OP_JUMP_IF_TRUE` | Default error. | Unsupported |
| `OP_SAY` | Converts a value and writes it plus newline to `stdout`. | Hosted implemented; native output path is not connected |
| `OP_CALL` | Pops arguments and dispatches registered built-ins. | Partial; internal routines and most standard functions are absent |
| `OP_RETURN` | Default error. | Unsupported |
| `OP_EXIT` | Consumes optional value, records it, terminates the program. | Implemented; CLI process mapping is limited to integral `int32_t` |
| `OP_COMMAND` | Consumes command text, dispatches in active environment, stores command RC in `RC`. | Implemented for parser-supported command clauses and installed dispatchers |
| `OP_ADDRESS` | Changes active environment or dispatches one command in a selected environment; no stack result. | Partial; environment names are simple symbols; default is guideXOS `KERNEL` |
| `OP_LABEL` | No operation. | Partial; no internal label transfer exists |
| `OP_DO_SETUP` | Accepts structural plain/forever/WHILE-loop setup only when no control symbol/flags are present. | Partial; counted/controlled loops fail safely; UNTIL uses unsupported `OP_JUMP_IF_TRUE` |
| `OP_DO_TEST` | Default error. | Unsupported |
| `OP_DO_NEXT` | Default error. | Unsupported |
| `OP_DO_END` | Structural no-op. | Implemented for accepted non-counted forms |
| `OP_LOAD_UNSET` | Pushes an unset argument placeholder. | Implemented |

## Feature compatibility matrix

| Feature | RX1 classification | Evidence / current boundary |
|---|---|---|
| ASCII case-insensitive simple identifiers | Fully executable for the tested subset | Hosted assignment/read tests pass with mixed-case spellings. |
| Assignment and unset simple-variable lookup | Fully executable for the tested subset | Unset names evaluate as uppercase strings. |
| Stem assignment/read | Partially implemented | Static stem/tail and empty-tail default are present; variable substitution inside compound tails is not. |
| String literals and concatenation | Partially implemented | Quoted strings, blank concatenation, `||`, and abuttal are executable; broader classic string rules remain unverified. |
| Arithmetic | Partially implemented | `+`, `-`, `*` only, all through binary floating point. `/`, `//`, `%`, unary arithmetic, and `**` fail at runtime. |
| Comparison and conditions | Partially implemented | `>` is the sole comparison opcode handled. Comparisons use double conversion or byte ordering; condition truthiness accepts any nonempty string/nonzero number. |
| IF and basic branch execution | Partially implemented | `JUMP` and `JUMP_IF_FALSE` work for supported conditions; SELECT can compile but uses the same limited condition path. |
| DO groups, DO FOREVER, and DO WHILE | Partially implemented | Compiler emits branch loops; counted/controlled `DO` emits unsupported `DO_TEST`/`DO_NEXT` or flagged setup. DO UNTIL emits unsupported `JUMP_IF_TRUE`. Instruction budget stops runaway execution. |
| Built-in functions | Partially implemented | Seven registered functions listed above; tests cover their common forms. Most standard REXX built-ins are absent. |
| SAY | Fully executable in hosted mode | Output bytes and line breaks are checked by the hosted CLI suite. It writes directly to C `stdout`. |
| EXIT | Fully executable for tested hosted CLI cases | EXIT 0, 7, numeric-string 7, valueless EXIT, and fractional-value rejection are tested. VM retains the full value; CLI requires an integral `int32_t`. |
| ADDRESS and command RC | Partially implemented | Persistent environment, one-command temporary environment, dispatcher invocation, RC, and stack balance are tested. There are no real kernel environment routes yet; unregistered dispatch returns 127. |
| CALL/RETURN, internal routines, procedures | Unsupported end-to-end | Parser/compiler recognize some forms, but VM has no routine frames or RETURN handler. `OP_CALL` only reaches built-ins. |
| PARSE, ARG, PULL, PUSH, QUEUE, INTERPRET, NUMERIC, OPTIONS, DROP, EXPOSE, SIGNAL, TRACE | Unsupported | Lexer token definitions do not provide an executable instruction. Parser has no AST form for these instructions. |
| Hex/binary strings | Unverified end-to-end | Lexer, compiler pool, and VM decoder contain paths, but no RX1 regression case covers byte-exact semantics. |
| Labels | Partially implemented | Label clauses compile to `OP_LABEL`, which is a no-op; `SIGNAL` and internal transfer do not work. |
| `.rex`, `.rexx`, `.cmd` file suffixes | Unverified policy | CLI accepts one supplied path without extension filtering; compatibility policy for `.cmd` has not been established. |

## Critical bugs found and fixed

1. **EXIT had no VM case.** The compiler emitted `OP_EXIT`, but the VM returned unsupported-opcode. RX1 now terminates execution, preserves the optional `RexxValue` in `vm.exit_value`, exposes `exit_requested`/`exit_has_value`, and releases the evaluation stack. The hosted CLI maps numeric integral results to process status. EXIT's result does not reuse or overwrite `vm.rc`.
2. **ADDRESS leaked a value on the VM stack.** The VM previously pushed command RC after `OP_ADDRESS`, while the compiler emitted no matching pop. Address and command instructions now consume command strings and leave no stack entry; each successful program ends with an empty stack.
3. **ADDRESS environment-only syntax was parsed as a command.** `ADDRESS DEFAULT` now parses as an environment change. `ADDRESS environment` changes the VM's active environment; `ADDRESS environment command` uses that environment for one command and restores the prior active environment afterward, following classic ADDRESS forms. `OP_COMMAND` now dispatches symbol-leading command clauses through the active environment.
4. **RC was not readable as a REXX variable.** A dispatched command now writes its result to both the VM's command-RC field and the REXX `RC` local. The CLI process status remains independent.
5. **Execution was unbounded.** Each VM starts with a 1,000,000-instruction per-execution budget. Embedders can set a positive budget with `rexx_vm_set_instruction_budget`; a zero budget is rejected. Exhaustion returns distinct `REXX_VM_STATUS_INSTRUCTION_LIMIT` / `instruction budget exhausted`, clears the stack, and reports the next instruction index.
6. **Malformed/unsupported execution must fail safely.** Runtime errors now clear stack-owned values. Unsupported arithmetic and control operations still fail explicitly rather than being treated as implemented.

## Hosted test commands and results

Before changes, the documented C11 hosted driver was compiled with MinGW GCC and its built-in suite passed **4/4**: arithmetic/assignment, stem variable, IF/DO, and ADDRESS dispatcher. That baseline did not inspect the final evaluation-stack depth or EXIT.

Run the expanded hosted checks from the repository root:

```powershell
pwsh.exe -NoProfile -ExecutionPolicy Bypass -File tests/run_rexx_tests.ps1
```

The script builds both the runtime assertions and a CLI driver with `gcc -std=c11 -Wall -Wextra -Wpedantic -Werror`, then checks exact output and exit status. Results on 2026-10-13:

- Hosted runtime assertions: **11/11 passed**.
- Hosted CLI cases: **16/16 passed**.
- Regina differential cases: **0 run**; Regina was not installed/on PATH.
- The 16 MiB + 1 byte source-file check failed with the expected size-limit diagnostic and status 1.
- Syntax error and unsupported division bytecode both returned status 1 with diagnostics.
- Synthetic self-jumping bytecode exhausted a 32-instruction test budget and returned the distinct instruction-limit status; no actual infinite-running source test was used.
- A one-entry VM stack reported stack overflow and was empty after failure.

The compatibility examples' expected outputs are recorded in the test script. They are derived from classic REXX/IBM reference behavior, not presented as Regina observations. When `regina` is available, the script runs differential checks for safe arithmetic, assignment, concatenation, comparison, stems, built-ins, SAY, and EXIT cases. Host-command dispatch cases are excluded from that reference run to avoid invoking a real host command.

## Classic REXX compatibility findings

RX1 deliberately retains the existing `double` value representation; this is not a numeric engine rewrite. Current arithmetic parses numeric text with `strtod`, stores numbers in binary `double`, and formats with `%.15g`. This does not implement decimal precision, default NUMERIC DIGITS 9, configurable DIGITS, FUZZ comparison precision, or FORM exponent formatting. It also makes numeric behavior dependent on floating-point rounding and C conversion details. `NUMERIC` is currently lexed but rejected by the parser.

Classic REXX treats values as character strings with decimal arithmetic settings. A compatible migration should make the canonical value string-oriented and use a decimal engine based on sign, coefficient digits, and base-10 scale/exponent (or an arbitrary-precision decimal digit buffer). Parse decimal strings without first rounding through `double`; apply NUMERIC DIGITS and FUZZ at the specified operation/comparison boundaries; use FORM only when rendering results. Keep the existing `double` only as a temporary compatibility accessor, not the canonical stored value. RX1 does not undertake that rewrite.

Other known incompatibilities include:

- Most arithmetic, logical, and comparison opcodes are emitted but unsupported by the VM.
- Non-strict string comparison does not implement classic blank trimming/padding; strict comparisons are absent.
- Condition evaluation accepts nonempty strings as true instead of enforcing classic condition result rules.
- Compound stems do not substitute variables in their tails.
- Many standard built-ins, internal routine calls, `RETURN`, `PARSE`, `PROCEDURE`, `SIGNAL`, `TRACE`, and `NUMERIC` are absent.
- The parser only builds command clauses for symbol-leading clauses; a quoted expression as a standalone command clause is currently rejected.
- ADDRESS environment syntax is limited to a simple name token. There is no `ADDRESS()` built-in or standard command-error/failure condition handling.

Compatibility references used for RX1:

- [ANSI X3.274-1996 REXX standard information](https://www.rexxla.org/rexxlang/standards/)
- [IBM EXIT instruction](https://www.ibm.com/docs/en/zos/2.5.0?topic=instructions-exit-instruction)
- [IBM changing the host command environment](https://www.ibm.com/docs/en/zos/2.5.0?topic=exec-changing-host-command-environment)
- [IBM host commands and the RC variable](https://www.ibm.com/docs/en/zos/2.5.0?topic=environments-commands)
- [IBM NUMERIC DIGITS, FUZZ, and FORM](https://www.ibm.com/docs/en/zos/2.5.0?topic=instructions-numeric)
- [IBM numeric comparisons](https://www.ibm.com/docs/en/zos/2.5.0?topic=definition-numeric-comparisons)

IBM documents that ADDRESS with only an environment changes the active destination, while ADDRESS with an environment and command routes only that command. Command completion updates RC. EXIT can return an expression to its invoker. NUMERIC DIGITS controls arithmetic precision, FUZZ reduces comparison precision, and FORM controls exponential rendering. RX1 applies those ADDRESS/RC/EXIT distinctions where implemented and documents the numeric gaps above.

## Native integration readiness

### Source/build inspection

- `kernel/Makefile` selects `core/*.cpp`, `shell/*.cpp`, and architecture C++/assembly files. Its only C source list is for vendored Mbed TLS. It does not include the root `rexx_*.c` objects.
- `guideXOSKernel.vcxproj` includes `kernel/shell/rexx_shell_cmd.cpp/.h`, but no REXX C translation units. The MSVC branch in the bridge has a strong `guide_rexx_main` reference; link behavior was not tested.
- In the GNU kernel bridge, `guide_rexx_main` is weak. Without the omitted interpreter objects, `rexx_cli_available()` is false and the shell reports that the REXX engine is not linked.
- `kernel/core/vfs.cpp` contains the C ABI expected by `rexx_os_io.c` (`open`, `size`, `read`, `close`) with an 8-bit VFS handle. This is source-inspected only; the interpreter does not currently call it from a linked kernel image.
- Kernel shell code already copies the CLI's integer result into `s_lastExitCode`. That path is ready to receive a status after the engine is linked.
- VM `SAY` calls `fwrite`/`fputc` on `stdout`; CLI diagnostics use `fprintf`/`stderr`. The kernel `stdio.h` only forward-declares `FILE` and provides no stream/output functions. There is no REXX-to-shell output callback.
- Kernel `stdlib.h` declares allocation calls, but the REXX C modules also include standard `string.h`, `errno.h`, `math.h`, `stdio.h`, and use `strtod`, `snprintf`, `fwrite`, and `fprintf`. The repository has no `string.h`, `errno.h`, or `math.h` compatibility headers. `rexx_os_set_address_dispatcher` and VFS registration are global statics, with no current shell registration for named REXX environments.

### Safe cross-compile probe

The full kernel build was not run. `ESP` already exists, and the existing full build can rewrite staged ESP outputs; the kernel build target also does not list the REXX C objects. An isolated compile used the installed `i686-elf-gcc` and repository kernel include paths, with freestanding x86 flags, writing only under ignored `tmp/rx1-cross-objects/`.

- `rexx_lexer.c`: cross-compiled to an object.
- `kernel/shell/rexx_shell_cmd.cpp`: cross-compiled to an object.
- `rexx_parser.c`, `rexx_compiler.c`, and `rexx_os_io.c`: compile stopped at missing `string.h`.
- `rexx_vm.c`, `rexx_builtin.c`, and `rexx_cli.c`: compile stopped at missing `errno.h`.
- No full interpreter link, kernel build, or QEMU/guest run was performed.

This establishes that the complete interpreter cannot currently be compiled and linked into the genuine kernel with the checked-in source lists and current C header surface. No libc stubs or broad kernel changes were added to conceal the gap. Native output delivery and named host environment wiring also remain unresolved.

## Recommended RX2

1. Establish a deliberate C-to-kernel boundary: add the REXX C objects to the supported kernel build, provide only the standard C primitives the interpreter actually requires through the existing kernel runtime, and verify a real kernel link without fake libc stubs.
2. Replace direct `stdout`/`stderr` usage with injected output/error sinks so SAY and diagnostics reach the shell console. Register ADDRESS environments through per-runtime/per-VM state rather than unsynchronized process globals.
3. Add a kernel build/link smoke followed by QEMU shell execution of a `.rex` file, exact SAY output, and process status checks. Record guest evidence separately from hosted results.
4. After native wiring is stable, expand classic semantics with decimal arithmetic and NUMERIC DIGITS/FUZZ/FORM, then implement the remaining operators and control opcodes under Regina-backed regression cases where available.
5. Keep the unsupported-opcode matrix in sync with every new compiler emission so parser/compiler recognition never overstates runtime support.
