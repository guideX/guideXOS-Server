@echo off
REM
REM guideXOS QEMU Launcher (Windows Batch)
REM
REM Launches guideXOS in QEMU with UEFI firmware
REM
REM Copyright (c) 2024 guideX
REM

setlocal enabledelayedexpansion

REM Get the directory where this script is located
set "SCRIPT_DIR=%~dp0"

echo ====================================
echo   guideXOS UEFI Boot
echo ====================================
echo.

REM Check if OVMF.fd exists (try local first, then QEMU's built-in)
REM QEMU ships split images: edk2-x86_64-code.fd (code) + edk2-x86_64-vars.fd (vars)
REM Combined OVMF.fd works as a single pflash but split images need both units
set "OVMF_CODE="
set "OVMF_VARS="
set "SPLIT_PFLASH=0"

REM Try local combined OVMF.fd first (simplest setup)
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

REM Try QEMU's built-in split images
if exist "C:\Program Files\qemu\share\edk2-x86_64-code.fd" (
    set "OVMF_CODE=C:\Program Files\qemu\share\edk2-x86_64-code.fd"
    set "SPLIT_PFLASH=1"
    echo Using QEMU's built-in UEFI firmware [split images]
    
    REM Check for vars template and create local copy
    if exist "C:\Program Files\qemu\share\edk2-x86_64-vars.fd" (
        if not exist "%SCRIPT_DIR%OVMF_VARS.fd" (
            echo Creating local UEFI variable store...
            copy "C:\Program Files\qemu\share\edk2-x86_64-vars.fd" "%SCRIPT_DIR%OVMF_VARS.fd" >nul
        )
        set "OVMF_VARS=%SCRIPT_DIR%OVMF_VARS.fd"
        goto :ovmf_found
    )
    REM No vars file found - create an empty 128KB vars file
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

REM No UEFI firmware found
echo ERROR: UEFI firmware not found!
echo.
echo Please download OVMF.fd:
echo   https://github.com/tianocore/edk2/releases
echo.
echo Or run this PowerShell command:
echo   Invoke-WebRequest -Uri "https://github.com/kraxel/edk2/raw/binaries/OVMF.fd" -OutFile "OVMF.fd"
echo.
pause
exit /b 1

:ovmf_found

REM Check if ESP directory exists
if not exist "%SCRIPT_DIR%ESP\" (
    echo ERROR: ESP directory not found!
    echo.
    echo Please run build.ps1 first:
    echo   powershell -ExecutionPolicy Bypass -File build.ps1
    echo.
    pause
    exit /b 1
)

REM Check if kernel exists
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

REM Check if QEMU is available
set QEMU_EXE=qemu-system-x86_64
where qemu-system-x86_64 >nul 2>&1
if errorlevel 1 (
    REM Check common QEMU installation paths
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
        echo After installing, add to PATH:
        echo   C:\Program Files\qemu
        echo.
        pause
        exit /b 1
    )
)

echo Launching QEMU with UEFI firmware...
echo.
echo   Mouse: Press Ctrl+Alt+G to grab mouse, Ctrl+Alt+G again to release
echo   Serial debug output from the kernel will appear below.
echo   Press Ctrl+C in this window to exit QEMU
echo ========================================
echo.

REM Change to script directory so relative paths work
cd /d "%SCRIPT_DIR%"

REM Launch QEMU with UEFI firmware
REM
REM The kernel uses PS/2 mouse input (IRQ12, i8042 controller).
REM The Q35 machine type includes HPET, LPC (for i8042 PS/2), and a
REM modern chipset that OVMF expects. usb=off ensures mouse events are
REM routed to the PS/2 controller instead of a USB tablet.
REM You must click inside the QEMU window (or press Ctrl+Alt+G) to
REM grab the mouse so QEMU routes mouse events to the PS/2 controller.
REM
REM Split pflash images (edk2-x86_64-code.fd + vars) require two pflash
REM drives: unit 0 for code (read-only) and unit 1 for variables (read-write).
REM This eliminates "Invalid read at addr 0xFFC00000" errors from OVMF
REM trying to access unmapped flash regions.

if "%SPLIT_PFLASH%"=="1" (
    echo Using split pflash: CODE + VARS
    "%QEMU_EXE%" ^
        -machine q35,usb=off ^
        -drive if=pflash,format=raw,unit=0,readonly=on,file="%OVMF_CODE%" ^
        -drive if=pflash,format=raw,unit=1,file="%OVMF_VARS%" ^
        -drive file=fat:rw:ESP,format=raw ^
        -m 1024M ^
        -vga std ^
        -serial stdio ^
        -no-reboot
) else (
    echo Using combined pflash: OVMF.fd
    "%QEMU_EXE%" ^
        -machine q35,usb=off ^
        -drive if=pflash,format=raw,readonly=on,file="%OVMF_CODE%" ^
        -drive file=fat:rw:ESP,format=raw ^
        -m 1024M ^
        -vga std ^
        -serial stdio ^
        -no-reboot
)

echo.
echo QEMU exited
pause
