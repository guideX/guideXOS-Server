# Inherited fault evidence

The exact inherited Outcome E disassembly, registers, source mapping, and
effective-address equation are preserved in
`../run-20260809-154140040/inherited-fault-analysis.md`.

That analysis uses the immutable inherited artifact from
`out/dotnet/gc-first-non-null-root-callback-boundary/build/artifact/NativeAotGcSingleThreadSuspendEe.exe`.
The current proof image was relinked and has a different address layout, so
its disassembly at `0x1008E2BE` is not evidence for the inherited fault.
