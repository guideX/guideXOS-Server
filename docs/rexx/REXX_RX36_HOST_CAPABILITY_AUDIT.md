# REXX RX36 Host Capability Audit

## LINESIZE reference contract

IBM classifies LINESIZE as a non-SAA function. Its result describes the logical point where the language processor breaks SAY output. IBM TSO/E foreground returns the configured terminal width minus one. TSO/E background returns a documented batch value, and non-TSO/E execution returns the OUTDD logical record length. These are distinct environments; RX36 does not assign any of them to guideXOS by analogy.

Primary reference: [IBM z/OS 2.5 LINESIZE](https://www.ibm.com/docs/en/zos/2.5.0?topic=functions-linesize).

## guideXOS REXX output path

### VM and hosted CLI

OP_SAY in rexx_vm.c converts the value to bytes and invokes the VM's output writer once for the text and once for a newline. The writer type in rexx_vm.h accepts only user data, a byte pointer, and a length. Each VM stores its writer and user-data pointer, but has no width query or output-mode metadata; the embedding manages the lifetime of its callback context.

The hosted CLI routes SAY through RexxOutputHost. That host structure carries write, flush, context, and byte limits for stdout and stderr; it has no terminal detection, logical column count, or record-length field. With no host writer, hosted REXX writes directly to stdout, which may be redirected. A freestanding VM without a writer returns an I/O error. Neither path reports a foreground, background, terminal, or record-oriented mode to LINESIZE.

### Kernel shell adapter

The bare-metal shell adapter in kernel/shell/rexx_shell_cmd.cpp routes the REXX output callback to kernel::shell::write_rexx_output. Its callback context is null. The adapter does not install a width provider or distinguish interactive, redirected, batch, or record output.

In kernel/core/shell.cpp, MAX_LINE_LENGTH is 120. write_rexx_output chunks input to fit that buffer, and output_string splits stored lines when it reaches 119 bytes or sees a newline. This is a fixed storage bound, not metadata owned by the REXX VM or the logical width of every destination.

### Rendering, width, wrapping, and resizing

The shell draw routine receives a drawable rectangle. It computes textW as the rectangle width minus margins and computes charsPerLine as textW divided by the shell glyph width. That local result is not used by the output writer or returned to the VM. The draw routine gets current geometry when rendering, but there is no resize notification or width accessor connected to the REXX VM. Stored strings are drawn from the shell output buffer; the output callback does not observe the renderer's logical column count.

The shell's current renderer uses a fixed glyph advance, but the actual visible area is still determined by draw geometry. A pixel width cannot be reported as text columns without a shared output contract. No per-VM width state, width callback, or resize notification is connected to REXX. The native CLI can instead route output to stdout or an arbitrary host writer, for which the shell rectangle and font do not apply.

SAY itself does not wrap using a VM line-width setting. It forwards the full string; subsequent shell storage splitting is at the buffer limit, while display geometry is separate. Therefore returning 119 would report a storage chunk size, not a verified terminal or SAY wrapping width.

## Other candidate prerequisites

RX34's status API resolves a provider-owned stable stream identity and exposes bounded per-VM status. The provider callback table supplies resolve, read, and optional byte seek. It does not expose available byte count, remaining line count, write operations, record boundaries, open/close, or persistent path access. In the kernel shell, the only supplied byte-stream provider is behind GXOS_REXX_RX34_FIXTURE; the normal production call installs none. CHARIN uses the test fixture and remains Partial; STREAM is a read-only observation subset and remains Partial.

The existing SAY output writer is not a named stream provider. It writes output from REXX execution but has no stream name, cursor, seek, open, close, or remaining-output query. It cannot directly implement CHAROUT, LINEOUT, CHARS, or LINES with their stream semantics.

No accepted identity callback, user account, or environment identity is exposed to REXX. USERID therefore has no trustworthy production source.

## Feasibility decision

**C — Missing output-width architecture.** Do not implement LINESIZE in RX36. There is no authoritative logical output width that the REXX embedding can query, and no evidence that a candidate number matches the actual SAY wrapping point across supported output destinations.

The dependency is a destination-owned output capability shared with SAY wrapping. It must return a validated logical column width only when known, identify noninteractive/record output distinctly, and update on supported resizes. A future LINESIZE implementation must fail closed when that capability is unavailable instead of manufacturing a value.

## Source audit map

- rexx_vm.c: OP_SAY output callback calls
- rexx_vm.h: per-VM output writer type, fields, and setter
- rexx_output.h and rexx_output.c: hosted stdout/stderr writer contract and byte limits
- rexx_cli.c: hosted CLI installation of the VM output writer
- kernel/shell/rexx_shell_cmd.cpp: kernel output adapter
- kernel/core/shell.cpp: 120-byte line storage, 119-byte splitting boundary, and renderer geometry
- rexx_vm.h and rexx_vm.c: RX34 byte-stream provider and per-VM status APIs
- rexx_builtin.c: built-in dispatch table; no descriptor for any remaining Not implemented entry
