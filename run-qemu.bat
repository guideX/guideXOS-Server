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
set "OVMF_PATH="
if exist "%SCRIPT_DIR%OVMF.fd" (
    set "OVMF_PATH=%SCRIPT_DIR%OVMF.fd"
) else if exist "%SCRIPT_DIR%ovmf.fd" (
    set "OVMF_PATH=%SCRIPT_DIR%ovmf.fd"
) else if exist "C:\Program Files\qemu\share\edk2-x86_64-code.fd" (
    set "OVMF_PATH=C:\Program Files\qemu\share\edk2-x86_64-code.fd"
    echo Using QEMU's built-in UEFI firmware
) else (
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
)

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
echo Press Ctrl+C in this window to exit QEMU
echo ========================================
echo.

REM Change to script directory so relative paths work
cd /d "%SCRIPT_DIR%"

REM Launch QEMU with UEFI using pflash drives (modern way)
"%QEMU_EXE%" ^
    -drive if=pflash,format=raw,readonly=on,file="%OVMF_PATH%" ^
    -drive file=fat:rw:ESP,format=raw ^
    -m 1024M ^
    -serial stdio ^
    -no-reboot

echo.
echo QEMU exited
pause
