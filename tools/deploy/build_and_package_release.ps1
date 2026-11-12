# NETA DRX9000 - Temiz Release Derleme ve OUTPUT Paketleyici
$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectDir = (Resolve-Path "$ScriptDir\..\..").Path
$OutputDir = "$ProjectDir\OUTPUT"
$QtBin = "C:\Software\Qt\5.15.0\mingw81_32\bin"
if (-not (Test-Path "$QtBin\qmake.exe")) {
    $QtBin = "C:\Embedded\Qt\5.15.0\mingw81_32\bin"
}
$MinGWBin = "C:\Software\Qt\Tools\mingw810_32\bin"
if (-not (Test-Path "$MinGWBin\mingw32-make.exe")) {
    $MinGWBin = "C:\Embedded\Qt\Tools\mingw810_32\bin"
}

$env:PATH = "$QtBin;$MinGWBin;" + $env:PATH

# 0. project.ini dosyasından ayarları oku ve kaynak kodları senkronize et
$SyncScript = "$ProjectDir\sync_version.ps1"
if (Test-Path $SyncScript) {
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $SyncScript
}

# TargetExe adını project.ini'den tespit et (varsayılan: NETA_Antenna_Test)
$TargetExe = "NETA_Antenna_Test"
$iniPath = "$ProjectDir\project.ini"
if (Test-Path $iniPath) {
    $iniLines = Get-Content $iniPath -Encoding UTF8
    foreach ($line in $iniLines) {
        if ($line -match '^\s*TargetExe\s*=\s*(.+)$') {
            $TargetExe = $matches[1].Trim()
            break
        }
    }
}

Write-Host "=====================================================================" -ForegroundColor Cyan
Write-Host "    NETA DRX9000 - TEMIZ BUILD VE OUTPUT PAKETLEYICI (32-bit)       " -ForegroundColor Cyan
Write-Host "=====================================================================" -ForegroundColor Cyan
Write-Host "Hedef Calistirilabilir Dosya: $TargetExe.exe" -ForegroundColor White

# Varsa çalışan NETA süreçlerini sonlandır
Get-Process -Name "*$TargetExe*" -ErrorAction SilentlyContinue | Stop-Process -Force

# 1. Eski Klasörlerin Temizliği (dist, build, release_build, OUTPUT)
Write-Host "`n[1/6] Eski build, dist ve OUTPUT klasorleri siliniyor..." -ForegroundColor Yellow

$dirsToClean = @(
    "$ProjectDir\dist",
    "$ProjectDir\build",
    "$ProjectDir\release_build",
    "$ProjectDir\build_release",
    "$ProjectDir\temp_build",
    $OutputDir
)

foreach ($d in $dirsToClean) {
    if (Test-Path $d) {
        Write-Host "      Siliniyor: $d" -ForegroundColor DarkGray
        Remove-Item -Path $d -Recurse -Force -ErrorAction SilentlyContinue
    }
}

# 2. OUTPUT Dizinini Hazırla
Write-Host "[2/6] OUTPUT dizini hazirlaniyor..." -ForegroundColor Yellow
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
New-Item -ItemType Directory -Force -Path "$OutputDir\sqldrivers" | Out-Null

# 3. Sıfırdan Release Derlemesi Yap
Write-Host "[3/6] Proje sifirdan Release modunda derleniyor..." -ForegroundColor Yellow
$TempBuild = "$ProjectDir\temp_build"
New-Item -ItemType Directory -Force -Path $TempBuild | Out-Null

Set-Location $TempBuild
& "$QtBin\qmake.exe" "$ProjectDir\NETA_Antenna_Test.pro" -spec win32-g++ "CONFIG+=release"
if ($LASTEXITCODE -ne 0) {
    Write-Host "[HATA] QMake yapilandirmasi basarisiz oldu!" -ForegroundColor Red
    exit 1
}

& "$MinGWBin\mingw32-make.exe" -j4
if ($LASTEXITCODE -ne 0) {
    Write-Host "[HATA] MinGW derlemesi basarisiz oldu!" -ForegroundColor Red
    exit 1
}

$ExeSource = "$TempBuild\release\$TargetExe.exe"
if (-not (Test-Path $ExeSource)) {
    Write-Host "[HATA] $TargetExe.exe olusturulamadi!" -ForegroundColor Red
    exit 1
}

# 4. Derlenen EXE'yi OUTPUT'a Kopyala ve Geçici Derleme Klasörünü Temizle
Write-Host "[4/6] EXE OUTPUT dizinine aliniyor ve gecici dosyalar temizleniyor..." -ForegroundColor Yellow
Copy-Item -Path $ExeSource -Destination "$OutputDir\$TargetExe.exe" -Force
Set-Location $ProjectDir
Remove-Item -Path $TempBuild -Recurse -Force -ErrorAction SilentlyContinue

# Ayrıca 'build' klasörü oluştuysa onu da sil
if (Test-Path "$ProjectDir\build") {
    Remove-Item -Path "$ProjectDir\build" -Recurse -Force -ErrorAction SilentlyContinue
}

# 5. windeployqt ile Bağımlılıkları Çek
Write-Host "[5/6] Qt ve MinGW bagimliliklari paketleniyor (windeployqt)..." -ForegroundColor Yellow
& "$QtBin\windeployqt.exe" --no-translations --no-opengl-sw --no-system-d3d-compiler "$OutputDir\$TargetExe.exe" | Out-Null

# Qt5 bearer eklentisi Windows'ta acilis gecikmesine neden olur; kullanilmadigi icin kaldirilir
if (Test-Path "$OutputDir\bearer") {
    Remove-Item -Path "$OutputDir\bearer" -Recurse -Force -ErrorAction SilentlyContinue
}

# Platform eklentisi (qwindows.dll) kontrolü ve güvenlik kopyalaması
if (-not (Test-Path "$OutputDir\platforms\qwindows.dll")) {
    New-Item -ItemType Directory -Force -Path "$OutputDir\platforms" | Out-Null
    Copy-Item "$QtBin\..\plugins\platforms\qwindows.dll" "$OutputDir\platforms\" -Force
}

# MinGW Runtime DLL'leri
Copy-Item "$MinGWBin\libgcc_s_dw2-1.dll" "$OutputDir\" -Force
Copy-Item "$MinGWBin\libstdc++-6.dll" "$OutputDir\" -Force
Copy-Item "$MinGWBin\libwinpthread-1.dll" "$OutputDir\" -Force

# MySQL DLL'leri (+ 32-bit VC++ Runtime: libmysql.dll bağımlılığı)
Copy-Item "$ProjectDir\tools\sqldrivers\libmysql.dll" "$OutputDir\" -Force
Copy-Item "$ProjectDir\tools\sqldrivers\qsqlmysql.dll" "$OutputDir\sqldrivers\" -Force
foreach ($vcDll in @("vcruntime140.dll", "msvcp140.dll")) {
    $sysWowPath = Join-Path "$env:WINDIR\SysWOW64" $vcDll
    if (Test-Path $sysWowPath) {
        Copy-Item $sysWowPath "$OutputDir\" -Force
    }
}

# 6. Yönetici, Kurulum Betikleri, Portable MariaDB ve DRX Simülatörü OUTPUT'a Ekle
Write-Host "[6/6] Yonetici ag paylasim, Tasinabilir MariaDB ve DRX Simulator ekleniyor..." -ForegroundColor Yellow
Copy-Item "$ProjectDir\tools\deploy\host_scripts\1_Yonetici_Klasor_Paylasimini_Ac.bat" "$OutputDir\" -Force

# DRX9000 TCP Simülatörü: Build edilmemişse (veya kaynak kodu güncellendiyse) otomatik derle ve tools/ altına ekle
$SimDir = "$ProjectDir\tools\drx_simulator"
$SimOutDir = "$SimDir\OUTPUT"
$SimExe = "$SimOutDir\NETA_DRX_Simulator.exe"
$SimBuildScript = "$SimDir\build_sim.ps1"

if (Test-Path $SimBuildScript) {
    $needSimBuild = -not (Test-Path $SimExe)
    if (-not $needSimBuild) {
        $simExeTime = (Get-Item $SimExe).LastWriteTime
        $newerSrc = Get-ChildItem -Path "$SimDir\*" -File -Include *.cpp,*.h,*.pro |
                    Where-Object { $_.LastWriteTime -gt $simExeTime } |
                    Select-Object -First 1
        if ($newerSrc) {
            $needSimBuild = $true
        }
    }

    if ($needSimBuild) {
        Write-Host "      DRX Simulator build edilmemis veya guncel degil; otomatik derleniyor..." -ForegroundColor Cyan
        & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $SimBuildScript
        if ($LASTEXITCODE -ne 0) {
            Write-Host "[HATA] DRX Simulator derlemesi basarisiz oldu!" -ForegroundColor Red
            exit 1
        }
    } else {
        Write-Host "      Mevcut DRX Simulator OUTPUT paketi guncel; dahil ediliyor." -ForegroundColor DarkGray
    }

    if (Test-Path $SimOutDir) {
        $DestSimDir = "$OutputDir\tools\drx_simulator"
        New-Item -ItemType Directory -Force -Path $DestSimDir | Out-Null
        Copy-Item "$SimOutDir\*" $DestSimDir -Recurse -Force
    }
}

# Taşınabilir MariaDB Motorunu OUTPUT'a kopyala
if (Test-Path "$ProjectDir\tools\mariadb_portable") {
    New-Item -ItemType Directory -Force -Path "$OutputDir\tools\mariadb_portable" | Out-Null
    Copy-Item "$ProjectDir\tools\mariadb_portable\*" "$OutputDir\tools\mariadb_portable\" -Recurse -Force

    # data/ içini tamamen temizle (my.ini bir ust klasorde zaten var):
    # mysql_install_db.exe bos olmayan data/ klasorunde hata verdigi icin bos birakilir
    $dbDataOut = "$OutputDir\tools\mariadb_portable\data"
    if (Test-Path $dbDataOut) {
        Write-Host "      MariaDB data/ icerigi temizleniyor (temiz ilk kurulum icin)..." -ForegroundColor DarkGray
        Get-ChildItem $dbDataOut -Force | Remove-Item -Recurse -Force -ErrorAction SilentlyContinue
    }
}

# Hızlı Veritabanı Başlatıcı ve Durdurucu Kısayolları Ekle
$DbStartLauncher = @"
@echo off
title NETA DRX9000 - Portable MariaDB Baslatici
call "%~dp0tools\mariadb_portable\start_db.bat"
"@
Set-Content -Path "$OutputDir\0_Veritabanini_Baslat.bat" -Value $DbStartLauncher -Encoding Ascii

$DbStopLauncher = @"
@echo off
title NETA DRX9000 - Portable MariaDB Durdurucu
call "%~dp0tools\mariadb_portable\stop_db.bat"
"@
Set-Content -Path "$OutputDir\0_Veritabanini_Durdur.bat" -Value $DbStopLauncher -Encoding Ascii

# Yönetici Masaüstü Başlatıcısı Ekle
$HostLauncher = @"
@echo off
title NETA DRX9000 - Yonetici Sunucu
start "" "%~dp0$TargetExe.exe" --host
"@
Set-Content -Path "$OutputDir\0_Yonetici_Sunucuyu_Baslat.bat" -Value $HostLauncher -Encoding Ascii

# Son Kontrol: dist veya build kalmış mı?
if (Test-Path "$ProjectDir\dist") { Remove-Item -Path "$ProjectDir\dist" -Recurse -Force -ErrorAction SilentlyContinue }
if (Test-Path "$ProjectDir\build") { Remove-Item -Path "$ProjectDir\build" -Recurse -Force -ErrorAction SilentlyContinue }

Write-Host "`n=====================================================================" -ForegroundColor Green
Write-Host "                OUTPUT PAKETI HAZIR! (BUILD BASARILI)               " -ForegroundColor Green
Write-Host "=====================================================================" -ForegroundColor Green
Write-Host "Cikti Klasoru:" -ForegroundColor White
Write-Host "  $OutputDir`n" -ForegroundColor Cyan
Write-Host "Not: dist ve build klasorleri tamamen temizlendi, sadece OUTPUT klasoru hazirlandi." -ForegroundColor Gray
