@echo off
title NETA DRX9000 - Inno Setup Tek EXE Kurulum Paketi Uretici
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\installer\build_setup.ps1"
if %errorlevel% neq 0 (
    echo.
    echo [HATA] Setup olusturma basarisiz oldu, paylasim baslatilmadi.
    pause
    exit /b %errorlevel%
)
echo.
call "%~dp0share_setup.bat"
