# QAC ownership timeline

All S22R10 guests used QAC CLI mode `acceptance` for project `guideXOSServer`. Each reservation was exclusive and QEMU was its managed child.

| Event | Reservation | QEMU PID | QMP | QAC result |
|---|---|---:|---:|---|
| Early preflight observed another managed acceptance guest from `D:\dev\guideXOSServer_FONT_SUPPORT`. It was left untouched. A later status snapshot showed no managed sessions. | — | 24124 (other checkout) | — | No S22R10 guest launched until QAC granted an exclusive reservation. |
| Boot 1, 2026-10-20 21:39:32 local | `eb86ad86e6614aa497c5718a82910ccc` | 35140 | `127.0.0.1:65252` | Exit 0; reservation released at 21:48:40 local. |
| Boot 2, 2026-10-20 21:48:56 local | `3e1e1a5df9f74158a49de98ae1f5dfb2` | 31568 | `127.0.0.1:65253` | Exit 0; reservation released at 21:52:40 local. |
| Boot 3, 2026-10-20 21:54:24 local | `70473d3b88a54c0ea2003945c586e09c` | 29928 | `127.0.0.1:65254` | Exit 0; reservation released at 21:58:38 local. |

The early other-checkout guest was observed during preflight and was not attached to, queried through QMP/VNC, interrupted, terminated, or overwritten. Later QAC snapshots reported zero sessions before the first S22R10 admission.

After boot 3, QAC reported Online, zero QEMU processes, zero foreign QEMU processes, zero managed sessions, and zero queued reservations. See [final-qac-status.txt](final-qac-status.txt). Per-run CLI diagnostics are preserved in each guest directory’s `qac-run.log`.
