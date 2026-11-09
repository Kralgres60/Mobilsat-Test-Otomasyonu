@echo off
chcp 65001 >nul
title NETA DRX9000 - Istasyon Baslatici

set "CONFIG_FILE=%~dp0istasyon_ayari.ini"
set "LOCAL_DIR=C:\NETA_Istasyon"

:: 1. Ayar dosyasını oku veya ilk açılışta sor
if exist "%CONFIG_FILE%" (
    for /f "tokens=1,2 delims==" %%a in (%CONFIG_FILE%) do (
        if "%%a"=="SUNUCU" set "SERVER_HOST=%%b"
        if "%%a"=="MASA_NO" set "STATION_ID=%%b"
    )
)

if "%SERVER_HOST%"=="" (
    cls
    echo =======================================================
    echo         NETA DRX9000 - ILK KURULUM YAPILANDIRMASI
    echo =======================================================
    echo.
    echo Bu istasyon bilgisayari icin kurulum yapiliyor.
    echo.
    set /p SERVER_HOST="Yonetici PC Bilgisayar Adi veya IP Adresini Giriniz: "
    set /p STATION_ID="Bu Masanin Numarasini Giriniz (1, 2 veya 3): "

    (
        echo SUNUCU=%SERVER_HOST%
        echo MASA_NO=%STATION_ID%
    ) > "%CONFIG_FILE%"
    echo.
    echo [BILGI] Ayarlar kaydedildi! Bir sonraki acilista tekrar sorulmayacaktir.
    echo.
    timeout /t 2 >nul
)

cls
echo =======================================================
echo     NETA DRX9000 - ISTASYON %STATION_ID% BASLATILIYOR
echo =======================================================
echo.
echo Sunucu: \\%SERVER_HOST%\NETA_Dagitim
echo.
echo [1/2] Guncel surum kontrol ediliyor...

:: Robocopy: Sadece yeni veya degisen dosyalari kopyala (/XO), bat/msi/log dosyalari haric tut (/XF)
robocopy "\\%SERVER_HOST%\NETA_Dagitim" "%LOCAL_DIR%" /MIR /XO /XF "*.bat" "*.msi" "*.log" "istasyon_ayari.ini" /NJH /NJS /ndl /nc /ns >nul 2>&1

if errorlevel 8 (
    echo [UYARI] Sunucuya erisilemedi. Yerel kurulu surum ile devam ediliyor...
) else (
    echo [BASARILI] Program surumu guncel!
)

echo.
echo [2/2] Istasyon arayuzu baslatiliyor...
start "" "%LOCAL_DIR%\NETA_Antenna_Test.exe" --client -s %STATION_ID%
