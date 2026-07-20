@echo off
setlocal
cd /d "%~dp0"

echo ============================================================
echo   INSTALL EVERYTHING AND BUILD THE ONE-FILE EXE
echo ============================================================
echo.
echo This installs the required C++ tools and Windows SDK.
echo The final distributable will be one EXE file.
echo.

powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass ^
    -File "%CD%\INSTALL_ALL_REQUIRED_TOOLS.ps1"

if errorlevel 1 (
    echo.
    echo INSTALLATION OR BUILD FAILED
    pause
    exit /b 1
)

echo.
echo COMPLETED SUCCESSFULLY
pause
