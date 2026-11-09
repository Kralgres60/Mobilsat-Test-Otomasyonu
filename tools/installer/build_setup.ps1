# NETA DRX9000 - Temiz Derleme ve Inno Setup Tek EXE Kurulum Paketi Üretici
$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectDir = (Resolve-Path "$ScriptDir\..\..").Path
$SetupOutputDir = "$ProjectDir\SETUP_OUTPUT"
$IssFile = "$ScriptDir\neta_setup.iss"

Write-Host "=====================================================================" -ForegroundColor Cyan
Write-Host "     NETA DRX9000 - INNO SETUP TEK EXE KURULUM MOTORU              " -ForegroundColor Cyan
Write-Host "=====================================================================" -ForegroundColor Cyan

# 1. Inno Setup Derleyicisini (ISCC.exe) Tespit Et
$possiblePaths = @(
    "C:\Software\Inno Setup 7\ISCC.exe",
    "C:\Software\Inno Setup 6\ISCC.exe",
    "C:\Program Files\Inno Setup 7\ISCC.exe",
    "C:\Program Files (x86)\Inno Setup 7\ISCC.exe",
    "C:\Program Files\Inno Setup 6\ISCC.exe",
    "C:\Program Files (x86)\Inno Setup 6\ISCC.exe",
    "C:\Program Files\Inno Setup 5\ISCC.exe",
    "C:\Program Files (x86)\Inno Setup 5\ISCC.exe"
)

$IsccPath = $null
foreach ($p in $possiblePaths) {
    if (Test-Path $p) {
        $IsccPath = $p
        break
    }
}

if (-not $IsccPath) {
    $cmd = Get-Command iscc -ErrorAction SilentlyContinue
    if ($cmd) { $IsccPath = $cmd.Source }
}

if (-not $IsccPath) {
    Write-Host "`n[HATA] Inno Setup Derleyicisi (ISCC.exe) bulunamadi!" -ForegroundColor Red
    Write-Host "Lutfen Inno Setup programinin kurulu oldugundan emin olunuz." -ForegroundColor Yellow
    exit 1
}

Write-Host "`nInno Setup Derleyicisi: $IsccPath" -ForegroundColor DarkCyan

# 2. SETUP_OUTPUT Klasörünü Temizle
Write-Host "`n[1/3] SETUP_OUTPUT dizini temizleniyor..." -ForegroundColor Yellow
if (Test-Path $SetupOutputDir) {
    Remove-Item -Path $SetupOutputDir -Recurse -Force -ErrorAction SilentlyContinue
}
New-Item -ItemType Directory -Force -Path $SetupOutputDir | Out-Null

# 3. OUTPUT Klasörünü Sıfırdan Temizleyip Yeniden Derle
Write-Host "`n[2/3] OUTPUT paketi sifirdan temizlenip Release olarak derleniyor..." -ForegroundColor Yellow
$DeployScript = "$ProjectDir\tools\deploy\build_and_package_release.ps1"
if (-not (Test-Path $DeployScript)) {
    Write-Host "[HATA] $DeployScript bulunamadi!" -ForegroundColor Red
    exit 1
}

& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $DeployScript
if ($LASTEXITCODE -ne 0) {
    Write-Host "[HATA] OUTPUT paketi olusturulamadi, kurulum iptal edildi!" -ForegroundColor Red
    exit 1
}

# 4. Inno Setup ile Tek EXE Haline Getir
Write-Host "`n[3/3] Inno Setup ile tek EXE kurulum dosyasi olusturuluyor..." -ForegroundColor Yellow
& "$IsccPath" "$IssFile"
if ($LASTEXITCODE -ne 0) {
    Write-Host "[HATA] Inno Setup paketleme islemi basarisiz oldu!" -ForegroundColor Red
    exit 1
}

# 5. Sonuç Kontrolü
$installerFile = Get-ChildItem -Path $SetupOutputDir -Filter "*.exe" | Select-Object -First 1

if ($installerFile) {
    $sizeMb = [math]::Round($installerFile.Length / 1MB, 2)
    Write-Host "`n=====================================================================" -ForegroundColor Green
    Write-Host "            KURULUM EXE'SI BASARIYLA OLUSTURULDU!                   " -ForegroundColor Green
    Write-Host "=====================================================================" -ForegroundColor Green
    Write-Host "Kurulum Dosyasi : $($installerFile.FullName)" -ForegroundColor Cyan
    Write-Host "Dosya Boyutu    : $sizeMb MB" -ForegroundColor White
    Write-Host "`nBu tek dosyalik kurulum dosyasini istediginiz bilgisayara tasiyip" -ForegroundColor Yellow
    Write-Host "cift tiklayarak NETA DRX9000 sistemini dogrudan kurabilirsiniz.`n" -ForegroundColor Yellow
} else {
    Write-Host "[HATA] SETUP_OUTPUT icinde kurulum dosyasi bulunamadi!" -ForegroundColor Red
    exit 1
}
