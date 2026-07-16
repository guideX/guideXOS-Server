@echo off
REM
REM guideXOS QEMU-only VirtIO-GPU logical-resolution manual launcher
REM
REM This is an interactive, stay-open launcher for the QEMU-only probe build.
REM Build ESP with the bounded QEMU probe configuration first; this wrapper
REM intentionally leaves GXOS_QEMU_DISPLAY_PROBE_NO_PAUSE unset so the QEMU
REM window remains open until the operator exits it.
REM
setlocal
set "GXOS_QEMU_DISPLAY_PROBE_HEADLESS=0"
set "GXOS_QEMU_DISPLAY_PROBE_CAPTURE=0"
set "GXOS_QEMU_DISPLAY_PROBE_NO_PAUSE=0"
call "%~dp0run-qemu-display-probe.bat" virtio-gpu
endlocal
