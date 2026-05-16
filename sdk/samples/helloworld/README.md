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

## Tiny SDK UI helper

This sample uses `guidexos/ui.h` for a panel, label, and fake button.

`ui.h` is not an OS widget toolkit. It is only a tiny header-only helper layer over existing Native ELF host calls such as `draw_rect`, `draw_text`, and `poll_event`.

Apps still own their event handling and UI state. For example, this sample checks mouse coordinates, tracks click state, redraws on paint, exits on close, and exits on Escape. OS-side widgets may come later.

## Current runtime scope

- Dynamic linking is not implemented.
- The helper layer does not allocate memory and does not create OS-managed controls.
- Experimental Native ELF execution remains gated by the guideXOS build configuration.

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
