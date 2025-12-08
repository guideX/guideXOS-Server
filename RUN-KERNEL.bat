@echo off
REM Quick launcher for Visual Studio
REM Just double-click this file to build and run!

cd /d "%~dp0"

echo.
echo ========================================
echo guideXOS Kernel Quick Launcher
echo ========================================
echo.

REM Build the kernel
echo [1/2] Building C++ kernel...
cd kernel
call build-x86.bat
if %errorlevel% neq 0 (
    echo.
    echo Build failed!
    pause
    exit /b 1
)

cd ..

echo.
echo [2/2] Launching QEMU...
call scripts\run-qemu-x86-with-build.bat
