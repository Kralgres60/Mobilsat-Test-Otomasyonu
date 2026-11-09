@echo off
title NETA Anten Test - Debug Build (MinGW 32-bit)
echo ========================================================
echo   NETA ANTENNA TEST - DEBUG BUILD
echo ========================================================
echo.

set SCRIPT_DIR=%~dp0
set QT_MINGW_DIR=C:\Software\Qt\Tools\mingw810_32\bin
set QT_BIN_DIR=C:\Software\Qt\5.15.0\mingw81_32\bin
if not exist "%QT_BIN_DIR%\qmake.exe" (
    set QT_MINGW_DIR=C:\Embedded\Qt\Tools\mingw810_32\bin
    set QT_BIN_DIR=C:\Embedded\Qt\5.15.0\mingw81_32\bin
)

if not exist "%QT_BIN_DIR%\qmake.exe" (
    echo ERROR: qmake.exe not found at %QT_BIN_DIR%
    pause
    exit /b 1
)

set PATH=%QT_MINGW_DIR%;%QT_BIN_DIR%;%PATH%

if not exist "%SCRIPT_DIR%build" mkdir "%SCRIPT_DIR%build"
cd /d "%SCRIPT_DIR%build"

echo [1/2] Running QMake...
qmake.exe "%SCRIPT_DIR%NETA_Antenna_Test.pro" -spec win32-g++ "CONFIG+=debug"
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: QMake failed!
    pause
    exit /b 1
)

echo [2/2] Compiling with MinGW...
mingw32-make.exe -j4
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: Compilation failed!
    pause
    exit /b 1
)

echo.
echo ========================================================
echo   SUCCESS: Debug executable built at build\debug\NETA_Antenna_Test.exe
echo ========================================================
pause
