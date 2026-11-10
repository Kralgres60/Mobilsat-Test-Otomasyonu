@echo off
title NETA DRX9000 - Portable MariaDB Server
cd /d "%~dp0"

echo =====================================================================
echo       NETA DRX9000 - TASINABILIR MARIADB SUNUCUSU (PORTABLE)         
echo =====================================================================
echo.

netstat -ano | findstr ":3306" | findstr "LISTENING" >nul
if %errorlevel% equ 0 (
    echo [BILGI] Port 3306 uzerinde calisan bir MySQL/MariaDB servisi tespit edildi.
    goto check_alive
)

if not exist "%~dp0data\mysql" (
    echo [BILGI] Ilk calistirma tespit edildi. Veritabani tablolari olusturuluyor...
    "%~dp0bin\mysql_install_db.exe" --datadir="%~dp0data" --password="neta123" --allow-remote-root-access --port=3306 --silent
    timeout /t 1 /nobreak >nul
)

echo [1/2] MariaDB motoru baslatiliyor...
start "" /b "%~dp0bin\mysqld.exe" --defaults-file="%~dp0my.ini" --datadir="%~dp0data"

timeout /t 2 /nobreak >nul

:check_alive
echo [2/2] Sunucu erisimi test ediliyor...
"%~dp0bin\mysqladmin.exe" -h 127.0.0.1 -P 3306 -u root -pneta123 ping >nul 2>&1
if %errorlevel% equ 0 (
    echo [BASARILI] MariaDB Portable aktif ve baglantilara hazir! (Port: 3306)
) else (
    echo [UYARI] MariaDB baslatilamadi veya baglanti reddedildi.
)

echo.
echo Bu pencereyi kapatabilirsiniz (Sunucu arka planda calisir).
echo Durdurmak icin 'stop_db.bat' calistiriniz.
echo =====================================================================
pause
