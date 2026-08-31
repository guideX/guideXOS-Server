# Phase 27N multi-file compiler proof

The `src` directory intentionally has no `sourceEntry`, so the bare-metal
Developer Studio build service enumerates and sorts all bounded `.cpp` sources.
The guest compiler compiles each file as an isolated translation unit, resolves
the declarations and relocations in its in-memory internal linker, and emits a
single NativeElf artifact. No host compiler or linker participates in that
guest Build operation.
