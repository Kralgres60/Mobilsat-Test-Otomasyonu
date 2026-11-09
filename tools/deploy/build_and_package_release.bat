@echo off
title NETA DRX9000 - Release Build ve OUTPUT Paketleyici
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0build_and_package_release.ps1"
echo.
pause
