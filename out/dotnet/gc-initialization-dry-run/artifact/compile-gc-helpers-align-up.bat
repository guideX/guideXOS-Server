@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cl.exe /nologo /std:c++17 /TP /c /MT /GS- /GR- /EHs-c- /Zl /Oi /O2 /Zc:inline /Brepro /Fo:"D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT\out\dotnet\gc-initialization-dry-run\artifact\gc-helpers-align-up.obj" "D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT\out\dotnet\gc-initialization-dry-run\artifact\gc-helpers-align-up.cpp"
exit /b %errorlevel%
