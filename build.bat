@echo off
REM Build script for guideXOS Server
REM Copyright (c) 2024 guideX

echo Building guideXOS Server...

REM Compiler settings
set CXX=g++
set CXXFLAGS=-std=c++14 -Wall -O2 -I.
set LDFLAGS=-lws2_32 -lgdi32 -luser32

REM Source files (exclude kernel)
set SOURCES=^
allocator.cpp ^
calculator.cpp ^
clock.cpp ^
compositor.cpp ^
console_service.cpp ^
console_window.cpp ^
desktop_service.cpp ^
desktop_state.cpp ^
file_explorer.cpp ^
fs.cpp ^
gxm_loader.cpp ^
icons.cpp ^
ipc_bus.cpp ^
logger.cpp ^
notepad.cpp ^
process.cpp ^
save_changes_dialog.cpp ^
save_dialog.cpp ^
scheduler.cpp ^
server.cpp ^
task_manager.cpp ^
vfs.cpp ^
vnc_server.cpp ^
workspace_manager.cpp

REM Output
set OUTPUT=guideXOSServer.exe

echo Compiling...
%CXX% %CXXFLAGS% %SOURCES% %LDFLAGS% -o %OUTPUT%

if %ERRORLEVEL% EQU 0 (
    echo Build successful: %OUTPUT%
    echo Run with: %OUTPUT%
) else (
    echo Build failed!
    exit /b 1
)
