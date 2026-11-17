-- ==============================================================================
-- NETA DRX9000 Antenna Test Automation - Initial Seed Data
-- Simple A1-A2 English Descriptions
-- ==============================================================================

USE `neta_antenna_test`;

-- 1. Default User Accounts (Default Password: 123)
INSERT INTO `users` (`username`, `password_hash`, `full_name`, `role`, `is_active`)
VALUES 
    ('admin', '123', 'System Administrator', 'ADMIN', 1),
    ('operator1', '123', 'Station 1 Operator', 'OPERATOR', 1),
    ('operator2', '123', 'Station 2 Operator', 'OPERATOR', 1),
    ('operator3', '123', 'Station 3 Operator', 'OPERATOR', 1)
ON DUPLICATE KEY UPDATE `password_hash` = VALUES(`password_hash`), `full_name` = VALUES(`full_name`);

-- 2. System Settings (Simple A1-A2 English descriptions)
INSERT INTO `system_settings` (`setting_key`, `setting_value`, `description`)
VALUES 
    ('LOCK_TIMEOUT_SEC', '90', 'Safety timeout for reference device lock (seconds)'),
    ('SAMPLE_DURATION_SEC', '5', 'Signal measurement time per channel (seconds)'),
    ('DEFAULT_TOLERANCE_DB', '1.50', 'Default allowed Delta dB limit'),
    ('TCP_PORT', '9100', 'Main Host TCP port for commands and sync'),
    ('UDP_BEACON_PORT', '9101', 'Local network port for auto discovery'),
    ('REFERENCE_MODE', 'LIVE', 'Reference mode: LIVE (Live Antenna) or GOLDEN (Saved Golden Sample)'),
    ('ACTIVE_GOLDEN_RECORD_ID', '0', 'Active Golden Sample record ID selected by Admin'),
    ('REPORT_OUTPUT_DIR', 'reports', 'Folder path to save test reports')
ON DUPLICATE KEY UPDATE `description` = VALUES(`description`);

-- 3. Default Turksat 42.0E Test Profile
INSERT INTO `satellite_profiles` (`id`, `profile_name`, `satellite_name`, `orbital_position`, `default_tolerance_db`, `is_active`)
VALUES (1, 'Türksat Standart Üretim Testi (10 Kanal)', 'Türksat 42.0°E', '42.0E', 1.50, 1)
ON DUPLICATE KEY UPDATE `profile_name` = VALUES(`profile_name`);

-- 4. Default 10 Test Channels for Profile 1
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

-- 5. Default Production Work Order
INSERT INTO `work_orders` (`id`, `order_no`, `customer_name`, `product_code`, `satellite_profile_id`, `target_quantity`, `status`, `notes`)
VALUES (1, 'NETA-2026-001', 'Genel Üretim', 'DRX9000-ANT-V1', 1, 100, 'IN_PROGRESS', 'Standard factory production batch 1')
ON DUPLICATE KEY UPDATE `customer_name` = VALUES(`customer_name`);
