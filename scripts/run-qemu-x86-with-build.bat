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
echo [Step 2/2] Requesting QAC admission and launching QEMU...
echo.

echo Kernel: build\x86\bin\kernel.elf
echo QAC will launch QEMU as its direct child after development admission.
echo ==========================================
echo.

pwsh.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0run-qemu-x86-qac.ps1"
exit /b %ERRORLEVEL%
