@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b %errorlevel%
where link.exe
where cl.exe
cl.exe /nologo /TC /c /GS- /Zl /Fo:"D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT\out\dotnet\managed-hostlog-active-pal-allocating\artifacts\runtime_support.obj" "D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT\samples\managed\HostLogProof\runtime_support.c"
if errorlevel 1 exit /b %errorlevel%
"C:\Program Files\dotnet\dotnet.exe" publish "D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT\samples\managed\HostLogProof\HostLogProof.csproj" -c Release -r win-x64 --self-contained true -p:PublishAot=true -p:InvariantGlobalization=true -p:IlcGenerateStackTraceData=false -p:IlcUseEnvironmentalTools=true -p:HostLogProofRuntimeSupportObj=D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT\out\dotnet\managed-hostlog-active-pal-allocating\artifacts\runtime_support.obj -p:HostLogProofMapPath=D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT\out\dotnet\managed-hostlog-active-pal-allocating\artifacts\HostLogProof.map -p:HostLogProofMode=Allocating -p:BaseOutputPath=D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT\out\dotnet\managed-hostlog-active-pal-allocating\bin\ -p:BaseIntermediateOutputPath=D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT\out\dotnet\managed-hostlog-active-pal-allocating\obj\ -p:HostLogProofRuntimePackObj=D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT\out\dotnet\runtime-pack-active-pal-allocating\guidexos_nativeaot_platform.obj -p:IlcSdkPath=D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT\out\dotnet\runtime-pack-active-pal-allocating\sdk\
exit /b %errorlevel%
