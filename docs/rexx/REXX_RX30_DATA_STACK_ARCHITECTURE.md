# RX30 External Data-Stack Architecture

## Scope and reference behavior

RX30 adds a VM-owned external REXX data stack for executable `QUEUE`, `PUSH`, `PULL`, `PARSE PULL`, and `QUEUED()`. IBM classic REXX is the semantic reference: `QUEUE` appends, `PUSH` places an item ahead of existing queued data, and `PULL` is the uppercase form of parsing from the data stack. `PARSE PULL` preserves case. See IBM's [QUEUE instruction](https://www.ibm.com/docs/en/zos/2.5.0?topic=instructions-queue), [PULL uppercase behavior](https://www.ibm.com/docs/en/zos/2.5.0?topic=parsing-using-upper), [PARSE instruction summary](https://www.ibm.com/docs/en/zos/2.5.0?topic=parsing-instructions-summary), and [QUEUED function](https://www.ibm.com/docs/SSGMCP_5.5.0/reference/rexx/queued.html).

This is an in-process queue per `RexxVM`. It is not a terminal service, named queue, cross-process stack, persistent queue, or OS IPC facility.

## Representation and limits

`RexxDataStack` is a doubly-linked list of individually allocated flexible-array nodes. Each node records the exact payload byte length and owns a byte-for-byte copy, so empty strings, embedded NUL bytes, high-bit bytes, spaces, and case are preserved. The queue has head and tail pointers, one authoritative `uint32_t` element count, and an aggregate `size_t` payload-byte total. `RexxVM.stack` remains solely the expression-evaluation stack.

Enforced limits:

| Resource | Limit |
| --- | ---: |
| Element count | 4,096 (`uint32_t`) |
| One element payload | 16 MiB |
| Aggregate payload | 16 MiB |
| Node header | 24 bytes on the AMD64 kernel ABI |
| Maximum linked-node header overhead | 98,304 bytes at 4,096 elements |
| Preallocation | None; each insertion allocates only its node |

The per-element and aggregate byte limits follow the existing 16 MiB REXX string cap. Insert checks every addition/subtraction and limit before modifying links or counters. It allocates and copies before linking, so allocation or resource-limit failure leaves the queue unchanged. Allocator-internal metadata is implementation-owned and sits outside the queue's explicit payload/header accounting.

## Ownership and lifecycle

The VM initializes its queue during `rexx_vm_init` using the kernel-compatible VM allocator and destroys it during `rexx_vm_free`. The queue owns each inserted byte copy. `QUEUE`/`PUSH` first evaluate and convert the expression through the existing value-to-string path, then insert; the temporary expression value and conversion buffer are released on both success and failure.

A successful head removal transfers the node allocation into a local `RexxDataStackItem`. The PULL path releases it after discard or after parsing finishes, including error paths. Parsed variables receive their own VM-managed values through the existing parse overlay/commit path. `QUEUED()` reads the queue's authoritative count.

A queue persists across repeated `rexx_vm_execute` calls on the same VM, internal routine returns, nested calls, and `INTERPRET`. Errors and routine returns do not roll back successful queue mutations. Separate VM instances have separate queue heads and allocators. VM teardown releases every node; no mutable global queue state is used. `rexx_data_stack_destroy` is safe for a zeroed or initialized queue and leaves it zeroed.

## Failure behavior

- Expression evaluation/conversion failure: no insertion; no new element.
- Queue node allocation failure or capacity exhaustion: no insertion; prior elements and count remain unchanged.
- Empty-stack read: no fabricated input element; fail with `REXX_VM_STATUS_INPUT_UNAVAILABLE` and the message `external data stack is empty and no input provider is available`.
- PULL after an element is found: remove it before uppercase conversion/template parsing/assignment. Any later failure consumes that element, following the selected REXX consume-before-parse behavior. The owned detached node is still freed on every path.
- VM unwind/teardown: remaining linked elements are released, regardless of prior runtime status.

The runtime has no bounded input-provider callback. Consequently it does not emulate IBM's default terminal-input path when the queue is empty. It does not use hosted stdin or block waiting for input. This limitation is explicit and fail-closed.

## Character model

PULL uppercase uses guideXOS's established ASCII byte transform (`a`–`z` to `A`–`Z`). Other bytes remain unchanged. This does not claim Unicode, locale, DBCS, or EBCDIC transcoding.

## Qualification

Storage is independently checked against a fixed-seed bounded reference deque (`0x52583330`), including byte-exact payloads, empty elements, NUL/high-bit bytes, head/tail ordering, resource boundaries, failed allocations, repeated init/destroy, and isolated instances. VM integration uses a separate fixed-seed model (`0x52583331`) over executable queue operations and language lifecycle cases. See `evidence/rx30/hosted-regression-final.log`, `evidence/rx30/sanitizers-final.log`, and `evidence/rx30/native-build/native-link-evidence-c0110bd146864e2fa3a561f007db3e8f.json` for run results.