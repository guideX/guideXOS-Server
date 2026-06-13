@echo off
REM Build script for guideXOS Server
REM Copyright (c) 2024 guideX

echo Building guideXOS Server...

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
set CXXFLAGS=-std=c++17 -Wall -O2 -iquote . -Ithird_party/mbedtls/include -Ithird_party/mbedtls/tf-psa-crypto/include -Ithird_party/mbedtls/tf-psa-crypto/drivers/builtin/include -Ithird_party/mbedtls/tf-psa-crypto/drivers/builtin/src -Ithird_party/mbedtls/tf-psa-crypto/dispatch -Ithird_party/mbedtls/tf-psa-crypto/extras -Ithird_party/mbedtls/tf-psa-crypto/platform -Ithird_party/mbedtls/tf-psa-crypto/utilities -DMBEDTLS_CONFIG_FILE=\"third_party/mbedtls/guidexos/mbedtls_config.h\" -DTF_PSA_CRYPTO_CONFIG_FILE=\"third_party/mbedtls/guidexos/crypto_config.h\" -DGXOS_APPMODEL_TYPED_DISPATCH_ENABLED
set LDFLAGS=-lws2_32 -lsecur32 -lcrypt32 -lbcrypt -lgdi32 -luser32 -lmsimg32

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
display_options.cpp ^
control_panel.cpp ^
elf_validator.cpp ^
executable_memory.cpp ^
file_explorer.cpp ^
file_icon_provider.cpp ^
firewall.cpp ^
focus_indicator.cpp ^
fs.cpp ^
gxapp_container.cpp ^
gxapp_loader.cpp ^
gxm_loader.cpp ^
gxos_tls_foundation.cpp ^
gxos_tls_prerequisites.cpp ^
image.cpp ^
image_adapter.cpp ^
image_renderer.cpp ^
image_viewer.cpp ^
icon_theme_manager.cpp ^
icons.cpp ^
ipc_bus.cpp ^
guide_web_html_parser.cpp ^
guide_web_http.cpp ^
kernel/core/architecture_detector.cpp ^
kernel/core/system_font.cpp ^
lifecycle.cpp ^
logger.cpp ^
message_box.cpp ^
module_manager.cpp ^
navigator.cpp ^
navigator_file_io.cpp ^
navigator_html_parser.cpp ^
native_app_debug_log.cpp ^
native_app_process_table.cpp ^
native_app_runtime.cpp ^
native_elf_executor.cpp ^
native_elf_image_loader.cpp ^
native_elf_launch_pipeline.cpp ^
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
trash.cpp ^
universal_app_loader.cpp ^
video_backend.cpp ^
vfs.cpp ^
vnc_server.cpp ^
wallpaper_registry.cpp ^
welcome.cpp ^
workspace_manager.cpp

REM Output
set OUTPUT=out\appmodel-typed-dispatch-flags\guideXOSServer.enabled-only.exe

echo Compiling...
"%CXX%" %CXXFLAGS% %SOURCES% %LDFLAGS% -o %OUTPUT%

if %ERRORLEVEL% EQU 0 (
    echo Build successful: %OUTPUT%
    echo Run with: %OUTPUT%
) else (
    echo Build failed!
    exit /b 1
)

