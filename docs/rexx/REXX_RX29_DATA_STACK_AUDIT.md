# RX29 — Data-Stack Architecture Audit

## Decision

No REXX external data stack exists at accepted RX28 HEAD `cdf037707fbc5b9fe4755fff0b94337fb740cb7f`. `QUEUE`, `PUSH`, and `PULL` are recognized by the lexer only. The parser has no executable instruction form for them, the compiler emits no data-stack operation, and the VM has no queue storage or queue callback. `PARSE PULL` is unsupported. Therefore there is no correct element count for `QUEUED()` to read. Implementing the whole stack would be an unrelated subsystem expansion, so RX29 stops before source changes.

## Findings by layer

| Layer | Observed behavior | Evidence |
|---|---|---|
| Lexer | Defines `TOKEN_KEYWORD_PULL`, `TOKEN_KEYWORD_PUSH`, and `TOKEN_KEYWORD_QUEUE` and their spellings. | `rexx_lexer.c:40-42,786-788`; `rexx_lexer.h:47-49` |
| Parser | No AST instruction for PULL, PUSH, or QUEUE. The RX1 compatibility table records PARSE/PULL/PUSH/QUEUE as unsupported because there is no executable parser form. | `docs/rexx/REXX_RX1_HANDOFF_AND_COMPATIBILITY.md:125` |
| PARSE sources | The implemented sources are ARG, VAR, and VALUE. PULL and external input sources are explicitly outside RX6. | `docs/rexx/REXX_RX6_PARSE_SUPPORT.md:184-185` |
| Compiler | No data-stack opcode or emission path exists. Lexer tokens do not become stack operations. | `rexx_compiler.h` opcode enum and `rexx_compiler.c` instruction dispatch; no PULL/PUSH/QUEUE token references |
| VM storage | `RexxVM.stack`, `stack_capacity`, and `sp` are the bounded expression evaluation stack; the comment defines the top as `stack[sp - 1]`. These `RexxValue` operands are not queued source lines. | `rexx_vm.h:187-190` |
| Host integration | VM callbacks cover command dispatch, ADDRESS dispatch, output, and clock reads. There is no external input or data-queue provider. The kernel shell bridge tokenizes shell commands and has no REXX data queue. | `rexx_vm.h` callback and `RexxVM` fields; `kernel/shell/rexx_shell_cmd.cpp` |
| Built-in dispatch | RX28 registers FIND and has no QUEUED entry. The RX28 inventory marks QUEUED Not implemented. | `rexx_builtin.c:4318`; `docs/rexx/REXX_RX28_BUILTIN_INVENTORY.csv`, QUEUED row |

## Ownership, lifetime, and operations

| Question | Actual RX28 runtime behavior |
|---|---|
| Queue storage and owner | None exists. The VM owns its expression stack and other execution state; it does not own or reference external data lines. |
| FIFO `QUEUE` insertion | Not executable; no insertion order exists. |
| LIFO `PUSH` insertion | Not executable; no insertion order exists. |
| `PULL` / `PARSE PULL` removal | Not executable. No empty-stack terminal read is present in these unsupported paths. |
| Queue length or empty behavior | No count and no active external queue exist. |
| Empty queued lines and binary data | No queue element representation exists, so these cases have no runtime behavior. General runtime strings are length-bearing and retain the existing 16 MiB limit; that does not imply data-stack support. |
| Internal routines | Routine execution uses VM routine frames, but there is no queue state for routines to share or mutate. |
| `INTERPRET` | Dynamic code runs through the VM's INTERPRET continuation machinery; there is no queue state to observe across that boundary. |
| Separate VMs / program restart | Each `RexxVM` has its own expression evaluation stack. There is no shared queue, per-VM queue, inherited queue, or reset policy to test. |
| Host-provided external data | No host queue integration exists. Command, address, output, and clock callbacks do not provide queued data. |
| Buffer or stack levels | No MAKEBUF, DROPBUF, NEWSTACK, or DELSTACK support or marker representation exists. |
| Allocation, teardown, and recovery | No queue nodes, queue allocations, count maintenance, queue teardown, or queue failure paths exist. Queue-specific fault injection would have nothing real to exercise. |

## Consequence for QUEUED

A built-in-only implementation could not satisfy the requested observable contract. Returning zero would lie after QUEUE/PUSH (which currently cannot execute); counting parsed instructions would not represent data lines and would fail after removals, errors, or nested execution. Reusing `RexxVM.stack` would count expression operands. Adding a mutable counter without queue storage would likewise fabricate state.

A future stack implementation needs a bounded line-bearing store, explicit byte lengths, defined FIFO/LIFO removal and empty-input behavior, ownership and lifetime rules, and failure-atomic insertion/removal. Its tests must drive the same operations that production instructions execute. `QUEUED()` can then inspect that real active stack, traversing existing nodes safely or reading a maintained count whose every mutation path is audited.

## RX29 boundary and evidence

This audit is the completed architecture gate. It does not add a stack, input service, host queue, persistence layer, buffer markers, or source changes. No direct, property, fault-injection, sanitizer, freestanding, inherited-regression, or guest test was run after the gate. See `REXX_RX29_SCORECARD.json` and `docs/rexx/evidence/rx29/` for the exact preflight, QAC availability, artifact hashes, and preservation records.