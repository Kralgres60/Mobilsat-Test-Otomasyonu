@echo off
setlocal
title NETA DRX9000 - Doxygen Documentation Generator

echo =====================================================================
echo    NETA DRX9000 TEST AUTOMATION - DOXYGEN DOCUMENTATION ENGINE
echo =====================================================================
echo.

set "DOX_CMD=doxygen"

where doxygen >nul 2>&1
if %ERRORLEVEL% equ 0 (
    set "DOX_CMD=doxygen"
) else (
    if exist "C:\Program Files\doxygen\bin\doxygen.exe" (
        set "DOX_CMD=C:\Program Files\doxygen\bin\doxygen.exe"
    ) else (
        echo [HATA] Doxygen bulunamadi!
        echo Lutfen 'winget install DimitrivanHeesch.Doxygen' ile kurun.
        pause
        exit /b 1
    )
)

echo [1/2] Doxygen calistiriliyor ve kaynak kodlar ayristiriliyor...
"%DOX_CMD%" Doxyfile

if %ERRORLEVEL% neq 0 (
    echo [HATA] Doxygen dokumantasyon uretirken hata olustu!
    pause
    exit /b %ERRORLEVEL%
)

echo.
echo =====================================================================
echo [2/2] Dokumantasyon Basariyla Uretildi!
echo HTML Dizini: docs\doxygen\html\index.html
echo =====================================================================
echo.

if exist "docs\doxygen\html\index.html" (
    echo Dokumantasyon varsayilan tarayicida aciliyor...
    start "" "docs\doxygen\html\index.html"
)

echo Tamamlandi.
pause
