; NETA DRX9000 ANTENNA Test Otomasyonu - Inno Setup 7 Kurulum Scripti
#define MyAppName "NETA DRX9000 ANTENNA TEST AUTOMATION"
#define MyAppVersion "1.0.0"
#define MyAppPublisher "NETA Elektronik A.S. (Semi Eren Gökyıldız)"
#define MyAppURL "https://www.neta.com.tr"
#define MyAppExeName "NETA_Antenna_Test.exe"

[Setup]
AppId={{A38D5C22-6B9E-43B1-94E5-B890F3D3C281}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}
AppCopyright=Copyright (C) 2026 NETA Elektronik A.S. - Gelistirici: Semi Eren Gokyildiz
AppComments=NETA DRX9000 ANTENNA TEST AUTOMATION - Gelistirici: Semi Eren Gokyildiz
VersionInfoDescription=NETA DRX9000 ANTENNA TEST AUTOMATION - Gelistirici: Semi Eren Gokyildiz
VersionInfoProductName={#MyAppName}
VersionInfoCompany=NETA Elektronik A.S.
VersionInfoCopyright=Copyright (C) 2026 NETA Elektronik A.S. - Gelistirici: Semi Eren Gokyildiz
DefaultDirName={autopf}\NETA\DRX9000_Antenna_Test
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=auto
OutputDir=..\..\SETUP_OUTPUT
OutputBaseFilename=NETA_DRX9000_ANTENNA_Test_Setup_v{#MyAppVersion}
SetupIconFile=..\..\resources\images\neta_app.ico
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=admin
ArchitecturesInstallIn64BitMode=
CloseApplications=yes
RestartApplications=no

[Languages]
Name: "turkish"; MessagesFile: "compiler:Languages\Turkish.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
; OUTPUT klasöründeki tüm dosyaları ve alt klasörleri kurulum dizinine kopyala
Source: "..\..\OUTPUT\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\0. Yönetici Sunucuyu Başlat"; Filename: "{app}\0_Yonetici_Sunucuyu_Baslat.bat"
Name: "{group}\1. Ağ Paylaşımını Yapılandır"; Filename: "{app}\1_Yonetici_Klasor_Paylasimini_Ac.bat"
Name: "{group}\2. Taşınabilir Veritabanını Başlat"; Filename: "{app}\0_Veritabanini_Baslat.bat"
Name: "{group}\3. Taşınabilir Veritabanını Durdur"; Filename: "{app}\0_Veritabanini_Durdur.bat"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent
