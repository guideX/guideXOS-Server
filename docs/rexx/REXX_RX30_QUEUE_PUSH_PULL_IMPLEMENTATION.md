# RX30 QUEUE, PUSH, PULL, and PARSE PULL

## Compiled instructions

`QUEUE [expression]` and `PUSH [expression]` use the existing expression parser, compiler, and `RexxValue` conversion path. `QUEUE` inserts at the tail; `PUSH` inserts at the head. Omitting the expression inserts one empty element. Evaluation and conversion complete before queue insertion, and node storage is copied before any queue links change. Spaces, original case, binary bytes, and exact length are retained.

`PULL [template]` compiles to the existing parse-plan mechanism with source `PULL` and uppercase enabled. It removes the head line; with no template it frees and discards the line. With a template, ASCII uppercase is applied to a private parsing copy and the existing template engine assigns values. Its behavior is equivalent to `PARSE UPPER PULL template`.

`PARSE PULL [template]` selects the same queue source and parse-template engine without uppercase. The already-supported template implementation handles variable lists, word splitting, final remainder assignment, placeholder `.`, supported positional patterns, and empty input. `PARSE UPPER PULL` sets the existing parse-plan uppercase flag.

IBM references: [QUEUE](https://www.ibm.com/docs/en/zos/2.5.0?topic=instructions-queue), [PULL uppercase processing](https://www.ibm.com/docs/en/zos/2.5.0?topic=parsing-using-upper), [PARSE](https://www.ibm.com/docs/en/zos/3.1.0?topic=instructions-parse).

## Lifecycle and error recovery

All of these operations mutate the per-VM queue. Queue state is shared by the main program, internal routines, nested calls, and `INTERPRET`; routine return and trapped or untrapped errors do not undo prior successful insertions. It is released only by `rexx_vm_free`.

A PULL consumes the element before later transforms and template work. Failures in uppercase allocation, parse overlay/template processing, or variable assignment do not restore it. Detached node ownership remains local through cleanup, avoiding leaks and dangling queue references. A failed QUEUE/PUSH expression or insertion does not create a phantom entry.

If the queue is empty, PULL returns a deterministic unavailable-default-input runtime error. There is no injected or production input callback in the embedding API today. It neither synthesizes an empty line nor reads hosted stdin nor blocks.

## Byte behavior

The data stack copies exact byte spans and lengths. Uppercase changes only ASCII lowercase bytes in the retrieved copy; queued siblings are untouched. The implementation makes no Unicode, locale, DBCS, or EBCDIC conversion claim. Queue storage limits and VM ownership are detailed in `REXX_RX30_DATA_STACK_ARCHITECTURE.md`.

## Qualification

The guest demo exercises consecutive and mixed insertion, `QUEUED()`, case-preserving and uppercase retrieval, an empty element, multi-variable/placeholder parsing, routine sharing, and `INTERPRET`. Hosted fault injection covers expression, conversion/allocation, queue insertion, pull transformation, template/assignment, and recovery paths. See the RX30 hosted and sanitizer logs under `evidence/rx30/`.