# Developer Studio hosted validation

This document records the reproducible hosted Native ELF path for the
`v0.5_DEVELOPER_STUDIO` checkout. It is an experimental development path, not
a production filesystem sandbox or a bare-metal VFS contract.

## PNG dependency

`png_loader.cpp` includes `third_party/stb/stb_image.h`. The authoritative
local source selected for this checkout is:

```text
source:      <SERVER_ROOT>/third_party/stb/stb_image.h
destination: <SERVER_ROOT>/third_party/stb/stb_image.h
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

To restore a fresh checkout, verify the repository-relative destination and
SHA-256 before building. Do not substitute an arbitrary latest stb release or
retrieve an unpinned dependency.

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
$DeveloperStudioRoot = (Resolve-Path (Join-Path (Split-Path (Get-Location) -Parent) "guideXOS_Developer_Studio")).Path
Set-Location -LiteralPath $DeveloperStudioRoot
powershell -ExecutionPolicy Bypass -File .\build.ps1
```

That build refreshes the tracked package input at
`Apps/DeveloperStudio/bin/amd64/developerstudio.elf` in the Server checkout.
Audit snapshot from 2026-08-05: size `675664` bytes, ELF64 `ET_EXEC`,
`EM_X86_64`, symbol `gx_main`, SHA-256
`1104FC755272069692A3075622B7434FE59FBD4C00659E16239D5ED0FB851B5F`.
The hash is evidence for that package snapshot, not a permanent build-output
claim; recompute it after rebuilding the Developer Studio checkout.

## Filesystem ABI semantics

The Native ABI is append-only. After the original slots, the current table
keeps `get_ticks_ms`, then appends `file_stat`, `file_read_workspace`,
`file_list`, `file_write_all`, `file_create_directory`, `file_remove`,
`build_project_start`, `build_project_poll`, `build_project_release`,
`development_run_prepare`, `development_run_start`,
`development_run_poll`, `development_run_request_close`, and
`development_run_release`. The workspace calls use an explicit hosted
absolute path and require the corresponding `filesystem.read` or
`filesystem.write` permission. `file_read_workspace` is a full-file read with
no offset.

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
semantics. File-read boundary diagnostics are opt-in through
`GXOS_HOSTED_INPUT_DIAGNOSTICS`; their fields are semantic and do not emit
raw process-memory addresses.

## App Model identity and duplicate-display policy

Canonical application IDs are the activation identity. Exact IDs resolve
directly, while display-name compatibility is centralized in the Server
registry and never depends on filesystem traversal order. A display-name
candidate must match the current architecture and have a valid, contained
launch entry when its kind requires a Native ELF or GXApp package. The source
priority is:

```text
Package > BuiltIn > UserApps > SystemApps > DevelopmentTemporary
```

Only one eligible candidate at the best priority resolves. Equal-priority
eligible candidates are reported as ambiguous and are not launched. Temporary
Development Run records are excluded from display-name compatibility and must
be reached through their exact temporary ID plus owner/generation checks.
This preserves the existing owner-bound development route while preventing a
temporary or SDK record from shadowing an installed package.

The Start Menu carries a parallel canonical-ID list for its display labels,
and desktop shortcuts persist `targetAppId`. Mouse, keyboard, context-menu,
Start Menu, and desktop-shortcut activation all pass that ID through to the
launch resolver. An ambiguous legacy label remains visible for diagnosis but
is not dispatched. The focused repro commands are:

```text
desktop.apps.verbose
desktop.launch.resolve Hello World
desktop.launch.resolve Resource Viewer
desktop.launch.resolve com.guidexos.helloworld
desktop.launch.resolve com.guidexos.resourceviewer
```

The packaged records should win the two display-name lookups, while the
canonical-ID lookups should remain exact. The SDK sample manifests are still
discoverable for inspection, but their missing native entry files make them
ineligible compatibility candidates.

## Native render acknowledgement and keyboard evidence

Native `draw_text`, `draw_rect`, image, and `present_frame` messages are
fire-and-forget. The compositor intentionally drops render acknowledgements
for a nonzero native destination because the host API does not consume them;
`MT_RequestFrame` remains the paint event delivered through `poll_event`.
No new draw/present waits are permitted. `request_window` still waits for its
creation response, and input/paint/close events still use the existing bounded
poll path.

Keyboard validation must exercise both key-down and key-up for ordinary keys
and modifiers, including an Alt down/up pair, and verify that the focused
window receives the matching transitions without duplicate or missing events.
The bounded hosted shell remains a diagnostic aid; the complete editor
keyboard/edit/save sequence is still manual unless that interaction has been
run and recorded separately.

## Validation procedure and evidence

Normal startup is run without a launch command, then checked with
`desktop.apps`, `desktop.appmodel.inventory`, `desktop.windows.owners`, and
`nativeapp.processes`. Developer Studio is then launched explicitly by
canonical ID with:

```text
desktop.launch com.guidexos.developerstudio
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
launch discovery, lifecycle markers, and process/window cleanup. The
experimental shell exposes focused-window `gui.key` input for bounded hosted
checks, but visual inspection and complete compositor keyboard/mouse editing
remain manual validation. Do not report the complete interactive editor slice
as automated until that manual portion has been performed.

Observed validation on 2026-07-25 includes a normal experimental-server start
with no Developer Studio construction marker, `windowCount=0`, and zero native
processes; a live `<WORKSPACE_ROOT>` workspace open with deterministic Explorer entries;
the automated host-side workflow over `sample.cpp`, `notes.txt`, nested JSON,
binary, oversized, save, discard, and cancel cases; and two explicit launches
in one Server process. The repeated launches used window IDs 1000 and 1001,
each reached initial render and clean close, and each left zero windows. A
fresh Server process also passed the full hosted App Model smoke. The complete
live keyboard edit/save/dirty-close sequence remains a manual follow-up, not
an asserted acceptance result, because the bounded shell input path is not yet
a reliable scripted driver for every interaction.

The ordinary `scripts/smoke-startup-appmodel-regression.ps1 -SkipBuild` path
completed its normal-startup, persistence, and explicit-canonical-launch
checks. The normal `build.bat` executable is suitable for App Model identity
and registry validation; the executable produced by
`build-native-experimental.bat` remains the authoritative hosted Native ELF
path used for native lifecycle evidence.

The deterministic contract command is:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\run-native-filesystem-contract-test.ps1
```

The filesystem-contract wrapper can outlive the bounded shell command while
the compiler is still running; after the executable appeared, running it
returned `Native filesystem contract test PASS`. The layout command completed
with `Native ABI layout test PASS`.

The repeated lifecycle and close-ownership command is:

```powershell
$DeveloperStudioRoot = (Resolve-Path (Join-Path (Split-Path (Get-Location) -Parent) "guideXOS_Developer_Studio")).Path
powershell -ExecutionPolicy Bypass -File (Join-Path $DeveloperStudioRoot "tests\smoke-developer-studio-repeated.ps1")
```

It completed with two unique window IDs, zero windows after each close,
exited native processes, and clean Server termination.

## Checkpoint history

The environment created automatic checkpoint commits during the prior
Developer Studio work. They were inspected without staging, committing,
pushing, resetting, cleaning, stashing, rebasing, reverting, amending,
squashing, or rewriting history. The exact hashes and file-level audit remain
in the Developer Studio phase report and repository history.
