@echo off
@echo off
REM
REM Windows Build Script for guideXOS Kernel (x86)
REM Alternative to GNU Make for Windows users
REM Run this from the kernel directory!
REM
REM Copyright (c) 2024 guideX
REM

setlocal enabledelayedexpansion

REM ============================================
REM Toolchain Path Configuration
REM ============================================

REM Try to auto-detect toolchain in common locations
set "TOOLCHAIN_PATH="
set "NASM_PATH="

REM Check common ELF toolchain locations
if exist "D:\bkup\elfbin\bin\i686-elf-gcc.exe" (
    set "TOOLCHAIN_PATH=D:\bkup\elfbin\bin"
    echo [INFO] Found ELF toolchain at: D:\bkup\elfbin\bin
)
if exist "C:\i686-elf\bin\i686-elf-gcc.exe" (
    set "TOOLCHAIN_PATH=C:\i686-elf\bin"
    echo [INFO] Found ELF toolchain at: C:\i686-elf\bin
)
if exist "C:\Program Files\i686-elf-tools\bin\i686-elf-gcc.exe" (
    set "TOOLCHAIN_PATH=C:\Program Files\i686-elf-tools\bin"
    echo [INFO] Found ELF toolchain at: C:\Program Files\i686-elf-tools\bin
)

REM Check common NASM locations
if exist "D:\bkup\elfbin\bin\nasm.exe" (
    set "NASM_PATH=D:\bkup\elfbin\bin"
    echo [INFO] Found NASM at: D:\bkup\elfbin\bin
)
if exist "C:\Program Files\NASM\nasm.exe" (
    set "NASM_PATH=C:\Program Files\NASM"
    echo [INFO] Found NASM at: C:\Program Files\NASM
)
if exist "C:\nasm\nasm.exe" (
    set "NASM_PATH=C:\nasm"
    echo [INFO] Found NASM at: C:\nasm
)

REM Add to PATH if found (using quotes to handle spaces and special characters)
if defined TOOLCHAIN_PATH (
    set "PATH=%TOOLCHAIN_PATH%;%PATH%"
    echo [OK] Added toolchain to PATH
)
if defined NASM_PATH (
    if not "%NASM_PATH%"=="%TOOLCHAIN_PATH%" (
        set "PATH=%NASM_PATH%;%PATH%"
        echo [OK] Added NASM to PATH
    )
)

echo.

REM ============================================
REM Configuration
REM ============================================

set "ARCH=x86"
set "BUILD_DIR=build\%ARCH%"
set "OBJ_DIR=%BUILD_DIR%\obj"
set "BIN_DIR=%BUILD_DIR%\bin"
set "KERNEL=%BIN_DIR%\kernel.elf"

REM Toolchain
set "CXX=i686-elf-g++"
set "AS=nasm"
set "LD=i686-elf-ld"

REM Compiler flags (using relative paths from kernel directory)
set "CFLAGS=-std=c++14 -ffreestanding -O2 -Wall -Wextra -fno-exceptions -fno-rtti"
set "CFLAGS=%CFLAGS% -nostdlib -nostdinc++ -fno-builtin"
set "CFLAGS=%CFLAGS% -m32 -march=i686"
set "CFLAGS=%CFLAGS% -Icore/include -Iarch/%ARCH%/include"

REM Assembler flags
set "ASFLAGS=-f elf32"

REM Linker flags (using relative path)
set "LDFLAGS=-T arch/%ARCH%/linker.ld -nostdlib"

echo ==========================================
echo Building guideXOS Kernel (x86)
echo ==========================================
echo.

REM ============================================
REM Check we're in the right directory
REM ============================================

if not exist "arch\x86\boot.asm" (
    echo [ERROR] Cannot find arch\x86\boot.asm
    echo.
    echo This script must be run from the kernel directory!
    echo.
    echo Current directory: %CD%
    echo.
    echo Please run:
    echo   cd kernel
    echo   build-x86.bat
    echo.
    pause
    exit /b 1
)

echo [OK] Found source files
echo.

REM ============================================
REM Verify Toolchain
REM ============================================

echo Checking toolchain...
echo.

where %CXX% >nul 2>&1
if %errorlevel% neq 0 (
    echo [ERROR] %CXX% not found in PATH
    echo.
    echo Toolchain not found! Please do one of the following:
    echo.
    echo Option 1: Install to D:\bkup\elfbin\bin
    echo   - Download from: https://github.com/lordmilko/i686-elf-tools/releases
    echo   - Extract so that i686-elf-gcc.exe is at: D:\bkup\elfbin\bin\i686-elf-gcc.exe
    echo.
    echo Option 2: Add to PATH manually
    echo   - Open System Properties ^> Environment Variables
    echo   - Add your toolchain bin directory to PATH
    echo.
    echo Option 3: Edit this script
    echo   - Find the "Toolchain Path Configuration" section
    echo   - Add your custom path to the detection logic
    echo.
    pause
    exit /b 1
) else (
    %CXX% --version 2>nul | findstr /C:"gcc" >nul
    if %errorlevel%==0 (
        %CXX% --version | findstr /C:"gcc"
        echo [OK] Cross-compiler found
    ) else (
        echo [OK] Cross-compiler found
    )
)

echo.

where %AS% >nul 2>&1
if %errorlevel% neq 0 (
    echo [ERROR] %AS% not found in PATH
    echo.
    echo NASM not found! Please do one of the following:
    echo.
    echo Option 1: Download NASM
    echo   - Download from: https://www.nasm.us/pub/nasm/releasebuilds/
    echo   - Extract to D:\bkup\elfbin\bin or C:\nasm
    echo.
    echo Option 2: Install via Chocolatey
    echo   - Run: choco install nasm
    echo.
    pause
    exit /b 1
) else (
    %AS% --version 2>nul | findstr /C:"NASM" >nul
    if %errorlevel%==0 (
        %AS% --version | findstr /C:"NASM"
        echo [OK] Assembler found
    ) else (
        echo [OK] Assembler found
    )
)

echo.

REM ============================================
REM Build Process
REM ============================================

REM Create directories
echo Creating build directories...
if not exist "%OBJ_DIR%\core" mkdir "%OBJ_DIR%\core"
if not exist "%OBJ_DIR%\arch" mkdir "%OBJ_DIR%\arch"
if not exist "%BIN_DIR%" mkdir "%BIN_DIR%"
echo [OK] Directories created
echo.

REM Assemble boot.asm
echo Assembling boot.asm...
%AS% %ASFLAGS% arch\%ARCH%\boot.asm -o "%OBJ_DIR%\arch\boot.o"
if %errorlevel% neq 0 (
    echo [ERROR] Failed to assemble boot.asm
    pause
    exit /b 1
)
echo [OK] boot.o created

REM Compile x86 arch.cpp
echo Compiling x86 arch.cpp...
%CXX% %CFLAGS% -c arch\%ARCH%\arch.cpp -o "%OBJ_DIR%\arch\arch.o"
if %errorlevel% neq 0 (
    echo [ERROR] Failed to compile x86 arch.cpp
    pause
    exit /b 1
)
echo [OK] arch.o (x86) created

REM Compile all core .cpp files
echo.
echo Compiling core source files...
set "CORE_OBJS="
set "COMPILE_ERRORS=0"

for %%f in (core\*.cpp) do (
    set "SRC=%%f"
    set "OBJ=%OBJ_DIR%\core\%%~nf.o"
    echo   Compiling %%~nxf...
    %CXX% %CFLAGS% -c "%%f" -o "%OBJ_DIR%\core\%%~nf.o"
    if !errorlevel! neq 0 (
        echo   [ERROR] Failed to compile %%~nxf
        set "COMPILE_ERRORS=1"
    )
    set "CORE_OBJS=!CORE_OBJS! "%OBJ_DIR%\core\%%~nf.o""
)

if %COMPILE_ERRORS%==1 (
    echo.
    echo [ERROR] One or more files failed to compile
    pause
    exit /b 1
)
echo [OK] All core files compiled
echo.

REM Link kernel
echo Linking kernel...
%LD% %LDFLAGS% -o "%KERNEL%" "%OBJ_DIR%\arch\boot.o" "%OBJ_DIR%\arch\arch.o" %CORE_OBJS%

if %errorlevel% neq 0 (
    echo [ERROR] Failed to link kernel
    pause
    exit /b 1
)
echo [OK] kernel.elf created

echo.
echo ==========================================
echo BUILD SUCCESSFUL!
echo ==========================================
echo.
echo Kernel: %KERNEL%
dir "%KERNEL%" | find "kernel.elf"
echo.
echo Toolchain used:
where %CXX%
where %AS%
where %LD%
echo.
echo Next steps:
echo   Run in QEMU: ..\scripts\run-qemu-x86.bat
echo.
pause
