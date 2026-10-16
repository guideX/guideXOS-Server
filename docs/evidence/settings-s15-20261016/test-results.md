# S15 Test Results

Date: 2026-10-16
Starting checkpoint: `6b89e42ef9a188da4c55fc0a5e80863e32ad79a4` (`main`)

| Validation | Result |
| --- | --- |
| Native Settings runtime/provider harness | 842/842 passed; 100 lifecycle repetitions, 250 provider refresh cycles, and 1,000 notification/state transitions |
| Freestanding Settings presentation validation | Passed compile, bounded link, and unresolved-symbol checks |
| Clean AMD64 production kernel build | Passed; 0 compiler/linker errors, 233 warning lines |
| S15 production QEMU gradient guest | Passed Start → Settings → Personalization; snapshot showed gradient palette and all supported system icon visibility values |
| S15 production QEMU image-wallpaper guest | Passed Start → Settings → `P` Personalization; snapshot showed the selected built-in wallpaper, scale mode, and system icon visibility values |
| Read-only persistence check | Passed; each guest's `/desktop.cfg` SHA-256 matched its pre-Settings copy |

The test runner output is preserved in `native-runtime.log` and `freestanding.log`. The complete production build log and both isolated QEMU serial/QMP logs are in this evidence directory.
