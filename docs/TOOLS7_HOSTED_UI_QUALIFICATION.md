# TOOLS7 Hosted UI Qualification

**Outcome: A — `TOOLS7_HOSTED_UI_VERIFIED` for the hosted desktop runtime.**

The Task List was exercised in the real hosted Server UI through its compositor input path, application dispatch, scan lifecycle, and editor navigation. This qualification does not cover native guest execution or pixel-by-pixel visual review.

## Repository and executable provenance

| Item | Qualification record |
| --- | --- |
| Developer Studio source branch | `DEVELOPER_STUDIO_TOOLS` |
| Source HEAD before TOOLS7 | `4e6425249b304b5a5b3dfaf1598ab35e659e2491` |
| Source commit | `508a8204f6fe99147c27b89e66fa0ff8eebd244e` (`TOOLS7 qualify hosted Task List UI`) |
| Server branch | `DEVELOPER_STUDIO_TOOLS` |
| Server source HEAD used to build the hosted runtime | `a5613cdcdb9e4b954bef53264201beaaa21de1cc` |
| Hosted Server executable | `D:\dev\guideXOS_DeveloperStudio_Tools\guideXOSServer.experimental.exe` |
| Hosted Server SHA256 | `95905244D2685EA17F64E405591590C471CDF76B63804400FFEC81089C84FADC` |

The Server executable was built from the existing authorized source with `build-native-experimental.bat`. No Server runtime, storage, or filesystem source was changed. The Developer Studio packages were staged under `out\validation\tools7-hosted-ui`; production Server ELF paths were not used.

## Qualification levels

| Level | Result | Evidence and limit |
| --- | --- | --- |
| Unit and model tests | Passed | Existing Task List, Tools registry, navigation, replace preview, build/run, debugger, and other CTest coverage. |
| Hosted UI dispatch and lifecycle | Passed | One new opt-in CTest launches the actual Server executable and the packaged AMD64 app, then sends mouse and keyboard events to the compositor-owned app window. |
| Live hosted application | Passed | The native app was loaded by `NativeElfExecutor`, created window 1000, rendered through the host draw APIs, received compositor events, and shut down cleanly. This is hosted Windows execution, not guest execution. |
| Screenshot/pixel review | Not captured | This run retained Server/compositor logs and draw calls. It did not save a screenshot, so pixel-level layout approval is not claimed. |
| Native guest interaction | Not run | QEMU was not started. `qac.exe` was not available on `PATH`; no QAC configuration or workaround was used. |

## Hosted UI execution

The harness is `tests/smoke-developer-studio-task-list.ps1`, registered by CMake as `developer_studio_task_list_hosted_ui_test` when `GUIDEXOS_ENABLE_TASK_LIST_HOSTED_UI_TEST=ON`.

The run used the actual `guideXOSServer.experimental.exe`, an isolated copy of the staged AMD64 package, and a disposable Server runtime directory. It issued `gui.start`, `desktop.launch com.guidexos.developerstudio`, and `desktop.windows.owners`; the owner query returned the real Developer Studio window ID. It then sent `gui.mouse` events and `gui.keyto` events to that ID. The Open Project dialog was opened with its keyboard command and the fixture path was typed as key events, so project activation passed through the application's UI.

The test verified:

- Developer Studio launch, initial render, Tools menu activation, and Task List panel visibility.
- A scan without an active project is rejected coherently.
- An annotation-free project containing both an ordinary source file and an empty source file completes with zero tasks.
- A 129-annotation input reaches the packaged app's existing 32-task bound, reports `truncated=true`, and renders `Scan truncated at a resource limit`.
- A project fixture with ordinary TODO/FIXME comments, block comments, multiple annotations, strings/raw strings that must not match, 18 ordinary source files, and a trailing unterminated block comment completes with `todo=3 fixme=2 tasks=5 files=22 truncated=false`.
- Scanning and opening a selected task leave the fixture bytes unchanged. After the source changes following the scan, activation opens `src/00_tasks.cpp` at line 3 and reports caret offset 34.
- Repeated Tools-menu activation and rescanning preserve the panel and result state.
- A selected result for a deleted file fails safely; closing the workspace clears selection so a stale result is rejected.
- A visible Cancel-button click cancels the active scan. Closing Developer Studio while a scan is active follows the clean application close path.
- Find in Files (`Ctrl+Shift+F`), Go to File (`Ctrl+P`), Go to Line (`Ctrl+G`), and Replace in Files preview (`Ctrl+Shift+H`) open through real keyboard dispatch. Replace Apply remains disabled with the existing atomic-replace/durable-sync explanation.

The Task List panel previously consumed toolbar/menu clicks while open. TOOLS7 lets top-level menu clicks pass through, preserving normal Tools-menu activation while the panel is visible. Lifecycle markers now record scan terminal states, panel visibility, and selected-task navigation details for repeatable acceptance evidence. The hosted entry wrapper also probes the bounded Windows worker stack in page-sized steps before entering the app's larger main frame; this allowed the real hosted app to reach initial render without changing Server code.

The final hosted execution log is at:

`D:\dev\guideXOS_DeveloperStudio_Tools_Source\out\tools7-final\tools7-hosted-ui-evidence\task-list-hosted-ui.log`

It records `result=PASS`, the exact queued UI commands, application lifecycle markers, the completion/cancellation/navigation states above, and host counters including 66,684 draw-text calls, 20,671 draw-rectangle calls, six paint events, and 60 mouse events. The log is generated in the build output and is not a checked-in fixture.

## Regression and build results

- One new CTest was registered and executed: `developer_studio_task_list_hosted_ui_test`.
- The 38 existing CTests plus the new integration test passed: **39/39** in the final default CMake configuration.
- A clean CMake rebuild completed all **111/111** build steps.
- The package build script completed for AMD64 and ARM64, and `tests/validate-developer-studio-package.ps1` passed the manifest/content audit and ELF checks.

The default full CTest command was:

```powershell
ctest --test-dir out/tools7-final --output-on-failure
```

An additional full CTest run using `CMAKE_BUILD_TYPE=Release` completed the hosted UI test but had three configuration-sensitive failures: `developer_studio_file_workflow_test` and `developer_studio_project_search_test` segfaulted, and `developer_studio_debugger_conditional_breakpoints_test` failed its `DebugTargetFromBuild` assertion. All three passed in the final default configuration. Their cause was not isolated, so the Release CTest configuration is not claimed clean.

## Package audit

Package root: `D:\dev\guideXOS_DeveloperStudio_Tools_Source\out\validation\tools7-hosted-ui\Apps\DeveloperStudio`

| Architecture | File | Size (bytes) | ELF | Entry point | SHA256 |
| --- | --- | ---: | --- | --- | --- |
| AMD64 | `bin/amd64/developerstudio.elf` | 1,217,172 | ELF64, little-endian, ET_EXEC; no section metadata/debug sections | `0x500cda10` | `265BA3F5E16470674CFD36CC499E5BC1D7FD12EB00299CCDBFC935E585DF97F9` |
| ARM64 | `bin/arm64/developerstudio.elf` | 1,396,548 | ELF64, little-endian, ET_EXEC; no section metadata/debug sections | `0x500ef170` | `42E98813E65C5060A6FDCDED9B1F179F8F2AA755009E21D520D2E66DE521D0B7` |

The package audit found exactly `app.json` and the two architecture ELFs, with manifest entries matching their paths and architectures.

## Repeat procedure

From the Server checkout root, build the hosted Server executable:

```powershell
cmd /c .\build-native-experimental.bat
```

Then, from the Developer Studio source root, build and stage each architecture into the TOOLS7-specific package root:

```powershell
.\build.ps1 -ServerRoot D:\dev\guideXOS_DeveloperStudio_Tools -StagingRoot D:\dev\guideXOS_DeveloperStudio_Tools_Source\out\validation\tools7-hosted-ui -BuildRoot D:\dev\guideXOS_DeveloperStudio_Tools_Source\out\tools7-package-amd64 -TargetArchitecture amd64
.\build.ps1 -ServerRoot D:\dev\guideXOS_DeveloperStudio_Tools -StagingRoot D:\dev\guideXOS_DeveloperStudio_Tools_Source\out\validation\tools7-hosted-ui -BuildRoot D:\dev\guideXOS_DeveloperStudio_Tools_Source\out\tools7-package-arm64 -TargetArchitecture arm64
```

Configure the hosted UI test, clean-build, and run all tests:

```powershell
cmake -S . -B out/tools7-final -G Ninja -DCMAKE_BUILD_TYPE= -DGUIDEXOS_ENABLE_TASK_LIST_HOSTED_UI_TEST=ON -DGUIDEXOS_TASK_LIST_UI_SERVER_ROOT=D:/dev/guideXOS_DeveloperStudio_Tools -DGUIDEXOS_TASK_LIST_UI_APP_PACKAGE=D:/dev/guideXOS_DeveloperStudio_Tools_Source/out/validation/tools7-hosted-ui/Apps/DeveloperStudio
cmake --build out/tools7-final --clean-first
ctest --test-dir out/tools7-final --output-on-failure
```

Run the package audit:

```powershell
pwsh -NoProfile -File .\tests\validate-developer-studio-package.ps1 -ServerRoot D:\dev\guideXOS_DeveloperStudio_Tools -PackageRoot D:\dev\guideXOS_DeveloperStudio_Tools_Source\out\validation\tools7-hosted-ui\Apps\DeveloperStudio
```

## Remaining acceptance work and TOOLS8 recommendation

Guest acceptance remains a separate qualification. When the .NET Support guest priority permits and QAC/guest infrastructure is available, a coordinated pass should install and launch these packages in the guest, repeat the Task List open/scan/navigation/close path, and capture guest-side results. TOOLS7 did not start QEMU, modify QAC, change storage safety, or add another Developer Studio utility. No TOOLS8 work was started.
