@echo off
setlocal EnableExtensions DisableDelayedExpansion
cd /d "%~dp0"
echo DriverSource C build launcher is starting...
echo.
set "DRIVER_TOOLCHAIN=auto"
set "DRIVER_NO_PAUSE=0"
set "DRIVER_DIAGNOSE=0"
if not "%~1"=="" set "DRIVER_NO_PAUSE=1"
if defined CI set "DRIVER_NO_PAUSE=1"
:parse
if "%~1"=="" goto run
if /I "%~1"=="mingw" (set "DRIVER_TOOLCHAIN=mingw"& shift& goto parse)
if /I "%~1"=="msvc" (set "DRIVER_TOOLCHAIN=msvc"& shift& goto parse)
if /I "%~1"=="--no-pause" (set "DRIVER_NO_PAUSE=1"& shift& goto parse)
if /I "%~1"=="--diagnose" (set "DRIVER_DIAGNOSE=1"& shift& goto parse)
echo Unknown build argument.
echo Usage: build-windows.bat [mingw^|msvc] [--diagnose] [--no-pause]
set "DRIVER_BUILD_EXIT=2"
goto finish
:run
set "DRIVER_POWERSHELL=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
if not exist "%DRIVER_POWERSHELL%" (
    echo Windows PowerShell was not found. See README.md for CMake builds.
    set "DRIVER_BUILD_EXIT=2"
    goto finish
)
if not exist "%~dp0scripts\build-windows.ps1" (
    echo Missing scripts\build-windows.ps1. Extract the complete Source-and-Build package.
    set "DRIVER_BUILD_EXIT=2"
    goto finish
)
if "%DRIVER_DIAGNOSE%"=="1" goto diagnose
"%DRIVER_POWERSHELL%" -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "%~dp0scripts\build-windows.ps1" -Toolchain %DRIVER_TOOLCHAIN%
goto result
:diagnose
"%DRIVER_POWERSHELL%" -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "%~dp0scripts\build-windows.ps1" -Toolchain %DRIVER_TOOLCHAIN% -Diagnose
:result
set "DRIVER_BUILD_EXIT=%ERRORLEVEL%"
echo.
echo Build exit code: %DRIVER_BUILD_EXIT%
echo Log: "%~dp0build\build.log"
:finish
if "%DRIVER_NO_PAUSE%"=="1" goto return

echo Press any key to close this window.
pause >nul
:return
exit /b %DRIVER_BUILD_EXIT%
