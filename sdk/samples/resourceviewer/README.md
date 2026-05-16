# guideXOS Native ELF Resource Viewer

This is the second guideXOS Native ELF sample app. It exists to prove the Native ELF app model supports more than the first HelloWorld sample.

The app uses the guideXOS C ABI entry point:

```cpp
extern "C" gx_result gx_main(gx_app_context* ctx);
```

## What this tests

- Manifest discovery for a second Native ELF app: `com.guidexos.resourceviewer`.
- `file.read` resource access through `file_exists` and `file_read_all`.
- Drawing with the existing `draw_rect` and `draw_text` host calls through `guidexos/ui.h` helpers.
- Paint handling by redrawing the window contents.
- Close handling by exiting when the window closes.
- Keyboard handling by exiting when Escape is pressed.

No new host calls or App Model schema changes are required.

## Current runtime scope

Native ELF execution remains experimental and gated by the guideXOS build configuration. By default, the hosted runtime can discover the manifest, validate the ELF, load the image, and prepare the runtime, then stops at the executor gate without running app code.

## Build

From this directory:

```sh
cmake -S . -B build -DGUIDEXOS_SAMPLE_ARCH=amd64
cmake --build build
```

The build writes:

- `Apps/ResourceViewer/bin/amd64/resourceviewer.elf`
- `Apps/ResourceViewer/app.json`
- `Apps/ResourceViewer/resources/about.txt`

The output layout is intended to match guideXOS external app discovery.
