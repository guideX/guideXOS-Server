# REXX RX31 — Bounded Host Input Provider

## Result

RX31 adds an optional, synchronous, per-VM input reader for `PULL` and `PARSE PULL`. The external data stack remains first in priority. The native kernel shell does not install a reader because it has no safe line-input service; an empty queue therefore remains fail-closed in production.

The provider is embedding infrastructure, not a REXX built-in. RX31 does not add a built-in inventory entry or change DATE, TIME, or TRACE classifications.

## API

The public C types are declared in `rexx_vm.h`:

```c
typedef struct RexxVMInputLine {
    const unsigned char *bytes;
    size_t length;
    RexxVMInputOwnership ownership;
} RexxVMInputLine;

typedef RexxVMInputReadStatus (*RexxVMInputReader)(
    void *user_data, RexxVMInputLine *line);

RexxVMStatus rexx_vm_set_input_reader(
    RexxVM *vm, RexxVMInputReader reader, void *user_data);
```

`rexx_vm_set_input_reader(vm, NULL, NULL)` clears the hook. The callback returns one immediate result:

| Callback result | VM result |
| --- | --- |
| `REXX_VM_INPUT_READ_OK` with valid borrowed span | Parse the line, including a valid zero-length line |
| `REXX_VM_INPUT_READ_UNAVAILABLE` | `REXX_VM_STATUS_INPUT_NOT_READY` |
| `REXX_VM_INPUT_READ_END_OF_INPUT` | `REXX_VM_STATUS_INPUT_END_OF_FILE` |
| `REXX_VM_INPUT_READ_ERROR` | `REXX_VM_STATUS_INPUT_PROVIDER_ERROR` |
| `REXX_VM_INPUT_READ_PENDING` | `REXX_VM_STATUS_INPUT_PENDING_UNSUPPORTED` |
| Unknown status, invalid ownership, or NULL with nonzero length | `REXX_VM_STATUS_INPUT_RESULT_INVALID` |
| Length greater than 16 MiB | `REXX_VM_STATUS_RESOURCE_LIMIT` |
| VM copy or parse allocation failure | `REXX_VM_STATUS_OUT_OF_MEMORY` |

With no callback installed, the RX30 result `REXX_VM_STATUS_INPUT_UNAVAILABLE` is preserved. The distinct callback statuses prevent a valid empty line from being confused with absent, unavailable, ended, or failed input.

## Ownership and lifetime

- The host owns the callback and `user_data`; the VM does not retain or free either allocation.
- On `OK`, the provider marks the line `REXX_VM_INPUT_OWNERSHIP_BORROWED`. The returned byte span is immutable and remains valid until the next callback invocation or until the reader is replaced while the VM is quiescent.
- The VM uses the explicit `length`; it does not call `strlen` on provider bytes. A NULL pointer is valid only with length zero.
- The VM copies a non-discarded line into VM-owned storage before template parsing. It frees that copy on success and all parse/error exits. Provider memory is never freed by the VM.
- For any non-`OK` result, the VM ignores the line structure. The provider must release any temporary allocation it made before returning that status; the VM has no ownership transfer on failure.
- If a valid line is acquired and a later VM allocation or template operation fails, the input is already consumed. This matches RX30 queue semantics, which remove a line before template parsing; the external queue itself is unchanged by provider success or failure.
- The callback and its context must remain alive until the host clears the callback or the VM is freed. Clear it before destroying provider context when the VM remains alive.
- Reader replacement is allowed only while the VM is initialized and not executing. The setter returns `REXX_VM_STATUS_INVALID_ARGUMENT` on replacement during execution. Concurrent VM access is unsupported.
- VM teardown does not invoke the provider or free its context. Once `rexx_vm_free` returns, that VM no longer retains or calls the callback; the host still owns and releases the provider context.
- The callback must return immediately. `PENDING` is explicitly unsupported; there is no polling, busy-wait, scheduler, timeout, cancellation, or terminal editing contract in RX31.
- Reentrant `rexx_vm_execute` is rejected. Reentrant `rexx_vm_free` does not tear down an executing VM.

## Queue priority and REXX semantics

For each pull source, the VM first removes the head element from its own external data stack. The provider is called only when that stack is empty. Provider data is parsed directly and is never enqueued, so it does not increase `QUEUED()` or consume the stack's 4,096-element / 16 MiB aggregate budget.

Both sources use the existing parse-plan and template engine:

- `PULL` performs ASCII `a`–`z` uppercasing before template parsing.
- `PARSE PULL` preserves byte case.
- `PARSE UPPER PULL` performs ASCII uppercasing.
- Embedded NUL and high-bit bytes are retained as bytes. No Unicode, locale, DBCS, or EBCDIC conversion is claimed.
- A pull with no template consumes/discards the acquired input, matching the existing queue behavior.

The provider line limit is `REXX_VM_MAX_PARSE_SOURCE_LENGTH` (16 MiB). Oversized lines are rejected without truncation. They are not charged against queue payload limits. When parsing needs storage, allocation failure is explicit and recoverable on a later VM execution.

## Production availability

The REXX VM previously had per-VM clock, output, and command-dispatch callbacks, but no input hook. RX31 follows those embedding conventions with a per-VM reader. The kernel shell consumes characters into a private command buffer and exposes complete-command execution, not a reusable REXX line-input service. Wiring that shell buffer to `PULL` would introduce unsafe reentrancy and input ownership assumptions. No hosted `stdin` access or fabricated input was added to freestanding code.

Therefore the production VM leaves the input reader uninstalled. Empty-queue `PULL` reports input unavailable, and the interpreter can report/recover through its existing condition path. Injected providers are available to embedding hosts and deterministic tests.

## Qualification

`tests/rexx_rx31_host_input_test.c` exercises case behavior, templates, empty and whitespace lines, binary bytes, queue priority, no-provider behavior, unavailable/EOF/error/pending, invalid spans and metadata, exact and oversized limits, provider and VM allocation failure, recovery, callback replacement, teardown, two-VM isolation, nested routines, and `INTERPRET`.

The fixed model seed is `0x52583331`; it performs 256 mixed queue/provider operations. The recorded focused target result is 169 assertions, 0 failures. The inherited RX30 data-stack reference-model test also runs in RX31.

Full phase results are summarized in `REXX_RX31_SCORECARD.json`; raw hosted, sanitizer, native-build, and preservation evidence is under `evidence/rx31/`.
