@echo off
REM
REM Windows QEMU Run Script for guideXOS Kernel (RISC-V 64 / RV64IMA)
REM
REM Launches qemu-system-riscv64 emulating the "virt" machine with OpenSBI.
REM
REM Usage:  run-riscv64_in_windows.bat [path-to-kernel.elf]
REM
REM Copyright (c) 2026 guideXOS Server
REM

setlocal enabledelayedexpansion

REM ============================================
REM Configuration
REM ============================================

REM Default kernel path (relative to scripts directory)
set "KERNEL=%~dp0..\build\riscv64\bin\kernel.elf"

if not "%~1"=="" (
    set "KERNEL=%~1"
)

REM Locate QEMU
set "QEMU="
if exist "C:\Program Files\qemu\qemu-system-riscv64.exe" (
    set "QEMU=C:\Program Files\qemu\qemu-system-riscv64.exe"
)
if exist "C:\Program Files (x86)\qemu\qemu-system-riscv64.exe" (
    set "QEMU=C:\Program Files (x86)\qemu\qemu-system-riscv64.exe"
)
where qemu-system-riscv64.exe >nul 2>&1
if !errorlevel! equ 0 (
    set "QEMU=qemu-system-riscv64.exe"
)

if not defined QEMU (
    echo [ERROR] qemu-system-riscv64.exe not found.
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
    echo     ./build-riscv64.sh
    echo.
    pause
    exit /b 1
)

echo ============================================
echo   guideXOS RISC-V 64 - QEMU Launcher
echo ============================================
echo.
echo   QEMU:    %QEMU%
echo   Kernel:  %KERNEL%
echo.
echo   Machine: virt (OpenSBI firmware)
echo   RAM:     256 MB
echo.
echo   Press Ctrl+A then X to quit (nographic mode)
echo   Press Ctrl+C to interrupt
echo.

REM ============================================
REM Launch QEMU - serial console mode
REM ============================================

"%QEMU%" ^
    -machine virt ^
    -bios default ^
    -m 256 ^
    -kernel "%KERNEL%" ^
    -nographic ^
    -serial mon:stdio ^
    -d guest_errors

REM ============================================
REM Alternative: graphical mode (uncomment below, comment above)
REM ============================================

REM "%QEMU%" ^
REM     -machine virt ^
REM     -bios default ^
REM     -m 256 ^
REM     -kernel "%KERNEL%" ^
REM     -vga std ^
REM     -d guest_errors

echo.
echo QEMU exited.
pause
