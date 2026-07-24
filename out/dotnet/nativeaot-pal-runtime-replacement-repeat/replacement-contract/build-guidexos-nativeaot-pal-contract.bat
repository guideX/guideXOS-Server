@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /std:c++17 /TP /c /MT /GS- /GR- /EHs-c- /Zl /Oi /O2 /Brepro /DGUIDEXOS_NATIVEAOT_PAL_CONTRACT /Fo:"D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT\out\dotnet\nativeaot-pal-runtime-replacement-repeat\replacement-contract\guidexos_nativeaot_pal_contract.obj" "D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT\tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_pal_contract.cpp"
exit /b %errorlevel%
