@echo off
REM
REM Windows QEMU Run Script for guideXOS Kernel (SPARC v8)
REM
REM Launches qemu-system-sparc emulating a Sun4m SPARCstation 5.
REM
REM Usage:  run-sparc.bat [path-to-kernel.elf]
REM
REM Copyright (c) 2026 guideXOS Server
REM

setlocal enabledelayedexpansion

REM ============================================
REM Configuration
REM ============================================

REM Default kernel path (relative to repo root)
set "KERNEL=%~dp0..\build\sparc\bin\kernel.elf"

REM Override with command-line argument if provided
if not "%~1"=="" (
    set "KERNEL=%~1"
)

REM Locate QEMU
set "QEMU="
if exist "C:\Program Files\qemu\qemu-system-sparc.exe" (
    set "QEMU=C:\Program Files\qemu\qemu-system-sparc.exe"
)
if exist "C:\Program Files (x86)\qemu\qemu-system-sparc.exe" (
    set "QEMU=C:\Program Files (x86)\qemu\qemu-system-sparc.exe"
)
REM Also try PATH
where qemu-system-sparc.exe >nul 2>&1
if !errorlevel! equ 0 (
    set "QEMU=qemu-system-sparc.exe"
)

if not defined QEMU (
    echo [ERROR] qemu-system-sparc.exe not found.
    echo.
    echo   Download QEMU for Windows from: https://www.qemu.org/download/#windows
    echo   Default install path: C:\Program Files\qemu
    echo.
    pause
    exit /b 1
)

REM Check kernel file exists
if not exist "%KERNEL%" (
    echo [ERROR] Kernel ELF not found: %KERNEL%
    echo.
    echo   Build the kernel on your Linux VM first:
    echo     ./kernel/build-sparc.sh
    echo.
    echo   Then copy build/sparc/bin/kernel.elf to:
    echo     %KERNEL%
    echo.
    pause
    exit /b 1
)

echo ============================================
echo   guideXOS SPARC v8 - QEMU Launcher
echo ============================================
echo.
echo   QEMU:   %QEMU%
echo   Kernel: %KERNEL%
echo.
echo   Machine: Sun4m (SPARCstation 5)
echo   RAM:     128 MB
echo   Video:   TCX framebuffer (1024x768)
echo.
echo   Press Ctrl+Alt+G to release mouse grab
echo   Press Ctrl+Alt+Q to quit QEMU
echo.

REM ============================================
REM Launch QEMU
REM ============================================

"%QEMU%" ^
    -machine SS-5 ^
    -m 128 ^
    -kernel "%KERNEL%" ^
    -nographic -serial mon:stdio ^
    -d guest_errors

REM ============================================
REM Alternative: graphical TCX framebuffer mode
REM Uncomment the block below and comment out the
REM -nographic launch above to see the desktop.
REM ============================================

REM "%QEMU%" ^
REM     -machine SS-5 ^
REM     -m 128 ^
REM     -kernel "%KERNEL%" ^
REM     -vga tcx ^
REM     -d guest_errors

echo.
echo QEMU exited.
pause