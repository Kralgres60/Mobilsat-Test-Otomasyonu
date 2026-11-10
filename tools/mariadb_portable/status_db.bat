@echo off
title NETA DRX9000 - Portable MariaDB Status
cd /d "%~dp0"

echo =====================================================================
echo              MARIADB PORTABLE DURUM BILGISI                         
echo =====================================================================
echo.
"%~dp0bin\mysqladmin.exe" -h 127.0.0.1 -P 3306 -u root -pneta123 status
echo.
"%~dp0bin\mysqladmin.exe" -h 127.0.0.1 -P 3306 -u root -pneta123 ping
echo.
echo =====================================================================
pause
