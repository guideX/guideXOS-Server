@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cl.exe /nologo /std:c++17 /TP /c /GS- /GR- /EHs-c- /Zl /Oi /O2 /Brepro /Fo:"D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT\out\dotnet\gc-initialization-dry-run\artifact\guidexos_nativeaot_gc_startup_platform_contract.obj" /I"D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT\tools\dotnet\runtime-pack\src\platform" "D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT\tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_gc_startup_platform_contract.cpp"
exit /b %errorlevel%
