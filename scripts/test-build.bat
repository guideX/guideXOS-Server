@echo off
REM
REM Test build script for guideXOS kernel (Windows)
REM
REM Copyright (c) 2024 guideX
REM

echo ==========================================
echo guideXOS Kernel Build Test
echo ==========================================
echo.

REM Check for required tools
echo Checking required tools...
where i686-elf-gcc >nul 2>&1
if %errorlevel% neq 0 (
    echo Error: i686-elf-gcc not found in PATH
    echo Please install cross-compiler from: https://github.com/lordmilko/i686-elf-tools/releases
    pause
    exit /b 1
)

where nasm >nul 2>&1
if %errorlevel% neq 0 (
    echo Error: nasm not found in PATH
    echo Please install NASM from: https://www.nasm.us/
    pause
    exit /b 1
)

echo [OK] Cross-compiler found
echo [OK] Assembler found
echo.

REM Navigate to kernel directory
cd /d "%~dp0..\kernel"

REM Clean build
echo Cleaning previous build...
make ARCH=x86 clean 2>nul
echo.

REM Build kernel
echo Building x86 kernel...
make ARCH=x86
if %errorlevel% neq 0 (
    echo.
    echo ==========================================
    echo [X] BUILD FAILED
    echo ==========================================
    echo.
    echo Check the error messages above.
    echo Common issues:
    echo   - Missing cross-compiler ^(i686-elf-gcc^)
    echo   - Missing NASM assembler
    echo   - Syntax errors in source files
    echo.
    pause
    exit /b 1
)

echo.
echo ==========================================
echo [OK] BUILD SUCCESSFUL
echo ==========================================
echo.
echo Kernel location: build\x86\bin\kernel.elf
echo.

REM Show size
dir build\x86\bin\kernel.elf | find "kernel.elf"

echo.
echo Next steps:
echo   1. Run in QEMU: scripts\run-qemu-x86.bat
echo   2. Or use: "C:\Program Files\qemu\qemu-system-i386.exe" -kernel build\x86\bin\kernel.elf
echo.
pause
