@echo off
REM
REM guideXOS Experimental QEMU Multimonitor Probe Launcher
REM
REM This is a thin wrapper around run-qemu-display-probe.bat that
REM selects the multi-output-capable virtio-gpu probe mode.
REM
REM Copyright (c) 2024 guideX
REM

setlocal
call "%~dp0run-qemu-display-probe.bat" virtio-gpu
exit /b %ERRORLEVEL%
