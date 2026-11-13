@echo off
title NETA DRX9000 - Web Veritabani Yonetim Konsolu
cd /d "%~dp0"

echo =====================================================================
echo       NETA DRX9000 - TASINABILIR WEB VERITABANI YONETIM KONSOLU
echo =====================================================================
echo.

REM 1. MariaDB Calisiyor mu Kontrol Et
netstat -ano | findstr ":3306" | findstr "LISTENING" >nul
if %errorlevel% neq 0 (
    echo [1/3] MariaDB aktif degil, otomatik baslatiliyor...
    if not exist "%~dp0data\mysql" (
        echo [BILGI] Ilk calistirma: Veritabani tablolari olusturuluyor...
        "%~dp0bin\mysql_install_db.exe" --datadir="%~dp0data" --password="neta123" --allow-remote-root-access --port=3306 --silent
    )
    start "NETA MariaDB Engine" /b "%~dp0bin\mysqld.exe" --defaults-file="%~dp0my.ini" --datadir="%~dp0data"
    timeout /t 3 /nobreak >nul
) else (
    echo [1/3] MariaDB Port 3306 uzerinde zaten aktif!
)

REM 2. Python Kontrolu
set "PYTHON_CMD="
where python >nul 2>nul
if %errorlevel% equ 0 (
    set "PYTHON_CMD=python"
) else (
    if exist "%LOCALAPPDATA%\Programs\Python\Python311\python.exe" set "PYTHON_CMD=%LOCALAPPDATA%\Programs\Python\Python311\python.exe"
    if exist "C:\Python311\python.exe" set "PYTHON_CMD=C:\Python311\python.exe"
    if exist "C:\Python310\python.exe" set "PYTHON_CMD=C:\Python310\python.exe"
)

if "%PYTHON_CMD%"=="" (
    echo [HATA] Sistemde Python tespit edilemedi!
    echo Lutfen Python 3 yukleyin veya yoneticiye danisin.
    pause
    exit /b 1
)

REM 3. Web Konsol Servisi Baslat
echo [2/3] Web Konsol Servisi Baslatiliyor (Port 8080)...
start "NETA Web DB Server" "%PYTHON_CMD%" "%~dp0web\neta_db_web.py"

timeout /t 1 /nobreak >nul

REM 4. Tarayicida Ac
echo [3/3] Tarayici baslatiliyor: http://localhost:8080
start http://localhost:8080

echo.
echo =====================================================================
echo [BASARILI] Web Veritabani Konsolu Baslatildi!
echo URL: http://localhost:8080
echo Ayni agdaki diger cihazlardan erisim: http://[BU_PC_IP_ADRESI]:8080
echo =====================================================================
echo.
echo Bu pencereyi kapatabilirsiniz. Web konsol arka planda calisacaktir.
echo Konsolu tamamen kapatmak icin acilan "NETA Web DB Server" penceresini kapatabilirsiniz.
echo.
pause
