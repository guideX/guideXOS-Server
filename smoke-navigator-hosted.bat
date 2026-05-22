@echo off
setlocal
powershell -ExecutionPolicy Bypass -File "%~dp0scripts\smoke-navigator-hosted.ps1" %*
exit /b %ERRORLEVEL%
