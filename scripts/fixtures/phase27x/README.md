# Phase 27X compiler-built GUI fixture

This project is staged by `smoke-compiler-bootstrap.ps1 -Phase27XOnly`.
The compiler-generated NativeElf calls the bounded `gx_window_*` runtime
surface; the loader does not create a canned window for the fixture.
