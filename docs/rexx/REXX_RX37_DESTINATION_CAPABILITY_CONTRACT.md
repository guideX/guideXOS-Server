# REXX RX37 Destination Capability Contract

## API surface

`rexx_output_capability.h` defines a C-compatible, bounded, pointer-free `RexxVMOutputCapability`. Each `RexxVM` may install an optional read-only callback with `rexx_vm_set_output_capability_reader()` and request a fresh snapshot with `rexx_vm_get_output_capability()`. `RexxOutputHost` has an optional `read_capability` callback for embeddings that own the destination. The CLI adapter calls that callback with the same host context used for output writes.

The query is synchronous and does not allocate. The VM copies the provider's value into caller storage and retains no destination pointer or capability snapshot. A provider must keep the callback context alive while installed and must populate every field, including `struct_size`, identity, and generation. Width values are bounded by `REXX_VM_OUTPUT_CAPABILITY_MAX_WIDTH` (1,048,576); zero is never a known width.

## Metadata

| Field | Contract |
|---|---|
| `destination_kind` | None, interactive, redirected, background, or record |
| `state` | Known, no meaningful width, destination not installed, temporarily unknown, or stale |
| `width_unit` | None, character columns, output bytes, record bytes, or pixels; no implicit conversion |
| `logical_line_width` | Positive bounded value only when state is Known; interpretation is determined by `width_unit` and `wrap_policy` |
| `wrap_policy` | Unknown, none, explicit newlines, destination width, record boundary, or storage limit |
| `record_length` | Nonzero only for a record-byte boundary and equal to its declared width |
| `destination_identity` | Nonzero provider-owned identity for installed destinations |
| `destination_generation` | Nonzero provider-owned version for this snapshot; the host advances it when relevant metadata changes |

The VM checks struct size, enum values, reserved bits, width bounds, and valid unit/policy combinations. It does not convert pixels or bytes to columns. Character-column and pixel widths require destination-width wrapping; record bytes require record-boundary policy; output bytes may describe destination width or a storage limit. A non-known state cannot carry a width or record length.

## Query results and lifecycle

`rexx_vm_get_output_capability()` distinguishes invalid arguments, no installed provider, provider failure, unsupported query, invalid metadata, and a stale observation. Provider status `UNSUPPORTED` is distinct from a successful `NO_MEANINGFUL_WIDTH` result. The latter can still identify a real destination while truthfully carrying no width.

Replacing the VM output writer clears its capability reader and advances the VM's provider-binding generation. The host must install the new reader explicitly; a previous reader cannot describe a replacement destination by accident. The query snapshots the writer, user-data pointer, reader, and binding generation before calling the provider. If any binding changes during the callback, it returns `QUERY_STALE` and a zero-width stale result. Teardown during a capability callback is rejected; the caller may free the VM after the query returns.

`rexx_vm_set_output_capability_reader()` requires an initialized VM, an installed writer for a non-null reader, a non-executing VM, no active capability callback, and available generation space. Configure providers outside execution. The VM does not cache a capability between calls. Resizing, renderer changes, or destination reconfiguration must be reflected by the provider on the next query; the provider owns its generation and any synchronization needed to make its snapshot atomic. The VM cannot discover an internal resize that the host does not report.

## Destination policy

The contract describes capabilities; it does not create destination types. RX37's tests exercise interactive fixed columns, resize, raw redirected bytes, background record bytes, record output, pixel-valued geometry, no-width states, provider failure, replacement, and invalid metadata. The production shell adapter currently installs no capability reader. Hosted CLI hosts may provide one, but no production host in this repository currently supplies a verified logical line width. This API is not a REXX built-in and changes no language-visible behavior.

## LINESIZE use

Only an authoritative character-column boundary that matches actual SAY wrapping can support the selected TSO/E foreground-style compatibility profile. An output-byte storage cap, pixel width, screen rectangle, fixed font metric, IBM batch constant, or record size is not silently converted to that result. RX37 does not implement `LINESIZE()`.
