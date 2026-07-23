@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /std:c++17 /TP /c /GS- /GR- /EHs-c- /Zl /Oi /O2 /Brepro /DGXOS_BARE_METAL /DGXOS_TRUE_VIRTUAL_MEMORY /D_FEATURE_NATIVEAOT /DNATIVEAOT /DTARGET_AMD64 /DHOST_AMD64 /DHOST_64BIT /D_WIN64 /I"D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT\out\dotnet\gc-feasibility-baseline\nativeaot-runtime\src\coreclr\gc" /I"D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT\out\dotnet\gc-feasibility-baseline\nativeaot-runtime\src\coreclr\gc\env" /I"D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT\out\dotnet\gc-feasibility-baseline\nativeaot-runtime\src\coreclr\nativeaot\Runtime" /I"D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT\out\dotnet\gc-feasibility-baseline\nativeaot-runtime\src\native" /Fo:"D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT\out\dotnet\gc-platform-object-replacement\rebuilt\guidexos_gcenv.obj" "D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT\tools\dotnet\runtime-pack\src\gcenv\guidexos_gcenv.cpp"
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /std:c++17 /TP /c /GS- /GR- /EHs-c- /Zl /Oi /O2 /Brepro /DGXOS_BARE_METAL /Fo:"D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT\out\dotnet\gc-platform-object-replacement\rebuilt\guidexos_gc_platform_services.obj" "D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT\tools\dotnet\runtime-pack\src\gcenv\guidexos_gc_platform_services.cpp"
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /std:c++17 /TP /c /GS- /GR- /EHs-c- /Zl /Oi /O2 /Brepro /DGXOS_BARE_METAL /DGXOS_TRUE_VIRTUAL_MEMORY /I"D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT\kernel\core\include" /Fo:"D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT\out\dotnet\gc-platform-object-replacement\rebuilt\guidexos_virtual_memory_region.obj" "D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT\runtime\memory\guidexos_virtual_memory_region_baremetal.cpp"
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /std:c++17 /TP /c /GS- /GR- /EHs-c- /Zl /Oi /O2 /Brepro /DGXOS_BARE_METAL /DGXOS_TRUE_VIRTUAL_MEMORY /D_FEATURE_NATIVEAOT /DNATIVEAOT /DTARGET_AMD64 /DHOST_AMD64 /DHOST_64BIT /D_WIN64 /Fo:"D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT\out\dotnet\gc-platform-object-replacement\rebuilt\guidexos_nativeaot_virtual_memory_adapter.obj" "D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT\tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_virtual_memory_adapter.cpp"
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /std:c++17 /TP /c /GS- /GR- /EHs-c- /Zl /Oi /O2 /Brepro /DGXOS_BARE_METAL /Fo:"D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT\out\dotnet\gc-platform-object-replacement\rebuilt\guidexos_event.obj" "D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT\runtime\synchronization\guidexos_event_baremetal.cpp"
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /std:c++17 /TP /c /GS- /GR- /EHs-c- /Zl /Oi /O2 /Brepro /DGXOS_BARE_METAL /DGXOS_TRUE_VIRTUAL_MEMORY /D_FEATURE_NATIVEAOT /DNATIVEAOT /DTARGET_AMD64 /DHOST_AMD64 /DHOST_64BIT /D_WIN64 /Fo:"D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT\out\dotnet\gc-platform-object-replacement\rebuilt\guidexos_nativeaot_event_adapter.obj" "D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT\tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_event_adapter.cpp"
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /std:c++17 /TP /c /GS- /GR- /EHs-c- /Zl /Oi /O2 /Brepro /DGXOS_BARE_METAL /Fo:"D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT\out\dotnet\gc-platform-object-replacement\rebuilt\guidexos_mutex.obj" "D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT\runtime\synchronization\guidexos_mutex_baremetal.cpp"
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /std:c++17 /TP /c /GS- /GR- /EHs-c- /Zl /Oi /O2 /Brepro /DGXOS_BARE_METAL /DGXOS_TRUE_VIRTUAL_MEMORY /D_FEATURE_NATIVEAOT /DNATIVEAOT /DTARGET_AMD64 /DHOST_AMD64 /DHOST_64BIT /D_WIN64 /Fo:"D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT\out\dotnet\gc-platform-object-replacement\rebuilt\guidexos_nativeaot_critical_section_adapter.obj" "D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT\tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_critical_section_adapter.cpp"
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /std:c++17 /TP /c /GS- /GR- /EHs-c- /Zl /Oi /O2 /Brepro /DGXOS_BARE_METAL /Fo:"D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT\out\dotnet\gc-platform-object-replacement\rebuilt\guidexos_native_thread.obj" "D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT\runtime\thread\guidexos_native_thread_baremetal.cpp"
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /std:c++17 /TP /c /GS- /GR- /EHs-c- /Zl /Oi /O2 /Brepro /DGXOS_BARE_METAL /Fo:"D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT\out\dotnet\gc-platform-object-replacement\rebuilt\guidexos_local_storage.obj" "D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT\runtime\local_storage\guidexos_local_storage.cpp"
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /std:c++17 /TP /c /GS- /GR- /EHs-c- /Zl /Oi /O2 /Brepro /DGXOS_BARE_METAL /Fo:"D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT\out\dotnet\gc-platform-object-replacement\rebuilt\guidexos_nativeaot_fls_adapter.obj" "D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT\tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_fls_adapter.cpp"
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /std:c++17 /TP /c /GS- /GR- /EHs-c- /Zl /Oi /O2 /Brepro /DGXOS_BARE_METAL /Fo:"D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT\out\dotnet\gc-platform-object-replacement\rebuilt\guidexos_nativeaot_thread_adapter.obj" "D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT\tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_thread_adapter.cpp"
if errorlevel 1 exit /b %errorlevel%
exit /b 0
