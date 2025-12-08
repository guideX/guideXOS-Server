@echo off
REM Quick diagnostic for toolchain issues
REM Run this and share the output if you need help

echo ==========================================
echo Quick Toolchain Diagnostic
echo ==========================================
echo.

echo [1] Checking D:\bkup\elfbin\bin...
if exist "D:\bkup\elfbin\bin" (
    echo Directory exists: D:\bkup\elfbin\bin
    echo.
    echo Files:
    dir "D:\bkup\elfbin\bin\*.exe" /b 2>nul
    if errorlevel 1 (
        echo No .exe files found!
    )
) else (
    echo Directory NOT found: D:\bkup\elfbin\bin
)

echo.
echo [2] Checking D:\bkup\elfbin...
if exist "D:\bkup\elfbin" (
    echo Directory exists: D:\bkup\elfbin
    echo.
    echo Files:
    dir "D:\bkup\elfbin\*.exe" /b 2>nul
    if errorlevel 1 (
        echo No .exe files in root
    )
) else (
    echo Directory NOT found: D:\bkup\elfbin
)

echo.
echo [3] Checking current PATH...
echo %PATH%

echo.
echo [4] Testing if tools are accessible...
echo.

where i686-elf-gcc 2>nul
if %errorlevel%==0 (
    echo [OK] i686-elf-gcc found in PATH
) else (
    echo [NOT FOUND] i686-elf-gcc not in PATH
)

where i686-elf-g++ 2>nul
if %errorlevel%==0 (
    echo [OK] i686-elf-g++ found in PATH
) else (
    echo [NOT FOUND] i686-elf-g++ not in PATH
)

where nasm 2>nul
if %errorlevel%==0 (
    echo [OK] nasm found in PATH
) else (
    echo [NOT FOUND] nasm not in PATH
)

echo.
echo [5] Trying direct execution...
echo.

if exist "D:\bkup\elfbin\bin\i686-elf-gcc.exe" (
    echo Testing: D:\bkup\elfbin\bin\i686-elf-gcc.exe
    "D:\bkup\elfbin\bin\i686-elf-gcc.exe" --version 2>nul
    if %errorlevel%==0 (
        echo [OK] Direct execution works!
    ) else (
        echo [ERROR] Direct execution failed - might be missing DLLs
    )
) else (
    echo File not found: D:\bkup\elfbin\bin\i686-elf-gcc.exe
)

echo.
echo ==========================================
echo Diagnosis Complete
echo ==========================================
echo.
echo If you see errors above, run:
echo   setup-toolchain.bat
echo.
echo Or see: TOOLCHAIN-SETUP.md
echo.

pause
