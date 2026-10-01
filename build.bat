@echo off
setlocal EnableExtensions
chcp 65001 >nul

set "BUILD_TYPE=%~1"
if "%BUILD_TYPE%"=="" set "BUILD_TYPE=Release"
set "BUILD_JOBS=%BONGOCAT_BUILD_JOBS%"
if "%BUILD_JOBS%"=="" set "BUILD_JOBS=2"
set "CLEAN_ARG="
if /I "%BONGOCAT_CLEAN_BUILD%"=="1" set "CLEAN_ARG=-Clean"
if /I "%~2"=="-Clean" set "CLEAN_ARG=-Clean"
if /I "%~3"=="-Clean" set "CLEAN_ARG=-Clean"
set "CUBISM_ARG="
if /I "%BONGOCAT_REQUIRE_CUBISM%"=="1" set "CUBISM_ARG=-RequireCubism"
set "PACKAGE_ARG="
if /I "%~2"=="-Package" set "PACKAGE_ARG=-Package"
if /I "%~3"=="-Package" set "PACKAGE_ARG=-Package"

where powershell.exe >nul 2>&1
if errorlevel 1 (
    echo Error: Windows PowerShell is required to show build progress.
    exit /b 1
)

powershell.exe -NoProfile -ExecutionPolicy Bypass ^
    -File "%~dp0scripts\build-windows.ps1" ^
    -Configuration "%BUILD_TYPE%" ^
    -BuildDir "%~dp0build-cubism" ^
    -Jobs %BUILD_JOBS% %CUBISM_ARG% %CLEAN_ARG% %PACKAGE_ARG%

exit /b %ERRORLEVEL%
