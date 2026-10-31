# Native Application — C Subset

This small project is a NativeElf application for the production guideXOS
bare-metal Build service. It creates a 320 by 180 NativeElf window, displays
**Hello World**, and returns the window-run result when the user closes it.

The project uses the bounded C-like language supported by the guideXOS
compiler. The `.c` source, quoted local declaration file, `gx_main` entry
point, AMD64 target, ABI, and output path match the existing compiler Build
service contract. `guidexos_app.h` declares the four compiler-recognized
`gx_window_*` calls; it is not a general C or C++ SDK header.

The source does not use a standard C runtime, C++ features, preprocessing
other than its local `#include`, or the COMP4O bytes library. The project
descriptor and manifest match the production bare-metal compiler Build
service. The Developer Studio UI is maintained in a separate repository: it
does not offer this template yet and its project-open action still opens
`src/main.cpp` unconditionally. The UI needs the COMP4P integration contract
before this `.c` project is a complete user-facing workflow.

The Build service writes the executable to
`build/bin/amd64/hello-world.elf`. Its project request uses target
`guidexos.amd64.baremetal.bootstrap.native` and build system
`guidexos-native-baremetal-bootstrap-v1`. This sample requires the actual
guideXOS guest runtime to show a window; hosted compilation alone does not
prove visible output.
