# Phase 27O cross-file global data proof

This bounded three-file project exercises the in-guideXOS compiler's mutable
signed-32-bit global storage and external data-symbol linker. `state.cpp`
defines one global, `math.cpp` reads and writes it through `extern int`, and
`main.cpp` calls the helper, logs from read-only data, and reads the same
linked storage before returning 42.

The project has no `sourceEntry`; Developer Studio enumerates the three
translation units independently, then the guest compiler's internal linker
lays out code, read-only strings, and writable global data deterministically.
