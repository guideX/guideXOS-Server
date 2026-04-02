@echo off
REM Launch guideXOS kernel in QEMU with VNC support
REM This allows remote viewing from another computer
REM
REM Copyright (c) 2024 guideX

REM Save current directory and change to script directory
pushd %~dp0

REM Go to project root (one level up from scripts)
cd ..

echo Starting guideXOS Kernel with VNC support...
echo.
echo VNC Server will be available on:
echo   - localhost:5900 (from this computer)
echo   - %COMPUTERNAME%:5900 (from network)
echo   - [Your IP]:5900 (from other computers)
echo.
echo Connect with any VNC client:
echo   vncviewer localhost:5900
echo   or use TightVNC, RealVNC, etc.
echo.

REM Path to QEMU
set QEMU=C:\Program Files\qemu\qemu-system-i386.exe

REM Path to kernel
set KERNEL=kernel\build\x86\bin\kernel.elf

REM Check if kernel exists
if not exist "%KERNEL%" (
    echo Error: Kernel not found at %KERNEL%
    echo Please build the kernel first:
    echo   cd kernel
    echo   build-x86.bat
    popd
    pause
    exit /b 1
)

REM Launch QEMU with VNC and USB tablet for absolute mouse positioning
REM USB tablet works well with VNC clients that support absolute positioning
"%QEMU%" ^
    -machine pc ^
    -device usb-ehci,id=ehci ^
    -device usb-tablet,bus=ehci.0 ^
    -kernel "%KERNEL%" ^
    -m 128M ^
    -vga std ^
    -vnc :0 ^
    -k en-us ^
    -serial stdio

REM If QEMU not found, show error
if errorlevel 1 (
    echo.
    echo Error: QEMU not found or failed to start
    echo Please check QEMU installation path
    popd
    pause
)

REM Restore original directory
popd

