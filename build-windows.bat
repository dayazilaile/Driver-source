@echo off
setlocal
cd /d "%~dp0"
py -3.12 -m venv .venv-build
if errorlevel 1 goto failed
".venv-build\Scripts\python.exe" -m pip install pyinstaller==6.11.1
if errorlevel 1 goto failed
".venv-build\Scripts\python.exe" -m PyInstaller --noconfirm --clean --onefile --windowed --name DriverSource --add-data "sources.js;." desktop.py
if errorlevel 1 goto failed
echo Build completed: dist\DriverSource.exe
pause
exit /b 0
:failed
echo Build failed. Check the error above.
pause
exit /b 1
