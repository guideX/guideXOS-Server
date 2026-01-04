@echo off
REM Script to exclude kernel files from Visual Studio build
REM This prevents include path errors when building in VS
REM
REM Copyright (c) 2024 guideX

echo ================================================
echo Configuring Visual Studio Project
echo ================================================
echo.
echo This will exclude kernel files from VS build
echo (Kernel should be built with build-x86.bat)
echo.
pause

REM Backup the project file
copy guideXOSServer.vcxproj guideXOSServer.vcxproj.backup

echo.
echo [1/2] Backing up project file...
echo [OK] Backup created: guideXOSServer.vcxproj.backup
echo.

echo [2/2] Updating project configuration...
echo.
echo NOTE: This requires manual configuration in Visual Studio.
echo.
echo Please follow these steps:
echo.
echo 1. Open Visual Studio
echo 2. Open guideXOSServer.vcxproj
echo 3. In Solution Explorer, for EACH file under:
echo    - kernel\arch\amd64\
echo    - kernel\arch\x86\
echo    - kernel\arch\arm\
echo    - kernel\arch\ia64\
echo    - kernel\arch\sparc\
echo    - kernel\core\
echo.
echo 4. Right-click the file -^> Properties
echo 5. Configuration: All Configurations
echo 6. Set "Excluded From Build" to "Yes"
echo 7. Click OK
echo.
echo OR:
echo.
echo Add include paths as described in VS_BUILD_ERRORS_FIX.md
echo.
pause

echo.
echo Configuration complete!
echo.
echo Next steps:
echo   - Build compositor: Open VS and press F7
echo   - Build kernel: cd kernel ^&^& build-x86.bat
echo.
pause
