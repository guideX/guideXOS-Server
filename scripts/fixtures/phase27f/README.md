# Phase 27F fixture

This bounded NativeElf project is used by the Developer Studio
Build-before-Run QEMU proof. The source is intentionally small enough for the
bootstrap compiler and changes between runs to prove artifact identity,
application output isolation, non-zero exit-code propagation, and recovery
after a failed build.
