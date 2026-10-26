# TOOLS1 Developer Utilities

## Result

**Outcome C — Architectural blocker.** The Tools checkout contains the
Developer Studio application package and its prebuilt Native ELF files, but
not the editable Developer Studio application sources. The source checkout is
a separate repository on `main`, while this task is constrained to the
`DEVELOPER_STUDIO_TOOLS` branch. The missing Tools menu/registration and
Find-in-Files error reporting require changes to the application UI and search
service. Making those changes here would require copying or otherwise changing
code outside the assigned checkout, so TOOLS1 is not marked verified.

The audit also found that a substantial Find in Files implementation is
already present in the shipped AMD64 ELF and in the separate source checkout.
It was not recreated.

## Repository and ownership audit

At the start of this work, `D:\dev\guideXOS_DeveloperStudio_Tools` was on
`DEVELOPER_STUDIO_TOOLS` at `e24ee589a0f13132d42487e1b04532a6e6d3cde3`,
clean and even with `origin/DEVELOPER_STUDIO_TOOLS` (0 commits ahead and 0
behind). No TOOLS, Compilation, or Debugging phase marker was changed.

The editable app source was found at
`D:\dev\guideXOS_Developer_Studio`, on clean `main` at
`058c83fe1ae270a103eadc7caca961328e8abe2a`. That checkout owns `src/`,
`tests/`, and `build.ps1`; the build script compiles the app sources and stages
the output into a Server checkout's `Apps/DeveloperStudio` package. It is not
the requested Tools branch, and it was left unchanged.

| Area | Existing owner and interfaces | TOOLS1 relevance |
| --- | --- | --- |
| Developer Studio UI and app lifecycle | Developer Studio `src/main.cpp`; Native ELF window, event, draw, and present calls from `sdk/include/guidexos/abi.h` | The app uses static panel state and event handlers. There is no general tool registry or Tools menu. |
| Active project and documents | `WorkspaceController` and `WorkspaceModel` in the Developer Studio source repository | Search receives the active project's ID, generation, and root and snapshots dirty documents. |
| Project files | `WorkspaceFileSystem` and the Server Native ABI's `file_stat`, `file_list`, and `file_read_workspace` operations | Enumeration and reads are mediated by the host filesystem service; search does not write files. |
| Editor navigation | `WorkspaceControllerOpenDocumentAtLocation` in the Developer Studio source repository | Find-in-Files result activation opens the file and moves to its line and column. |
| Tool result presentation | The existing search panel in `src/main.cpp`; `OutputService` for operation messages | The panel draws result groups, line/column, preview, status, and empty state. There is no reusable registered-tool result surface. |
| Build and Debug integration | Separate Build/Run and Debug controllers in the Developer Studio source repository | No changes were made to either controller or their UI. Their model tests passed in the hosted test tier below. |
| Package integration | `Apps/DeveloperStudio/app.json` and `Apps/DeveloperStudio/bin/{amd64,arm64}/developerstudio.elf` in this Tools checkout | The package is tracked here, but rebuilding it requires the separate app source checkout. |

The Server checkout's Native ABI is append-only and already supplies the
filesystem calls needed by the existing search service. The source app
contains no dynamic plugin loader, and none is needed for the bounded utility
requested here.

## Existing Find in Files behavior

The current app source implements `ProjectSearchService` in
`src/developer_studio_project_search.{h,cpp}`. Its operation lifecycle is
`ProjectSearchServiceInit`, `ProjectSearchStart`, repeated bounded
`ProjectSearchPoll` calls, optional `ProjectSearchCancel`, and
`ProjectSearchRelease`. The UI polls a bounded amount of work per frame and
cancels when the active project generation changes. A new operation replaces
the previous operation in the fixed-capacity service; there is no per-search
heap resource to retain.

The UI supports a query, include and exclude patterns, case sensitivity,
whole-word matching, a result list grouped by file, line and column, bounded
previews, a match count, truncation status, and a no-match message. Selecting
or double-clicking a result opens it through the existing editor navigation
path. Dirty open documents are searched from bounded snapshots. The feature is
read-only.

The discoverable entry point requested by TOOLS1 is missing: the current UI
opens Find in Files with `Ctrl+Shift+F`, but its visible menu bar contains
File, Debug, and Build and has no Tools entry. The service is registered
directly as application globals and event handlers; it is not an independently
registered utility with a shared initialize/activate/refresh/close interface.

The service uses fixed-capacity storage and these bounds:

| Limit | Hosted build | Bare-metal build |
| --- | ---: | ---: |
| Query | `kFindMaxQueryBytes` | `kFindMaxQueryBytes` |
| File bytes | `kMaxEditorBytes` (256 KiB in the current hosted validation contract) | `kMaxEditorBytes` |
| Total bytes scanned | 256 MiB | 256 MiB |
| Files enumerated | 100,000 | 256 |
| Directories visited / pending | 4,096 / 512 | 128 / 32 |
| Maximum traversal depth | 64 | 64 |
| Result files / total matches / matches per file | 256 / 4,096 / 512 | 32 / 256 / 32 |
| Preview | 512 bytes | 512 bytes |
| Work per poll | 16 entries, at most one file read | 16 entries, at most one file read |
| Operation duration | 5 minutes | 5 minutes |

The UI initializes source-oriented include patterns and excludes common build,
vendor, and generated directories by default. Paths are normalized and checked
against the project root; traversal components are rejected. Binary data is
not matched. The synchronous read of one bounded file per poll is the current
responsiveness limit; no kernel thread is created.

### Remaining TOOLS1 gaps

- No visible Tools menu or explicit bounded utility registry/lifecycle exists.
- Unreadable files, binary files, and files over the per-file limit can be
  skipped without a useful per-file result. Error enum values exist, but the
  current UI does not present a complete skipped-file report.
- The existing `project_search_test.cpp` covers basic matches, include/exclude
  patterns, case behavior, preview positions, dirty documents, cancellation,
  repeated/stale operation IDs, binary skipping, and an invalid pattern. It
  does not cover all requested overlong-input, oversized-line, result-cap,
  enumeration-cap, missing/unreadable-file, changed-file, containment, and
  navigation cases.
- No QAC-managed guest acceptance was run. `qac.exe` was not on `PATH` and no
  built QAC CLI apphost was found in the QAC checkout. No QEMU process was
  started outside QAC.
- The guest UX and navigation have not been qualified in this TOOLS1 run.

## Hosted validation

The existing Developer Studio fast validation tier was run against the
unchanged source checkout with package building and the live hosted debugger
smoke skipped:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File `
  D:\dev\guideXOS_Developer_Studio\tests\run-developer-studio-validation-fast.ps1 `
  -ServerRoot D:\dev\guideXOS_DeveloperStudio_Tools `
  -BuildDirectory D:\dev\guideXOS_DeveloperStudio_Tools\out\validation\tools1-studio-fast `
  -TraceDirectory D:\dev\guideXOS_DeveloperStudio_Tools\out\validation\tools1-traces `
  -SkipPackage -SkipHosted
```

Result: CMake configure/build passed and CTest passed **34/34 tests, 0 failed**.
The passing cases included project search, project/workspace, Build, Run,
editor navigation, and debugger models. Manifest resolution also passed.
Package rebuilding, live hosted application smoke, and guest qualification
were not run. The AMD64 package in this checkout contains Find-in-Files UI and
project-search strings; that artifact inspection is not guest qualification.

## TOOLS2 recommendation

First place the editable Developer Studio UI and utility sources under an
explicit Tools-track branch or otherwise establish that the Tools track owns
the app-source changes. Then add a small static utility descriptor/registry,
expose Find in Files through a visible Tools entry, and reuse the existing
`ProjectSearchService` behind it. Extend focused tests for the missing limits,
error reporting, filesystem changes, containment, and selection/navigation
cases before packaging and QAC qualification. Keep Build and Debug controllers
as consumers of the shared project/editor interfaces rather than folding them
into the Tools registry.
