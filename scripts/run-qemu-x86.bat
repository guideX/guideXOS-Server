@echo off
REM
REM QEMU x86 Launcher for guideXOS (Windows)
REM
REM Copyright (c) 2024 guideX
REM

set KERNEL_PATH=build\x86\bin\kernel.elf
set QEMU_PATH=C:\Program Files\qemu\qemu-system-i386.exe

if not exist "%KERNEL_PATH%" (
    echo Error: Kernel not found at %KERNEL_PATH%
    echo Please build the kernel first: make ARCH=x86
    pause
    exit /b 1
)

if not exist "%QEMU_PATH%" (
    echo Error: QEMU not found at %QEMU_PATH%
    echo Please install QEMU from: https://www.qemu.org/download/#windows
    pause
    exit /b 1
)

echo Launching guideXOS x86 kernel in QEMU...
echo Kernel: %KERNEL_PATH%
echo.
echo Press Ctrl+C in this window to exit QEMU
echo ----------------------------------------
echo.

"%QEMU_PATH%" ^
    -kernel "%KERNEL_PATH%" ^
    -m 128M ^
    -serial stdio ^
    -display gtk ^
    -no-reboot ^
    -no-shutdown
