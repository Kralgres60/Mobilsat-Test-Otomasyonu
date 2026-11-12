# NETA DRX9000 Sanal Cihaz Simülatörü - Release Derleme ve OUTPUT Paketleyici
$ErrorActionPreference = "Stop"

$SimDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$OutputDir = "$SimDir\OUTPUT"
$TempBuild = "$SimDir\temp_build"

$QtBin = "C:\Software\Qt\5.15.0\mingw81_32\bin"
if (-not (Test-Path "$QtBin\qmake.exe")) {
    $QtBin = "C:\Embedded\Qt\5.15.0\mingw81_32\bin"
}
$MinGWBin = "C:\Software\Qt\Tools\mingw810_32\bin"
if (-not (Test-Path "$MinGWBin\mingw32-make.exe")) {
    $MinGWBin = "C:\Embedded\Qt\Tools\mingw810_32\bin"
}

if (-not (Test-Path "$QtBin\qmake.exe")) {
    Write-Host "[HATA] Qt 5.15.0 (mingw81_32) qmake.exe bulunamadi!" -ForegroundColor Red
    exit 1
}

$env:PATH = "$QtBin;$MinGWBin;" + $env:PATH
$TargetExe = "NETA_DRX_Simulator"

Write-Host "=====================================================================" -ForegroundColor Cyan
Write-Host "  NETA DRX9000 SANAL CIHAZ SIMULATORU - TASINABILIR OUTPUT PAKETI    " -ForegroundColor Cyan
Write-Host "=====================================================================" -ForegroundColor Cyan

Get-Process -Name "*$TargetExe*" -ErrorAction SilentlyContinue | Stop-Process -Force

if (Test-Path $TempBuild) { Remove-Item -Path $TempBuild -Recurse -Force -ErrorAction SilentlyContinue }
if (Test-Path $OutputDir) { Remove-Item -Path $OutputDir -Recurse -Force -ErrorAction SilentlyContinue }

New-Item -ItemType Directory -Force -Path $TempBuild | Out-Null
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

Write-Host "`n[1/3] Release modunda derleniyor..." -ForegroundColor Yellow
Set-Location $TempBuild
& "$QtBin\qmake.exe" "$SimDir\DRX_Simulator.pro" -spec win32-g++ "CONFIG+=release"
if ($LASTEXITCODE -ne 0) {
    Write-Host "[HATA] qmake basarisiz!" -ForegroundColor Red
    exit 1
}

& "$MinGWBin\mingw32-make.exe" -j4
if ($LASTEXITCODE -ne 0) {
    Write-Host "[HATA] mingw32-make basarisiz!" -ForegroundColor Red
    exit 1
}

$ExeSource = "$TempBuild\release\$TargetExe.exe"
if (-not (Test-Path $ExeSource)) {
    Write-Host "[HATA] $TargetExe.exe olusturulamadi!" -ForegroundColor Red
    exit 1
}

Write-Host "[2/3] OUTPUT dizinine aliniyor ve gecici dosyalar siliniyor..." -ForegroundColor Yellow
Copy-Item -Path $ExeSource -Destination "$OutputDir\$TargetExe.exe" -Force
Set-Location $SimDir
Remove-Item -Path $TempBuild -Recurse -Force -ErrorAction SilentlyContinue

Write-Host "[3/3] Qt ve MinGW bagimliliklari paketleniyor (windeployqt)..." -ForegroundColor Yellow
& "$QtBin\windeployqt.exe" --no-translations --no-opengl-sw --no-system-d3d-compiler "$OutputDir\$TargetExe.exe" | Out-Null

# Qt5 bearer eklentisi Windows'ta acilis gecikmesine neden olur; kullanilmadigi icin kaldirilir
if (Test-Path "$OutputDir\bearer") {
    Remove-Item -Path "$OutputDir\bearer" -Recurse -Force -ErrorAction SilentlyContinue
}

if (-not (Test-Path "$OutputDir\platforms\qwindows.dll")) {
    New-Item -ItemType Directory -Force -Path "$OutputDir\platforms" | Out-Null
    Copy-Item "$QtBin\..\plugins\platforms\qwindows.dll" "$OutputDir\platforms\" -Force
}

Copy-Item "$MinGWBin\libgcc_s_dw2-1.dll" "$OutputDir\" -Force
Copy-Item "$MinGWBin\libstdc++-6.dll" "$OutputDir\" -Force
Copy-Item "$MinGWBin\libwinpthread-1.dll" "$OutputDir\" -Force

Write-Host "`n=====================================================================" -ForegroundColor Green
Write-Host "  SIMULATOR OUTPUT HAZIR! Klasoru dogrudan diger PC'ye atabilirsiniz." -ForegroundColor Green
Write-Host "=====================================================================" -ForegroundColor Green
Write-Host "  $OutputDir\$TargetExe.exe`n" -ForegroundColor Cyan
