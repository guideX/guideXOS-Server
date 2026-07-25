@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /TP /c /GS- /GR- /EHs-c- /Zl /Oi /O2 /Brepro  /Fo:"D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT\out\dotnet\pal-win64-qemu-bridge\managed-proofs-final\runtime-pack-hostlog\guidexos_nativeaot_platform.obj" "D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT\tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_platform.cpp"
exit /b %errorlevel%
