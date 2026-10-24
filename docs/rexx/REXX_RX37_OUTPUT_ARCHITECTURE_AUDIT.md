# REXX RX37 Output Architecture Audit

## Result

RX37 establishes an optional, per-VM destination-capability query and qualifies it with bounded hosted destinations. The production kernel shell still provides no trustworthy logical text-column width. **Outcome B — partial.** `LINESIZE()` remains Not implemented.

Starting state: branch `REXX_SUPPORT`, HEAD `034de0e75b9c0db1125de360f0764ff58e8bde6d`; tracked worktree and index clean; 134,930 untracked paths matched the canonical RX36 path manifest; all 31 protected root `.obj`/`.pdb` files matched the RX36/RX35 snapshot. RX36 inventory counts are retained unchanged: 61 Implemented, 7 Not implemented, 5 Partial, 1 Not assessed, 1 Not implemented by design (75 entries).

## SAY pipeline

1. `OP_SAY` in `rexx_vm.c` moves the evaluated value to a byte string. A value is owned by the VM until it is freed after writing.
2. When a VM writer exists, SAY calls it once for non-empty value bytes and once more for a single LF. The callback accepts a pointer and length; it receives no width, destination kind, or formatting policy. A failed callback makes execution return an I/O error. Without a writer, hosted builds use stdout; freestanding builds fail closed.
3. `rexx_cli.c` binds the writer to its per-run `RexxOutputContext`. `RexxOutputHost` supplies write/flush callbacks, context, and cumulative stdout/stderr byte quotas. Those quotas are execution resource limits, not line widths. RX37 adds an optional same-destination capability reader; it does not detect whether stdout is a terminal.
4. The kernel shell adapter has a null callback context and sends each writer call to `kernel::shell::write_rexx_output`.
5. `MAX_LINE_LENGTH` is 120 bytes including the terminator. The adapter processes at most 119 payload bytes per local chunk and rejects NUL bytes. It calls `output_string` once per chunk. `output_string` has its own 120-byte local line buffer and flushes any residual text as a stored row when that call ends. Consequently the current storage chunk boundary does create a stored-row break in this implementation; this is storage-induced behavior, not an independently configured logical text width.
6. An explicit LF also stores a row. Because VM SAY sends its final LF in a separate callback, a non-empty SAY stores its value row(s) and then an empty row; an empty SAY stores an empty row. Embedded LF bytes break rows inside a callback. CR is stored as a byte and is later drawn as a blank glyph. High-bit bytes are also drawn as blanks; NUL is rejected by the shell adapter. Each output callback starts with fresh local line state.
7. Scrollback stores 100 rows of 119 bytes plus NUL. When full, it shifts out the oldest row. That count is a history capacity, not a width.
8. The renderer draws each stored row using a fixed 8-pixel glyph advance. It computes `charsPerLine = textW / kFontW`, but does not use that value to wrap or clip the row. Rows extending past the drawable rectangle can be clipped by the framebuffer. The renderer's pixel width and drawable area are separate from the writer's row creation.
9. Resizing changes the rectangle passed to drawing, so the visible area and number of visible rows can change. There is no resize-to-REXX notification, no reflow of stored rows, and no width capability installed by the shell adapter.

### Width and storage distinctions

| Quantity | Current value or behavior | Meaning |
|---|---|---|
| VM SAY writer payload | Full value byte length per callback (plus a separate 1-byte LF callback) | No line-size limit in the VM callback signature |
| Kernel temporary chunk | 119 payload bytes in a 120-byte local array | Adapter processing bound |
| Kernel stored row | Up to 119 bytes plus NUL | Storage row capacity; callback-local flush makes chunk boundaries visible as rows |
| Scrollback | 100 stored rows | History capacity |
| Logical character-column width | Not declared | No shared REXX/SAY column policy |
| Renderer glyph advance | 8 pixels per byte drawn | Fixed bitmap advance, not a destination-owned column count |
| Drawable width | Current shell rectangle minus 16 pixels | Render geometry; does not drive writer wrapping |
| Pixel clipping | Framebuffer bounds | May clip a stored row without changing its stored bytes |

The 119-byte limit is therefore not reported as `LINESIZE()`: the public capability unit is explicit, and the kernel shell does not install a provider. The current implementation also has callback-boundary and newline behavior that a single character-column number would not describe.

## Existing destinations

- **Interactive kernel shell:** the REXX writer targets the shell scrollback; the optional capability reader is not installed. It has no logical character-column declaration and no resize generation shared with REXX.
- **Hosted CLI stdout:** `RexxOutputHost` can route bytes to stdout or an arbitrary host writer. It cannot infer terminal, redirect, background, or record semantics. A host may install a read-only capability callback when it owns truthful metadata; otherwise a query reports unsupported.
- **stderr:** used for CLI diagnostics, not the SAY writer destination. No REXX SAY mode query is exposed for it.
- **Redirected output:** the host can redirect its own writer, but the CLI has no separate file/record contract and does not identify raw bytes versus logical records.
- **Background output:** no distinct guideXOS REXX background destination or record policy is installed.
- **Record-oriented output:** no production record destination or logical record length is installed.

The last three categories are vocabulary in the capability contract and test doubles only; RX37 does not add those destination implementations.

## Evidence and remaining gap

The hosted model verifies the provider API, units, unknown/unsupported states, destination replacement, resize generations, and width-to-formatting equivalence independently. ASan and UBSan each pass all 36 executable targets. The freestanding AMD64 build and QAC lane are recorded in the RX37 scorecard. The existing exclusive QAC coordinator had an active acceptance reservation at preflight, so RX37 did not start a guest or take control of its QEMU process.

No test mock is represented as native Console evidence. RX38 should first qualify one production destination-owned policy and its actual renderer relationship, then repeat the isolated QAC run when the coordinator is free. Do not expand this dependency into a terminal emulator, proportional-font layout engine, or general redirection system.
