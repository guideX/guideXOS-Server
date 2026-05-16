# guideXOS SDK

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
nativeapp.inspect com.guidexos.helloworld
nativeapp.smoketest com.guidexos.helloworld
nativeapp.inspect com.guidexos.resourceviewer
nativeapp.smoketest com.guidexos.resourceviewer
```

Native ELF execution remains experimental and gated by the guideXOS build configuration. By default, the hosted runtime should discover the manifests, validate the ELF images, load them, prepare runtime state, and stop at the executor gate.
