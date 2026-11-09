# ==============================================================================
# NETA DRX9000 ANTENNA TEST - project.ini Okuyucu ve Senkronizasyon Betigi
# ==============================================================================
# Bu betik root dizindeki 'project.ini' dosyasini okur ve tum bagimli dosyalara:
#   - src/core/appinfo.h
#   - NETA_Antenna_Test.pro
#   - tools/installer/neta_setup.iss
# otomatik olarak aktarir.
#
# Kullanim:
#   .\sync_version.ps1                   -> project.ini'deki degerleri tum dosyalara uygular
#   .\sync_version.ps1 -NewVersion 1.2.0 -> project.ini'deki surumu 1.2.0 yapar ve senkronize eder
#   .\sync_version.ps1 -Bump patch       -> 1.0.0 -> 1.0.1 (Kucuk guncelleme)
#   .\sync_version.ps1 -Bump minor       -> 1.0.0 -> 1.1.0 (Ozellik guncellemesi)
#   .\sync_version.ps1 -Bump major       -> 1.0.0 -> 2.0.0 (Buyuk revizyon)
# ==============================================================================

param(
    [string]$NewVersion = "",
    [string]$Bump = ""
)

$ErrorActionPreference = "Stop"

$scriptDir    = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectDir   = $scriptDir
$iniPath      = Join-Path $projectDir "project.ini"
$appInfoPath  = Join-Path $projectDir "src\core\appinfo.h"
$issPath      = Join-Path $projectDir "tools\installer\neta_setup.iss"
$proPath      = Join-Path $projectDir "NETA_Antenna_Test.pro"

$utf8NoBom = New-Object System.Text.UTF8Encoding $false

# 1. project.ini dosyasını oku
if (-not (Test-Path $iniPath)) {
    Write-Host "[HATA] project.ini bulunamadi ($iniPath)!" -ForegroundColor Red
    exit 1
}

function Parse-Ini ($path) {
    $dict = @{}
    $lines = [System.IO.File]::ReadAllLines($path, [System.Text.Encoding]::UTF8)
    foreach ($line in $lines) {
        $trimmed = $line.Trim()
        if ($trimmed.StartsWith(";") -or $trimmed.StartsWith("#") -or [string]::IsNullOrWhiteSpace($trimmed)) {
            continue
        }
        if ($trimmed.Contains("=")) {
            $parts = $trimmed.Split('=', 2)
            $k = $parts[0].Trim()
            $v = $parts[1].Trim()
            $dict[$k] = $v
        }
    }
    return $dict
}

$iniData = Parse-Ini $iniPath

$appName         = if ($iniData.ContainsKey("AppName")) { $iniData["AppName"] } else { "NETA DRX9000 ANTENNA TEST AUTOMATION" }
$appTitle        = if ($iniData.ContainsKey("AppTitle")) { $iniData["AppTitle"] } else { "NETA DRX9000 ANTENNA TEST" }
$currentVersion  = if ($iniData.ContainsKey("Version")) { $iniData["Version"] } else { "1.0.0" }
$developer       = if ($iniData.ContainsKey("Developer")) { $iniData["Developer"] } else { "Semi Eren Gökyıldız" }
$company         = if ($iniData.ContainsKey("Company")) { $iniData["Company"] } else { "NETA Elektronik A.S." }
$domain          = if ($iniData.ContainsKey("Domain")) { $iniData["Domain"] } else { "neta.com.tr" }
$website         = if ($iniData.ContainsKey("Website")) { $iniData["Website"] } else { "https://www.neta.com.tr" }
$copyright       = if ($iniData.ContainsKey("Copyright")) { $iniData["Copyright"] } else { "Copyright (C) 2026 NETA Elektronik A.S." }
$targetExe       = if ($iniData.ContainsKey("TargetExe")) { $iniData["TargetExe"] } else { "NETA_Antenna_Test" }
$installerPrefix = if ($iniData.ContainsKey("InstallerPrefix")) { $iniData["InstallerPrefix"] } else { "NETA_DRX9000_ANTENNA_Test_Setup" }

# 2. Sürüm Değişikliği (Bump veya NewVersion) İsteği Varsa project.ini'yi Güncelle
$targetVersion = $currentVersion

if ($Bump -ne "") {
    $parts = $currentVersion.Split('.')
    $major = [int]$parts[0]
    $minor = if ($parts.Length -gt 1) { [int]$parts[1] } else { 0 }
    $patch = if ($parts.Length -gt 2) { [int]$parts[2] } else { 0 }

    switch ($Bump.ToLower()) {
        "major" { $major++; $minor = 0; $patch = 0 }
        "minor" { $minor++; $patch = 0 }
        "patch" { $patch++ }
        default { Write-Host "[HATA] Gecersiz bump tipi ($Bump). 'patch', 'minor' veya 'major' kullanin." -ForegroundColor Red; exit 1 }
    }
    $targetVersion = "$major.$minor.$patch"
} elseif ($NewVersion -ne "") {
    $targetVersion = $NewVersion.Trim()
}

if ($targetVersion -ne $currentVersion) {
    $iniRaw = [System.IO.File]::ReadAllText($iniPath, [System.Text.Encoding]::UTF8)
    $iniRaw = [regex]::Replace($iniRaw, 'Version\s*=\s*[^\r\n]+', "Version=$targetVersion")
    [System.IO.File]::WriteAllText($iniPath, $iniRaw, $utf8NoBom)
    Write-Host "[OK] project.ini guncellendi: Version = $targetVersion" -ForegroundColor Green
    $currentVersion = $targetVersion
}

Write-Host "=====================================================================" -ForegroundColor Cyan
Write-Host "     NETA DRX9000 ANTENNA TEST - PROJE SENKRONIZASYON MOTORU         " -ForegroundColor Cyan
Write-Host "=====================================================================" -ForegroundColor Cyan
Write-Host "project.ini Dosyasindan Okunan Ayarlar:" -ForegroundColor White
Write-Host "  Uygulama Adi   : $appName" -ForegroundColor Yellow
Write-Host "  Kisa Baslik    : $appTitle" -ForegroundColor Yellow
Write-Host "  Surum          : $currentVersion" -ForegroundColor Green
Write-Host "  Gelistirici    : $developer" -ForegroundColor Green
Write-Host "  Sirket         : $company" -ForegroundColor DarkCyan
Write-Host "  Web Sitesi     : $website" -ForegroundColor DarkCyan
Write-Host "  Telif Hakki    : $copyright" -ForegroundColor DarkCyan
Write-Host "  Hedef EXE      : $targetExe.exe" -ForegroundColor White
Write-Host "  Setup Adi      : ${installerPrefix}_v${currentVersion}.exe" -ForegroundColor Magenta
Write-Host "---------------------------------------------------------------------" -ForegroundColor DarkGray

# 3. src/core/appinfo.h Güncelle
$appInfoContent = @"
#ifndef APPINFO_H
#define APPINFO_H

#define NETA_APP_NAME            "$appName"
#define NETA_APP_TITLE           "$appTitle"
#define NETA_APP_VERSION         "$currentVersion"
#define NETA_APP_DEVELOPER       "$developer"
#define NETA_APP_ORGANIZATION    "$company"
#define NETA_APP_DOMAIN          "$domain"
#define NETA_APP_COPYRIGHT       "$copyright"

#endif // APPINFO_H
"@

[System.IO.File]::WriteAllText($appInfoPath, $appInfoContent.Trim() + "`r`n", $utf8NoBom)
Write-Host "[OK] src/core/appinfo.h senkronize edildi." -ForegroundColor Green

# 4. NETA_Antenna_Test.pro Güncelle
if (Test-Path $proPath) {
    $proContent = [System.IO.File]::ReadAllText($proPath, [System.Text.Encoding]::UTF8)
    $proContent = [regex]::Replace($proContent, 'TARGET\s*=\s*[^\r\n]+', "TARGET = $targetExe")
    $proContent = [regex]::Replace($proContent, 'VERSION\s*=\s*[^\r\n]+', "VERSION = $currentVersion")
    [System.IO.File]::WriteAllText($proPath, $proContent, $utf8NoBom)
    Write-Host "[OK] NETA_Antenna_Test.pro senkronize edildi." -ForegroundColor Green
}

# 5. tools/installer/neta_setup.iss Güncelle
if (Test-Path $issPath) {
    $issContent = [System.IO.File]::ReadAllText($issPath, [System.Text.Encoding]::UTF8)
    $issContent = [regex]::Replace($issContent, '#define MyAppName "[^"]*"', "#define MyAppName `"$appName`"")
    $issContent = [regex]::Replace($issContent, '#define MyAppVersion "[^"]*"', "#define MyAppVersion `"$currentVersion`"")
    $issContent = [regex]::Replace($issContent, '#define MyAppPublisher "[^"]*"', "#define MyAppPublisher `"$company ($developer)`"")
    $issContent = [regex]::Replace($issContent, '#define MyAppURL "[^"]*"', "#define MyAppURL `"$website`"")
    $issContent = [regex]::Replace($issContent, '#define MyAppExeName "[^"]*"', "#define MyAppExeName `"$targetExe.exe`"")
    $issContent = [regex]::Replace($issContent, 'OutputBaseFilename=[^\r\n]+', "OutputBaseFilename=${installerPrefix}_v{#MyAppVersion}")
    [System.IO.File]::WriteAllText($issPath, $issContent, $utf8NoBom)
    Write-Host "[OK] tools/installer/neta_setup.iss senkronize edildi." -ForegroundColor Green
}

Write-Host "Tum proje dosyalari basariyla senkronize edildi!`n" -ForegroundColor Green
