# ffc.h provenance

- Upstream: [kolemannix/ffc.h](https://github.com/kolemannix/ffc.h)
- Revision: `7e85c6d11a0289d5eead7c1404de8203c52f8cf8`
- Upstream header: `ffc.h`, v26.09.01, 160,480 bytes at the revision above.
- License: MIT; the accompanying `LICENSE-MIT` is the upstream license text.
- Purpose: correctly rounded, locale-independent decimal-to-binary64 parsing
  used for already grammar-validated JSON number spans.

Target-specific changes in the vendored header are intentionally small:

1. The `intrin.h` include is limited to MSVC or non-MinGW ARM64 builds because
   the upstream guard selected an unavailable header for the AMD64 MinGW
   freestanding target.
2. The `<math.h>` include is removed. The wrapper in
   `navigator_javascript/json_number_impl.inl` supplies `NAN` and `INFINITY`
   with compiler builtins on the target (or `numeric_limits` in hosted MSVC).
3. Trailing whitespace and the extra terminal blank line are removed from the
   vendored header; this is whitespace-only cleanup.

The wrapper also maps the implementation's byte-copy operations to complete
local byte-loop helpers. These are real `memcpy`, `memmove`, and `memset`
semantics for the implementation, not link-only stubs. The freestanding
qualification audits the resulting object for unresolved symbols.

The C++ fast_float headers were evaluated separately but were not selected:
their standard-library integration pulled unavailable hosted headers/runtime
dependencies in the target profile. The ffc C99 port passed the focused
AMD64 freestanding compile and dependency audit.
