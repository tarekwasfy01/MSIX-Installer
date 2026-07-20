@echo off
setlocal EnableExtensions EnableDelayedExpansion
cd /d "%~dp0"

set "NO_OPEN=0"
set "AFTER_INSTALL=0"

:parse_args
if "%~1"=="" goto :args_done
if /I "%~1"=="--no-open" set "NO_OPEN=1"
if /I "%~1"=="--after-install" set "AFTER_INSTALL=1"
shift
goto :parse_args

:args_done
echo ============================================================
echo   MSIX INSTALLER - ONE-FILE NATIVE C++ BUILD
echo ============================================================
echo.

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VCVARS="

if exist "%VSWHERE%" (
    for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find VC\Auxiliary\Build\vcvars64.bat`) do (
        if not defined VCVARS set "VCVARS=%%I"
    )
)

if not defined VCVARS (
    for %%R in (
        "%ProgramFiles%\Microsoft Visual Studio\2022"
        "%ProgramFiles(x86)%\Microsoft Visual Studio\2022"
        "%ProgramFiles%\Microsoft Visual Studio\18"
        "%ProgramFiles(x86)%\Microsoft Visual Studio\18"
    ) do (
        if exist "%%~R" (
            for /f "delims=" %%I in ('dir /b /s "%%~R\vcvars64.bat" 2^>nul') do (
                if not defined VCVARS set "VCVARS=%%I"
            )
        )
    )
)

if not defined VCVARS (
    if "%AFTER_INSTALL%"=="0" goto :auto_install
    echo ERROR: The MSVC x64 environment was not found.
    goto :failed
)

echo [1/4] Loading the MSVC x64 environment...
call "%VCVARS%" >nul
if errorlevel 1 goto :failed

set "CL_EXE="
if defined VCToolsInstallDir (
    if exist "%VCToolsInstallDir%bin\Hostx64\x64\cl.exe" (
        set "CL_EXE=%VCToolsInstallDir%bin\Hostx64\x64\cl.exe"
    )
)

set "RC_EXE="
for /f "usebackq delims=" %%I in (`powershell.exe -NoLogo -NoProfile -Command "$p = Get-ChildItem 'C:\Program Files (x86)\Windows Kits\10\bin\*\x64\rc.exe' -ErrorAction SilentlyContinue | Sort-Object FullName -Descending | Select-Object -First 1 -ExpandProperty FullName; if ($p) { $p }"`) do (
    if not defined RC_EXE set "RC_EXE=%%I"
)

set "SIGNTOOL_EXE="
for /f "usebackq delims=" %%I in (`powershell.exe -NoLogo -NoProfile -Command "$p = Get-ChildItem 'C:\Program Files (x86)\Windows Kits\10\bin\*\x64\signtool.exe' -ErrorAction SilentlyContinue | Sort-Object FullName -Descending | Select-Object -First 1 -ExpandProperty FullName; if ($p) { $p }"`) do (
    if not defined SIGNTOOL_EXE set "SIGNTOOL_EXE=%%I"
)

if not defined CL_EXE (
    if "%AFTER_INSTALL%"=="0" goto :auto_install
    echo ERROR: cl.exe was not found.
    goto :failed
)

if not defined RC_EXE (
    if "%AFTER_INSTALL%"=="0" goto :auto_install
    echo ERROR: rc.exe was not found.
    goto :failed
)

if not defined SIGNTOOL_EXE (
    if "%AFTER_INSTALL%"=="0" goto :auto_install
    echo ERROR: signtool.exe was not found.
    goto :failed
)

echo [2/4] Embedding the signing components...
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass ^
    -File "%CD%\COLLECT_SIGNTOOL.ps1"
if errorlevel 1 goto :failed

if exist "build" rmdir /s /q "build"
if not exist "build\obj" mkdir "build\obj"
if not exist "output" mkdir "output"

echo [3/4] Compiling the embedded resources...
"%RC_EXE%" /nologo /fo "build\obj\app.res" "src\app.rc"
if errorlevel 1 goto :failed

echo [4/4] Building the standalone EXE...
"%CL_EXE%" /nologo /std:c++20 /EHsc /W4 /permissive- /utf-8 ^
    /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DNOMINMAX ^
    /MT /O2 /Gy /Gw ^
    /Fo"build\obj\main.obj" ^
    /Fe"output\MSIXInstaller.exe" ^
    "src\main.cpp" "build\obj\app.res" ^
    /link /SUBSYSTEM:WINDOWS /OPT:REF /OPT:ICF ^
    Comctl32.lib Dwmapi.lib Ole32.lib Shell32.lib User32.lib Gdi32.lib

if errorlevel 1 goto :failed

rmdir /s /q "build"

echo.
echo ============================================================
echo   ONE-FILE EXE CREATED SUCCESSFULLY
echo ============================================================
echo.
echo Final distributable:
echo %CD%\output\MSIXInstaller.exe
echo.
echo The EXE contains the PowerShell workflow and SignTool.
echo No companion files are required.
echo.

if "%NO_OPEN%"=="0" start "" "%CD%\output"
exit /b 0

:auto_install
echo.
echo Required build components are missing.
echo They will now be installed automatically.
echo.

powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass ^
    -File "%CD%\INSTALL_ALL_REQUIRED_TOOLS.ps1" -NoBuild

if errorlevel 1 goto :failed

if "%NO_OPEN%"=="1" (
    call "%~f0" --after-install --no-open
) else (
    call "%~f0" --after-install
)

exit /b %ERRORLEVEL%

:failed
echo.
echo ============================================================
echo   BUILD FAILED
echo ============================================================
echo.
if "%NO_OPEN%"=="0" pause
exit /b 1
