@echo off
title NETA DRX9000 - Veritabani Semasini Yukle
cd /d "%~dp0"

echo =====================================================================
echo       NETA DRX9000 - SEMAYI VE SEED VERILERINI YUKLE                
echo =====================================================================
echo.

set SQL_SCHEMA=..\..\sql\schema_mysql.sql
set SQL_SEED=..\..\sql\seed_data.sql

if not exist "%SQL_SCHEMA%" set SQL_SCHEMA=sql\schema_mysql.sql
if not exist "%SQL_SEED%" set SQL_SEED=sql\seed_data.sql

echo [1/2] Tablo semasi yukleniyor (%SQL_SCHEMA%)...
"%~dp0bin\mysql.exe" -h 127.0.0.1 -P 3306 -u root -pneta123 < "%SQL_SCHEMA%"
if %errorlevel% neq 0 (
    echo [HATA] Sema yuklenemedi!
    goto end
)

echo [2/2] Turksat 42E tohum verileri yukleniyor (%SQL_SEED%)...
"%~dp0bin\mysql.exe" -h 127.0.0.1 -P 3306 -u root -pneta123 neta_antenna_test < "%SQL_SEED%"
if %errorlevel% neq 0 (
    echo [HATA] Seed verileri yuklenemedi!
    goto end
)

echo.
echo [BASARILI] Sema ve test verileri basariyla MariaDB Portable'a yuklendi!
:end
echo =====================================================================
pause
