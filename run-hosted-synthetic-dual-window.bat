@echo off
setlocal

set "GXOS_SYNTHETIC_DUAL_MONITOR=1"
set "GXOS_SYNTHETIC_DUAL_WINDOW_OUTPUT=1"

call "%~dp0run-server.bat" %*
exit /b %ERRORLEVEL%
