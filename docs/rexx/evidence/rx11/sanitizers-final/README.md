# RX11 final sanitizer qualification

The final combined command completed successfully:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests/run_rexx_sanitizers.ps1 -Sanitizer Both
```

The `address-both/` run used MSVC x64 AddressSanitizer (MSVC 19.51.36260). The `undefined-both/` run used Clang/LLVM `clang-cl` 22.1.8 with the Visual Studio x64 UBSan runtime. Each run built and passed 13 hosted C executables and ran all 78 guideXOS corpus cases with their expected exit statuses. Regina was unavailable, so the independent side is `NOT_RUN` for all 78 cases. Neither final sanitizer run reported a finding.

`both-run.log` is the console transcript; `summary.json` records the compiler and toolchain versions. Each sanitizer folder has per-executable build/run logs and the corpus report with raw output. The earlier pre-fix ASan use-after-free is preserved separately at `../sanitizers/asan-initial-rx10-uaf.stderr.log`.

The ordinary hosted runner also exercises the C++ shell bridge and freestanding numeric adapter, which are outside this sanitizer matrix. No sanitizer dependency or flag is used by the kernel build.
