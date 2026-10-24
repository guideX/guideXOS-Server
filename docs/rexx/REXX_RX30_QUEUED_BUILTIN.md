# RX30 QUEUED() Built-in

## Contract

`QUEUED()` accepts exactly zero arguments and returns a normal decimal whole-number string containing the number of elements in the active `RexxVM` external data stack. Empty strings each count as one item. It reads the queue's authoritative bounded `uint32_t` count, does not consume or mutate queue state, does not read input, and does not inspect the expression-evaluation stack or change numeric settings.

IBM reference: [QUEUED function](https://www.ibm.com/docs/SSGMCP_5.5.0/reference/rexx/queued.html).

## Backing state

`QUEUED()` is a VM-aware built-in registered in the normal function table. The active queue lives in `RexxVM.data_stack`, isolated per VM and shared by instructions executed in the main program, internal routines, nested calls, and `INTERPRET`. There is no independent counter that can drift from queue contents.

## Errors and limits

Wrong argument count is a normal invalid-function-call error. A corrupt/uninitialized stack is an invalid-bytecode/runtime-state error. Queue insertion is bounded at 4,096 elements, 16 MiB per element, and 16 MiB aggregate payload; a successful count therefore fits its normal whole-number representation. `QUEUED()` remains usable after a failed insertion or PULL recovery and reports only elements still present.

If PULL runs with an empty stack, no queue item is fabricated. Since the VM has no supported input provider, the read fails with an explicit unavailable-input error; `QUEUED()` itself never triggers that fallback.

## Qualification

Direct tests check zero-argument validation, empty and non-empty counts, inclusion of empty strings, non-consumption, successful QUEUE/PUSH/PULL updates, failure atomicity, routine/`INTERPRET` sharing, and VM isolation. The fixed-seed language corpus and regression evidence are listed in `evidence/rx30/`.