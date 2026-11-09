@echo off
chcp 65001 >nul
title NETA DRX9000 - Yonetici Paylasim ve Ag Kurulumu

:: Yonetici (Administrator) yetkisi kontrolu
openfiles >nul 2>&1
if %errorLevel% neq 0 (
    echo [UYARI] Windows Yonetici (UAC) yetkisi gerekiyor...
    powershell -NoProfile -ExecutionPolicy Bypass -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
    exit /b
)

set "CURRENT_DIR=%~dp0"
:: Sondaki ters slash'i temizle
if "%CURRENT_DIR:~-1%"=="\" set "CURRENT_DIR=%CURRENT_DIR:~0,-1%"

cls
echo =====================================================================
echo       NETA DRX9000 - YONETICI PC AG PAYLASIM VE DAGITIM KURUCU
echo =====================================================================
echo.
echo Bu betik, bu klasoru yerel agda diger 3 istasyonun otomatik
echo guncelleme alabilmesi icin guvenli paylasima acacaktir.
echo.
echo Hedef Klasor: %CURRENT_DIR%
echo Paylasim Adi : NETA_Dagitim
echo.

:: 1. Eski paylasim varsa kapat
net share NETA_Dagitim /delete /y >nul 2>&1

:: 2. Klasoru yerel agda salt-okunur olarak paylasima ac
echo [1/4] Klasor agda paylasima aciliyor...
net share NETA_Dagitim="%CURRENT_DIR%" /grant:everyone,READ >nul
if %errorLevel% equ 0 (
    echo      [BASARILI] Klasor agda paylasildi!
) else (
    echo      [HATA] Klasor paylasima acilamadi. Lutfen yetkileri kontrol edin.
)

:: 3. Windows Guvenlik Duvarinda Gerekli Portlari Ac
echo [2/4] Guvenlik duvari kurallari tanimlaniyor...
netsh advfirewall firewall set rule group="Dosya ve Yazıcı Paylaşımı" new enable=Yes >nul 2>&1
netsh advfirewall firewall set rule group="File and Printer Sharing" new enable=Yes >nul 2>&1

:: MySQL 3306
netsh advfirewall firewall add rule name="NETA MySQL Database (Port 3306)" dir=in action=allow protocol=TCP localport=3306 profile=any >nul 2>&1
:: NETA Host TCP 45454
netsh advfirewall firewall add rule name="NETA Host TCP (Port 45454)" dir=in action=allow protocol=TCP localport=45454 profile=any >nul 2>&1
:: NETA UDP Beacon 45455
netsh advfirewall firewall add rule name="NETA UDP Beacon (Port 45455)" dir=in action=allow protocol=UDP localport=45455 profile=any >nul 2>&1
echo      [BASARILI] Portlar (3306, 45454, 45455) ve Dosya Paylasimi izinlendirildi.

:: 4. Yonetici IP ve Bilgisayar Adini Al
echo [3/4] Ag baglantisi tespit ediliyor...
for /f "tokens=2 delims=:" %%a in ('ipconfig ^| findstr /c:"IPv4" /c:"IP Address"') do (
    set "HOST_IP=%%a"
    goto :found_ip
)
:found_ip
:: Bosluklari temizle
set HOST_IP=%HOST_IP: =%
set HOST_NAME=%COMPUTERNAME%

echo      Yonetici Bilgisayar Adi : %HOST_NAME%
echo      Yonetici IP Adresi      : %HOST_IP%
echo      Ag Paylasim Yolu        : \\%HOST_NAME%\NETA_Dagitim  veya  \\%HOST_IP%\NETA_Dagitim

:: 5. Istasyonlar icin hazir baslaticilari olustur
echo [4/4] Istasyonlar icin ozel baslatici betikleri olusturuluyor...
set "OUT_DIR=%CURRENT_DIR%\ISTASYONLAR_ICIN_BASLATICILAR"
if not exist "%OUT_DIR%" mkdir "%OUT_DIR%"

:: Masa 1
(
echo @echo off
echo chcp 65001 ^>nul
echo title NETA DRX9000 - Istasyon 1 Baslatiliyor...
echo.
echo [1/2] Yonetici PC'den guncel surum kontrol ediliyor...
echo robocopy "\\%HOST_NAME%\NETA_Dagitim" "C:\NETA_Istasyon" /MIR /XO /XF "*.bat" "*.msi" "*.log" /NJH /NJS /ndl /nc /ns
echo robocopy "\\%HOST_NAME%\NETA_Dagitim" "C:\NETA_Istasyon" /MIR /XO /XF "*.bat" "*.msi" "*.log" /NJH /NJS /ndl /nc /ns ^>nul 2^>^&1
echo if errorlevel 8 ^(
echo     echo [UYARI] Ag baglantisi kurulamadi, yerel kopya ile devam ediliyor...
echo ^) else ^(
echo     echo [BASARILI] Program guncel!
echo ^)
echo.
echo [2/2] Istasyon 1 baslatiliyor...
echo start "" "C:\NETA_Istasyon\NETA_Antenna_Test.exe" --client -s 1
) > "%OUT_DIR%\NETA_Istasyon_1_Baslat.bat"

:: Masa 2
(
echo @echo off
echo chcp 65001 ^>nul
echo title NETA DRX9000 - Istasyon 2 Baslatiliyor...
echo.
echo [1/2] Yonetici PC'den guncel surum kontrol ediliyor...
echo robocopy "\\%HOST_NAME%\NETA_Dagitim" "C:\NETA_Istasyon" /MIR /XO /XF "*.bat" "*.msi" "*.log" /NJH /NJS /ndl /nc /ns ^>nul 2^>^&1
echo if errorlevel 8 ^(
echo     echo [UYARI] Ag baglantisi kurulamadi, yerel kopya ile devam ediliyor...
echo ^) else ^(
echo     echo [BASARILI] Program guncel!
echo ^)
echo.
echo [2/2] Istasyon 2 baslatiliyor...
echo start "" "C:\NETA_Istasyon\NETA_Antenna_Test.exe" --client -s 2
) > "%OUT_DIR%\NETA_Istasyon_2_Baslat.bat"

:: Masa 3
(
echo @echo off
echo chcp 65001 ^>nul
echo title NETA DRX9000 - Istasyon 3 Baslatiliyor...
echo.
echo [1/2] Yonetici PC'den guncel surum kontrol ediliyor...
echo robocopy "\\%HOST_NAME%\NETA_Dagitim" "C:\NETA_Istasyon" /MIR /XO /XF "*.bat" "*.msi" "*.log" /NJH /NJS /ndl /nc /ns ^>nul 2^>^&1
echo if errorlevel 8 ^(
echo     echo [UYARI] Ag baglantisi kurulamadi, yerel kopya ile devam ediliyor...
echo ^) else ^(
echo     echo [BASARILI] Program guncel!
echo ^)
echo.
echo [2/2] Istasyon 3 baslatiliyor...
echo start "" "C:\NETA_Istasyon\NETA_Antenna_Test.exe" --client -s 3
) > "%OUT_DIR%\NETA_Istasyon_3_Baslat.bat"

:: Masaüstüne Yönetici Kısayolu Bırak
echo.
echo =====================================================================
echo                      KURULUM TAMAMLANDI!
echo =====================================================================
echo.
echo YAPMANIZ GEREKEN TEK SEY:
echo  1. "%OUT_DIR%" klasorundeki:
echo     - 'NETA_Istasyon_1_Baslat.bat' dosyasini 1. Masanin Masaustune kopyalayin.
echo     - 'NETA_Istasyon_2_Baslat.bat' dosyasini 2. Masanin Masaustune kopyalayin.
echo     - 'NETA_Istasyon_3_Baslat.bat' dosyasini 3. Masanin Masaustune kopyalayin.
echo.
echo  2. Artık siz bu klasore her yeni 'NETA_Antenna_Test.exe' attiginizda;
echo     istasyonlar baslatildigi anda 1 saniyede otomatik guncellenecektir!
echo =====================================================================
echo.
pause
