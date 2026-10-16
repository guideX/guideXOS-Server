# guideXOS Server REXX — RX3R1 Qualification Closeout

**Outcome A — RX3 accepted for its documented decimal foundation.** The hosted gates, deterministic VM allocation-failure matrix, AMD64 build, and isolated guest run all passed. RX3 is formally closed for the bounded feature set described in the RX3 foundation report.

## Repository state

| Item | RX3R1 result |
|---|---|
| Repository | `D:\dev\guideXOSServer_REXX` |
| Branch | `REXX_SUPPORT` |
| Starting HEAD | `23a4f69489dce9adb4e43c8e3e546cc8831afd79` |
| Starting worktree | Clean; branch was one commit ahead of `origin/REXX_SUPPORT` |
| Qualified code/test ending HEAD | `acd16eb041638f741995ef844384ab1238455153` |
| Root `.phase` | Absent at preflight and left untouched |
| QEMU preflight | No QEMU process was active; this guest was started with dedicated resources and then shut down through its own QMP connection |
| Push | None |

The closeout report is committed as a documentation-only child of the qualified code/test commit. The final repository HEAD is reported in the task response.

## Audit and changes

The audit covered the RX3 decimal implementation and all 40 files in the RX3 foundation commit. `RexxDecimal` keeps a sign, a most-significant-first coefficient of up to 512 digits, and a signed decimal scale. Parsing, arithmetic, comparison, rounding, and formatting use bounded local storage; source values remain strings until a numeric operation needs conversion. `NUMERIC DIGITS` is stored in the VM context, and arithmetic resource work is bounded. Unsupported opcodes continue to return the VM's explicit unsupported-opcode status.

The arithmetic implementation was not rewritten. A test-only allocator seam now wraps allocations owned by `rexx_vm.c` when `REXX_VM_TEST_ALLOCATOR` is defined. It is excluded from normal and `GXOS_BARE_METAL` builds and does not replace the kernel allocator. The seam covers VM string storage, value copying, and local/stem table allocation and cleanup.

## Allocation-failure qualification

The decimal core itself has no heap-allocation sites. With the VM allocator set to fail on its next call, direct numeric parsing, addition, subtraction, multiplication, precision adjustment, and formatting all completed without making an allocation attempt. Failures were injected at every reachable VM allocation point in the corresponding numeric-expression and value-management paths.

| Path | Injected failure positions |
|---|---:|
| Addition, subtraction, multiplication | 4 each (both numeric source values, formatted result storage, and SAY output conversion) |
| `NUMERIC DIGITS` plus rounded multiplication/output | 5 |
| Direct string value copy and replacement | 1 each |
| Existing local-variable replacement | 3 |
| Existing stem replacement | 5 |
| New local insertion, including table growth | 5 |
| New stem insertion, including table growth | 6 |
| **Total deterministic injected failures** | **38** |

All injected operations returned out-of-memory, cleared the evaluation stack, and returned VM-owned live-block counts to their pre-operation baselines. Failed local and stem replacements retained their previous values. New entries were absent after failed insertion. Each VM then completed a successful execution; VM destruction returned the tracked live-block count to zero. The driver reports 361 passing assertions.

The direct numeric core cannot fail allocation because it does not allocate. The tests therefore verify that fact with an armed failpoint and inject failures in the surrounding VM string/value paths. This is targeted coverage of the numeric operations and their immediate value-management paths; it is not a claim that every allocation site in the full interpreter was fault-injected.

**Sanitizer evidence:** the Microsoft C compiler's AddressSanitizer build ran the 361-assertion allocation suite successfully, with no sanitizer report. The build used `/wd4701` because `/W4 /WX` otherwise stopped on two “potentially uninitialized” diagnostics in unchanged `rexx_builtin.c` and `rexx_vm.c` code (`haystack` and `number`). GCC AddressSanitizer was unavailable because the installed MinGW linker could not find `-lasan`. The VM allocator's live-block counter independently returned to zero after each cleanup.

## Hosted regression gates

Ran `powershell -NoProfile -ExecutionPolicy Bypass -File tests\run_rexx_tests.ps1` after adding the allocation suite. No inherited coverage was removed or weakened.

| Suite | Result |
|---|---:|
| RX1 runtime | 11/11 checks |
| RX1 CLI | 16/16 cases |
| RX2 VFS/output | 45 assertions |
| RX2 freestanding numeric | 18 assertions |
| RX2 shell bridge | 7 assertions |
| RX3 decimal | 26 assertions |
| RX3 compatibility fixtures | 13/13 fixtures |
| RX3 allocator failure | 361/361 assertions |
| Regina differential | 0 run; Regina was unavailable |

## AMD64 build and real guest run

The AMD64 kernel build completed with exit code 0 in the isolated output directory `kernel\build\rexx-rx3r1-qualification-amd64`. It compiled and linked the freestanding REXX modules into `bin\kernel.pe` and `bin\kernel.elf`. The UEFI bootloader also built successfully into the RX3R1 scratch directory.

The guest used a copied ESP at `tmp\rexx-rx3r1-qualification-20261015\ESP`, the new kernel, the isolated bootloader, serial output under the same scratch directory, and a QMP port bound to loopback. The production ESP, production disk images, and root `.phase` were not modified. QMP listener ownership was verified against the launched QEMU PID before commands were sent. The guest was shut down cleanly through that QEMU instance's QMP connection.

The plain-text `rx3r1.rex` script was loaded from the guest FAT VFS. The serial log records `/rx3r1.rex` and `/rx3_numeric_error.rex` opens. The Console displayed:

```text
001.00
1.00
0.3
4.00
0.02
5
1
0
```

These outputs show that the assigned string stays unchanged until arithmetic, arithmetic keeps the supported trailing-zero behavior, `NUMERIC DIGITS` changes the context observed by `DIGITS()`, ordinary `01 = 1` is numeric equality, and strict `'01' == '1'` compares distinct strings. The shell reported status `0` after the acceptance script. A separate invalid-numeric script produced the expected numeric runtime diagnostic and status `1`; the following `echo guest-responsive` succeeded, with the shell still responsive.

Build, serial, and graphical evidence is retained in the ignored RX3R1 scratch directory:

- `native-kernel-build.log`, `bootloader-build.log`, and `qemu-arguments.txt`
- `serial.log` and `guest-media-manifest.txt`
- `guest-first-script.png` (complete decimal output) and `guest-acceptance.png` (exit statuses, numeric error recovery, and continued shell response)

The two screenshots are available as [complete decimal output](../../tmp/rexx-rx3r1-qualification-20261015/guest-first-script.png) and [guest recovery and responsiveness](../../tmp/rexx-rx3r1-qualification-20261015/guest-acceptance.png).

## Classic compatibility observations

The acceptance outputs match the documented classic REXX rules for this supported subset: numeric comparisons use numeric values and leading zeroes do not change numeric equality; strict comparison checks exact string identity. IBM's references describe these rules in [numeric comparisons](https://www.ibm.com/docs/en/zos/2.5.0?topic=definition-numeric-comparisons) and [comparison operators](https://www.ibm.com/docs/en/cics-ts/5.6.0?topic=operators-comparison). Regina was not installed, so no differential result is claimed.

## Remaining limits

- `/`, `%`, `//`, and `**` remain unsupported VM operations.
- `NUMERIC FUZZ`, `NUMERIC FORM`, `FUZZ()`, and `FORM()` remain unsupported.
- The implementation limits remain: 254 `NUMERIC DIGITS`, 512-byte numeric input, ±1,000,000 decimal exponent/scale inputs, and the VM numeric-work budget.
- Regina differential testing was unperformed. Allocation injection was targeted to numeric operations and immediate value management; the report does not claim full-interpreter allocation-site coverage.
- Other language gaps already recorded in the RX3 foundation report remain outside this phase.

## Acceptance decision and next phase

**RX3 is formally closed for the documented bounded decimal foundation.** The required hosted gates, deterministic allocation-failure coverage, AMD64 native build, and real isolated guest execution all passed. No language feature was added during RX3R1.

**Recommended next phase:** RX4 planning for the deferred numeric operations and numeric-context features. RX4 is not started by this closeout.

## Local commit

The qualification code and tests are committed locally as `acd16eb041638f741995ef844384ab1238455153` (`test: qualify RX3 allocation failures`). The closeout report is a separate documentation-only commit.
