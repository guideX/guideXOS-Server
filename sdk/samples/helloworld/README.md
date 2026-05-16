# guideXOS Native ELF Hello World

This is the first minimal guideXOS Native App SDK v0 smoke-test application.

The sample builds a Native ELF app with the guideXOS C ABI entry point:

```cpp
extern "C" gx_result gx_main(gx_app_context* ctx);
```

## What this tests

- The app manifest can be discovered by the guideXOS app registry.
- `AppLaunchResolver` can select the `amd64` Native ELF entry.
- The Native ELF launch pipeline can locate and read the ELF file.
- The ELF validator can validate the ELF header and architecture.

## What this does not do yet

- The ELF is not executed.
- guideXOS does not jump to `gx_main` yet.
- Dynamic linking is not implemented.
- Host calls/syscalls are placeholders only.

A successful launch attempt should report that the Native ELF was validated, but execution is not implemented yet.

## Build

From this directory:

```sh
cmake -S . -B build -DGUIDEXOS_SAMPLE_ARCH=amd64
cmake --build build
```

The build writes:

- `Apps/HelloWorld/bin/amd64/helloworld.elf`
- `Apps/HelloWorld/app.json`

The output layout is intended to match guideXOS external app discovery.
