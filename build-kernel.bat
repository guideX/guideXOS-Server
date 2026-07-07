@echo off
REM
REM guideXOS Kernel Staging Wrapper
REM
REM Preserve the historical smoke-script entry point, but delegate to the
REM canonical full build script so ESP\kernel.elf, BOOTX64.EFI, and ramdisk.img
REM are produced together.
REM
REM Copyright (c) 2024 guideX
REM

setlocal
powershell -ExecutionPolicy Bypass -File "%~dp0build.ps1" %*
exit /b %ERRORLEVEL%
