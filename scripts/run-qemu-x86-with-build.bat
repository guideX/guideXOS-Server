@echo off
REM
REM Build and Run Script for Visual Studio
REM Builds the kernel and launches QEMU
REM
REM Copyright (c) 2024 guideX
REM

setlocal

echo ==========================================
echo Visual Studio - Build and Run Kernel
echo ==========================================
echo.

REM Navigate to kernel directory
cd /d "%~dp0..\kernel"

echo [Step 1/2] Building kernel...
echo.

call build-x86.bat

if %errorlevel% neq 0 (
    echo.
    echo [ERROR] Build failed! Cannot launch QEMU.
    echo.
    pause
    exit /b 1
)

echo.
echo [Step 2/2] Launching QEMU...
echo.

REM Check if QEMU is installed
set "QEMU_PATH=C:\Program Files\qemu\qemu-system-i386.exe"

if not exist "%QEMU_PATH%" (
    echo [ERROR] QEMU not found at: %QEMU_PATH%
    echo.
    echo Please install QEMU from: https://www.qemu.org/download/#windows
    echo.
    pause
    exit /b 1
)

REM Check if kernel was built
if not exist "build\x86\bin\kernel.elf" (
    echo [ERROR] Kernel not found at: build\x86\bin\kernel.elf
    echo.
    pause
    exit /b 1
)

echo Starting QEMU...
echo Kernel: build\x86\bin\kernel.elf
echo.
echo Press Ctrl+C to stop QEMU
echo ==========================================
echo.

REM Launch QEMU
"%QEMU_PATH%" ^
    -kernel "build\x86\bin\kernel.elf" ^
    -m 128M ^
    -serial stdio ^
    -display gtk ^
    -no-reboot ^
    -no-shutdown

echo.
echo QEMU closed.
echo.
