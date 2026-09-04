@echo off
setlocal

set MSBUILD="C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\amd64\MSBuild.exe"

echo ========================================
echo   Building x64 Release...
echo ========================================
%MSBUILD% mr-cli-ffmpeg.sln /t:Rebuild /p:Configuration=Release /p:Platform=x64
if %errorlevel% neq 0 (
    echo [ERROR] x64 build failed!
    pause
    exit /b 1
)

echo.
echo ========================================
echo   Building x86 Release...
echo ========================================
%MSBUILD% mr-cli-ffmpeg.sln /t:Rebuild /p:Configuration=Release /p:Platform=x86
if %errorlevel% neq 0 (
    echo [ERROR] x86 build failed!
    pause
    exit /b 1
)

echo.
echo ========================================
echo   Copying executables...
echo ========================================
copy /Y "x64\Release\mr-cli-ffmpeg.exe" "mr-cli-ffmpeg-v1.1.3-x64.exe"
copy /Y "Release\mr-cli-ffmpeg.exe" "mr-cli-ffmpeg-v1.1.3-x86.exe"

echo.
echo ========================================
echo   Done! Both builds successful.
echo ========================================
pause
