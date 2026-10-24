# REXX RX33 — Bounded Byte-Stream Provider

## Purpose

RX33 adds a synchronous, read-only byte-stream hook owned by one REXX VM. It is an embedding contract for CHARIN; it is not a file API or a kernel-global stream table. The production kernel does not install a provider.

The C declarations are in rexx_vm.h. An embedding installs a copied callback table with rexx_vm_set_byte_stream_provider(vm, provider, user_data). Passing a null provider clears the hook. A provider supplies a resolver and reader; a seek callback is required only for identities it advertises as seekable.

## Call contract

The resolver receives a flag distinguishing an omitted name (the provider's default input stream) from an explicitly supplied name, plus a pointer and length for that name. It returns an opaque identity, readable/seekable capability bits, and borrowed identity ownership. Only the provider decides which identities exist. A host string is never opened as a path by the VM.

The reader receives the identity and a requested byte count. On success it returns a pointer, the exact byte length, borrowed-buffer ownership, and an optional end-of-file-after-read flag. It may return fewer bytes than requested. The provider owns the cursor and advances it by the reported byte count once. The VM does not apply another cursor increment. A non-OK callback result carries no buffer metadata and must not advance the cursor.

The optional seeker receives a one-based position. CHARIN calls it only after the resolver advertises the identity as seekable. A non-seekable identity rejects an explicit start before calling the seeker or reader. A zero-length request performs no read; an explicit start still invokes seek before the empty result is returned.

## Ownership and lifecycle

- The embedding owns provider code, user_data, stream identities, and all provider resources. The VM copies the callback table but never closes or frees provider state.
- Keep user_data alive until the provider has been cleared or the VM has been freed. Keep each opaque identity valid for its callbacks. A provider may map an identity to a persistent cursor in its own context.
- Read bytes are borrowed through the next provider callback. The VM allocates the result buffer before invoking callbacks and copies a validated byte span immediately. It never calls strlen on stream data.
- Provider replacement is allowed only while the initialized VM is quiescent. Replacement from a callback is rejected. VM teardown attempted inside a byte-stream callback is ignored to protect the active VM. Concurrent access to one VM is unsupported.
- VM teardown does not call the provider. The embedding releases its own context and stream resources after clearing the provider or after VM teardown.
- Internal routines and INTERPRET use the same VM and therefore the same installed provider and provider-owned cursor. Separate VMs have separate callback tables and contexts unless the embedding deliberately shares context.

The RX30 external data stack and RX31 complete-line input reader remain separate. CHARIN does not pop, push, count, or otherwise touch queued lines and never calls the RX31 line callback.

## Status and bounds

The VM distinguishes unavailable provider, unknown identity, not-ready input, end of file, provider error, unsupported operation, pending operation, and invalid provider result. These runtime statuses enter the interpreter's existing generic SYNTAX error path, whose description is “Stream I/O error”; RX33 does not add a stream-specific condition or STREAM query.

The reader must return immediately. PENDING is reported as unsupported, and the VM never blocks, polls, or busy-waits. Result length cannot exceed the request or the 16 MiB REXX string cap. A nonzero length requires a non-null borrowed span. Malformed result metadata is rejected and the REXX result is cleared.

The VM allocates the maximum requested result buffer before resolving, seeking, or reading, so an allocation failure cannot consume input or move the cursor. Once a provider reports a successful read, the provider has advanced its cursor. An injected hosted test-only copy refusal returns out-of-memory with no result after that provider-completed read; the test checks that the cursor advances exactly once and that the next call resumes after those bytes. Production copying is a bounded memcpy into the preallocated result and has no recoverable failure return.

## Production boundary

There is no production default stream or named-stream provider today. CHARIN fails closed with no fabricated bytes. The implementation contains no hosted stdin or file-opening calls and does not interpret names as paths. The hosted memory provider in tests is deterministic test code, not a production adapter.

The interface intentionally does not add CHAROUT, LINEIN, LINEOUT, CHARS, LINES, STREAM, filesystem mounting, or persistent host stream tables. RX34 should first address the recorded CHARIN stream-condition gap and decide whether a narrowly scoped, approved native fixture provider exists before adding more stream functions.
