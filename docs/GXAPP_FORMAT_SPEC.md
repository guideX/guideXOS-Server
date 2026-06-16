# GXAPP Universal Application Format Specification

Version: 1
Status: Implemented custom container
Last updated: 2026

This document defines the canonical `.gxapp` package format used by the current guideXOS Server implementation. The format is a native magic-header container. It is not ZIP, does not use compression, and does not include signatures yet.

## 1. Goals

- Single file containing metadata and one or more architecture-specific binaries.
- Simple bounded parser suitable for OS loader/package-manager paths.
- Deterministic binary paths and architecture names.
- Fail-closed validation for malformed or ambiguous packages.

## 2. File layout

All integer fields are little-endian.

```text
GXAPP header
entry[0]
entry[1]
...
entry[n-1]
```

### Header

| Field | Size | Value |
|---|---:|---|
| magic | 8 bytes | `47 58 41 50 50 0D 0A 1A` (`GXAPP\r\n\x1A`) |
| formatVersion | u32 | `1` |
| flags | u32 | bit 0 must be set |
| entryCount | u32 | number of following entries |

### Entry record

Each entry is stored inline after its record header.

| Field | Size | Description |
|---|---:|---|
| kind | u32 | `1` = metadata, `2` = binary |
| architecture | u32 | canonical `CpuArchitecture` enum value; metadata uses `Unknown` |
| pathSize | u16 | byte length of `path` |
| dataSize | u64 | byte length of `data` |
| path | pathSize | UTF-8 path bytes, no NUL terminator |
| data | dataSize | entry payload |

No trailing bytes are allowed after the last declared entry in format version 1.

## 3. Required entries

A valid package contains exactly one metadata entry and at least one binary entry.

### Metadata entry

- `kind`: `1`
- `architecture`: `Unknown`
- `path`: `metadata.json`
- `data`: UTF-8 JSON object
- Maximum size: 1 MiB

The current implementation requires these top-level string fields:

```json
{
  "format": "gxapp",
  "formatVersion": 1,
  "applicationName": "calculator",
  "version": "1.0.0",
  "requiredGuideXOSVersion": "1.0.0",
  "binaries": [
    { "architecture": "amd64", "path": "bin/amd64/app.bin", "entryPoint": "main" }
  ]
}
```

### Binary entries

- `kind`: `2`
- `architecture`: one supported non-Unknown architecture enum value
- `path`: `bin/<arch>/app.bin`
- `data`: native executable payload for that architecture
- Maximum size: 64 MiB per binary

Duplicate architecture binary entries are invalid.

## 4. Architecture names

The canonical `CpuArchitecture` enum is shared by the kernel, package manager, loader, and `.gxapp` container code.

| Enum | String |
|---|---|
| Unknown | `unknown` |
| X86 | `x86` |
| AMD64 | `amd64` |
| ARM | `arm` |
| ARM64 | `arm64` |
| IA64 | `ia64` |
| LOONGARCH64 | `loongarch64` |
| MIPS64 | `mips64` |
| PPC64 | `ppc64` |
| SPARC | `sparc` |
| SPARC64 | `sparc64` |
| RISCV64 | `riscv64` |
| S390X | `s390x` |

## 5. Parser limits

Current hard limits:

| Limit | Value |
|---|---:|
| Maximum package size | 128 MiB |
| Maximum entry count | 64 |
| Maximum metadata size | 1 MiB |
| Maximum binary size | 64 MiB |
| Maximum entry point string size when creating packages | 4096 bytes |

## 6. Validation rules

When opening a package, implementations must reject:

- Invalid magic, unsupported version, or invalid required flags.
- Zero or excessive entry counts.
- Truncated entry headers, paths, or data.
- Integer overflow when computing path or data extents.
- Entry extents outside the package buffer.
- More than one metadata entry.
- Missing metadata entry.
- Metadata not stored at `metadata.json`.
- Metadata larger than the configured limit.
- Malformed metadata or missing required metadata strings.
- Unknown entry kinds.
- Unknown architecture values for binary entries.
- Binary paths that do not match `bin/<arch>/app.bin`.
- Empty or oversized binary entries.
- Duplicate architecture binary entries.
- Packages with no binary entries.
- Trailing bytes after the declared entries.

## 7. Package installation behavior

The package manager installs packages under the stable guideXOS application root `/system/apps`.

Installation flow:

1. Validate source package extension and parse the source `.gxapp`.
2. Verify the current CPU architecture has a matching binary.
3. Create `/system/apps` and `/system/apps/.staging` using filesystem helpers, not shell commands.
4. Copy the package to a staging path.
5. Re-open and validate the staged copy.
6. Atomically rename the staged package into the final path if no existing package is present.
7. If a package already exists, leave it untouched and store the staged package as a logged safe replacement path.

## 8. Not implemented yet

The current format intentionally does not yet implement:

- ZIP containers.
- Compression.
- Repository downloading.
- Signature files.
- Hash validation.
- Full JSON schema validation.
- ELF class/machine/endian validation in the loader.

## 9. App manifest discovery fixture

`Apps/HelloWorld/app.json` is a diagnostic App Manifest fixture for AppRegistry scanning. It describes a future external `NativeElf` app with relative ELF paths for `amd64`, `x86`, and `arm64`, but the ELF files are intentionally not present.

This fixture is for manifest discovery only. AppRegistry should load and register it, DesktopService should list it, and launch attempts should continue to return the existing “manifest found but execution is not implemented yet” response until NativeElf execution is added in a future pass.
