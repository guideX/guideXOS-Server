# S19R focused validation

## Native runtime regression

Command:

```powershell
scripts/run-settings-native-runtime-test.ps1 -OutputDirectory out\validation\settings-s19r-native-ui-20261017\tests
```

Result: **1,417/1,417 passed.** The three added assertions cover null appearance IDs, gradient and wallpaper applied-marker matching, and cross-category marker behavior.

The historical S19 native runtime result was 1,414/1,414. Other test counts in the acceptance report are historical; the full matrix was not rerun for this bounded legacy marker repair.

## AMD64 production build

Command:

```powershell
mingw32-make -C kernel ARCH=amd64 BUILD_DIR='D:/dev/guideXOSServer/out/validation/settings-s19r-native-ui-20261017/kernel-build' -j2
```

Result: completed successfully. The resulting `kernel.elf` is 2,760,592 bytes with SHA-256 `1D2C119CB947B170B0529D9444F7B8F8BF7FE0904E6EC42CE4916165257FAE3F`. The same bytes were staged in the private rebuilt guest image and verified by FAT readback before boot.
