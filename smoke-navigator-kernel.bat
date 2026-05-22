@echo off
setlocal
powershell -ExecutionPolicy Bypass -File "%~dp0scripts\smoke-navigator-kernel.ps1" %*
exit /b %ERRORLEVEL%
