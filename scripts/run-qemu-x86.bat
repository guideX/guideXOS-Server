@echo off
setlocal

REM Launch the existing x86 kernel through QAC without building or replacing it.
REM The PowerShell runner resolves the artifact and preserves this launcher's QEMU settings.
set "RUNNER=%~dp0run-qemu-x86-prebuilt-qac.ps1"
if not exist "%RUNNER%" (
    echo Error: QAC prebuilt-kernel launcher not found at "%RUNNER%".
    exit /b 1
)

pwsh.exe -NoProfile -ExecutionPolicy Bypass -File "%RUNNER%" %*
exit /b %ERRORLEVEL%
