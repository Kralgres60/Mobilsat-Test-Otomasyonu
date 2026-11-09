@echo off
setlocal
title NETA DRX - Veritabani Sifirla ve Yeniden Yukle

net session >nul 2>&1
if %errorLevel% NEQ 0 (
    powershell -NoProfile -ExecutionPolicy Bypass -Command "Start-Process cmd -ArgumentList '/c \"\"%~f0\"\"' -Verb RunAs"
    exit /b
)

set SCRIPT_DIR=%~dp0
set PROJECT_ROOT=%SCRIPT_DIR%..\..
cd /d "%SCRIPT_DIR%"

echo ========================================================
echo   NETA VERITABANI SIFIRLAMA VE YENIDEN YUKLEME
echo ========================================================
echo.
echo DIKKAT: Mevcut tum test sonuclari silinecektir!
set /p CONFIRM="Devam etmek istiyor musunuz (E/H)? "
if /i "%CONFIRM%" NEQ "E" (
    echo Islem iptal edildi.
    pause
    exit /b
)

:: mysql.exe bul
set MYSQL_BIN=""
if exist "C:\Program Files\MariaDB 10.11\bin\mysql.exe" set MYSQL_BIN="C:\Program Files\MariaDB 10.11\bin\mysql.exe"
if exist "C:\Program Files\MySQL\MySQL Server 8.0\bin\mysql.exe" set MYSQL_BIN="C:\Program Files\MySQL\MySQL Server 8.0\bin\mysql.exe"

if %MYSQL_BIN%=="" (
    echo ERROR: mysql.exe bulunamadi!
    pause
    exit /b 1
)

echo.
echo [1/2] Tablolar yeniden olusturuluyor...
type "%PROJECT_ROOT%\sql\schema_mysql.sql" | %MYSQL_BIN% -u root -pneta123

echo [2/2] Fabrika ayarlari ve test profilleri yukleniyor...
type "%PROJECT_ROOT%\sql\seed_data.sql" | %MYSQL_BIN% -u root -pneta123

echo.
echo ========================================================
echo   BASARILI: Veritabani fabrika durumuna sifirlandi!
echo ========================================================
pause
