# RX12 native guest run

The final RX12 kernel was staged into a copied disposable ESP and booted with QEMU 11.0.0-12122-ga4bb4b10c9. QEMU used `-net none`, serial output at `serial-rx12-final.log`, and a private QMP port. The build hash in `qemu-invocation-final.json` matches the staged kernel. The production ESP and disk images were not used.

| Guest command | Observed result | Shell status |
| --- | --- | ---: |
| `rexx /rx12-string-builtins.rex` | String built-ins, nested `INTERPRET`, and completion marker | 0 |
| `rexx /rx12-arg.rex` | Top-level, omitted/empty, routine, and function `ARG()` values | 0 |
| `rexx /rx12-syntax-recovery.rex` | `SYNTAX`, `RC: 40`, `SIGL: 3`, detail, then recovery marker | 0 |
| `rexx /rx12-oversized-output.rex` | 16 MiB result guard reports `VM resource limit exceeded` | 1 |
| `rexx /rx12-recovery.rex` | Subsequent VM command prints `RX12_NATIVE_GUEST_RESPONSIVE: 9` | 0 |
| `rexx /rx12-exit-status.rex` | Script marker, followed by `echo $?` reporting the requested exit | 23 |

The QMP screenshots are the `rx12-*.png` files in this directory. `hosted-script-precheck.log` records the same scripts and expected status values before launch. `qemu-shutdown.json` records the explicit QMP `quit` acknowledgement for RX12 PID 24768; the before/after censuses redact all foreign process details and show that foreign QEMU processes were not touched.

The serial log records the bootloader/kernel handoff, the mounted VFS opening the staged REXX scripts, and continuing kernel heartbeat output through the final guest session. The Console accepted each next command after the trapped language error and terminal resource rejection.
