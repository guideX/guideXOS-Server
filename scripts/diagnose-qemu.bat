@echo off
REM Diagnostic script to check QEMU boot issue
REM
REM Copyright (c) 2024 guideX

echo ========================================
echo QEMU Boot Diagnostic
echo ========================================
echo.

REM Change to project root
pushd %~dp0
cd ..

echo [1/5] Checking kernel file...
set KERNEL_PATH=kernel\build\x86\bin\kernel.elf
if exist "%KERNEL_PATH%" (
    echo [OK] Kernel found at: %KERNEL_PATH%
    dir "%KERNEL_PATH%" | find "kernel.elf"
) else (
    echo [ERROR] Kernel NOT found at: %KERNEL_PATH%
    echo.
    echo Please build the kernel first:
    echo   cd kernel
    echo   build-x86.bat
    popd
    pause
    exit /b 1
)

echo.
echo [2/5] Checking QEMU installation...
set QEMU_PATH=C:\Program Files\qemu\qemu-system-i386.exe
if exist "%QEMU_PATH%" (
    echo [OK] QEMU found at: %QEMU_PATH%
    "%QEMU_PATH%" --version | find "version"
) else (
    echo [ERROR] QEMU NOT found at: %QEMU_PATH%
    echo Please install QEMU or update the path
    popd
    pause
    exit /b 1
)

echo.
echo [3/5] Checking kernel file size...
for %%A in ("%KERNEL_PATH%") do (
    echo File size: %%~zA bytes
    if %%~zA LSS 1000 (
        echo [WARNING] Kernel seems very small - might be corrupted
    ) else (
        echo [OK] Kernel size looks reasonable
    )
)

echo.
echo [4/5] Checking kernel format...
REM Use file command if available, otherwise just note
echo Note: Kernel should be ELF format
echo You can verify with: file kernel.elf (on Linux/Mac)

echo.
echo [5/5] Testing QEMU launch with verbose output...
echo.
echo Command that will be executed:
echo "%QEMU_PATH%" -kernel "%KERNEL_PATH%" -m 128M -serial stdio -display gtk
echo.
echo Press any key to launch QEMU...
pause >nul

echo.
echo Launching QEMU with kernel...
echo If you see "Booting from ROM..." the kernel is not being loaded correctly.
echo.

"%QEMU_PATH%" ^
    -kernel "%KERNEL_PATH%" ^
    -m 128M ^
    -serial stdio ^
    -display gtk ^
    -no-reboot ^
    -no-shutdown

echo.
echo QEMU exited.
popd
pause
