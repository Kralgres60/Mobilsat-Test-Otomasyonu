@echo off
title NETA DRX9000 - Stop Portable MariaDB
cd /d "%~dp0"

echo =====================================================================
echo         NETA DRX9000 - MARIADB SUNUCUSUNU GUVENLE DURDUR             
echo =====================================================================
echo.

echo MariaDB kapatma komutu gonderiliyor (Graceful Shutdown)...
"%~dp0bin\mysqladmin.exe" -h 127.0.0.1 -P 3306 -u root -pneta123 shutdown >nul 2>&1

timeout /t 1 /nobreak >nul

netstat -ano | findstr ":3306" | findstr "LISTENING" >nul
if %errorlevel% equ 0 (
    echo [BILGI] mysqld hala calisiyor, zorla kapatiliyor...
    taskkill /f /im mysqld.exe >nul 2>&1
)

echo [BASARILI] MariaDB sunucusu tamamen durduruldu.
echo =====================================================================
pause
