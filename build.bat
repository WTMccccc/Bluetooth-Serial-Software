@echo off
setlocal enabledelayedexpansion

rem ---------------------------------------------------------------------------
rem  BLE UART Host - build script (MSVC + Windows 10 SDK)
rem  Usage: build.bat          -> build\ble_uart.exe
rem         build.bat cli      -> build\ble_cli.exe
rem ---------------------------------------------------------------------------

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" set "VSWHERE=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo [ERROR] vswhere.exe not found. Install Visual Studio 2019/2022 with "Desktop development with C++".
    exit /b 1
)

for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%i"
if not defined VSPATH (
    echo [ERROR] Visual Studio with the MSVC toolset was not found.
    exit /b 1
)
call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1

cd /d "%~dp0"
if not exist build mkdir build

set "CFLAGS=/nologo /W4 /utf-8 /DUNICODE /D_UNICODE /O2 /MT /wd4201 /wd4244"
set "LIBS=user32.lib gdi32.lib comctl32.lib comdlg32.lib shell32.lib bthprops.lib runtimeobject.lib ole32.lib uuid.lib dbghelp.lib"

if /i "%~1"=="cli" (
    cl %CFLAGS% src\ble.c src\util.c tests\ble_cli.c /Fo:build\ /Fe:build\ble_cli.exe /link %LIBS% || exit /b 1
    echo Built: build\ble_cli.exe
    exit /b 0
)

rc /nologo /I src /fo build\app.res src\app.rc || exit /b 1
cl %CFLAGS% src\main.c src\ble.c src\util.c src\crashlog.c src\logview.c build\app.res /Fo:build\ /Fe:build\ble_uart.exe ^
   /link /SUBSYSTEM:WINDOWS %LIBS%
if errorlevel 1 exit /b 1

echo.
echo Built: build\ble_uart.exe
