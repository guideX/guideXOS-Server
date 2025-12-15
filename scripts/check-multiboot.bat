@echo off
REM Check if Multiboot magic is in the kernel
REM This requires xxd or hexdump tool

echo Checking kernel Multiboot header...
echo.

set KERNEL=kernel\build\x86\bin\kernel.elf

if not exist "%KERNEL%" (
    echo ERROR: Kernel not found!
    pause
    exit /b 1
)

echo Kernel file: %KERNEL%
for %%A in ("%KERNEL%") do echo File size: %%~zA bytes
echo.

echo Checking for Multiboot magic (0x1BADB002)...
echo.
echo To manually check, use a hex editor and look for:
echo   02 B0 AD 1B  (little-endian bytes)
echo.
echo This should appear in the first 8192 bytes of the file.
echo.

REM Try to use PowerShell to find it
powershell -Command "$bytes = [IO.File]::ReadAllBytes('%KERNEL%'); $found = $false; for ($i = 0; $i -lt [Math]::Min(8192, $bytes.Length - 3); $i++) { if ($bytes[$i] -eq 0x02 -and $bytes[$i+1] -eq 0xB0 -and $bytes[$i+2] -eq 0xAD -and $bytes[$i+3] -eq 0x1B) { Write-Host '[OK] Multiboot magic found at offset' $i; $found = $true; break; } }; if (-not $found) { Write-Host '[ERROR] Multiboot magic NOT found!'; Write-Host 'The kernel will not boot in QEMU.'; }"

echo.
pause
