@echo off
setlocal

set "MINGW_BIN=C:\mingw64\bin"
set "EXE=%~dp0guideXOSServer.experimental.exe"

if not exist "%EXE%" (
    echo ERROR: Missing experimental hosted runtime executable: %EXE%
    echo Run build-native-experimental.bat first.
    exit /b 1
)

if exist "%MINGW_BIN%" (
    set "PATH=%MINGW_BIN%;%PATH%"
) else (
    echo WARNING: %MINGW_BIN% was not found. If the runtime exits with 0xC0000135, install MinGW or copy required runtime DLLs next to the executable.
)

for %%D in (libgcc_s_seh-1.dll libstdc++-6.dll libwinpthread-1.dll) do (
    if not exist "%~dp0%%D" if not exist "%MINGW_BIN%\%%D" (
        echo WARNING: Missing likely MinGW runtime dependency: %%D
    )
)

"%EXE%" %*
set "RC=%ERRORLEVEL%"
if "%RC%"=="-1073741515" (
    echo ERROR: Experimental hosted runtime failed to start with 0xC0000135. A runtime DLL dependency is missing.
    echo Checked %MINGW_BIN% and the repository directory for libgcc_s_seh-1.dll, libstdc++-6.dll, and libwinpthread-1.dll.
)
exit /b %RC%
