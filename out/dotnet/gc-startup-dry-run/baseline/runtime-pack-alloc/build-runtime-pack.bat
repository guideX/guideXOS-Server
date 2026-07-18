@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /TP /c /GS- /GR- /EHs-c- /Zl /Oi /O2 /Brepro /DGUIDEXOS_NATIVEAOT_MANAGED_ALLOCATION /DGUIDEXOS_MANAGED_HEAP_BYTES=65536  /Fo:"D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT\out\dotnet\gc-startup-dry-run\baseline\runtime-pack-alloc\guidexos_nativeaot_platform.obj" "D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT\tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_platform.cpp"
exit /b %errorlevel%
