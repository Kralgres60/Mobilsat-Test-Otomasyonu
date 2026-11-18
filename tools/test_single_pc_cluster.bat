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
if "%APP_EXE%"=="" (
    echo [HATA] NETA_Antenna_Test.exe bulunamadi! Lutfen once projeyi derleyiniz (build.bat).
    pause
    goto EXIT
)
echo Bu arac ile gercek ag kablolarina ihtiyac duymadan tek PC'de:
echo - Yonetici (Host) icin 1 adet Referans DRX (Port 8080)
echo - Her Test Istasyonu icin ayri birer Test DRX (Port 8081, 8082, 8083)
echo acarak tam fabrika kume simulasyonunu test edebilirsiniz.
echo =====================================================================
echo [1] Tam Fabrika Testini Baslat (4 DRX Sim + 1 Host + 3 Istasyon)
echo [2] Ikili Test (2 DRX Sim + 1 Host + 1 Istasyon)
echo [3] Sadece 4 Adet DRX Simulatorunu Baslat (Port 8080, 8081, 8082, 8083)
echo [4] Sadece Yonetici Sunucuyu (HOST) Baslat (Port 9100)
echo [5] Sadece Bir Test Istasyonu Baslat (Masa 1, 2 veya 3)
echo [6] Calisan Tum Simule Surecleri Kapat (Temizle)
echo [0] Cikis
echo =====================================================================
set /p SECIM="Lutfen seciminizi giriniz [1-6]: "

if "%SECIM%"=="1" goto START_FULL
if "%SECIM%"=="2" goto START_DUAL
if "%SECIM%"=="3" goto START_SIMS
if "%SECIM%"=="4" goto START_HOST
if "%SECIM%"=="5" goto START_STATION
if "%SECIM%"=="6" goto STOP_ALL
if "%SECIM%"=="0" goto EXIT
goto MENU

:START_FULL
cls
echo =====================================================================
echo [1/8] DRX Referans Simulatoru baslatiliyor (Host icin Port 8080)...
if not "%SIM_EXE%"=="" (
    start "" /D "%SIM_DIR%" "%SIM_EXE%" --port 8080 --name "Referans DRX (Host)" --auto-start
)
timeout /t 1 >nul

echo [2/8] DRX Istasyon 1 Simulatoru baslatiliyor (Masa 1 icin Port 8081)...
if not "%SIM_EXE%"=="" (
    start "" /D "%SIM_DIR%" "%SIM_EXE%" --port 8081 --name "Istasyon 1 DRX" --auto-start
)
timeout /t 1 >nul

echo [3/8] DRX Istasyon 2 Simulatoru baslatiliyor (Masa 2 icin Port 8082)...
if not "%SIM_EXE%"=="" (
    start "" /D "%SIM_DIR%" "%SIM_EXE%" --port 8082 --name "Istasyon 2 DRX" --auto-start
)
timeout /t 1 >nul

echo [4/8] DRX Istasyon 3 Simulatoru baslatiliyor (Masa 3 icin Port 8083)...
if not "%SIM_EXE%"=="" (
    start "" /D "%SIM_DIR%" "%SIM_EXE%" --port 8083 --name "Istasyon 3 DRX" --auto-start
)
timeout /t 1 >nul

echo [5/8] Merkezi Yonetici Sunucu (HOST) baslatiliyor (TCP 9100, Ref DRX 8080)...
start "" /D "%APP_DIR%" "%APP_EXE%" --host --drx 127.0.0.1:8080
timeout /t 3 >nul

echo [6/8] Test Istasyonu 1 baslatiliyor (Masa 1 -> DUT DRX 8081)...
start "" /D "%APP_DIR%" "%APP_EXE%" --client -s 1 --drx 127.0.0.1:8081
timeout /t 1 >nul

echo [7/8] Test Istasyonu 2 baslatiliyor (Masa 2 -> DUT DRX 8082)...
start "" /D "%APP_DIR%" "%APP_EXE%" --client -s 2 --drx 127.0.0.1:8082
timeout /t 1 >nul

echo [8/8] Test Istasyonu 3 baslatiliyor (Masa 3 -> DUT DRX 8083)...
start "" /D "%APP_DIR%" "%APP_EXE%" --client -s 3 --drx 127.0.0.1:8083

echo.
echo =====================================================================
echo [BASARILI] 4 DRX Alicisi + 1 Host + 3 Istasyon calisiyor!
echo =====================================================================
pause
goto MENU

:START_DUAL
cls
echo =====================================================================
echo [1/4] DRX Referans Simulatoru baslatiliyor (Host icin Port 8080)...
if not "%SIM_EXE%"=="" (
    start "" /D "%SIM_DIR%" "%SIM_EXE%" --port 8080 --name "Referans DRX (Host)" --auto-start
)
timeout /t 1 >nul

echo [2/4] DRX Istasyon 1 Simulatoru baslatiliyor (Masa 1 icin Port 8081)...
if not "%SIM_EXE%"=="" (
    start "" /D "%SIM_DIR%" "%SIM_EXE%" --port 8081 --name "Istasyon 1 DRX" --auto-start
)
timeout /t 1 >nul

echo [3/4] Yonetici Sunucu (HOST) baslatiliyor (TCP 9100, Ref DRX 8080)...
start "" /D "%APP_DIR%" "%APP_EXE%" --host --drx 127.0.0.1:8080
timeout /t 3 >nul

echo [4/4] Test Istasyonu 1 baslatiliyor (Masa 1 -> DUT DRX 8081)...
start "" /D "%APP_DIR%" "%APP_EXE%" --client -s 1 --drx 127.0.0.1:8081
echo.
echo =====================================================================
echo [BASARILI] Ikili test ortami baslatildi!
echo - Host: Ref DRX 127.0.0.1:8080
echo - Istasyon 1: DUT DRX 127.0.0.1:8081
echo =====================================================================
pause
goto MENU

:START_SIMS
cls
echo 4 Adet Sanal DRX9000 Alicisi baslatiliyor...
if not "%SIM_EXE%"=="" (
    start "" /D "%SIM_DIR%" "%SIM_EXE%" --port 8080 --name "Referans DRX (Host)" --auto-start
    start "" /D "%SIM_DIR%" "%SIM_EXE%" --port 8081 --name "Istasyon 1 DRX" --auto-start
    start "" /D "%SIM_DIR%" "%SIM_EXE%" --port 8082 --name "Istasyon 2 DRX" --auto-start
    start "" /D "%SIM_DIR%" "%SIM_EXE%" --port 8083 --name "Istasyon 3 DRX" --auto-start
    echo [OK] Port 8080, 8081, 8082 ve 8083 uzerinde DRX alicilari yayinda!
) else (
    echo [HATA] DRX Simulator bulunamadi.
)
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
echo Hangi istasyonu baslatmak istiyorsunuz?
echo [1] Istasyon 1 (Masa 1, DRX: 8081)
echo [2] Istasyon 2 (Masa 2, DRX: 8082)
echo [3] Istasyon 3 (Masa 3, DRX: 8083)
set /p ST_NO="Secim [1-3]: "
set /a DRX_P=8080+%ST_NO%
start "" /D "%APP_DIR%" "%APP_EXE%" --client -s %ST_NO% --drx 127.0.0.1:%DRX_P%
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

:EXIT
exit /b 0
