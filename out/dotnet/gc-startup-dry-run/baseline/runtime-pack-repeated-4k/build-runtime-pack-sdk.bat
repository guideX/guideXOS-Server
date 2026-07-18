@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b %errorlevel%
lib.exe /nologo /OUT:"D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT\out\dotnet\gc-startup-dry-run\baseline\runtime-pack-repeated-4k\sdk\Runtime.WorkstationGC.lib" "C:\Users\guideX\.nuget\packages\runtime.win-x64.microsoft.dotnet.ilcompiler\9.0.0\sdk\Runtime.WorkstationGC.lib" /REMOVE:"nativeaot\Runtime\Full\CMakeFiles\Runtime.WorkstationGC.dir\__\EHHelpers.cpp.obj" /REMOVE:"nativeaot\Runtime\Full\CMakeFiles\Runtime.WorkstationGC.dir\__\thread.cpp.obj" /REMOVE:"nativeaot\Runtime\Full\CMakeFiles\Runtime.WorkstationGC.dir\__\amd64\AllocFast.asm.obj" "D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT\out\dotnet\gc-startup-dry-run\baseline\runtime-pack-repeated-4k\thread.cpp.obj.renamed.obj" "D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT\out\dotnet\gc-startup-dry-run\baseline\runtime-pack-repeated-4k\EHHelpers.cpp.obj.renamed.obj" "D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT\out\dotnet\gc-startup-dry-run\baseline\runtime-pack-repeated-4k\AllocFast.asm.obj.renamed.obj"
exit /b %errorlevel%
