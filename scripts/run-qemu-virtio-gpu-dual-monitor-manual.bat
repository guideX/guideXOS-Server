@echo off
REM
REM guideXOS QEMU-only dual-monitor manual validation launcher
REM
REM This wrapper owns the manual-validation build and evidence setup, then
REM delegates QEMU command construction to run-qemu-display-probe.bat. It does
REM not inject input, change guest state, run a proof coordinator, or apply a
REM topology change automatically.

setlocal EnableExtensions EnableDelayedExpansion
set "ROOT_DIR=%~dp0.."
for %%I in ("%ROOT_DIR%") do set "ROOT_DIR=%%~fI"

for /f "usebackq delims=" %%S in (`powershell -NoProfile -Command "(Get-Date).ToString('yyyyMMdd-HHmmss')"`) do set "MANUAL_STAMP=%%S"
set "EVIDENCE_ROOT=%ROOT_DIR%\logs\manual-validation-%MANUAL_STAMP%"
if not exist "%EVIDENCE_ROOT%" mkdir "%EVIDENCE_ROOT%"
set "CONFIG_STORE=%EVIDENCE_ROOT%\display-config-store.img"
set "SERIAL_LOG=%EVIDENCE_ROOT%\guest.serial.log"

if not defined GXOS_QEMU_DISPLAY_PROBE_QMP_PORT (
    for /f "usebackq delims=" %%P in (`powershell -NoProfile -Command "$listener=[Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback,0); try { $listener.Start(); ([Net.IPEndPoint]$listener.LocalEndpoint).Port } finally { $listener.Stop() }"`) do set "GXOS_QEMU_DISPLAY_PROBE_QMP_PORT=%%P"
)

echo ================================================================================
echo Manual dual-monitor validation: mode=manual-dual-monitor-validation backend=virtio-gpu outputs=2
echo Active mode/primary/resolutions/virtualDesktop: guest-reported in the serial banner after boot; no host configuration injection
echo Default expectation before human Apply: mode=Extend primary=1 with QEMU-preferred per-output resolutions; virtual desktop is guest-reported
echo Persistence artifact: %CONFIG_STORE% guestPath=/display.cfg
echo topologyTestControls=enabled=yes topologyInjectionAvailable=yes automaticProof=disabled realHardware=no
echo QEMU-only safety boundary: REAL HARDWARE GPU/MMIO ENABLEMENT IS MULE TERRITORY AND REQUIRES A SEPARATE SAFETY CHECKPOINT.
echo QMP: lifecycle and screenshots only; no guest IPC; port=%GXOS_QEMU_DISPLAY_PROBE_QMP_PORT%
echo Evidence root: %EVIDENCE_ROOT%
echo ================================================================================
echo Manual checklist - record each item with record-virtio-gpu-dual-monitor-manual-result.ps1
echo A. Initial Extend: both live outputs, compositor content, primary-only taskbar, wallpaper, pointer crossing, focus.
echo B. Cross-monitor interaction: drag, focus, maximize/restore, return drag, spanning window, repaint stability.
echo C. Display Options: two real outputs, no synthetic duplicates, status/connector, mode/primary/resolutions, unavailable refresh/rotation, no stale request.
echo D. Primary switching: Display 2 Apply and verify taskbar, reachability, pointer, reopen; reverse to Display 1.
echo E. Mixed Extend: Display 1 1280x800, Display 2 1024x768, desktop bounds, pointer, drag/maximize, stride/color/clipping.
echo F. Mirror: reject mismatch without mutation; apply 1024x768 Mirror; same desktop/taskbar/cursor/clicks; return Extend.
echo G. Apply/OK/Cancel: Cancel unchanged, Apply stays open, OK closes, failed Apply stays open, reopen actual values.
echo H. Persistence: Extend, Display 2 primary, 1280x800 plus 1024x768; clean exit/relaunch restores without reinjection/fallback.
echo I. Pending topology: injected removal preview/Keep Current/apply, one output, injected addition preview/apply, dual output restored; label test events.
echo J. Primary-removal fallback: Display 2 primary, injected removal, preview/apply, Display 1 fallback, then restore Display 2; automated-only if control is unavailable.
echo K. Stability: bounded human observation; no freeze/black/corruption/spin/unresponsive input/repaint/fallback/GPU failures.
echo Exact checklist details: docs\MULTI_MONITOR_V0_2_EXPERIMENT.md section Manual QEMU Validation
echo ================================================================================

if not exist "%CONFIG_STORE%" (
    echo Creating writable persistent configuration artifact...
    powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0smoke-virtio-gpu-display-configuration-persistence.ps1" -FormatOnly -ImagePath "%CONFIG_STORE%"
    if errorlevel 1 (
        echo ERROR: could not create %CONFIG_STORE%
        exit /b 1
    )
)

set "EXTRA_CFLAGS=-DGXOS_QEMU_VIRTIO_GPU_PROBE_ACTIVE -DGXOS_QEMU_VIRTIO_GPU_SCANOUT1_ACTIVE -DGXOS_QEMU_VIRTIO_GPU_COMPOSITOR_LIVE -DGXOS_QEMU_VIRTIO_GPU_DISPLAY_EVENTS_ACTIVE -DGXOS_QEMU_VIRTIO_GPU_DISPLAY_CONFIGURATION_CONTROL_ACTIVE -DGXOS_QEMU_VIRTIO_GPU_DISPLAY_CONFIGURATION_PERSISTENCE_ACTIVE -DGXOS_QEMU_VIRTIO_GPU_MANUAL_VALIDATION_ACTIVE"
set "GXOS_QEMU_DISPLAY_PROBE_HEADLESS=0"
set "GXOS_QEMU_DISPLAY_PROBE_CAPTURE=0"
set "GXOS_QEMU_DISPLAY_PROBE_NO_PAUSE=0"
set "GXOS_QEMU_DISPLAY_PROBE_DISABLE_VNC=1"
set "GXOS_QEMU_DISPLAY_PROBE_SIMPLE_GTK=1"
set "GXOS_QEMU_DISPLAY_PROBE_SERIAL_LOG=%SERIAL_LOG%"

echo Rebuilding kernel objects with the manual-validation flags; automatic proof is disabled...
pushd "%ROOT_DIR%\kernel"
mingw32-make ARCH=amd64 clean
set "CLEAN_EXIT_CODE=%ERRORLEVEL%"
popd
if not "%CLEAN_EXIT_CODE%"=="0" (
    echo ERROR: manual-validation kernel clean failed.
    exit /b 1
)
call "%ROOT_DIR%\build-kernel.bat"
if errorlevel 1 (
    echo ERROR: manual-validation kernel build failed.
    exit /b 1
)

echo Starting interactive QEMU. Perform the documented checklist manually.
echo Host console banner mirrors the guest serial banner; guest serial log: %SERIAL_LOG%
call "%~dp0run-qemu-display-probe.bat" virtio-gpu "%CONFIG_STORE%"
set "QEMU_EXIT_CODE=%ERRORLEVEL%"
echo Manual dual-monitor validation ended. Evidence root: %EVIDENCE_ROOT%
echo Guest serial log: %SERIAL_LOG%
exit /b %QEMU_EXIT_CODE%
