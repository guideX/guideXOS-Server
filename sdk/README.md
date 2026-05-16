# guideXOS SDK

## Experimental Native ELF execution

Native ELF execution in the hosted runtime is experimental. Normal `build.bat` builds do not enable Native ELF code execution; they can still discover manifests, inspect Native ELF apps, validate ELF metadata, load images, prepare runtime state, and stop at the executor gate. `build-native-experimental.bat` builds `guideXOSServer.experimental.exe` with experimental execution enabled for local trusted validation only.

Only trusted Native ELF apps should be run with the experimental runtime.

Currently supported:

- host amd64
- app amd64
- static `ET_EXEC`
- no `PT_INTERP`
- no dynamic linking
- no relocations
- preferred base must map successfully
- guideXOS C ABI v1 only (`guidexos-c-abi-v1`)

Currently unsupported:

- `ET_DYN`/PIE
- shared libraries
- libc-heavy apps
- cross-architecture execution
- dynamic linker
- arbitrary host filesystem access

The current host call surface is intentionally small and fixed for this experiment: `log`, `get_api_version`, `request_window`, `draw_text`, `draw_rect`, `wait_for_close`, `poll_event`, `file_exists`, and `file_read_all`.

## Build all Native ELF samples

From the repository root:

```powershell
powershell -ExecutionPolicy Bypass -File sdk/build-samples.ps1
```

The script builds and stages the Native ELF SDK samples into the hosted runtime app layout:

- `Apps/HelloWorld/app.json`
- `Apps/HelloWorld/bin/amd64/helloworld.elf`
- `Apps/HelloWorld/resources/message.txt`
- `Apps/ResourceViewer/app.json`
- `Apps/ResourceViewer/bin/amd64/resourceviewer.elf`
- `Apps/ResourceViewer/resources/about.txt`

The script uses direct LLVM `x86_64-unknown-elf` compilation and `ld.lld` linking when available, so it does not require Visual Studio.

## Clean rebuild

```powershell
powershell -ExecutionPolicy Bypass -File sdk/build-samples.ps1 -Clean
```

Useful options:

- `-Clean` removes staged sample app output before rebuilding.
- `-Verbose` prints compiler and linker commands.
- `-SkipBuild` only stages manifests/resources and verifies expected files.
- `-SkipReadElf` skips external ELF header inspection.

## Test in guideXOS hosted runtime

After staging the apps under the hosted `/Apps` location, run `guideXOSServer.exe` and use:

```text
desktop.apps.verbose
nativeapp.capabilities
nativeapp.inspect com.guidexos.helloworld
nativeapp.smoketest com.guidexos.helloworld
nativeapp.inspect com.guidexos.resourceviewer
nativeapp.smoketest com.guidexos.resourceviewer
nativeapp.processes
```

Native ELF execution remains experimental and gated by the guideXOS build configuration. By default, the hosted runtime should discover the manifests, validate the ELF images, load them, prepare runtime state, and stop at the executor gate.

## Known-good validation sequence

From the repository root:

```powershell
powershell -ExecutionPolicy Bypass -File sdk/build-samples.ps1 -Clean
build.bat
build-native-experimental.bat
```

Then in the hosted runtime:

```text
desktop.apps.verbose
nativeapp.capabilities
nativeapp.inspect com.guidexos.helloworld
nativeapp.smoketest com.guidexos.helloworld
nativeapp.inspect com.guidexos.resourceviewer
nativeapp.smoketest com.guidexos.resourceviewer
nativeapp.processes
```
