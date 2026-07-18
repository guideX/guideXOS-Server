@echo off
REM
REM guideXOS QEMU-only VirtIO-GPU topology reconciliation manual launcher
REM
REM Build the explicit QEMU control image before using this launcher:
REM   set EXTRA_CFLAGS=-DGXOS_QEMU_VIRTIO_GPU_PROBE_ACTIVE -DGXOS_QEMU_VIRTIO_GPU_SCANOUT1_ACTIVE -DGXOS_QEMU_VIRTIO_GPU_COMPOSITOR_LIVE -DGXOS_QEMU_VIRTIO_GPU_COMPOSITOR_LIVE_BOUNDED -DGXOS_QEMU_VIRTIO_GPU_DISPLAY_EVENTS_ACTIVE -DGXOS_QEMU_VIRTIO_GPU_DISPLAY_CONFIGURATION_CONTROL_ACTIVE
REM   build-kernel.bat
REM
REM This launcher intentionally leaves the QEMU window open. Display Options
REM exposes explicit QEMU-only Test Remove/Test Restore buttons that inject a
REM pending notification; the operator must Review, Keep, or Apply it. There
REM is no automatic topology application and no genuine host-hotplug claim.
REM The Test buttons are unavailable in ordinary non-QEMU boots.

setlocal
set "GXOS_QEMU_DISPLAY_PROBE_HEADLESS=0"
set "GXOS_QEMU_DISPLAY_PROBE_CAPTURE=0"
set "GXOS_QEMU_DISPLAY_PROBE_NO_PAUSE=0"
call "%~dp0run-qemu-display-probe.bat" virtio-gpu
endlocal
