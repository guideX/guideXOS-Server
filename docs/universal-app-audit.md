# Universal app integration audit

## 1. What looks correct

- `.gxapp` remains a simple custom magic-header container with explicit magic, format version, flags, and entry count checks.
- The canonical format is now documented in `docs/GXAPP_FORMAT_SPEC.md`; the outdated ZIP/container/signature text was removed for now.
- CPU architecture naming is centralized in `cpu_architecture.h` and shared by gxapp, package manager, loader, and kernel architecture detection.
- The canonical architecture set now includes `Unknown`, `X86`, `AMD64`, `ARM`, `ARM64`, `IA64`, `LOONGARCH64`, `MIPS64`, `PPC64`, `SPARC`, `SPARC64`, `RISCV64`, and `S390X`.
- `.gxapp` parsing now applies package, metadata, binary, and entry-count limits before accepting package content.
- Entry path and data extents are bounds-checked with overflow protection, and version 1 packages now reject trailing bytes after declared entries.
- Duplicate binary entries for the same architecture are rejected instead of being silently overwritten.
- Metadata parsing now fails closed for non-object metadata, missing required metadata strings, and missing or incorrect `format: "gxapp"`.
- `FS::readAll` now has a structured result overload with maximum byte limits, size conversion checks, and final read verification.
- Package installation no longer shells out to create directories. It uses filesystem helpers and a stable `/system/apps` install root with `/system/apps/.staging` for staged installs.
- `.gxapp` installation now writes to a staging path, validates the staged copy, and uses rename for the final install path.
- Existing installed packages are not overwritten in place. When a final path already exists, the package manager logs the conflict and installs the staged copy to a safe replacement path.

## 2. What remains risky

- Metadata parsing is still a small hand-rolled parser, not a full JSON parser. It now rejects more malformed metadata, but it can still misread duplicate fields or misleading nested fields.
- `parseEntryPointForArchitecture` can still fall back to `main` when the per-architecture entry point is missing or malformed.
- Loader execution still casts an address to a function pointer and calls it directly. It does not yet validate ELF headers, ELF class, machine type, endianness, load segments, memory permissions, or ABI requirements.
- Atomic rename semantics depend on the host/filesystem implementation. The install path is safer, but cross-device staging/final paths must remain on the same filesystem.
- Existing-package replacement is safe but not a complete upgrade transaction; there is no manifest, version comparison, rollback marker, or user-approved replace step yet.
- `.gxapp` extension checks are still case-sensitive.
- The package format still has no signature or hash validation by design for this pass.
- `FS::writeAll` verifies stream state after writing, but there is no durable sync/fsync step.

## 3. What should be fixed next

1. Replace the remaining hand-rolled metadata and entry-point lookup logic with a bounded strict JSON parser or a purpose-built metadata scanner that rejects duplicates and validates the binary table against entries.
2. Harden `GXAppLoader`: validate executable format, architecture machine ID, class, endianness, segment bounds, and entry point before executing.
3. Add loader/package-manager logging for selected architecture, selected binary path, invalid executable headers, allocation failures, and execution failures.
4. Decide the final upgrade policy for existing packages: explicit replace API, version checks, rollback, and atomic swap/backup behavior.
5. Add case-insensitive `.gxapp` extension handling where appropriate for case-insensitive filesystems.
6. Add integrity/signature support in a future pass after the unsigned custom-container format is stable.
