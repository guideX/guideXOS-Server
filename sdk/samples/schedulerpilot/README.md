# SECURITY3F Scheduler Pilot

This sample opts into the Native ELF `cooperative-scheduler-v1` execution
model. It opens a bounded window, keeps a volatile local state value across
repeated `gx_app_yield()` calls, polls for Escape or window close, and returns
normally after at most 80 frames. The kernel retains the legacy synchronous
path for packages that omit the execution-model field.

Build with an AMD64 ELF GNU or Clang toolchain using the sample's CMake project.
The compiler must emit the guideXOS Microsoft x64 entry/host-call ABI; GNU
toolchains can use `-mabi=ms`. Do not use a Windows PE-only compiler output as
the `.elf` package executable.
