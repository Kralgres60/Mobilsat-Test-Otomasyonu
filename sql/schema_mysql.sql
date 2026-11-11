-- ==============================================================================
-- NETA DRX9000 Anten Test Otomasyonu - Veritabanı Şeması (MySQL / MariaDB)
-- Tarih: 03.11.2026
-- ==============================================================================

CREATE DATABASE IF NOT EXISTS `neta_antenna_test` 
CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;

USE `neta_antenna_test`;

-- 1. Kullanıcılar & Yetkiler (Operatör ve Admin)
CREATE TABLE IF NOT EXISTS `users` (
    `id` INT AUTO_INCREMENT PRIMARY KEY,
    `username` VARCHAR(50) NOT NULL UNIQUE,
    `password_hash` VARCHAR(128) NOT NULL,
    `full_name` VARCHAR(100) NOT NULL,
    `role` ENUM('ADMIN', 'OPERATOR') NOT NULL DEFAULT 'OPERATOR',
    `is_active` TINYINT(1) NOT NULL DEFAULT 1,
    `created_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP
) ENGINE=InnoDB;

-- 2. Üretim Siparişleri / İş Emirleri (Work Orders)
CREATE TABLE IF NOT EXISTS `work_orders` (
    `id` INT AUTO_INCREMENT PRIMARY KEY,
    `order_no` VARCHAR(64) NOT NULL UNIQUE,
    `customer_name` VARCHAR(128) NOT NULL,
    `product_code` VARCHAR(64) NOT NULL,
    `satellite_profile_id` INT DEFAULT NULL,
    `target_quantity` INT NOT NULL DEFAULT 0,
    `completed_quantity` INT NOT NULL DEFAULT 0,
    `passed_quantity` INT NOT NULL DEFAULT 0,
    `failed_quantity` INT NOT NULL DEFAULT 0,
    `status` ENUM('PENDING', 'IN_PROGRESS', 'COMPLETED', 'CANCELLED') NOT NULL DEFAULT 'PENDING',
    `notes` TEXT,
    `created_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    `updated_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP
) ENGINE=InnoDB;

-- 3. Uydu Test Profilleri (Satellite Profiles)
CREATE TABLE IF NOT EXISTS `satellite_profiles` (
    `id` INT AUTO_INCREMENT PRIMARY KEY,
    `profile_name` VARCHAR(100) NOT NULL UNIQUE,
    `satellite_name` VARCHAR(100) NOT NULL,
    `orbital_position` VARCHAR(20) DEFAULT '42.0E',
    `default_tolerance_db` DECIMAL(4, 2) NOT NULL DEFAULT 1.50,
    `is_active` TINYINT(1) NOT NULL DEFAULT 1,
    `created_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP
) ENGINE=InnoDB;

-- 4. Profil Kanal Tanımları (Her Profil İçin Kanallar + Golden Sample Referans Değerleri)
CREATE TABLE IF NOT EXISTS `profile_channels` (
    `id` INT AUTO_INCREMENT PRIMARY KEY,
    `profile_id` INT NOT NULL,
    `channel_no` TINYINT NOT NULL, -- 1 .. 10
    `channel_name` VARCHAR(64) NOT NULL,
    `frequency_mhz` INT NOT NULL,
    `symbol_rate` INT NOT NULL,
    `polarization` ENUM('V', 'H') NOT NULL,
    `tolerance_db` DECIMAL(4, 2) NOT NULL DEFAULT 1.50,
    `golden_cn_db` DECIMAL(5, 2) NOT NULL DEFAULT 0.00,
    `golden_pow_dbm` DECIMAL(6, 2) NOT NULL DEFAULT 0.00,
    `has_golden_sample` TINYINT(1) NOT NULL DEFAULT 0,
    `golden_updated_at` TIMESTAMP NULL DEFAULT NULL,
    `created_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (`profile_id`) REFERENCES `satellite_profiles`(`id`) ON DELETE CASCADE,
    UNIQUE KEY `uk_profile_channel` (`profile_id`, `channel_no`)
) ENGINE=InnoDB;

-- 5. Test Koşuları (Test Runs - Her Anten İçin Bir Kayıt)
CREATE TABLE IF NOT EXISTS `test_runs` (
    `id` BIGINT AUTO_INCREMENT PRIMARY KEY,
    `work_order_id` INT DEFAULT NULL,
    `station_id` TINYINT NOT NULL, -- 1, 2, 3
    `operator_id` INT DEFAULT NULL,
    `tester_name` VARCHAR(100) NOT NULL DEFAULT '',
    `antenna_sn` VARCHAR(64) NOT NULL,
    `satellite_profile_id` INT NOT NULL,
    `test_timestamp` TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    `duration_sec` INT NOT NULL DEFAULT 0,
    `overall_result` ENUM('PASS', 'FAIL', 'ABORTED') NOT NULL,
    `fail_reason` VARCHAR(255) DEFAULT NULL,
    `avg_delta_db` DECIMAL(5, 2) DEFAULT 0.00,
    `max_delta_db` DECIMAL(5, 2) DEFAULT 0.00,
    `notes` TEXT DEFAULT NULL,
    FOREIGN KEY (`work_order_id`) REFERENCES `work_orders`(`id`) ON DELETE SET NULL,
    FOREIGN KEY (`satellite_profile_id`) REFERENCES `satellite_profiles`(`id`),
    INDEX `idx_antenna_sn` (`antenna_sn`),
    INDEX `idx_timestamp` (`test_timestamp`),
    INDEX `idx_station` (`station_id`)
) ENGINE=InnoDB;

-- 6. Test Koşusu Kanal Detayları (10 Kanalın Örnekleme Verileri)
CREATE TABLE IF NOT EXISTS `test_run_channels` (
    `id` BIGINT AUTO_INCREMENT PRIMARY KEY,
    `test_run_id` BIGINT NOT NULL,
    `channel_no` TINYINT NOT NULL, -- 1 .. 10
    `channel_name` VARCHAR(64) NOT NULL,
    `frequency_mhz` INT NOT NULL,
    `symbol_rate` INT NOT NULL,
    `polarization` ENUM('V', 'H') NOT NULL,
    `dut_cn_avg` DECIMAL(5, 2) NOT NULL DEFAULT 0.00,
    `dut_pow_avg` DECIMAL(6, 2) NOT NULL DEFAULT 0.00,
    `ref_cn_avg` DECIMAL(5, 2) NOT NULL DEFAULT 0.00,
    `ref_pow_avg` DECIMAL(6, 2) NOT NULL DEFAULT 0.00,
    `delta_db` DECIMAL(5, 2) NOT NULL DEFAULT 0.00,
    `carrier_locked` TINYINT(1) NOT NULL DEFAULT 0, -- S:+
    `video_locked` TINYINT(1) NOT NULL DEFAULT 0,   -- V:+
    `audio_locked` TINYINT(1) NOT NULL DEFAULT 0,   -- A:+
    `tolerance_db` DECIMAL(4, 2) NOT NULL DEFAULT 1.50,
    `result` ENUM('PASS', 'FAIL', 'WARNING') NOT NULL,
    FOREIGN KEY (`test_run_id`) REFERENCES `test_runs`(`id`) ON DELETE CASCADE,
    INDEX `idx_run_channel` (`test_run_id`, `channel_no`)
) ENGINE=InnoDB;

-- 7. Sistem Ayarları ve Parametreler (Admin Tarafından Dinamik Ayarlanabilir)
CREATE TABLE IF NOT EXISTS `system_settings` (
    `setting_key` VARCHAR(64) PRIMARY KEY,
    `setting_value` VARCHAR(255) NOT NULL,
    `description` VARCHAR(255) DEFAULT NULL,
    `updated_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP
) ENGINE=InnoDB;

-- 8. Algoritma 2: 3 PC Eşzamanlı Mod İçin Dağıtık Referans Kilit Tablosu
CREATE TABLE IF NOT EXISTS `reference_lock` (
    `id` INT PRIMARY KEY DEFAULT 1,
    `locked_by_station` INT NOT NULL DEFAULT 0,      -- 0: Boşta, 1: Masa 1, 2: Masa 2, 3: Masa 3
    `current_channel_no` INT NOT NULL DEFAULT 0,     -- Test edilen anlık kanal (1..10)
    `lock_acquired_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    `heartbeat_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP -- Canlılık zaman damgası (Deadlock koruması)
) ENGINE=InnoDB;

INSERT INTO `reference_lock` (`id`, `locked_by_station`, `current_channel_no`)
VALUES (1, 0, 0)
ON DUPLICATE KEY UPDATE `id`=`id`;

-- 9. Yönetici PC Tarafından Ölçülen Frekans Bazlı Golden Sample Referans Havuzu
CREATE TABLE IF NOT EXISTS `golden_frequencies` (
    `id` INT AUTO_INCREMENT PRIMARY KEY,
    `label` VARCHAR(64) NOT NULL DEFAULT '',
    `frequency_mhz` INT NOT NULL,
    `symbol_rate` INT NOT NULL,
    `polarization` ENUM('V', 'H') NOT NULL,
    `golden_cn_db` DECIMAL(5, 2) NOT NULL DEFAULT 0.00,
    `golden_pow_dbm` DECIMAL(6, 2) NOT NULL DEFAULT 0.00,
    `tolerance_db` DECIMAL(4, 2) NOT NULL DEFAULT 1.50,
    `updated_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    UNIQUE KEY `uk_golden_freq` (`frequency_mhz`, `symbol_rate`, `polarization`)
) ENGINE=InnoDB;

