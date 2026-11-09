-- ==============================================================================
-- NETA DRX9000 Anten Test Otomasyonu - Başlangıç Verileri (Seed Data)
-- Tarih: 03.11.2026
-- ==============================================================================

USE `neta_antenna_test`;

-- 1. Varsayılan Kullanıcılar (Şifre: neta123)
INSERT INTO `users` (`username`, `password_hash`, `full_name`, `role`, `is_active`)
VALUES 
    ('admin', 'neta123', 'Sistem Yöneticisi', 'ADMIN', 1),
    ('operator1', 'op123', 'Masa 1 Operatörü', 'OPERATOR', 1),
    ('operator2', 'op123', 'Masa 2 Operatörü', 'OPERATOR', 1),
    ('operator3', 'op123', 'Masa 3 Operatörü', 'OPERATOR', 1)
ON DUPLICATE KEY UPDATE `full_name` = VALUES(`full_name`);

-- 2. Sistem Ayarları (Admin tarafından UI'dan değiştirilebilir)
INSERT INTO `system_settings` (`setting_key`, `setting_value`, `description`)
VALUES 
    ('LOCK_TIMEOUT_SEC', '90', 'Referans cihazı kilit emniyet zaman aşımı süresi (saniye)'),
    ('SAMPLE_DURATION_SEC', '5', 'Kanal başına ortalama alma / telemetri toplama süresi (saniye)'),
    ('DEFAULT_TOLERANCE_DB', '1.50', 'Varsayılan Delta dB izin verilen tolerans payı'),
    ('TCP_PORT', '9100', 'Merkezi Host TCP komut ve senkronizasyon portu'),
    ('UDP_BEACON_PORT', '9101', 'LAN lobi anonsu ve otomatik keşif portu'),
    ('REPORT_OUTPUT_DIR', 'reports', 'Excel test raporlarının kaydedileceği yerel/ağ dizini')
ON DUPLICATE KEY UPDATE `description` = VALUES(`description`);

-- 3. Örnek Türksat 42.0°E Test Profili
INSERT INTO `satellite_profiles` (`id`, `profile_name`, `satellite_name`, `orbital_position`, `default_tolerance_db`, `is_active`)
VALUES (1, 'Türksat Standart Üretim Testi (10 Kanal)', 'Türksat 42.0°E', '42.0E', 1.50, 1)
ON DUPLICATE KEY UPDATE `profile_name` = VALUES(`profile_name`);

-- 4. Profil 1 için 10 Standart Test Kanalı (Farklı polarizasyon ve frekans dağılımları)
INSERT INTO `profile_channels` (`profile_id`, `channel_no`, `channel_name`, `frequency_mhz`, `symbol_rate`, `polarization`, `tolerance_db`)
VALUES 
    (1, 1,  'TRT 1 HD',         11054, 30000, 'V', 1.50),
    (1, 2,  'TRT SPOR HD',      11958, 27500, 'V', 1.50),
    (1, 3,  'ATV HD',           12053, 27500, 'H', 1.50),
    (1, 4,  'KANAL D HD',       12245, 27500, 'H', 1.50),
    (1, 5,  'STAR TV HD',       12015, 27500, 'H', 1.50),
    (1, 6,  'SHOW TV HD',       12210, 10000, 'H', 1.50),
    (1, 7,  'A HABER HD',       12053, 27500, 'H', 1.50),
    (1, 8,  'CNN TURK HD',      12245, 27500, 'H', 1.50),
    (1, 9,  'HABERTURK HD',     12209, 10000, 'H', 1.50),
    (1, 10, 'NTV HD',           12015, 27500, 'H', 1.50)
ON DUPLICATE KEY UPDATE `channel_name` = VALUES(`channel_name`);

-- 5. Örnek Sipariş
INSERT INTO `work_orders` (`id`, `order_no`, `customer_name`, `product_code`, `satellite_profile_id`, `target_quantity`, `status`, `notes`)
VALUES (1, 'NETA-2026-001', 'Genel Üretim', 'DRX9000-ANT-V1', 1, 100, 'IN_PROGRESS', 'Fabrika seri üretim parti 1')
ON DUPLICATE KEY UPDATE `customer_name` = VALUES(`customer_name`);
