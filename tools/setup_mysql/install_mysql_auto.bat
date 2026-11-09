@echo off
setlocal
title NETA DRX - Otomatik MySQL Kurulum Sihirbazi

:: 1. Yonetici Yetkisi Kontrolu ve Otomatik UAC Yükseltme
net session >nul 2>&1
if %errorLevel% NEQ 0 (
    echo ========================================================
    echo   Yonetici yetkisi isteniyor...
    echo ========================================================
    powershell -NoProfile -ExecutionPolicy Bypass -Command "Start-Process cmd -ArgumentList '/c \"\"%~f0\"\" %*' -Verb RunAs"
    exit /b
)

:: 2. PowerShell Kurulum Motorunu Calistir
set SCRIPT_DIR=%~dp0
cd /d "%SCRIPT_DIR%"
set LOG_FILE=%SCRIPT_DIR%install_stream.log

echo [BASLATILDI] NETA MySQL Kurulum Motoru Baslatildi > "%LOG_FILE%"

powershell -NoProfile -ExecutionPolicy Bypass -Command "& '%SCRIPT_DIR%install_mysql_auto.ps1' *>&1 | Tee-Object -FilePath '%LOG_FILE%' -Append"

echo.
if "%1"=="--auto" exit /b 0
if "%1"=="-auto" exit /b 0
if "%1"=="/silent" exit /b 0
pause
