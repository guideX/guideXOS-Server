@echo off
REM
REM guideXOS Experimental QEMU Multimonitor Launcher (Windows Batch)
REM
REM This launcher is intentionally separate from run-qemu.bat.
REM It starts QEMU with a multi-output-capable GPU device so the guest
REM can be tested once the display stack understands multiple monitors.
REM
REM Current status:
REM - QEMU can expose multiple outputs through virtio-gpu-pci / qxl.
REM - guideXOS Server v0.2 still has a single-framebuffer compositor path.
REM - This script does not fake multi-output support inside the OS.
REM
REM Copyright (c) 2024 guideX
REM

setlocal enabledelayedexpansion

set "SCRIPT_DIR=%~dp0"

echo ====================================
echo   guideXOS Experimental Multimonitor QEMU
echo ====================================
echo.
echo This launch path is experimental.
echo It exposes a multi-output-capable GPU device to the guest, but the
echo current guideXOS display stack still renders as a single framebuffer.
echo.

set "OVMF_CODE="
set "OVMF_VARS="
set "SPLIT_PFLASH=0"

if exist "%SCRIPT_DIR%OVMF.fd" (
    set "OVMF_CODE=%SCRIPT_DIR%OVMF.fd"
    echo Using local OVMF.fd
    goto :ovmf_found
)
if exist "%SCRIPT_DIR%ovmf.fd" (
    set "OVMF_CODE=%SCRIPT_DIR%ovmf.fd"
    echo Using local ovmf.fd
    goto :ovmf_found
)

if exist "C:\Program Files\qemu\share\edk2-x86_64-code.fd" (
    set "OVMF_CODE=C:\Program Files\qemu\share\edk2-x86_64-code.fd"
    set "SPLIT_PFLASH=1"
    echo Using QEMU's built-in UEFI firmware [split images]

    if exist "C:\Program Files\qemu\share\edk2-x86_64-vars.fd" (
        if not exist "%SCRIPT_DIR%OVMF_VARS.fd" (
            echo Creating local UEFI variable store...
            copy "C:\Program Files\qemu\share\edk2-x86_64-vars.fd" "%SCRIPT_DIR%OVMF_VARS.fd" >nul
        )
        set "OVMF_VARS=%SCRIPT_DIR%OVMF_VARS.fd"
        goto :ovmf_found
    )

    echo WARNING: edk2-x86_64-vars.fd not found, creating empty vars store
    if not exist "%SCRIPT_DIR%OVMF_VARS.fd" (
        fsutil file createnew "%SCRIPT_DIR%OVMF_VARS.fd" 131072 >nul 2>&1
        if errorlevel 1 (
            powershell -Command "$bytes = New-Object byte[] 131072; [System.IO.File]::WriteAllBytes('%SCRIPT_DIR%OVMF_VARS.fd', $bytes)" >nul 2>&1
        )
    )
    set "OVMF_VARS=%SCRIPT_DIR%OVMF_VARS.fd"
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

if not exist "%SCRIPT_DIR%ESP\" (
    echo ERROR: ESP directory not found!
    echo.
    echo Please run build.ps1 first:
    echo   powershell -ExecutionPolicy Bypass -File build.ps1
    echo.
    pause
    exit /b 1
)

if not exist "%SCRIPT_DIR%ESP\kernel.elf" (
    echo WARNING: kernel.elf not found in ESP!
    echo.
    echo The bootloader will run but needs a kernel to boot.
    echo To build the kernel, install MinGW and run:
    echo   powershell -ExecutionPolicy Bypass -File build.ps1
    echo.
    echo Press any key to continue anyway...
    pause >nul
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

echo Launching experimental multimonitor QEMU...
echo.
echo   Device: virtio-gpu-pci,max_outputs=2
echo   Display: GTK window
echo   Guest driver status: current guideXOS path still uses a single framebuffer
echo   Note: QEMU can expose the outputs, but the OS must still learn to render them
echo ========================================
echo.

cd /d "%SCRIPT_DIR%"

if "%SPLIT_PFLASH%"=="1" (
    echo Using split pflash: CODE + VARS
    "%QEMU_EXE%" ^
        -machine q35,usb=off ^
        -drive if=pflash,format=raw,unit=0,readonly=on,file="%OVMF_CODE%" ^
        -drive if=pflash,format=raw,unit=1,file="%OVMF_VARS%" ^
        -drive file=fat:rw:ESP,format=raw ^
        -netdev user,id=net0 ^
        -device e1000,netdev=net0 ^
        -m 1024M ^
        -vga none ^
        -device virtio-gpu-pci,max_outputs=2 ^
        -display gtk,show-tabs=on,zoom-to-fit=on ^
        -serial stdio ^
        -rtc base=utc,clock=host ^
        -no-reboot
) else (
    echo Using combined pflash: OVMF.fd
    "%QEMU_EXE%" ^
        -machine q35,usb=off ^
        -drive if=pflash,format=raw,readonly=on,file="%OVMF_CODE%" ^
        -drive file=fat:rw:ESP,format=raw ^
        -netdev user,id=net0 ^
        -device e1000,netdev=net0 ^
        -m 1024M ^
        -vga none ^
        -device virtio-gpu-pci,max_outputs=2 ^
        -display gtk,show-tabs=on,zoom-to-fit=on ^
        -serial stdio ^
        -rtc base=utc,clock=host ^
        -no-reboot
)

echo.
echo QEMU exited
pause
