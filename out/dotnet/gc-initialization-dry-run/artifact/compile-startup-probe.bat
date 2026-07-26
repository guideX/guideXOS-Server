@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cl.exe /nologo /std:c++17 /TP /c /MT /GS- /GR- /EHs-c- /Zl /Oi /O2 /Zc:inline /Brepro /I"D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT\tools\dotnet\runtime-pack\src\platform" /I"D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT\out\dotnet\pal-runtime-active-replacement-build\locked-source\src\coreclr\nativeaot\Runtime" /Fo:"D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT\out\dotnet\gc-initialization-dry-run\artifact\guidexos_nativeaot_gc_startup_probe.obj" "D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT\tools\dotnet\runtime-pack\src\probes\guidexos_nativeaot_gc_startup_probe.cpp"
exit /b %errorlevel%
