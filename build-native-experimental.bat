@echo off
REM Experimental build script for guideXOS Server hosted Native ELF execution testing only
REM Copyright (c) 2024 guideX

echo WARNING: Experimental Native ELF execution is enabled.
echo This build is for local hosted runtime validation only.
echo Building guideXOS Server experimental Native ELF runtime...

REM Compiler settings
if defined CXX (
    echo Checking configured compiler: %CXX%
) else (
    if exist "C:\mingw64\bin\g++.exe" set "CXX=C:\mingw64\bin\g++.exe"
)

if not defined CXX (
    for %%G in (g++.exe g++) do (
        for /f "delims=" %%P in ('where %%G 2^>nul') do if not defined CXX set "CXX=%%P"
    )
)

if not defined CXX (
    echo ERROR: Could not find g++ compiler.
    echo Checked: existing CXX environment variable, C:\mingw64\bin\g++.exe, and g++ on PATH.
    exit /b 1
)

echo Using compiler: %CXX%
set CXXFLAGS=-std=c++17 -Wall -O2 -iquote . -DGX_ENABLE_EXPERIMENTAL_NATIVE_ELF_EXECUTION
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
disk_manager.cpp ^
control_panel.cpp ^
elf_validator.cpp ^
executable_memory.cpp ^
file_explorer.cpp ^
firewall.cpp ^
focus_indicator.cpp ^
fs.cpp ^
gxapp_container.cpp ^
gxapp_loader.cpp ^
gxm_loader.cpp ^
image.cpp ^
image_renderer.cpp ^
image_viewer.cpp ^
icons.cpp ^
ipc_bus.cpp ^
kernel/core/architecture_detector.cpp ^
lifecycle.cpp ^
logger.cpp ^
message_box.cpp ^
module_manager.cpp ^
native_app_process_table.cpp ^
native_app_runtime.cpp ^
native_elf_executor.cpp ^
native_elf_image_loader.cpp ^
native_elf_launch_pipeline.cpp ^
native_elf_trampoline_win64.cpp ^
notepad.cpp ^
notification_manager.cpp ^
onscreen_keyboard.cpp ^
open_dialog.cpp ^
package_manager.cpp ^
paint.cpp ^
process.cpp ^
png_loader.cpp ^
right_click_menu.cpp ^
save_changes_dialog.cpp ^
save_dialog.cpp ^
scheduler.cpp ^
server.cpp ^
shutdown_dialog.cpp ^
special_effects.cpp ^
system_tray.cpp ^
task_manager.cpp ^
universal_app_loader.cpp ^
video_backend.cpp ^
vfs.cpp ^
vnc_server.cpp ^
welcome.cpp ^
workspace_manager.cpp

REM Output
set OUTPUT=guideXOSServer.experimental.exe

echo Compiling experimental hosted runtime...
"%CXX%" %CXXFLAGS% %SOURCES% %LDFLAGS% -o %OUTPUT%

if %ERRORLEVEL% EQU 0 (
    echo Build successful: %OUTPUT%
    echo WARNING: Experimental Native ELF execution is enabled in %OUTPUT%.
    echo Run with: %OUTPUT%
) else (
    echo Build failed!
    exit /b 1
)
