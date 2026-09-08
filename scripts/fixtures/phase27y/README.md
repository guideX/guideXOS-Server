# Phase 27Y asynchronous NativeElf GUI fixture

This project is staged by `smoke-compiler-bootstrap.ps1 -Phase27YOnly`.
The compiler-generated NativeElf creates the real compositor-backed window;
the smoke then observes it from the run owner before requesting production
close and completion.
