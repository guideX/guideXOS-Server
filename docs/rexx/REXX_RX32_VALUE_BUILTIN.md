# REXX RX32 — VALUE Built-in

## Compatibility contract

**Portable signature:** `VALUE(name[,newvalue])`.

The built-in requires one evaluated `name` argument. A second evaluated value is optional. An omitted second argument, including an explicitly omitted argument slot such as `VALUE(name,)`, requests lookup only. A supplied empty string is a real assignment and sets the named variable to the empty string. The third `selector` parameter is not part of the selected portable contract: IBM z/VM uses it to reach CMS global pools, which require CMS host services. It remains outside RX32.

`name` must be a nonempty, lexically valid REXX variable symbol, including a valid simple, stem, or compound symbol. The active VM’s current variable environment supplies symbol lookup, case folding, exposed-variable routing, compound-tail substitution, and assignment. The function returns a string containing the variable's value before any optional assignment. When the named variable is uninitialized, the default value is its uppercased resolved symbol name, and the lookup does not raise NOVALUE.

An invalid or empty symbol name, an omitted required argument, or excess arguments produce the interpreter's invalid-built-in-call status. Memory allocation and VM resource failures use the existing out-of-memory/resource-limit statuses. The function does not partially update a variable when it cannot obtain the old value or prepare the new value.

## Numeric, string, and state behavior

VALUE performs no numeric arithmetic and does not change NUMERIC DIGITS, FUZZ, FORM, RANDOM state, address state, clock state, or queue state. Argument expressions are evaluated by the ordinary interpreter before dispatch. The built-in manipulates explicit-length `RexxValue` values; it does not use NUL termination as a data boundary. Valid variable names use the existing ASCII symbol alphabet and are at most 65,535 bytes. Values and returned strings remain subject to the VM's 16 MiB string limit.

Values are stored in the VM's active variable generation. Root, routine, `PROCEDURE`, `EXPOSE`, and stem behavior follow the interpreter's existing variable rules. Separate VMs have separate environments. The function introduces no global mutable state or host service.

## Runtime design and helper audit

The dispatcher uses a VM-aware context built-in. `rexx_builtin.c` validates the dynamic name and delegates variable access to a narrow VM API. `rexx_vm.c` canonicalizes the name, loads it through the existing `environment_load_variable` path (which resolves compound tails without consulting NOVALUE), and writes an optional new value through `environment_set` in the active variable environment. The previous value is captured before assignment and returned to the caller.

This deliberately does not alias `SYMBOL`, `POS`/`INDEX`, `WORDPOS`/`FIND`, `SPACE`, or `JUSTIFY`: their contracts do not provide dynamic variable reads or writes. RX15 conversion helpers and RX16 binary-literal processing are also unrelated. No changes to RX25 RANDOM state, RX30 queue state, or RX31 input providers are required.

## Portability and limitations

The current REXX variable-pool form is a classic/SAA built-in. IBM documents the current variable-environment lookup and optional assignment in [z/OS 3.2 REXX VALUE](https://www.ibm.com/docs/en/zos/3.2.0?topic=functions-value). IBM z/VM documents `GLOBAL`, `LASTING`, `SESSION`, and group-name selectors for CMS global pools in [z/VM 7.2 REXX VALUE](https://www.ibm.com/docs/en/zvm/7.2.0?topic=functions-value); those selectors rely on CMS services and are excluded here. No external variable pool, operating-system environment, filesystem, or cross-process state is accessed.

The implementation preserves the project's ASCII case-folding and byte-string conventions. It does not transcode to EBCDIC, add DBCS behavior, or implement CMS global-pool persistence. Regina is used only if available; an unavailable external reference interpreter is reported as `NOT_RUN`.

## Qualification plan

The direct suite uses fixed seed `0x52583332`. Its independent model keeps a separate bounded map of test variable names to expected byte strings and does not call VM lookup helpers. Direct cases cover IBM's dynamic-name and old-value examples, defaults, empty assignment, valid/invalid names, argument count, name-length boundary, binary value bytes, allocation failure, recovery, repeated calls, and VM isolation. Language fixtures cover compounds, uninitialized names with NOVALUE enabled, nested routines, `PROCEDURE EXPOSE`, and recovery after a trapped invalid call.

The hosted RX32 phase reruns the inherited RX1–RX31 regression matrix, CLI fixtures, RX19 clock/calendar, RX24 ADDRESS/TRACE, RX25 RANDOM, RX26 INDEX/POS, RX27 JUSTIFY, RX28 FIND/WORDPOS, RX30 queue/data-stack models, and RX31 provider tests. Sanitizer, freestanding AMD64, QAC ownership/guest, Regina, protected artifact, and untracked-path results are recorded in the RX32 scorecard and evidence directory after execution.

## Observed RX32 qualification

- Direct VALUE suite: 27 direct assertions; seed `0x52583332`; 4,096 independent model cases with 20,481 property assertions; four boundary groups; 20,533 total assertions; zero failures. Allocation-failure, nested-scope, NOVALUE, recovery, binary-value, and VM-isolation cases passed.
- Hosted regressions and CLI fixtures passed, including RX19 calendar/clock, RX24 ADDRESS/TRACE, RX25 RANDOM, RX26 INDEX/POS, RX27 JUSTIFY, RX28 FIND/WORDPOS, RX30 queue/data stack, and RX31 input provider. The VALUE CLI reference fixture passed 1/1. Regina was unavailable, so external comparisons are `NOT_RUN`.
- ASan and UBSan each passed 32 executable targets with zero failures. Each sanitizer also exercised 196 local expected-result fixtures; Regina comparisons were `NOT_RUN`.
- Freestanding AMD64 build and link passed with 10 REXX C objects. Kernel ELF SHA-256: `a3588d5991d2529c31c97615e5d558d8ea6bb68b828fa68ad04723d23d4026dd`.
- Exclusive QAC acceptance passed with a disposable ESP and networking disabled. The reviewed console-focus probe preceded seven REXX guest commands. Screenshots show VALUE success, invalid-name recovery, queue regression, RX31 missing-provider fail-closed error and shell recovery, and DATE/TIME fail-closed behavior; the RX20 guest output shows PIT elapsed time increasing. The QAC CLI logged direct QEMU child exit 0 and released reservation 5e472717be7047f5b48d0c80065c8508; QMP shutdown was acknowledged, final QEMU census was empty, and the runner exited 0. The QAC parent exit is recorded as 0 by the documented child-exit passthrough contract (the runner Start-Process ExitCode field was null). See evidence/rx32/native-guest/run-6a14f833d6d74a159b1acb64ff8273d0/. RX31 Outcome B remains open and unchanged.
- The 31 protected root `.obj`/`.pdb` artifacts matched their starting paths, sizes, and SHA-256 values throughout hosted, sanitizer, and native build gates. Starting untracked-path set: 100,478 unique paths; RX32 reconciliation is recorded in the evidence directory.

The current phase result is Outcome A. The inventory records the portable active-variable-pool implementation and leaves the CMS selector extension unsupported.
