# S16 AMD64 Production Build

Command:

```text
mingw32-make -C kernel ARCH=amd64 BUILD_DIR=build/s16-clean-final-verified-20261016
```

The build directory was new and isolated from prior phase outputs.

| Artifact | Size | SHA-256 |
| --- | ---: | --- |
| `kernel/build/s16-clean-final-verified-20261016/bin/kernel.elf` | 2,724,704 bytes | `AE14A4645BE1E5D5C7A021DB39F67C7860937E80B9BFB9033847752EB83EAEB1` |
| `kernel/build/s16-clean-final-verified-20261016/bin/kernel.pe` | 2,838,667 bytes | `7F2B53159FCFF832A5DAC77323DE4B4FA2E80CFC37CE1173952A41FBEFBFBFEA` |

The build compiled 156 units, reported 0 errors and 233 warning lines, and linked both production formats. Warning diagnostics were compared with the accepted S15 build: there are no new diagnostic messages after removing one unused local found during the first S16 build. The production image contains definitions for the new gradient apply entry point and appearance notification callbacks. `nm -u` shows the new callback reference as well as its matching definition; the only unmatched symbol names are `__dso_handle` and `mainCRTStartup`, both present in the S15 baseline.

The complete compiler/linker log is committed at `build/amd64-production-build.log` and also retained at `out/validation/settings-s16-amd64-final-verified-build-20261016.log`. The freestanding Settings core validation output is retained at `out/validation/settings-s16-freestanding-20261016/`.
