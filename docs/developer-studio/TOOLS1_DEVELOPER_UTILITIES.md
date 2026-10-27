# TOOLS1R Developer Utilities

## Result

**Outcome B — `TOOLS1R_HOSTED_READY`.** The Tools source checkout is now
independent, and the existing Find in Files service is exposed from a visible
Tools menu and its existing `Ctrl+Shift+F` shortcut. Hosted model regressions
and both architecture staging builds pass. Live hosted UI interaction and QAC
guest acceptance remain pending because the required Server executable and
QAC CLI are unavailable in this environment. The production Server package was
not modified.

## Repository ownership

| Repository | Role | Starting state | TOOLS1R state |
| --- | --- | --- | --- |
| `D:\dev\guideXOS_DeveloperStudio_Tools` | Server integration and package owner | `DEVELOPER_STUDIO_TOOLS`, `3c28a1c013fac2b02dc7090406644a5dc154a2ea`; existing audit commit was one commit ahead of `origin/DEVELOPER_STUDIO_TOOLS` | Same branch; only this audit document is changed here before its local commit. No ELF was replaced. |
| `D:\dev\guideXOS_Developer_Studio` | Editable application source repository, original checkout | `main`, `058c83fe1ae270a103eadc7caca961328e8abe2a`; clean | Unchanged. |
| `D:\dev\guideXOS_DeveloperStudio_Tools_Source` | Isolated editable Tools source checkout | Created at verified source baseline `058c83fe1ae270a103eadc7caca961328e8abe2a` | Branch `DEVELOPER_STUDIO_TOOLS`; implementation is committed locally as `f7438f5455b0889dd730380f17ff3651753ac196`. |
| `D:\dev\guideXOS_Developer_Studio_Debugging` | Independent Debugging source worktree | At final audit: `DEVELOPER_STUDIO_DEBUGGING`, `d09e8e1b8ed40fd7a1277ebe835acf56344241d1`; clean | Unchanged. |

The Tools source worktree is a separate Git worktree of the Developer Studio
source repository, not a copy inside the Server repository. The source remote
is `git@github.com:guideX/guideXOS_Developer_Studio.git`; the Server remote is
`git@github.com:guideX/guideXOS-Server.git`. At worktree creation the source
`main` and Debugging worktree were both observed at `058c83f`; the Debugging
branch later advanced independently to `d09e8e1`. No Compilation worktree was
registered in this source repository's worktree list during this audit. No
other track's files, checkout state, or phase markers were changed.

## Implementation

- Added `src/developer_studio_tools.{h,cpp}` with a four-entry fixed-capacity
  `DeveloperToolRegistry`. It stores explicit command descriptors and
  activate/close callbacks; it is not a dynamic plugin loader.
- Added a visible **Tools** menu with the registered **Find in Files** command.
  The Tools menu and `Ctrl+Shift+F` call the same activation path. Repeated
  activation preserves the current query and operation. Closing the panel
  cancels and releases terminal search state; Escape during a running search
  requests cancellation before closing.
- Reused the existing `ProjectSearchService`, result-selection logic, and
  `WorkspaceControllerOpenDocumentAtLocation` navigation path. No separate
  search implementation or Server ABI change was introduced.
- Added fixed, saturating aggregate counters for skipped large, binary,
  unreadable, and changed/disappeared files. The panel shows one compact
  summary, plus a bounded truncation or failure reason. Directory-list
  truncation now has its own `PROJECT_SEARCH_DIRECTORY_ENTRIES_LIMIT` code;
  result-capacity truncation preserves the existing bounded partial results.
  Search limits and read-only behavior remain unchanged.
- Added `tests/tools_test.cpp` for registry registration, descriptor and
  shortcut, repeat activation, close veto and cleanup, and activation errors.
  Expanded `tests/project_search_test.cpp` for navigation, skipped-file
  diagnostics, and result capacity. Updated `docs/FIND_IN_FILES.md`.
- Updated `build.ps1` to compile/link the registry in both package targets and
  to run the new test. The script's explicit Run/debugger test source lists
  now include the existing Build, Symbols, and Workspace dependencies needed
  by those tests.

The hosted registry test exercises the shared descriptor activation and
lifecycle behavior. The Tools menu and shortcut route through that same
activation helper in `main.cpp`; a live UI click/keyboard run was not possible
without the hosted Server executable described under qualification.

## Validation and package integration

Results from the isolated source checkout:

- Focused CTest: **2/2 passed** (`developer_studio_tools_test`,
  `developer_studio_project_search_test`).
- Full Developer Studio CTest suite: **35/35 passed**, including Build, Run,
  workspace/navigation, and debugger model tests.
- Full CMake build: **68/68 targets passed**.
- `build.ps1` AMD64 package build: passed. Final scratch ELF SHA256:
  `33217C69B1828E3B54C584A474E939BF49238C7A242E0CBFF49C4877D496A885`.
- `build.ps1` ARM64 package build: passed. Final scratch ELF SHA256:
  `D2FE87A625FB16093597A96BA6A31E04475EA1BF444D9262CDCC7ADE47A769D1`.

Both package builds used the Server SDK include directory but set `ServerRoot`
to `D:\dev\guideXOS_DeveloperStudio_Tools_Source\out\tools1r-package`.
The generated manifest, temporary objects, and ELF files are confined to
that ignored scratch directory. The production files
`D:\dev\guideXOS_DeveloperStudio_Tools\Apps\DeveloperStudio\bin\amd64\developerstudio.elf`
and `...\arm64\developerstudio.elf` were left unchanged. No source files were
copied into the Server repository.

## Qualification limits

- No `guideXOSServer.experimental.exe` was present under the Server checkout,
  so the hosted application UI and real menu/shortcut interaction could not be
  launched here.
- `qac.exe` and a QAC CLI apphost were unavailable. No QEMU process was active
  or started, so guest qualification remains **PENDING**.
- Successful CTest and package builds are hosted/model and compile/link
  evidence only; they do not constitute guest acceptance.

## Local commits

- Developer Studio source branch `DEVELOPER_STUDIO_TOOLS`:
  `f7438f5455b0889dd730380f17ff3651753ac196`
  (`TOOLS1R: register Find in Files utility`).
- Server Tools branch `DEVELOPER_STUDIO_TOOLS`:
  this audit update is committed locally after the source commit; its exact
  commit is the final Server Tools branch `HEAD`.

Both commits are local only. Nothing was pushed, merged, rebased, reset, or
stashed. TOOLS2 was not started.
