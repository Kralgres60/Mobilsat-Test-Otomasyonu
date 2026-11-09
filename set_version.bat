@echo off
setlocal
set "SCRIPT_DIR=%~dp0"
set "ARG=%~1"

if "%ARG%"=="" (
    echo ========================================================
    echo NETA DRX 9000 ANTENNA TEST - Merkezi Surum Yonetimi
    echo ========================================================
    echo Kullanim:
    echo   set_version.bat patch       (Kucuk guncelleme: 1.0.0 -^> 1.0.1)
    echo   set_version.bat minor       (Orta guncelleme:  1.0.0 -^> 1.1.0)
    echo   set_version.bat major       (Buyuk guncelleme: 1.0.0 -^> 2.0.0)
    echo   set_version.bat 1.2.0       (Belirli bir surume ayarla)
    echo   set_version.bat             (Mevcut surumu tum dosyalara senkronize et)
    echo ========================================================
    echo.
    powershell -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT_DIR%sync_version.ps1"
    goto END
)

if /I "%ARG%"=="patch" (
    powershell -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT_DIR%sync_version.ps1" -Bump patch
    goto END
)
if /I "%ARG%"=="minor" (
    powershell -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT_DIR%sync_version.ps1" -Bump minor
    goto END
)
if /I "%ARG%"=="major" (
    powershell -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT_DIR%sync_version.ps1" -Bump major
    goto END
)

powershell -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT_DIR%sync_version.ps1" -NewVersion "%ARG%"

:END
echo.
pause
endlocal
