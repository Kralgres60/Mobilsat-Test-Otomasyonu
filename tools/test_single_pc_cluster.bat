@echo off
chcp 65001 >nul
title NETA DRX9000 - Coklu Istasyon ve Donanim Simulasyon Test Paneli

set "SCRIPT_DIR=%~dp0"
set "ROOT_DIR=%SCRIPT_DIR%.."

set "APP_EXE="
set "APP_DIR="
set "SIM_EXE="
set "SIM_DIR="

:: 1. NETA_Antenna_Test.exe konumunu belirle (Once OUTPUT, sonra build/debug)
if exist "%ROOT_DIR%\OUTPUT\NETA_Antenna_Test.exe" (
    set "APP_EXE=%ROOT_DIR%\OUTPUT\NETA_Antenna_Test.exe"
    set "APP_DIR=%ROOT_DIR%\OUTPUT"
) else if exist "%ROOT_DIR%\build\Desktop_Qt_5_15_0_MinGW_32_bit_Debug\debug\NETA_Antenna_Test.exe" (
    set "APP_EXE=%ROOT_DIR%\build\Desktop_Qt_5_15_0_MinGW_32_bit_Debug\debug\NETA_Antenna_Test.exe"
    set "APP_DIR=%ROOT_DIR%\build\Desktop_Qt_5_15_0_MinGW_32_bit_Debug\debug"
)

:: 2. NETA_DRX_Simulator.exe konumunu belirle
if exist "%ROOT_DIR%\OUTPUT\tools\drx_simulator\NETA_DRX_Simulator.exe" (
    set "SIM_EXE=%ROOT_DIR%\OUTPUT\tools\drx_simulator\NETA_DRX_Simulator.exe"
    set "SIM_DIR=%ROOT_DIR%\OUTPUT\tools\drx_simulator"
) else if exist "%ROOT_DIR%\tools\drx_simulator\OUTPUT\NETA_DRX_Simulator.exe" (
    set "SIM_EXE=%ROOT_DIR%\tools\drx_simulator\OUTPUT\NETA_DRX_Simulator.exe"
    set "SIM_DIR=%ROOT_DIR%\tools\drx_simulator\OUTPUT"
) else if exist "%ROOT_DIR%\tools\drx_simulator\build\Desktop_Qt_5_15_0_MinGW_32_bit_Debug\debug\NETA_DRX_Simulator.exe" (
    set "SIM_EXE=%ROOT_DIR%\tools\drx_simulator\build\Desktop_Qt_5_15_0_MinGW_32_bit_Debug\debug\NETA_DRX_Simulator.exe"
    set "SIM_DIR=%ROOT_DIR%\tools\drx_simulator\build\Desktop_Qt_5_15_0_MinGW_32_bit_Debug\debug"
)

:MENU
cls
echo =====================================================================
echo       NETA DRX9000 - TEK BILGISAYARDA FABRIKA KUME TEST MOTORU
echo =====================================================================
echo.
echo Ana Uygulama   : %APP_EXE%
echo Calisma Dizini : %APP_DIR%
echo DRX Simulator  : %SIM_EXE%
echo Sim Dizini     : %SIM_DIR%
echo.
if "%APP_EXE%"=="" goto ERR_NO_APP

echo Bu arac ile gercek ag kablolarina ihtiyac duymadan tek PC'de:
echo - Yonetici (Host) icin 1 adet Referans DRX [Port 8080]
echo - Her Test Istasyonu icin ayri birer Test DRX [Port 8081, 8082, 8083]
echo acarak tam fabrika kume simulasyonunu test edebilirsiniz.
echo =====================================================================
echo [1] Tam Fabrika Testini Baslat (4 DRX Sim + 1 Host + 3 Istasyon + Web DB)
echo [2] Ikili Test (2 DRX Sim + 1 Host + 1 Istasyon + Web DB)
echo [3] Sadece 4 Adet DRX Simulatorunu Baslat (Port 8080, 8081, 8082, 8083)
echo [4] Sadece Yonetici Sunucuyu (HOST) Baslat (Port 9100)
echo [5] Sadece Bir Test Istasyonu Baslat (Masa 1, 2, 3 ... N)
echo [6] Calisan Tum Simule Surecleri Kapat (Temizle)
echo [7] Web Veritabani Yonetim Konsolunu Ac [http://localhost:8888]
echo [8] Sadece Istasyon Programi (Yonetici Olmadan - Istasyon 1 + DRX Sim 8081)
echo [0] Cikis
echo =====================================================================
set /p SECIM="Lutfen seciminizi giriniz [0-8]: "

if "%SECIM%"=="1" goto START_FULL
if "%SECIM%"=="2" goto START_DUAL
if "%SECIM%"=="3" goto START_SIMS
if "%SECIM%"=="4" goto START_HOST
if "%SECIM%"=="5" goto START_STATION
if "%SECIM%"=="6" goto STOP_ALL
if "%SECIM%"=="7" goto START_WEB_DB_MENU
if "%SECIM%"=="8" goto START_STATION_STANDALONE
if "%SECIM%"=="0" goto EXIT
goto MENU

:START_FULL
cls
echo =====================================================================
echo [1/8] DRX Referans Simulatoru baslatiliyor - Host icin Port 8080...
if not "%SIM_EXE%"=="" start "" /D "%SIM_DIR%" "%SIM_EXE%" --port 8080 --name "Referans DRX - Host" --auto-start
timeout /t 1 >nul

echo [2/8] DRX Istasyon 1 Simulatoru baslatiliyor - Masa 1 icin Port 8081...
if not "%SIM_EXE%"=="" start "" /D "%SIM_DIR%" "%SIM_EXE%" --port 8081 --name "Istasyon 1 DRX" --auto-start
timeout /t 1 >nul

echo [3/8] DRX Istasyon 2 Simulatoru baslatiliyor - Masa 2 icin Port 8082...
if not "%SIM_EXE%"=="" start "" /D "%SIM_DIR%" "%SIM_EXE%" --port 8082 --name "Istasyon 2 DRX" --auto-start
timeout /t 1 >nul

echo [4/8] DRX Istasyon 3 Simulatoru baslatiliyor - Masa 3 icin Port 8083...
if not "%SIM_EXE%"=="" start "" /D "%SIM_DIR%" "%SIM_EXE%" --port 8083 --name "Istasyon 3 DRX" --auto-start
timeout /t 1 >nul

echo [5/8] Merkezi Yonetici Sunucu (HOST) baslatiliyor - TCP 9100, Ref DRX 8080...
start "" /D "%APP_DIR%" "%APP_EXE%" --host --drx 127.0.0.1:8080
timeout /t 3 >nul

echo [6/8] Test Istasyonu 1 baslatiliyor - Masa 1, DUT DRX 8081...
start "" /D "%APP_DIR%" "%APP_EXE%" --client -s 1 --drx 127.0.0.1:8081
timeout /t 1 >nul

echo [7/8] Test Istasyonu 2 baslatiliyor - Masa 2, DUT DRX 8082...
start "" /D "%APP_DIR%" "%APP_EXE%" --client -s 2 --drx 127.0.0.1:8082
timeout /t 1 >nul

echo [8/8] Test Istasyonu 3 baslatiliyor - Masa 3, DUT DRX 8083...
start "" /D "%APP_DIR%" "%APP_EXE%" --client -s 3 --drx 127.0.0.1:8083

call :LAUNCH_WEB_DB

echo.
echo =====================================================================
echo [BASARILI] 4 DRX Alicisi + 1 Host + 3 Istasyon + Web DB calisiyor!
echo Web DB URL: http://localhost:8888
echo =====================================================================
pause
goto MENU

:START_DUAL
cls
echo =====================================================================
echo [1/4] DRX Referans Simulatoru baslatiliyor - Host icin Port 8080...
if not "%SIM_EXE%"=="" start "" /D "%SIM_DIR%" "%SIM_EXE%" --port 8080 --name "Referans DRX - Host" --auto-start
timeout /t 1 >nul

echo [2/4] DRX Istasyon 1 Simulatoru baslatiliyor - Masa 1 icin Port 8081...
if not "%SIM_EXE%"=="" start "" /D "%SIM_DIR%" "%SIM_EXE%" --port 8081 --name "Istasyon 1 DRX" --auto-start
timeout /t 1 >nul

echo [3/4] Yonetici Sunucu (HOST) baslatiliyor - TCP 9100, Ref DRX 8080...
start "" /D "%APP_DIR%" "%APP_EXE%" --host --drx 127.0.0.1:8080
timeout /t 3 >nul

echo [4/4] Test Istasyonu 1 baslatiliyor - Masa 1, DUT DRX 8081...
start "" /D "%APP_DIR%" "%APP_EXE%" --client -s 1 --drx 127.0.0.1:8081

call :LAUNCH_WEB_DB

echo.
echo =====================================================================
echo [BASARILI] Ikili test ortami ve Web DB baslatildi!
echo - Host: Ref DRX 127.0.0.1:8080
echo - Istasyon 1: DUT DRX 127.0.0.1:8081
echo - Web DB: http://localhost:8888
echo =====================================================================
pause
goto MENU

:START_SIMS
cls
echo 4 Adet Sanal DRX9000 Alicisi baslatiliyor...
if "%SIM_EXE%"=="" goto ERR_NO_SIM
start "" /D "%SIM_DIR%" "%SIM_EXE%" --port 8080 --name "Referans DRX - Host" --auto-start
start "" /D "%SIM_DIR%" "%SIM_EXE%" --port 8081 --name "Istasyon 1 DRX" --auto-start
start "" /D "%SIM_DIR%" "%SIM_EXE%" --port 8082 --name "Istasyon 2 DRX" --auto-start
start "" /D "%SIM_DIR%" "%SIM_EXE%" --port 8083 --name "Istasyon 3 DRX" --auto-start
echo [OK] Port 8080, 8081, 8082 ve 8083 uzerinde DRX alicilari yayinda!
pause
goto MENU

:START_HOST
cls
echo Yonetici Sunucu (HOST) baslatiliyor...
start "" /D "%APP_DIR%" "%APP_EXE%" --host --drx 127.0.0.1:8080
pause
goto MENU

:START_STATION
cls
echo =====================================================================
echo                 TEST ISTASYONU BASLATMA
echo =====================================================================
echo Sistem sinirsiz sayida istasyonu destekler (1, 2, 3 ... 1000+).
echo.
echo Ornek: 1 yazarsaniz Masa 1 baslatilir [DRX Port: 8081].
echo Ornek: 5 yazarsaniz Masa 5 baslatilir [DRX Port: 8085].
echo =====================================================================
set /p ST_NO="Lutfen Istasyon Numarasini Giriniz [Varsayilan 1]: "
if "%ST_NO%"=="" set "ST_NO=1"
set /a DRX_P=8080+%ST_NO%
echo Istasyon %ST_NO% baslatiliyor - DUT DRX Port: %DRX_P%...
start "" /D "%APP_DIR%" "%APP_EXE%" --client -s %ST_NO% --drx 127.0.0.1:%DRX_P%
pause
goto MENU

:START_STATION_STANDALONE
cls
echo =====================================================================
echo      SADECE ISTASYON PROGRAMI (YONETICI / HOST OLMADAN)
echo =====================================================================
netstat -ano | findstr ":8081" | findstr "LISTENING" >nul
if %errorlevel% neq 0 (
    if not "%SIM_EXE%"=="" (
        echo [1/2] Istasyon 1 DRX Simulatoru baslatiliyor [Port 8081]...
        start "" /D "%SIM_DIR%" "%SIM_EXE%" --port 8081 --name "Istasyon 1 DRX" --auto-start
        timeout /t 1 >nul
    )
) else (
    echo [1/2] Port 8081 uzerinde DRX Simulatoru zaten calisiyor.
)
echo [2/2] Sadece Test Istasyonu 1 baslatiliyor [Yonetici olmadan]...
start "" /D "%APP_DIR%" "%APP_EXE%" --client -s 1 --drx 127.0.0.1:8081
echo [OK] Istasyon 1 bagimsiz olarak baslatildi.
pause
goto MENU

:STOP_ALL
cls
echo Calisan tum NETA test ve simulator surecleri kapatiliyor...
taskkill /f /im NETA_Antenna_Test.exe >nul 2>&1
taskkill /f /im NETA_DRX_Simulator.exe >nul 2>&1
echo [OK] Tum test surecleri temizlendi.
pause
goto MENU

:START_WEB_DB_MENU
cls
call :LAUNCH_WEB_DB
pause
goto MENU

:LAUNCH_WEB_DB
set "WEB_SCRIPT="
if exist "%APP_DIR%\tools\mariadb_portable\web\neta_db_web.py" set "WEB_SCRIPT=%APP_DIR%\tools\mariadb_portable\web\neta_db_web.py"
if "%WEB_SCRIPT%"=="" if exist "%~dp0mariadb_portable\web\neta_db_web.py" set "WEB_SCRIPT=%~dp0mariadb_portable\web\neta_db_web.py"
if "%WEB_SCRIPT%"=="" goto WEB_DB_MISSING

set "PY_EXE="
where python >nul 2>nul
if %errorlevel% equ 0 set "PY_EXE=python"
if "%PY_EXE%"=="" if exist "%LOCALAPPDATA%\Programs\Python\Python311\python.exe" set "PY_EXE=%LOCALAPPDATA%\Programs\Python\Python311\python.exe"
if "%PY_EXE%"=="" if exist "C:\Python311\python.exe" set "PY_EXE=C:\Python311\python.exe"
if "%PY_EXE%"=="" if exist "C:\Python310\python.exe" set "PY_EXE=C:\Python310\python.exe"
if "%PY_EXE%"=="" goto WEB_DB_NO_PY

netstat -ano | findstr ":8888" | findstr "LISTENING" >nul
if %errorlevel% equ 0 goto WEB_DB_OPEN_BROWSER

echo [WEB DB] Veritabani web konsolu baslatiliyor [Port 8888]...
start "NETA Web DB Server" /min "%PY_EXE%" "%WEB_SCRIPT%" --port 8888
timeout /t 1 /nobreak >nul

:WEB_DB_OPEN_BROWSER
echo [WEB DB] Tarayici aciliyor: http://localhost:8888
start http://localhost:8888
exit /b 0

:WEB_DB_MISSING
echo [UYARI] Web veritabani scripti bulunamadi: neta_db_web.py
exit /b 0

:WEB_DB_NO_PY
echo [UYARI] Sistemde Python 3 bulunamadigi icin Web DB konsolu baslatilamadi.
exit /b 0

:ERR_NO_APP
cls
echo [HATA] NETA_Antenna_Test.exe bulunamadi! Lutfen once projeyi derleyiniz.
pause
goto EXIT

:ERR_NO_SIM
cls
echo [HATA] DRX Simulator bulunamadi!
pause
goto MENU

:EXIT
exit /b 0
