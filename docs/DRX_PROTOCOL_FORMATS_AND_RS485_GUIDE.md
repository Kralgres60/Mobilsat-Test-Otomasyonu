# 📡 DRX Protokol Format Şablonları & Donanım Soyutlama (HAL) Rehberi

Bu doküman, NETA DRX9000 Anten Test Otomasyonu projesinde donanım haberleşme formatlarının modülerleştirilmesini, `config/drx_protocols.ini` şablon dosyasının kullanımını, ortak `sendData` Donanım Soyutlama (HAL) mimarisini ve **RS232 vs RS485 vs REST API** donanım araştırmasını açıklar.

---

## 📌 1. Neden Dinamik Format Şablonları?

Sahada veya laboratuvarda kullanılan DRX9000 veya gelecekteki yeni nesil alıcıların (DRX-X vb.) firmware güncellemelerinde:
- CGI POST parametre adları (`fr_frequency` ➔ `freq`, `query=alarmStatus` ➔ `get_status` gibi) değişebilir.
- Seri port (RS232 / USB-UART) komut sözdizimi (`tune 11054 30000 V` ➔ `SET_FREQ:11054,30000,V` gibi) değişebilir.
- Gelen JSON yanıtındaki alan adları (`power` yerine `rf_power`, `cn` yerine `snr` gibi) değişebilir.

Bu gibi durumlarda **C++ kaynak kodunu değiştirmeye ve uygulamayı yeniden derlemeye gerek kalmaksızın**, yalnızca `config/drx_protocols.ini` metin dosyasındaki şablon satırını düzenleyerek sisteme anında uyum sağlanır.

---

## ⚙️ 2. Yapılandırma Dosyası: `config/drx_protocols.ini`

Uygulamanın çalıştığı dizindeki `config/drx_protocols.ini` dosyası aşağıdaki bölümlerden oluşur:

### A. `[REST_CGI]` (Ethernet / Web REST API)
```ini
[REST_CGI]
# İstek Uç Noktası
EndpointPath=/cgi-bin/drx.cgi

# İstek Gönderme Formatları (Placeholders: {sessionKey}, {username}, {password}, {freqKhz}, {srate}, {pol}, {query})
LoginPayloadFormat=ResponseTo=login\nsessionKey=\nlg_username={username}\nlg_password={password}\n
StatusPayloadFormat=query=alarmStatus\nsessionKey={sessionKey}\n
DeviceInfoPayloadFormat=query=deviceInfo\nsessionKey={sessionKey}\n
TunePayloadFormat=ResponseTo=freq\nsessionKey={sessionKey}\nfr_frequency={freqKhz}\nfr_srate={srate}\nfr_pol={pol}\n
RawQueryFormat=query={query}\nsessionKey={sessionKey}\n

# Gelen JSON Alan Eşlemeleri (Virgülle ayrılmış alternatif adlar)
SessionKeyFields=sessionkey,sessionKey,key
PowerFields=power,pow,signal_power,rf_power
CnFields=cn,snr,cn_db,c_n
BerFields=ber,bit_error_rate
EbN0Fields=ebn0,eb_n0,ebno
CarrierLockFields=carrier_lock,carrier,lock,signal_locked
VideoLockFields=video_lock,video,video_locked
AudioLockFields=audio_lock,audio,audio_locked
ChannelNameFields=channel_name,service_name,ch_name,channel
```

### B. `[RS232_SERIAL]` (Seri Port & USB-UART)
```ini
[RS232_SERIAL]
# Komut İstemci Tespiti ve Durum Ön Ekleri
PromptDetectionTokens=-->,Select Option:,:
StatusResponsePrefixes=getstatus:,status:

# Komut Gönderme Formatları (Placeholders: {freqMhz}, {srate}, {pol})
StatusRequestFormat=getstatus\n
DeviceInfoRequestFormat=getstatus\n
TuneRequestFormat=tune {freqMhz} {srate} {pol}\n
TunerLogOnFormat=tunerlog on\n
TunerLogOffFormat=tunerlog off\n

# Gelen ASCII Telemetri ve Kilit Bayrakları
CarrierLockToken=S:+
VideoLockToken=V:+
AudioLockToken=A:+
PowerRegex=POW:\s*([+-]?[0-9.]+)\s*(?:dBm)?
CnRegex=C/N:\s*([+-]?[0-9.]+)\s*(?:dB)?
BerRegex=BER:\s*([0-9.eE+-]+)
```

### C. `[RS485_BUS]` (Endüstriyel Çok Noktalı Bus)
```ini
[RS485_BUS]
NodeAddress=1
FramingMode=ASCII
StatusRequestFormat=:{addr:02d}0300000002\r\n
TuneRequestFormat=:{addr:02d}06{freqMhz:04d}{srate:05d}{pol}\r\n
```

---

## 🔌 3. Donanım Soyutlama Katmanı (HAL) & Ortak `sendData`

`DataTransfer` saf sanal taban sınıfı, hem **Ethernet (TCP/HTTP)** hem de **RS232/USB (Seri)** için ortak bir `sendData` boru hattı sunar:

```
                          ┌───────────────────────┐
                          │   TestStateMachine    │
                          └──────────┬────────────┘
                                     │
                        IDrxDeviceTransport / DataTransfer
                                     │
           ┌─────────────────────────┴─────────────────────────┐
           ▼                                                   ▼
┌───────────────────────┐                           ┌───────────────────────┐
│       Ethernet        │                           │         RS232         │
│ (CAT6 Web CGI / REST) │                           │  (USB-UART / RS485)   │
└──────────┬────────────┘                           └──────────┬────────────┘
           │                                                   │
           ├─────────────────────────┬─────────────────────────┤
           ▼                         ▼                         ▼
   sendData(QByteArray)     sendData(QByteArray)     sendData(QByteArray)
    -> HTTP POST Payload     -> Serial Port Write     -> RS485 Bus Write
```

- **Ortak ve Bağımsız:** Üst seviye test yöneticisi (`TestStateMachine`) donanımın fiziksel soketini bilmez; `sendTune()`, `requestStatus()` veya ham veri gerektiğinde `sendData(QByteArray)` çağırır.
- Formatlama işlemi merkezi `DrxProtocolConfig::instance()` motorunda yürütüldükten sonra ilgili fiziksel taşıyıcıya (`m_netManager.post()` veya `m_serial.write()`) aktarılır.

---

## 🔬 4. RS232 vs RS485 vs REST API Donanım Karşılaştırması ve Araştırması

| Özellik | RS232 (Seri Konsol) | RS485 (Endüstriyel Bus) | REST Web CGI (Ethernet) |
| :--- | :--- | :--- | :--- |
| **Sinyalleşme Tipi** | Tek Uçlu (Single-ended, GND referanslı) | Diferansiyel (Balanced Differential, A-B uçları) | Diferansiyel Pals (CAT6 100Base-TX) |
| **Gürültü Dayanımı (EMI)** | Düşük (Fabrika motorları/invertörlerden etkilenir) | **Çok Yüksek** (Endüstriyel parazitleri ortak modda yok eder) | **Çok Yüksek** (Manyetik trafo izolasyonlu) |
| **Maksimum İletim Mesafesi** | ~15 metre (Baud rate düştükçe biraz artar) | **1200 metre** (4000 feet) | **100 metre** (Switch ile sınırsız) |
| **Ağ Topolojisi** | Noktadan Noktaya (1 Host ➔ 1 Cihaz) | **Çok Noktalı (Multi-drop, 32+ cihaz tek hatta)** | Yıldız (Switch üzerinden çoklu istemci) |
| **İletişim Yönü (Duplex)** | Full-Duplex (Rx / Tx ayrı hatlar) | Genellikle **Half-Duplex (2 telli)** veya Full-Duplex (4 telli) | Full-Duplex |
| **Yön Kontrolü Gereksinimi** | Yok (Hatlar bağımsız) | **Var:** Veri göndermeden önce DE (Driver Enable), dinlerken RE (Receiver Enable) pini çekilmelidir | Yok (TCP/IP stack yönetir) |
| **Adresleme Mekanizması** | Yok (Hatta tek cihaz vardır) | **Var:** Her komut paketinde düğüm adresi (`Node ID: 1..255`) yer alır | IP Adresi & Port (`192.168.0.x:80`) |
| **Veri Güvenliği / Checksum**| Opsiyonel Parity | Genellikle Modbus CRC-16 veya ASCII LRC | TCP Checksum & HTTP Durum Kodları |

### 🛠️ Fabrika Test Sahasında RS485 Kullanım Avantajları ve Entegrasyon
1. **Tek Bir USB Portundan Tüm Cihazların Yönetimi:**
   - RS232'de 3 cihaz için 3 ayrı COM portu gerekirken, RS485 Multi-drop sayesinde bilgisayara takılan **tek bir USB-RS485 adaptörü** ile 3 istasyon cihazı ve 1 referans cihazı paralel bağlanabilir.
2. **Kablo Karmaşasının Önlenmesi:**
   - 2 telli bükümlü çift (twisted pair) ve sonlandırma direnci (120Ω termination resistor) ile fabrika sahası boyunca yüzlerce metre kayıpsız iletişim sağlanır.
3. **`config/drx_protocols.ini` Hazırlığı:**
   - Hazırladığımız `[RS485_BUS]` şablonunda `NodeAddress=1`, `FramingMode=ASCII` tanımlanmış olup, `:{addr:02d}06{freqMhz:04d}...` formatıyla cihaz bazlı adresleme C++ derlemesine gerek kalmadan doğrudan devreye alınabilir.
