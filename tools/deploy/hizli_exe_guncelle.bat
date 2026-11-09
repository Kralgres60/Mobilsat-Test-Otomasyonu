@echo off
chcp 65001 >nul
title NETA DRX9000 - Hizli EXE Guncelleme

set "PROJ_DIR=%~dp0..\.."
set "OUTPUT_DIR=%PROJ_DIR%\OUTPUT"

echo =====================================================================
echo                NETA DRX9000 - HIZLI EXE GUNCELLEYICI
echo =====================================================================
echo.

if not exist "%OUTPUT_DIR%" (
    echo [UYARI] OUTPUT klasoru bulunamadi! Once 'build_release.bat' ile tam paket olusturunuz.
    pause
    exit /b 1
)

echo [1/2] Proje Release modunda derleniyor...
set "PATH=C:\Embedded\Qt\5.15.0\mingw81_32\bin;C:\Embedded\Qt\Tools\mingw810_32\bin;%PATH%"

set "TEMP_DIR=%PROJ_DIR%\temp_build"
if not exist "%TEMP_DIR%" mkdir "%TEMP_DIR%"
cd /d "%TEMP_DIR%"
qmake.exe "%PROJ_DIR%\NETA_Antenna_Test.pro" -spec win32-g++ "CONFIG+=release"
mingw32-make.exe -j4
if %errorLevel% neq 0 (
    echo [HATA] Derleme basarisiz oldu!
    pause
    exit /b %errorLevel%
)

echo.
echo [2/2] EXE dosyasi OUTPUT dizinine aktariliyor...
copy /y "%TEMP_DIR%\release\NETA_Antenna_Test.exe" "%OUTPUT_DIR%\NETA_Antenna_Test.exe" >nul

cd /d "%PROJ_DIR%"
rmdir /s /q "%TEMP_DIR%" >nul 2>&1
if exist "%PROJ_DIR%\build" rmdir /s /q "%PROJ_DIR%\build" >nul 2>&1

echo [BASARILI] OUTPUT\NETA_Antenna_Test.exe guncellendi!

:: Eger agdaki Yonetici PC'ye erisim varsa dogrudan ag klasorune de kopyala
if exist "\\NETA-HOST\NETA_Dagitim" (
    echo.
    echo [AG] Yonetici PC bulundu (\\NETA-HOST\NETA_Dagitim). Dogrudan kopyalaniyor...
    copy /y "%OUTPUT_DIR%\NETA_Antenna_Test.exe" "\\NETA-HOST\NETA_Dagitim\NETA_Antenna_Test.exe" >nul
    echo [AG BASARILI] Yonetici PC dogrudan guncellendi!
)

echo.
echo Tamamlandi!
timeout /t 3
