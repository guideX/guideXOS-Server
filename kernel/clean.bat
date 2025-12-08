@echo off
REM
REM Clean build artifacts (Windows)
REM
REM Copyright (c) 2024 guideX
REM

echo Cleaning build artifacts...

if exist "build\x86" (
    echo Removing build\x86...
    rmdir /s /q build\x86
)

if exist "build\amd64" (
    echo Removing build\amd64...
    rmdir /s /q build\amd64
)

if exist "build\arm" (
    echo Removing build\arm...
    rmdir /s /q build\arm
)

echo.
echo Clean complete!
