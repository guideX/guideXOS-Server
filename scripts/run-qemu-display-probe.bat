@echo off
REM
REM guideXOS Experimental QEMU Display Probe Launcher (Windows Batch)
REM
REM This launcher is intentionally separate from run-qemu.bat.
REM It is diagnostic-only and only proves the QEMU-side probe and
REM scanout-0 2D diagnostic pattern path.
REM
REM Usage:
REM   scripts\run-qemu-display-probe.bat [std|virtio-gpu|virtio-gpu-modern-only|virtio-vga|qxl-vga]
REM
REM - std         : legacy VGA / Bochs-style framebuffer probe
REM - virtio-gpu  : diagnostic virtio-gpu-pci probe (no rendering)
REM - virtio-gpu-modern-only : diagnostic modern-only virtio-gpu-pci probe (no rendering)
REM - virtio-vga   : VGA-compatible virtio probe
REM - qxl-vga      : QXL/SPICE diagnostic probe (no viewer required)
REM
REM Capture / smoke mode environment:
REM   GXOS_QEMU_DISPLAY_PROBE_HEADLESS=1
REM   GXOS_QEMU_DISPLAY_PROBE_NO_PAUSE=1
REM   GXOS_QEMU_DISPLAY_PROBE_SERIAL_LOG=path\to\serial.log
REM
REM Copyright (c) 2024 guideX
REM

setlocal enabledelayedexpansion

set "SCRIPT_DIR=%~dp0"
set "ROOT_DIR=%SCRIPT_DIR%.."
for %%I in ("%ROOT_DIR%") do set "ROOT_DIR=%%~fI"
set "DISPLAY_BACKEND=%~1"
if "%DISPLAY_BACKEND%"=="" set "DISPLAY_BACKEND=std"
if /I "%DISPLAY_BACKEND%"=="multimonitor" set "DISPLAY_BACKEND=virtio-gpu"
if /I "%DISPLAY_BACKEND%"=="virtio" set "DISPLAY_BACKEND=virtio-vga"
if /I "%DISPLAY_BACKEND%"=="qxl" set "DISPLAY_BACKEND=qxl-vga"

if /I not "%DISPLAY_BACKEND%"=="std" if /I not "%DISPLAY_BACKEND%"=="virtio-gpu" if /I not "%DISPLAY_BACKEND%"=="virtio-gpu-modern-only" if /I not "%DISPLAY_BACKEND%"=="virtio-vga" if /I not "%DISPLAY_BACKEND%"=="qxl-vga" (
    echo WARNING: Unknown display backend "%DISPLAY_BACKEND%"; defaulting to std.
    set "DISPLAY_BACKEND=std"
)

set "QEMU_HEADLESS=0"
if /I "%GXOS_QEMU_DISPLAY_PROBE_HEADLESS%"=="1" set "QEMU_HEADLESS=1"

set "QEMU_NO_PAUSE=0"
if /I "%GXOS_QEMU_DISPLAY_PROBE_NO_PAUSE%"=="1" set "QEMU_NO_PAUSE=1"

set "QEMU_CAPTURE=0"
if /I "%GXOS_QEMU_DISPLAY_PROBE_CAPTURE%"=="1" set "QEMU_CAPTURE=1"

set "QEMU_SERIAL_LOG="
if defined GXOS_QEMU_DISPLAY_PROBE_SERIAL_LOG set "QEMU_SERIAL_LOG=%GXOS_QEMU_DISPLAY_PROBE_SERIAL_LOG%"

set "QEMU_QMP_ARGS="
if defined GXOS_QEMU_DISPLAY_PROBE_QMP_PORT (
    set "QEMU_QMP_ARGS=-qmp tcp:127.0.0.1:%GXOS_QEMU_DISPLAY_PROBE_QMP_PORT%,server,nowait"
)

echo ====================================
echo   guideXOS Experimental Display Probe
echo ====================================
echo.
echo Probe backend: %DISPLAY_BACKEND%
echo Diagnostic scope: firmware and QEMU display exposure only
echo Guest note: guideXOS still consumes one selected framebuffer in normal modes; virtio-gpu probe mode renders a diagnostic test pattern on scanout 0
if "%QEMU_HEADLESS%"=="1" echo Headless capture mode: enabled
if "%QEMU_CAPTURE%"=="1" echo Visual capture mode: enabled
if not "%QEMU_SERIAL_LOG%"=="" echo Serial log capture: %QEMU_SERIAL_LOG%
if "%QEMU_NO_PAUSE%"=="1" echo Pause after exit: disabled
echo QEMU video args: %QEMU_VIDEO_ARGS%
if not "%QEMU_SPICE_ARGS%"=="" echo QEMU spice args: %QEMU_SPICE_ARGS%
echo QEMU display args: %QEMU_DISPLAY_ARGS%
echo QEMU VNC args: %QEMU_VNC_ARGS%
if not "%QEMU_QMP_ARGS%"=="" echo QEMU QMP args: %QEMU_QMP_ARGS%
echo QEMU serial args: %QEMU_SERIAL_ARGS%
echo.

REM Check if OVMF.fd exists (try local first, then QEMU's built-in)
REM QEMU ships split images: edk2-x86_64-code.fd (code) + edk2-x86_64-vars.fd (vars)
REM Combined OVMF.fd works as a single pflash but split images need both units
set "OVMF_CODE="
set "OVMF_VARS="
set "SPLIT_PFLASH=0"

if exist "%ROOT_DIR%\OVMF.fd" (
    set "OVMF_CODE=%ROOT_DIR%\OVMF.fd"
    echo Using local OVMF.fd
    goto :ovmf_found
)
if exist "%ROOT_DIR%\ovmf.fd" (
    set "OVMF_CODE=%ROOT_DIR%\ovmf.fd"
    echo Using local ovmf.fd
    goto :ovmf_found
)

if exist "C:\Program Files\qemu\share\edk2-x86_64-code.fd" (
    set "OVMF_CODE=C:\Program Files\qemu\share\edk2-x86_64-code.fd"
    set "SPLIT_PFLASH=1"
    echo Using QEMU's built-in UEFI firmware [split images]

    if exist "C:\Program Files\qemu\share\edk2-x86_64-vars.fd" (
        if not exist "%ROOT_DIR%\OVMF_VARS.fd" (
            echo Creating local UEFI variable store...
            copy "C:\Program Files\qemu\share\edk2-x86_64-vars.fd" "%ROOT_DIR%\OVMF_VARS.fd" >nul
        )
        set "OVMF_VARS=%ROOT_DIR%\OVMF_VARS.fd"
        goto :ovmf_found
    )

    echo WARNING: edk2-x86_64-vars.fd not found, creating empty vars store
    if not exist "%ROOT_DIR%\OVMF_VARS.fd" (
        fsutil file createnew "%ROOT_DIR%\OVMF_VARS.fd" 131072 >nul 2>&1
        if errorlevel 1 (
            powershell -Command "$bytes = New-Object byte[] 131072; [System.IO.File]::WriteAllBytes('%ROOT_DIR%\OVMF_VARS.fd', $bytes)" >nul 2>&1
        )
    )
    set "OVMF_VARS=%ROOT_DIR%\OVMF_VARS.fd"
    goto :ovmf_found
)

echo ERROR: UEFI firmware not found!
echo.
echo Please download OVMF.fd:
echo   https://github.com/tianocore/edk2/releases
echo.
pause
exit /b 1

:ovmf_found

if not exist "%ROOT_DIR%\ESP\" (
    echo ERROR: ESP directory not found!
    echo.
    echo Please run build.ps1 first:
    echo   powershell -ExecutionPolicy Bypass -File build.ps1
    echo.
    pause
    exit /b 1
)

if not exist "%ROOT_DIR%\ESP\kernel.elf" (
    echo ERROR: kernel.elf not found in ESP!
    echo.
    echo The probe needs a staged kernel image before QEMU can boot.
    echo Run the canonical staging build first:
    echo   powershell -ExecutionPolicy Bypass -File build-uefi.ps1
    echo   or
    echo   build-kernel.bat
    echo.
    pause
    exit /b 1
)

if not exist "%ROOT_DIR%\ESP\ramdisk.img" (
    echo ERROR: ramdisk.img not found in ESP!
    echo.
    echo The probe needs the boot-time wallpaper/runtime image in ESP.
    echo Run the canonical staging build first:
    echo   powershell -ExecutionPolicy Bypass -File build-uefi.ps1
    echo   or
    echo   build-kernel.bat
    echo.
    pause
    exit /b 1
)

set QEMU_EXE=qemu-system-x86_64
where qemu-system-x86_64 >nul 2>&1
if errorlevel 1 (
    if exist "C:\Program Files\qemu\qemu-system-x86_64.exe" (
        set "QEMU_EXE=C:\Program Files\qemu\qemu-system-x86_64.exe"
        echo Found QEMU at: C:\Program Files\qemu
    ) else if exist "C:\qemu\qemu-system-x86_64.exe" (
        set "QEMU_EXE=C:\qemu\qemu-system-x86_64.exe"
        echo Found QEMU at: C:\qemu
    ) else (
        echo ERROR: QEMU not found in PATH!
        echo.
        echo Please install QEMU:
        echo   https://www.qemu.org/download/#windows
        echo.
        pause
        exit /b 1
    )
)

set "QEMU_VIDEO_ARGS=-vga std"
set "QEMU_DISPLAY_ARGS=-display gtk"
set "QEMU_VNC_ARGS=-vnc :0"
set "QEMU_SPICE_ARGS="
set "QEMU_QMP_ARGS="
set "QEMU_PROBE_NOTE=legacy VGA/Bochs-style framebuffer"
set "QEMU_SERIAL_ARGS=-serial stdio"

if /I "%DISPLAY_BACKEND%"=="virtio-gpu" (
    set "QEMU_VIDEO_ARGS=-vga none -device virtio-gpu-pci,id=gpu0,max_outputs=2"
    set "QEMU_DISPLAY_ARGS=-display gtk,show-tabs=on,zoom-to-fit=on"
    set "QEMU_VNC_ARGS=-vnc :0"
    set "QEMU_PROBE_NOTE=virtio-gpu-pci diagnostic 2D test-pattern probe (scanout 0 only)"
)

if /I "%DISPLAY_BACKEND%"=="virtio-gpu-modern-only" (
    set "QEMU_VIDEO_ARGS=-vga none -device virtio-gpu-pci,id=gpu0,max_outputs=2,disable-legacy=on"
    set "QEMU_DISPLAY_ARGS=-display gtk,show-tabs=on,zoom-to-fit=on"
    set "QEMU_VNC_ARGS=-vnc :0"
    set "QEMU_PROBE_NOTE=virtio-gpu-pci modern-only diagnostic 2D test-pattern probe (scanout 0 only)"
)

if /I "%DISPLAY_BACKEND%"=="virtio-vga" (
    set "QEMU_VIDEO_ARGS=-vga virtio"
    set "QEMU_DISPLAY_ARGS=-display gtk,show-tabs=on,zoom-to-fit=on"
    set "QEMU_VNC_ARGS=-vnc :0"
    set "QEMU_PROBE_NOTE=virtio-vga diagnostic probe"
)

if /I "%DISPLAY_BACKEND%"=="qxl-vga" (
    set "QEMU_VIDEO_ARGS=-vga qxl"
    set "QEMU_DISPLAY_ARGS=-display gtk,show-tabs=on,zoom-to-fit=on"
    set "QEMU_VNC_ARGS=-vnc :0"
    set "QEMU_SPICE_ARGS=-spice addr=127.0.0.1,port=5930,disable-ticketing=on"
    set "QEMU_PROBE_NOTE=qxl-vga diagnostic probe with SPICE server"
)

if "%QEMU_HEADLESS%"=="1" (
    set "QEMU_DISPLAY_ARGS=-display none"
    if "%QEMU_CAPTURE%"=="0" set "QEMU_VNC_ARGS="
)

if not "%QEMU_SERIAL_LOG%"=="" (
    set "QEMU_SERIAL_ARGS=-serial file:%QEMU_SERIAL_LOG%"
)

if defined GXOS_QEMU_DISPLAY_PROBE_QMP_PORT (
    set "QEMU_QMP_ARGS=-qmp tcp:127.0.0.1:%GXOS_QEMU_DISPLAY_PROBE_QMP_PORT%,server,nowait"
)

echo Launching QEMU display probe...
echo.
echo   Probe note: %QEMU_PROBE_NOTE%
echo   Expected guest result: one selected framebuffer unless future logs prove otherwise
echo   Serial debug output from the kernel will appear below.
echo   Press Ctrl+C in this window to exit QEMU
echo ========================================
echo.

cd /d "%ROOT_DIR%"

if "%SPLIT_PFLASH%"=="1" (
    echo Using split pflash: CODE + VARS
    echo QEMU launch: %QEMU_EXE% -machine q35,usb=off -drive if=pflash,format=raw,unit=0,readonly=on,file="%OVMF_CODE%" -drive if=pflash,format=raw,unit=1,file="%OVMF_VARS%" -drive file=fat:rw:ESP,format=raw -netdev user,id=net0 -device e1000,netdev=net0 -m 1024M %QEMU_VIDEO_ARGS% %QEMU_SPICE_ARGS% %QEMU_DISPLAY_ARGS% %QEMU_VNC_ARGS% %QEMU_QMP_ARGS% %QEMU_SERIAL_ARGS% -rtc base=utc,clock=host -no-reboot
    "%QEMU_EXE%" ^
        -machine q35,usb=off ^
        -drive if=pflash,format=raw,unit=0,readonly=on,file="%OVMF_CODE%" ^
        -drive if=pflash,format=raw,unit=1,file="%OVMF_VARS%" ^
        -drive file=fat:rw:ESP,format=raw ^
        -netdev user,id=net0 ^
        -device e1000,netdev=net0 ^
        -m 1024M ^
        %QEMU_VIDEO_ARGS% ^
        %QEMU_SPICE_ARGS% ^
        %QEMU_DISPLAY_ARGS% ^
        %QEMU_VNC_ARGS% ^
        %QEMU_QMP_ARGS% ^
        %QEMU_SERIAL_ARGS% ^
        -rtc base=utc,clock=host ^
        -no-reboot
) else (
    echo Using combined pflash: OVMF.fd
    echo QEMU launch: %QEMU_EXE% -machine q35,usb=off -drive if=pflash,format=raw,readonly=on,file="%OVMF_CODE%" -drive file=fat:rw:ESP,format=raw -netdev user,id=net0 -device e1000,netdev=net0 -m 1024M %QEMU_VIDEO_ARGS% %QEMU_SPICE_ARGS% %QEMU_DISPLAY_ARGS% %QEMU_VNC_ARGS% %QEMU_QMP_ARGS% %QEMU_SERIAL_ARGS% -rtc base=utc,clock=host -no-reboot
    "%QEMU_EXE%" ^
        -machine q35,usb=off ^
        -drive if=pflash,format=raw,readonly=on,file="%OVMF_CODE%" ^
        -drive file=fat:rw:ESP,format=raw ^
        -netdev user,id=net0 ^
        -device e1000,netdev=net0 ^
        -m 1024M ^
        %QEMU_VIDEO_ARGS% ^
        %QEMU_SPICE_ARGS% ^
        %QEMU_DISPLAY_ARGS% ^
        %QEMU_VNC_ARGS% ^
        %QEMU_QMP_ARGS% ^
        %QEMU_SERIAL_ARGS% ^
        -rtc base=utc,clock=host ^
        -no-reboot
)

set "QEMU_EXIT_CODE=%ERRORLEVEL%"
echo.
echo QEMU exited
if "%QEMU_NO_PAUSE%"=="1" exit /b %QEMU_EXIT_CODE%
pause
exit /b %QEMU_EXIT_CODE%
