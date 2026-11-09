# ==============================================================================
# NETA DRX9000 Anten Test Otomasyonu - Otomatik MySQL / MariaDB Kurucu & Yapılandırıcı
# Tarih: 03.11.2026
# ==============================================================================

param(
    [string]$RootPassword = "neta123",
    [int]$Port = 3306,
    [string]$ServiceName = "NetaMySQL"
)

$ErrorActionPreference = "Stop"

function Write-Step {
    param([string]$Message)
    Write-Host "`n>>> $Message" -ForegroundColor Cyan
}

function Write-Success {
    param([string]$Message)
    Write-Host "[OK] $Message" -ForegroundColor Green
}

function Write-Warn {
    param([string]$Message)
    Write-Host "[!] $Message" -ForegroundColor Yellow
}

function Write-Err {
    param([string]$Message)
    Write-Host "[HATA] $Message" -ForegroundColor Red
}

Write-Host "========================================================" -ForegroundColor Blue
Write-Host "   NETA DRX9000 - OTOMATİK VERİTABANI SUNUCUSU KURULUMU   " -ForegroundColor White
Write-Host "   MySQL / MariaDB Sessiz Kurulum ve Ağ Yapılandırıcısı   " -ForegroundColor Yellow
Write-Host "========================================================" -ForegroundColor Blue

# 1. Yönetici Yetkisi Kontrolü
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    Write-Err "Bu betik Yönetici (Administrator) olarak calistirilmalidir!"
    exit 1
}

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectRoot = (Resolve-Path "$ScriptDir\..\..").Path
$MsiFileName = "mariadb-10.11.8-winx64.msi"
$LocalMsiPath = Join-Path $ScriptDir $MsiFileName
$DownloadUrl = "https://archive.mariadb.org/mariadb-10.11.8/winx64-packages/mariadb-10.11.8-winx64.msi"

# 2. Servis veya Port Kontrolü
Write-Step "1/6: Mevcut Veritabanı Servisleri ve Port Kontrol Ediliyor..."
$existingService = Get-Service -Name $ServiceName -ErrorAction SilentlyContinue
$mysqlBinPath = $null

if ($existingService) {
    Write-Warn "Servis '$ServiceName' zaten kurulu. Durum: $($existingService.Status)"
    if ($existingService.Status -ne 'Running') {
        Write-Host "Servis baslatiliyor..."
        Start-Service -Name $ServiceName
    }
} else {
    # Port 3306 kullanımda mı?
    $portActive = Get-NetTCPConnection -LocalPort $Port -State Listen -ErrorAction SilentlyContinue
    if ($portActive) {
        Write-Warn "Port $Port su anda baska bir proses tarafindan dinleniyor (PID: $($portActive.OwningProcess))."
    } else {
        # 3. İndirme (Download)
        if (-not (Test-Path $LocalMsiPath)) {
            Write-Step "2/6: MariaDB 10.11 LTS Resmi Kurulum Paketi Indiriliyor (~73 MB)..."
            Write-Host "Kaynak: $DownloadUrl" -ForegroundColor Gray
            
            try {
                [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
                $webClient = New-Object System.Net.WebClient
                
                # İndirme ilerleme çubuğu
                $webClient.DownloadFile($DownloadUrl, $LocalMsiPath)
                Write-Success "Indirme tamamlandi: $LocalMsiPath"
            } catch {
                Write-Err "Indirme basarisiz oldu: $_"
                Write-Host "Lutfen '$MsiFileName' dosyasini elle indirip '$ScriptDir' klasorune koyun." -ForegroundColor Yellow
                exit 1
            }
        } else {
            Write-Success "Kurulum paketi yerel dizinde mevcut: $LocalMsiPath"
        }

        # 4. Sessiz MSI Kurulumu (Silent Installation)
        Write-Step "3/6: MariaDB Arka Planda Sessizce Kuruluyor (Servis: $ServiceName, Port: $Port)..."
        $installDir = "C:\Program Files\MariaDB 10.11"
        $logPath = Join-Path $ScriptDir "install_msi.log"

        $msiArgs = @(
            "/i", $LocalMsiPath,
            "/qn",
            "SERVICENAME=$ServiceName",
            "PORT=$Port",
            "PASSWORD=$RootPassword",
            "ALLOWREMOTEROOTACCESS=1",
            "UTF8=1",
            "/L*v", $logPath
        )

        Write-Host "msiexec calistiriliyor..."
        $process = Start-Process -FilePath "msiexec.exe" -ArgumentList $msiArgs -Wait -PassThru
        if ($process.ExitCode -ne 0) {
            Write-Err "MSI Kurulumu basarisiz oldu! Cikis Kodu: $($process.ExitCode)"
            if (Test-Path $logPath) {
                Write-Host "--- MSI Hata Log Özeti ($logPath) ---" -ForegroundColor Red
                Get-Content $logPath -Tail 30 | Write-Host -ForegroundColor Yellow
            }
            exit 1
        }
        Write-Success "MariaDB sunucusu basariyla kuruldu."
    }
}

# 5. MariaDB / MySQL Binary Yolunu Tespit Et
Write-Step "4/6: mysql.exe Komut Satırı Aracı Bulunuyor..."
$possiblePaths = @(
    "C:\Program Files\MariaDB 10.11\bin\mysql.exe",
    "C:\Program Files\MariaDB*\bin\mysql.exe",
    "C:\Program Files\MySQL\MySQL Server*\bin\mysql.exe",
    "C:\MariaDB\bin\mysql.exe"
)

foreach ($p in $possiblePaths) {
    $found = Resolve-Path $p -ErrorAction SilentlyContinue
    if ($found) {
        $mysqlBinPath = $found[0].Path
        break
    }
}

if (-not $mysqlBinPath) {
    # PATH ortam değişkenine bak
    $cmd = Get-Command "mysql.exe" -ErrorAction SilentlyContinue
    if ($cmd) {
        $mysqlBinPath = $cmd.Source
    }
}

if (-not $mysqlBinPath) {
    Write-Err "mysql.exe bulunamadi! Kurulum dizinini kontrol ediniz."
    exit 1
}
Write-Success "Kullanilacak MySQL istemcisi: $mysqlBinPath"

# 6. Windows Güvenlik Duvarı Kuralı (Windows Firewall Inbound Rule)
Write-Step "5/6: Yerel Ağdaki 3 Test Masası İçin Güvenlik Duvarı Portu (TCP 3306) Açılıyor..."
try {
    $existingRule = Get-NetFirewallRule -DisplayName "NETA MySQL Port 3306" -ErrorAction SilentlyContinue
    if (-not $existingRule) {
        New-NetFirewallRule -DisplayName "NETA MySQL Port 3306" `
                            -Direction Inbound `
                            -LocalPort $Port `
                            -Protocol TCP `
                            -Action Allow `
                            -Profile Any `
                            -Description "NETA DRX Anten Test Otomasyonu Icin Merkezi Veritabani Ag Portu" | Out-Null
        Write-Success "Guvenlik duvari kurali basariyla eklendi (TCP 3306 Allow)."
    } else {
        Write-Success "Guvenlik duvari kurali zaten mevcut."
    }
} catch {
    Write-Warn "Guvenlik duvari kurali eklenirken uyari olustu: $_"
}

# 7. Şema ve Başlangıç Verilerini İçe Aktarma (SQL Schema & Seed)
Write-Step "6/6: NETA Veritabanı Şeması ve Test Profilleri Yükleniyor..."
$SchemaFile = Join-Path $ProjectRoot "sql\schema_mysql.sql"
$SeedFile = Join-Path $ProjectRoot "sql\seed_data.sql"

if (-not (Test-Path $SchemaFile)) {
    Write-Err "Sema dosyasi bulunamadi: $SchemaFile"
    exit 1
}

# Uzaktan erişim ve standart şifre doğrulama izinlerini ayarla (mysql_native_password)
$setupSql = @"
ALTER USER 'root'@'localhost' IDENTIFIED VIA mysql_native_password USING PASSWORD('$RootPassword');
ALTER USER 'root'@'127.0.0.1' IDENTIFIED VIA mysql_native_password USING PASSWORD('$RootPassword');
CREATE USER IF NOT EXISTS 'root'@'%' IDENTIFIED VIA mysql_native_password USING PASSWORD('$RootPassword');
ALTER USER 'root'@'%' IDENTIFIED VIA mysql_native_password USING PASSWORD('$RootPassword');
GRANT ALL PRIVILEGES ON *.* TO 'root'@'%' WITH GRANT OPTION;
GRANT ALL PRIVILEGES ON *.* TO 'root'@'localhost' WITH GRANT OPTION;
GRANT ALL PRIVILEGES ON *.* TO 'root'@'127.0.0.1' WITH GRANT OPTION;

CREATE USER IF NOT EXISTS 'neta'@'%' IDENTIFIED VIA mysql_native_password USING PASSWORD('$RootPassword');
ALTER USER 'neta'@'%' IDENTIFIED VIA mysql_native_password USING PASSWORD('$RootPassword');
CREATE USER IF NOT EXISTS 'neta'@'localhost' IDENTIFIED VIA mysql_native_password USING PASSWORD('$RootPassword');
ALTER USER 'neta'@'localhost' IDENTIFIED VIA mysql_native_password USING PASSWORD('$RootPassword');
CREATE USER IF NOT EXISTS 'neta'@'127.0.0.1' IDENTIFIED VIA mysql_native_password USING PASSWORD('$RootPassword');
ALTER USER 'neta'@'127.0.0.1' IDENTIFIED VIA mysql_native_password USING PASSWORD('$RootPassword');
GRANT ALL PRIVILEGES ON *.* TO 'neta'@'%' WITH GRANT OPTION;
GRANT ALL PRIVILEGES ON *.* TO 'neta'@'localhost' WITH GRANT OPTION;
GRANT ALL PRIVILEGES ON *.* TO 'neta'@'127.0.0.1' WITH GRANT OPTION;
FLUSH PRIVILEGES;
"@

$tempSql = Join-Path $ScriptDir "temp_perm.sql"
Set-Content -Path $tempSql -Value $setupSql -Encoding UTF8

Write-Host "Kullanici yetkileri ayarlaniyor..."
& "$mysqlBinPath" -u root "-p$RootPassword" -e "$setupSql" 2>&1 | Out-Null
Remove-Item $tempSql -Force -ErrorAction SilentlyContinue

Write-Host "Tablolar olusturuluyor (schema_mysql.sql)..."
Get-Content -Path $SchemaFile -Raw -Encoding UTF8 | & "$mysqlBinPath" -u root "-p$RootPassword"

if (Test-Path $SeedFile) {
    Write-Host "Standart Türksat test kanalları yukleniyor (seed_data.sql)..."
    Get-Content -Path $SeedFile -Raw -Encoding UTF8 | & "$mysqlBinPath" -u root "-p$RootPassword"
}

# 8. Doğrulama ve Sonuç Özeti
$testQuery = "USE neta_antenna_test; SELECT count(*) FROM profile_channels;"
$count = & "$mysqlBinPath" -u root "-p$RootPassword" -N -s -e "$testQuery"

$localIps = Get-NetIPAddress -AddressFamily IPv4 | Where-Object { $_.InterfaceAlias -notlike "*Loopback*" -and $_.IPAddress -notlike "169.254*" } | Select-Object -ExpandProperty IPAddress

Write-Host "`n========================================================" -ForegroundColor Green
Write-Host "   KURULUM BAŞARIYLA TAMAMLANDI!                         " -ForegroundColor Green
Write-Host "========================================================" -ForegroundColor Green
Write-Host "Veritabani Adi    : neta_antenna_test" -ForegroundColor White
Write-Host "Yuklenen Kanal    : $count adet profil kanali hazir" -ForegroundColor White
Write-Host "Kullanici Adi     : root (veya neta)" -ForegroundColor White
Write-Host "Sifre             : $RootPassword" -ForegroundColor White
Write-Host "Port              : $Port (Aga Acik)" -ForegroundColor White
Write-Host "`nTest Masalarinin (Client) Baglanacagi Yonetici PC IP'leri:" -ForegroundColor Yellow
foreach ($ip in $localIps) {
    Write-Host "  -> $ip" -ForegroundColor Cyan
}
Write-Host "========================================================`n" -ForegroundColor Green
