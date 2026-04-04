@echo off
REM
REM Windows QEMU Run Script for guideXOS Kernel (SPARC v9 / UltraSPARC)
REM
REM Launches qemu-system-sparc64 emulating a Sun4u (Ultra 5 / Ultra 60).
REM
REM Usage:  run-sparcv9_in_windows.bat [path-to-kernel.elf]
REM
REM Copyright (c) 2026 guideXOS Server
REM

setlocal enabledelayedexpansion

REM ============================================
REM Configuration
REM ============================================

REM Default kernel path (relative to scripts directory)
set "KERNEL=%~dp0..\build\sparc64\bin\kernel.elf"

if not "%~1"=="" (
    set "KERNEL=%~1"
)

REM Locate QEMU
set "QEMU="
if exist "C:\Program Files\qemu\qemu-system-sparc64.exe" (
    set "QEMU=C:\Program Files\qemu\qemu-system-sparc64.exe"
)
if exist "C:\Program Files (x86)\qemu\qemu-system-sparc64.exe" (
    set "QEMU=C:\Program Files (x86)\qemu\qemu-system-sparc64.exe"
)
where qemu-system-sparc64.exe >nul 2>&1
if !errorlevel! equ 0 (
    set "QEMU=qemu-system-sparc64.exe"
)

if not defined QEMU (
    echo [ERROR] qemu-system-sparc64.exe not found.
    echo.
    echo   Download QEMU for Windows from: https://www.qemu.org/download/#windows
    echo.
    pause
    exit /b 1
)

if not exist "%KERNEL%" (
    echo [ERROR] Kernel ELF not found: %KERNEL%
    echo.
    echo   Build the kernel on your Linux VM first:
    echo     cd scripts
    echo     ./build-sparcv9.sh
    echo.
    pause
    exit /b 1
)

echo ============================================
echo   guideXOS SPARC v9 - QEMU Launcher
echo ============================================
echo.
echo   QEMU:    %QEMU%
echo   Kernel:  %KERNEL%
echo.
echo   Machine: Sun4u (UltraSPARC)
echo   RAM:     256 MB
echo   Display: VGA Framebuffer (1024x768)
echo   VNC:     Secondary viewer on localhost:5900
echo.
echo   Press Ctrl+Alt+G to release mouse grab
echo   Press Ctrl+Alt+2 then type 'quit' to exit QEMU
echo.

REM ============================================
REM Launch QEMU - graphical framebuffer mode (primary) with VNC (secondary viewer)
REM ============================================

"%QEMU%" ^
    -machine sun4u ^
    -m 256 ^
    -kernel "%KERNEL%" ^
    -vga std ^
    -vnc :0 ^
    -d guest_errors

REM ============================================
REM Alternative: serial console mode (uncomment below, comment above)
REM ============================================

REM "%QEMU%" ^
REM     -machine sun4u ^
REM     -m 256 ^
REM     -kernel "%KERNEL%" ^
REM     -nographic ^
REM     -serial mon:stdio ^
REM     -d guest_errors

echo.
echo QEMU exited.
pause
