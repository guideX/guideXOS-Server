@echo off
setlocal

REM Keep this launcher build entry point on the canonical kernel Makefile.
cd /d "%~dp0"
if errorlevel 1 (
    echo [ERROR] Cannot enter the kernel directory: %~dp0
    exit /b 1
)

REM Reuse the existing local toolchain discovery without changing the host.
set "TOOLCHAIN_PATH="
set "NASM_PATH="
if exist "D:\bkup\elfbin\bin\i686-elf-g++.exe" set "TOOLCHAIN_PATH=D:\bkup\elfbin\bin"
if exist "C:\i686-elf\bin\i686-elf-g++.exe" set "TOOLCHAIN_PATH=C:\i686-elf\bin"
if exist "C:\Program Files\i686-elf-tools\bin\i686-elf-g++.exe" set "TOOLCHAIN_PATH=C:\Program Files\i686-elf-tools\bin"
if defined TOOLCHAIN_PATH set "PATH=%TOOLCHAIN_PATH%;%PATH%"

if exist "D:\bkup\elfbin\bin\nasm.exe" set "NASM_PATH=D:\bkup\elfbin\bin"
if exist "C:\Program Files\NASM\nasm.exe" set "NASM_PATH=C:\Program Files\NASM"
if exist "C:\nasm\nasm.exe" set "NASM_PATH=C:\nasm"
if defined NASM_PATH if /I not "%NASM_PATH%"=="%TOOLCHAIN_PATH%" set "PATH=%NASM_PATH%;%PATH%"

where i686-elf-g++.exe >nul 2>&1
if errorlevel 1 (
    echo [ERROR] i686-elf-g++ was not found in PATH.
    pause
    exit /b 1
)
where nasm.exe >nul 2>&1
if errorlevel 1 (
    echo [ERROR] NASM was not found in PATH.
    pause
    exit /b 1
)

set "MAKE_COMMAND=make.exe"
where make.exe >nul 2>&1
if errorlevel 1 set "MAKE_COMMAND=mingw32-make.exe"
where %MAKE_COMMAND% >nul 2>&1
if errorlevel 1 (
    echo [ERROR] GNU Make was not found. Install or add make.exe or mingw32-make.exe to PATH.
    pause
    exit /b 1
)

echo ==========================================
echo Building guideXOS Kernel (x86) via kernel\Makefile
echo ==========================================
echo Compiler: i686-elf-g++
echo Target: i686-elf, freestanding
%MAKE_COMMAND% ARCH=x86
set "BUILD_EXIT=%ERRORLEVEL%"
if not "%BUILD_EXIT%"=="0" (
    echo [ERROR] Canonical x86 kernel build failed with exit code %BUILD_EXIT%.
    pause
    exit /b %BUILD_EXIT%
)
if not exist "build\x86\bin\kernel.elf" (
    echo [ERROR] The x86 build completed without producing build\x86\bin\kernel.elf.
    pause
    exit /b 1
)

echo.
echo ==========================================
echo BUILD SUCCESSFUL!
echo ==========================================
echo Kernel: build\x86\bin\kernel.elf
dir "build\x86\bin\kernel.elf" | find "kernel.elf"
echo.
pause
exit /b 0
