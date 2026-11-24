@echo off
setlocal
cd /d "%~dp0"
where cl >nul 2>nul
if errorlevel 1 (
    echo Run this script from an x64 Native Tools Command Prompt for Visual Studio 2022.
    exit /b 1
)
if not exist dist mkdir dist
pushd native
rc /nologo /fo app.res app.rc
if errorlevel 1 goto failed
cl /nologo /TC /std:c17 /utf-8 /W4 /WX /O2 /D_CRT_SECURE_NO_WARNINGS main.c app.res /Fe:..\dist\DriverSource.exe /link /SUBSYSTEM:WINDOWS /MANIFEST:NO setupapi.lib cfgmgr32.lib ole32.lib oleaut32.lib comctl32.lib shell32.lib shlwapi.lib advapi32.lib user32.lib gdi32.lib uuid.lib
if errorlevel 1 goto failed
popd
echo Build completed: dist\DriverSource.exe
exit /b 0
:failed
popd
exit /b 1
