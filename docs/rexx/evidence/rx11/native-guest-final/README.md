# RX11 final native qualification

## Full build

The final runtime source was built with a forced full AMD64 freestanding rebuild:

```powershell
mingw32-make.exe -B -C kernel ARCH=amd64 BUILD_DIR=build/rexx-rx11-native-final-20261017 all
```

The command returned 0 and linked both PE and ELF images. The full compiler/linker transcript is `../native-build-final.log`.

| Artifact | SHA-256 | Bytes |
| --- | --- | ---: |
| `kernel.elf` | `9C557F4CE875DFEB6F9A7D504D64910CE191F9F5EFCA11321F042EC47CB6F3B8` | 2,808,840 |
| `kernel.pe` | `3043C6A423E62C8797478E5EA817F1A09C0DB11A1BCD18163067E30F4E395F08` | 2,918,475 |
| `BOOTX64.EFI` | `964CB594260193FFB827B4BF94747C4569CC57A7061A36C18B98C5F5FACF927C` | 56,832 |

## Isolated guest

QEMU 11.0.0-12122-ga4bb4b10c9 booted a copied disposable ESP with the final kernel and bootloader. Networking was disabled with `-net none`; the root production ESP and disk images were not used or modified. A process census was saved immediately before launch. Existing foreign QEMU processes were left untouched.

The guest ran `RX11LOGIC.REX`, which covers the AND, OR, and XOR truth tables, unary NOT for 0 and 1, and a SYNTAX trap for an invalid logical operand. It then ran `RX11RECOVER.REX` to verify basic script recovery. The reviewed screenshot shows the final truth-table values (including NOT results `1`, `0`), `RX11_NATIVE_SYNTAX_RECOVERED`, and `RX11_NATIVE_RECOVERY_SCRIPT_OK`.

The authoritative run is in `capture4/`:

- `qemu-invocation.json`, pre/post ownership censuses, and QMP session/shutdown transcript.
- `serial.log` and the two scripts.
- `console-logical-not-recovery.ppm`, the raw QEMU screenshot, plus a lossless PNG preview.
- `guest-status.json`, including QEMU exit status 0, and `sha256.txt` with final image/capture digests.

The PNG was generated from the preserved raw PPM because this QEMU build's HMP `screendump` emits PPM by default. Preliminary capture attempts are retained outside the tracked evidence tree under `tmp/rexx-rx11-native-guest-initial-capture-attempts-20261017/`; `capture4/` is the final reviewed result.
