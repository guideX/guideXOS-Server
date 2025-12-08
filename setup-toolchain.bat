@echo off
REM
REM Toolchain Setup Verification Script
REM Helps diagnose and fix PATH issues with ELF cross-compiler
REM
REM Copyright (c) 2024 guideX
REM

echo ==========================================
echo guideXOS Toolchain Setup Checker
echo ==========================================
echo.

REM ============================================
REM Check for ELF toolchain in known locations
REM ============================================

echo [Step 1] Searching for i686-elf cross-compiler...
echo.

set FOUND_TOOLCHAIN=0

if exist "D:\bkup\elfbin\bin\i686-elf-gcc.exe" (
    echo [FOUND] D:\bkup\elfbin\bin\i686-elf-gcc.exe
    set TOOLCHAIN_DIR=D:\bkup\elfbin\bin
    set FOUND_TOOLCHAIN=1
)

if exist "D:\bkup\elfbin\i686-elf-gcc.exe" (
    echo [FOUND] D:\bkup\elfbin\i686-elf-gcc.exe
    set TOOLCHAIN_DIR=D:\bkup\elfbin
    set FOUND_TOOLCHAIN=1
)

if exist "C:\i686-elf\bin\i686-elf-gcc.exe" (
    echo [FOUND] C:\i686-elf\bin\i686-elf-gcc.exe
    set TOOLCHAIN_DIR=C:\i686-elf\bin
    set FOUND_TOOLCHAIN=1
)

if exist "C:\Program Files\i686-elf-tools\bin\i686-elf-gcc.exe" (
    echo [FOUND] C:\Program Files\i686-elf-tools\bin\i686-elf-gcc.exe
    set TOOLCHAIN_DIR=C:\Program Files\i686-elf-tools\bin
    set FOUND_TOOLCHAIN=1
)

if %FOUND_TOOLCHAIN%==0 (
    echo [NOT FOUND] i686-elf toolchain not detected in common locations
    echo.
    echo Please tell me where you extracted the toolchain:
    echo Example: D:\bkup\elfbin\bin
    echo.
    set /p TOOLCHAIN_DIR="Enter path to toolchain bin directory: "
    
    if exist "%TOOLCHAIN_DIR%\i686-elf-gcc.exe" (
        echo [OK] Found toolchain at: %TOOLCHAIN_DIR%
        set FOUND_TOOLCHAIN=1
    ) else (
        echo [ERROR] i686-elf-gcc.exe not found at: %TOOLCHAIN_DIR%
        echo.
        echo Please check that the directory contains:
        echo   - i686-elf-gcc.exe
        echo   - i686-elf-g++.exe
        echo   - i686-elf-ld.exe
        echo   - i686-elf-as.exe
        echo.
        pause
        exit /b 1
    )
)

echo.
echo [SUCCESS] Toolchain found at: %TOOLCHAIN_DIR%
echo.

REM ============================================
REM List toolchain files
REM ============================================

echo [Step 2] Checking toolchain files...
echo.

dir "%TOOLCHAIN_DIR%\i686-elf-*.exe" /b

echo.

REM ============================================
REM Check NASM
REM ============================================

echo [Step 3] Searching for NASM assembler...
echo.

set FOUND_NASM=0

if exist "D:\bkup\elfbin\bin\nasm.exe" (
    echo [FOUND] D:\bkup\elfbin\bin\nasm.exe
    set NASM_DIR=D:\bkup\elfbin\bin
    set FOUND_NASM=1
)

if exist "D:\bkup\elfbin\nasm.exe" (
    echo [FOUND] D:\bkup\elfbin\nasm.exe
    set NASM_DIR=D:\bkup\elfbin
    set FOUND_NASM=1
)

if exist "C:\Program Files\NASM\nasm.exe" (
    echo [FOUND] C:\Program Files\NASM\nasm.exe
    set NASM_DIR=C:\Program Files\NASM
    set FOUND_NASM=1
)

if exist "C:\nasm\nasm.exe" (
    echo [FOUND] C:\nasm\nasm.exe
    set NASM_DIR=C:\nasm
    set FOUND_NASM=1
)

where nasm >nul 2>&1
if %errorlevel%==0 (
    echo [FOUND] NASM is already in PATH
    where nasm
    set FOUND_NASM=1
)

if %FOUND_NASM%==0 (
    echo [NOT FOUND] NASM not detected
    echo.
    echo NASM is required for building the kernel.
    echo Download from: https://www.nasm.us/pub/nasm/releasebuilds/
    echo.
    echo You can:
    echo   1. Extract nasm.exe to %TOOLCHAIN_DIR%
    echo   2. Or install to C:\Program Files\NASM
    echo   3. Or install via Chocolatey: choco install nasm
    echo.
)

echo.

REM ============================================
REM Test toolchain
REM ============================================

echo [Step 4] Testing toolchain...
echo.

REM Temporarily add to PATH for testing
set PATH=%TOOLCHAIN_DIR%;%PATH%
if defined NASM_DIR set PATH=%NASM_DIR%;%PATH%

echo Testing i686-elf-gcc...
i686-elf-gcc --version >nul 2>&1
if %errorlevel%==0 (
    i686-elf-gcc --version | findstr /C:"gcc"
    echo [OK] i686-elf-gcc works
) else (
    echo [ERROR] i686-elf-gcc failed to run
)

echo.
echo Testing i686-elf-g++...
i686-elf-g++ --version >nul 2>&1
if %errorlevel%==0 (
    i686-elf-g++ --version | findstr /C:"g++"
    echo [OK] i686-elf-g++ works
) else (
    echo [ERROR] i686-elf-g++ failed to run
)

echo.
echo Testing i686-elf-ld...
i686-elf-ld --version >nul 2>&1
if %errorlevel%==0 (
    i686-elf-ld --version | findstr /C:"GNU ld"
    echo [OK] i686-elf-ld works
) else (
    echo [ERROR] i686-elf-ld failed to run
)

echo.
if %FOUND_NASM%==1 (
    echo Testing nasm...
    nasm --version >nul 2>&1
    if %errorlevel%==0 (
        nasm --version | findstr /C:"NASM"
        echo [OK] nasm works
    ) else (
        echo [ERROR] nasm failed to run
    )
)

echo.

REM ============================================
REM Generate PATH setup commands
REM ============================================

echo ==========================================
echo Setup Instructions
echo ==========================================
echo.

echo To use the toolchain, you need to add it to your PATH.
echo.
echo Option 1: Permanent (Recommended)
echo   1. Press Win + X, select "System"
echo   2. Click "Advanced system settings"
echo   3. Click "Environment Variables"
echo   4. Under "User variables", select "Path", click "Edit"
echo   5. Click "New" and add: %TOOLCHAIN_DIR%
if defined NASM_DIR if not "%NASM_DIR%"=="%TOOLCHAIN_DIR%" (
    echo   6. Click "New" and add: %NASM_DIR%
)
echo   7. Click OK on all dialogs
echo   8. Restart your command prompt
echo.

echo Option 2: Temporary (This session only)
echo   Run this command in your command prompt:
echo.
echo   set PATH=%TOOLCHAIN_DIR%;%%PATH%%
if defined NASM_DIR if not "%NASM_DIR%"=="%TOOLCHAIN_DIR%" (
    echo   set PATH=%NASM_DIR%;%%PATH%%
)
echo.

echo Option 3: Use build-x86.bat
echo   The build script now automatically detects the toolchain at:
echo   %TOOLCHAIN_DIR%
echo.
echo   Just run: kernel\build-x86.bat
echo.

REM ============================================
REM Create quick-setup batch file
REM ============================================

echo Creating quick-setup.bat...
echo @echo off > quick-setup.bat
echo REM Add toolchain to PATH for this session >> quick-setup.bat
echo set PATH=%TOOLCHAIN_DIR%;%%PATH%% >> quick-setup.bat
if defined NASM_DIR if not "%NASM_DIR%"=="%TOOLCHAIN_DIR%" (
    echo set PATH=%NASM_DIR%;%%PATH%% >> quick-setup.bat
)
echo echo Toolchain added to PATH for this session >> quick-setup.bat
echo echo You can now run: cd kernel ^&^& build-x86.bat >> quick-setup.bat

echo.
echo [OK] Created quick-setup.bat
echo      Run it before building: quick-setup.bat
echo.

echo ==========================================
echo Ready to Build!
echo ==========================================
echo.
echo You can now build the kernel using:
echo   cd kernel
echo   build-x86.bat
echo.
echo Or if PATH is not set, run quick-setup.bat first:
echo   quick-setup.bat
echo   cd kernel
echo   build-x86.bat
echo.

pause
