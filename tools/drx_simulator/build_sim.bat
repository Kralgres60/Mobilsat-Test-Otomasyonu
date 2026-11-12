@echo off
title NETA DRX9000 Simulator - Release Build ve OUTPUT Paketleyici
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0build_sim.ps1"
echo.
pause
