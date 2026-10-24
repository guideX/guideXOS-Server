# RX32 Native Guest Attempts

Two QAC attempts stopped before a complete guest qualification. Their isolated build directories remain intact under `build/rexx/rx32/`; disposable ESP media and raw screenshots are not part of the committed evidence.

| Run ID | Result | Preserved finding |
|---|---|---|
| `e1a98cda178b4f2ebc6f9b1922e04c1c` | Stopped before REXX scripts | The console readiness probe was visibly echoed in the QMP screenshot, but the desktop terminal did not mirror that text to the serial log. The runner's serial-only readiness check was removed; the successful retry uses a human-reviewed screenshot marker. |
| `b8eec10851144f11a27d94dab1d93a6d` | Runner stopped after guest commands | The first command unnecessarily sent the terminal-open hotkey after readiness had already opened the terminal. It inserted a stray leading character, so the VALUE demo did not open. The other six commands ran; the missing VFS access was caught by the runner, which shut down its owned QEMU and released QAC. The RX32 command no longer reopens the terminal. |

The completed run is `run-6a14f833d6d74a159b1acb64ff8273d0`; it opened all seven scripts, passed the guest artifact guard, shut down through QMP, and released its QAC reservation.
