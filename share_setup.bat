@echo off
chcp 65001 >nul
title NETA DRX9000 - Setup LAN Paylasim Sunucusu

echo =====================================================================
echo      NETA DRX9000 - AYNI AGDA SETUP PAYLASIM SUNUCUSU
echo =====================================================================
echo.

if not exist "%~dp0SETUP_OUTPUT" (
    echo [HATA] SETUP_OUTPUT klasoru bulunamadi!
    echo        Lutfen once setup.bat calistirip kurulum paketini olusturun.
    echo.
    pause
    exit /b 1
)

echo Bu bilgisayarin yerel ag (LAN) IP adresleri:
powershell -NoProfile -Command "Get-NetIPAddress -AddressFamily IPv4 | Where-Object { $_.IPAddress -notlike '127.*' -and $_.IPAddress -notlike '169.254.*' } | ForEach-Object { Write-Host ('  -> http://' + $_.IPAddress + ':8000') -ForegroundColor Green }"
echo.
echo [BILGI] Diger bilgisayardan tarayiciya yukaridaki adresi yazip .exe dosyasini indirebilirsiniz.
echo [BILGI] Kapatmak icin bu pencerede Ctrl+C tuslarina basin.
echo =====================================================================
echo.

pushd "%~dp0SETUP_OUTPUT"

where python >nul 2>nul
if %errorlevel%==0 (
    python -m http.server 8000
    goto :done
)

where py >nul 2>nul
if %errorlevel%==0 (
    py -3 -m http.server 8000
    goto :done
)

echo [HATA] Python bulunamadi! Python yuklu degilse Windows klasor paylasimini kullanabilirsiniz.
pause

:done
popd
