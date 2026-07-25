# Developer Studio hosted validation

This document records the reproducible hosted Native ELF path for the
`v0.5_DEVELOPER_STUDIO` checkout. It is an experimental development path, not
a production filesystem sandbox or a bare-metal VFS contract.

## PNG dependency

`png_loader.cpp` includes `third_party/stb/stb_image.h`. The authoritative
local source selected for this checkout is:

```text
source:      D:\dev\guideXOSServerV0.2\third_party\stb\stb_image.h
destination: third_party/stb/stb_image.h
version:     stb_image v2.30
revision:    1692fe6e21ce7b7abbc6fcb6d1d3ff6ebe0b8537
SHA-256:     1F8C1B6B408F26E3B20CBFBBD4758AFB3DC9B837FF1E17C258928F406148A87C
license:     public domain (as stated in the header)
```

The same bytes were found in the local v1.1 guideXOS checkout and in the
historical Server `PNG Loader` commit before the header was deleted. The
source and destination Git blob are both
`9eedabedc45b3e6fd88fae6f14a160b4d53272ec`. Other vendor content remains
ignored; only this required header is permitted by `.gitignore`.

To restore a fresh checkout, copy the canonical header from the source path
above and verify the SHA-256 before building. Do not substitute an arbitrary
latest stb release or retrieve an unpinned dependency.

## Hosted build

The authoritative command is:

```powershell
cmd /c .\build-native-experimental.bat
```

The current toolchain is MinGW-w64 x86_64-msvcrt-posix-seh GCC 15.2.0 with GNU
binutils 2.46.0. The build enables
`GX_ENABLE_EXPERIMENTAL_NATIVE_ELF_EXECUTION` and produces
`guideXOSServer.experimental.exe`. The Developer Studio repository build is:

```powershell
powershell -ExecutionPolicy Bypass -File D:\dev\guideXOS_Developer_Studio\build.ps1
```

That build refreshes `Apps/DeveloperStudio/bin/amd64/developerstudio.elf` from
the Developer Studio checkout. The current packaged ELF is ELF64,
`ET_EXEC`, `EM_X86_64`, entry point `gx_main`, and SHA-256
`70FE5D4F66F0F401958C220DBF1E7752DB4AB5B62155EB4664259B545A2B5126`.
Packaged ELF files are tracked release/package inputs in the Server checkout;
they are not tracked build output in the Developer Studio checkout. The
binary must be refreshed by the Developer Studio build before a hosted smoke
run; its SHA-256 should be reported rather than hard-coded as a reproducibility
claim.

## Filesystem ABI semantics

The four append-only Native ABI calls are `file_stat`,
`file_read_workspace`, `file_list`, and `file_write_all`. The latter name is
historical: `file_read_workspace` is a full-file read with no offset, while
all four calls operate on an explicit hosted absolute path. The calls require
the corresponding `filesystem.read` or `filesystem.write` permission.

Hosted validation enforces:

```text
path:             maximum 240 bytes, strict UTF-8, no control bytes or NUL
                  in the effective C string, no relative/device/UNC path,
                  no .. segment, no symlink component
workspace I/O:    maximum 256 KiB per read or write
list capacity:    maximum 129 ABI entries (Developer Studio displays 128)
Developer Studio: maximum 8 open documents, 256 KiB editable file,
                  8 workspace path segments, 768-byte model path
```

The Server validates Native image/stack pointer ranges, output ranges,
pointer-plus-length overflow, structure sizes, result capacities, file-size
conversion, and optional list output pointers. Missing files, type mismatches,
access failures, short reads, invalid paths, and failed writes return stable
error results. Lists are directory-first and sorted case-insensitively with an
original-byte tie-break. Writes use truncate/write/flush; atomic replacement
and recovery from a power loss are not claimed. The Server intentionally does
not know the Developer Studio-selected workspace root, so the Developer Studio
guest model enforces root containment. This hosted policy must not be treated
as unrestricted production filesystem access or as the future bare-metal
semantics.

## Validation procedure and evidence

Normal startup is run without a launch command, then checked with
`desktop.apps`, `desktop.appmodel.inventory`, `desktop.windows.owners`, and
`nativeapp.processes`. Developer Studio is then launched explicitly with:

```text
desktop.launch guideXOS Developer Studio
```

The App Model registry identity is `com.guidexos.developerstudio`; the package
resolves to AMD64, `bin/amd64/developerstudio.elf`, `guidexos-c-abi-v1`, and
`gx_main`. Live markers observed in the hosted runtime are:

```text
GUIDEXOS_DEVELOPER_STUDIO_MARKER application_construction=PASS
GUIDEXOS_DEVELOPER_STUDIO_MARKER target_profile=guidexos.amd64.hosted.native maturity=experimental
GUIDEXOS_DEVELOPER_STUDIO_MARKER filesystem_api=workspace_extensions
GUIDEXOS_DEVELOPER_STUDIO_MARKER main_window_creation=PASS
GUIDEXOS_DEVELOPER_STUDIO_MARKER initial_render=PASS
GUIDEXOS_DEVELOPER_STUDIO_MARKER application_close=PASS
GUIDEXOS_DEVELOPER_STUDIO_MARKER clean_close=PASS
```

Automated validation covers ABI layout, the Native filesystem contract,
host-side workspace/document behavior, package identity, ELF validation,
launch discovery, lifecycle markers, and process/window cleanup. Manual
validation is still required for visual inspection and end-to-end compositor
keyboard/mouse editing; the current shell has no generic key/mouse injection
command. Do not report the complete interactive editor slice as automated
until that manual portion has been performed.

Observed validation on 2026-07-25 includes a normal experimental-server start
with no Developer Studio construction marker, `windowCount=0`, and zero native
processes; a live `D:\gxws` workspace open with deterministic Explorer entries;
the automated host-side workflow over `sample.cpp`, `notes.txt`, nested JSON,
binary, oversized, save, discard, and cancel cases; and two explicit launches
in one Server process. The repeated launches used window IDs 1000 and 1001,
each reached initial render and clean close, and each left zero windows. A
fresh Server process also passed the full hosted App Model smoke. The complete
live keyboard edit/save/dirty-close sequence remains a manual follow-up, not
an asserted acceptance result, because the bounded shell input path is not yet
a reliable scripted driver for every interaction.

The ordinary `scripts/smoke-startup-appmodel-regression.ps1` path was also
attempted. Its static checks passed, but execution stopped before server
startup because this checkout has no ordinary `guideXOSServer.exe` at the
expected root location. The experimental executable produced by
`build-native-experimental.bat` is the authoritative hosted Native ELF path
used for the live evidence above.

The deterministic contract command is:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\run-native-filesystem-contract-test.ps1
```

It completed with `Native filesystem contract build and test PASS`. The layout
command completed with `Native ABI layout test PASS`.

The repeated lifecycle and close-ownership command is:

```powershell
powershell -ExecutionPolicy Bypass -File D:\dev\guideXOS_Developer_Studio\tests\smoke-developer-studio-repeated.ps1
```

It completed with two unique window IDs, zero windows after each close,
exited native processes, and clean Server termination.

## Checkpoint history

The environment created automatic checkpoint commits during the prior
Developer Studio work. They were inspected without staging, committing,
pushing, resetting, cleaning, stashing, rebasing, reverting, amending,
squashing, or rewriting history. The exact hashes and file-level audit remain
in the Developer Studio phase report and repository history.
