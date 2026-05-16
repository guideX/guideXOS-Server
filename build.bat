@echo off
REM Build script for guideXOS Server
REM Copyright (c) 2024 guideX

echo Building guideXOS Server...

REM Compiler settings
set CXX=g++
set CXXFLAGS=-std=c++17 -Wall -O2 -I.
set LDFLAGS=-lws2_32 -lgdi32 -luser32 -lmsimg32

REM Source files (exclude kernel)
set SOURCES=^
allocator.cpp ^
app_launch_resolver.cpp ^
app_manifest.cpp ^
app_manifest_loader.cpp ^
app_manifest_validator.cpp ^
app_registry.cpp ^
calculator.cpp ^
clock.cpp ^
compositor.cpp ^
console_service.cpp ^
console_window.cpp ^
desktop_service.cpp ^
desktop_state.cpp ^
elf_validator.cpp ^
executable_memory.cpp ^
file_explorer.cpp ^
fs.cpp ^
gxapp_container.cpp ^
gxapp_loader.cpp ^
gxm_loader.cpp ^
image.cpp ^
image_renderer.cpp ^
image_viewer.cpp ^
icons.cpp ^
ipc_bus.cpp ^
logger.cpp ^
native_app_process_table.cpp ^
native_app_runtime.cpp ^
native_elf_executor.cpp ^
native_elf_image_loader.cpp ^
native_elf_launch_pipeline.cpp ^
notepad.cpp ^
package_manager.cpp ^
process.cpp ^
png_loader.cpp ^
save_changes_dialog.cpp ^
save_dialog.cpp ^
scheduler.cpp ^
server.cpp ^
task_manager.cpp ^
universal_app_loader.cpp ^
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
