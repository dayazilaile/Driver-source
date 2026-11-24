@echo off
setlocal EnableExtensions
cd /d "%~dp0"
if not exist build mkdir build
if not exist dist mkdir dist

if exist "tools\mingw64\bin\gcc.exe" set "PATH=%CD%\tools\mingw64\bin;%PATH%"
if exist "mingw64\bin\gcc.exe" set "PATH=%CD%\mingw64\bin;%PATH%"
if /I "%~1"=="msvc" goto msvc
if /I "%~1"=="mingw" goto mingw

where gcc >nul 2>nul
if errorlevel 1 goto msvc
gcc -dumpmachine 2>nul | findstr /i "x86_64-w64-mingw32" >nul
if errorlevel 1 goto msvc

:mingw
where gcc >nul 2>nul
if errorlevel 1 goto missing
gcc -dumpmachine 2>nul | findstr /i "x86_64-w64-mingw32" >nul
if errorlevel 1 (
    echo Please use a 64-bit MinGW-w64 GCC compiler.
    exit /b 1
)
where windres >nul 2>nul
if errorlevel 1 goto missing
windres -I native native\app.rc -o build\app.o
if errorlevel 1 exit /b 1
gcc -std=c17 -Wall -Wextra -Werror -finput-charset=UTF-8 -O2 -municode -mwindows native\main.c native\driver_backup.c native\device_db.c native\hardware_database.c native\wua_callbacks.c native\update_diagnostics.c build\app.o -o dist\DriverSource.exe -lsetupapi -lcfgmgr32 -lole32 -loleaut32 -lcomctl32 -lshell32 -lshlwapi -ladvapi32 -luser32 -lgdi32 -luuid -lwinhttp
if errorlevel 1 exit /b 1
goto done

:msvc
where cl >nul 2>nul
if not errorlevel 1 goto compile_msvc
if not exist "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" goto missing
for /f "usebackq delims=" %%I in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "DRIVER_VS_PATH=%%I"
if not defined DRIVER_VS_PATH goto missing
call "%DRIVER_VS_PATH%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1

:compile_msvc
pushd native
rc /nologo /fo ..\build\app.res app.rc
if errorlevel 1 goto failed
cl /nologo /TC /std:c17 /utf-8 /W4 /WX /O2 /MT /D_CRT_SECURE_NO_WARNINGS main.c driver_backup.c device_db.c hardware_database.c wua_callbacks.c update_diagnostics.c /Fo..\build\ /Fe"..\dist\DriverSource.exe" /link /SUBSYSTEM:WINDOWS /MANIFEST:NO ..\build\app.res setupapi.lib cfgmgr32.lib ole32.lib oleaut32.lib comctl32.lib shell32.lib shlwapi.lib advapi32.lib user32.lib gdi32.lib uuid.lib winhttp.lib
if errorlevel 1 goto failed
popd
goto done

:failed
popd
exit /b 1

:missing
echo A C compiler is required.
echo Option 1: put 64-bit MinGW-w64 under tools\mingw64 or add its bin folder to PATH.
echo Option 2: install Visual Studio 2022 Build Tools with MSVC and the Windows SDK.
echo Then run build-windows.bat again.
exit /b 1

:done
xcopy /E /I /Y "database" "dist\database" >nul
if errorlevel 1 (
    echo Failed to copy the offline hardware database.
    exit /b 1
)
echo Build completed: dist\DriverSource.exe
exit /b 0
