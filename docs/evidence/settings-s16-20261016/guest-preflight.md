# S16 Guest Preflight and QEMU Gate

The isolated guest was prepared at `out/validation/settings-s16-guest-20261016/guest-acceptance/` using a private writable ESP copy and copied OVMF firmware. It was **not booted** because a separate project continued to own an active QEMU instance throughout the permitted passive recheck window.

| Prepared input | SHA-256 |
| --- | --- |
| Final S16 kernel ELF copied to the private ESP | `AE14A4645BE1E5D5C7A021DB39F67C7860937E80B9BFB9033847752EB83EAEB1` |
| Initial private ESP `desktop.cfg` (Sunset) | `ED2CED9BD60AC15E67663BD955CBFC9DAAD36A47D368AB9DD3C2EF1CCA074A1D` |
| Copied OVMF firmware | `2DF617A6FD1BAE41EC7C0E3BCECA96C928A5A88D8DC198468D34733393AB2792` |
| Copied EFI bootloader | `E2FDB6AD78E0416AF250BEBBC6541640E3CCDC7DD9A14453C08143B7F4DA48AA` |

The configuration above is preserved as [`prepared-sunset-desktop.cfg`](guest-preflight/prepared-sunset-desktop.cfg), with its [`build-identity.txt`](guest-preflight/prepared-build-identity.txt). These are preflight inputs only; they are not post-apply evidence.

Passive process inventory found PID `10488`, created at 16:21:08 local time, command line rooted in `D:\dev\guideXOSServer_DiskManagerImprovements`, using that repository's ESP and QMP `127.0.0.1:61440`. Rechecks were made at approximately 16:23, 16:25, 16:27, 16:29, and 16:31 local time. PID `10488` remained active more than ten minutes after creation. No connection was made to its QMP endpoint, and the process and foreign repository were left untouched.

Guest acceptance stopped at the documented gate. No S16 guest screenshots, serial log, QMP transcript, post-apply configuration copy, or reboot persistence evidence were produced. The prepared ESP remains under `out/validation/`; it is not an acceptance result.
