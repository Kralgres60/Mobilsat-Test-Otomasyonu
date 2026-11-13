@echo off
setlocal
chcp 65001 >nul

echo =====================================================================
echo      NETA DRX9000 - DOXYGEN DOKUMANTASYON DERLEYICI
echo =====================================================================

REM Proje kok dizini
cd /d "%~dp0"

REM Doxygen yolunu tespit et (PATH veya Ozel Konumlar)
set "DOXYGEN_EXE="
where doxygen >nul 2>nul
if %errorlevel% equ 0 (
    set "DOXYGEN_EXE=doxygen"
) else (
    if exist "C:\Program Files\doxygen\bin\doxygen.exe" set "DOXYGEN_EXE=C:\Program Files\doxygen\bin\doxygen.exe"
    if exist "C:\Software\doxygen\doxygen.exe" set "DOXYGEN_EXE=C:\Software\doxygen\doxygen.exe"
    if exist "C:\Software\doxygen\bin\doxygen.exe" set "DOXYGEN_EXE=C:\Software\doxygen\bin\doxygen.exe"
)

if "%DOXYGEN_EXE%"=="" (
    echo [UYARI] Doxygen calistirilabilir dosyasi bulunamadi!
    echo Lutfen Doxygen'i kurup PATH'e ekleyin veya C:\Program Files\doxygen dizinine yukleyin.
    echo https://www.doxygen.nl/download.html
    pause
    exit /b 1
)

echo Doxygen Bulundu: %DOXYGEN_EXE%
echo Dokumantasyon 'docs/doxygen' dizinine olusturuluyor...

if not exist "docs\doxygen" mkdir "docs\doxygen"

"%DOXYGEN_EXE%" Doxyfile
if %errorlevel% neq 0 (
    echo [HATA] Doxygen derleme sirasinda hata verdi!
    pause
    exit /b %errorlevel%
)

echo =====================================================================
echo [BASARILI] Kod dokumantasyonu olusturuldu!
echo Ana sayfa: docs\doxygen\html\index.html
echo =====================================================================
pause
