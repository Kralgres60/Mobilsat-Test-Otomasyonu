-- ==============================================================================
-- NETA DRX9000 Antenna Test Automation - Database Schema (MySQL / MariaDB)
-- Simple A1-A2 English Documentation
-- ==============================================================================

CREATE DATABASE IF NOT EXISTS `neta_antenna_test` 
CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;

USE `neta_antenna_test`;

-- 1. Users and Roles (Operator and Admin accounts)
CREATE TABLE IF NOT EXISTS `users` (
    `id` INT AUTO_INCREMENT PRIMARY KEY,
    `username` VARCHAR(50) NOT NULL UNIQUE,
    `password_hash` VARCHAR(128) NOT NULL,
    `full_name` VARCHAR(100) NOT NULL,
    `role` ENUM('ADMIN', 'OPERATOR') NOT NULL DEFAULT 'OPERATOR',
    `is_active` TINYINT(1) NOT NULL DEFAULT 1,
    `created_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP
) ENGINE=InnoDB;

-- 2. Production Work Orders (Customer jobs and target counts)
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

-- 3. Satellite Test Profiles (Satellite name and default tolerance)
CREATE TABLE IF NOT EXISTS `satellite_profiles` (
    `id` INT AUTO_INCREMENT PRIMARY KEY,
    `profile_name` VARCHAR(100) NOT NULL UNIQUE,
    `satellite_name` VARCHAR(100) NOT NULL,
    `orbital_position` VARCHAR(20) DEFAULT '42.0E',
    `default_tolerance_db` DECIMAL(4, 2) NOT NULL DEFAULT 1.50,
    `is_active` TINYINT(1) NOT NULL DEFAULT 1,
    `created_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP
) ENGINE=InnoDB;

-- 4. Profile Channels (Transponder frequencies for each satellite profile)
CREATE TABLE IF NOT EXISTS `profile_channels` (
    `id` INT AUTO_INCREMENT PRIMARY KEY,
    `profile_id` INT NOT NULL,
    `channel_no` INT NOT NULL, -- Channel number (1 to 50)
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

-- 5. Test Runs (One summary row for each tested antenna)
CREATE TABLE IF NOT EXISTS `test_runs` (
    `id` BIGINT AUTO_INCREMENT PRIMARY KEY,
    `work_order_id` INT DEFAULT NULL,
    `station_id` TINYINT NOT NULL,
    `operator_id` INT DEFAULT NULL,
    `tester_name` VARCHAR(100) NOT NULL DEFAULT '',
    `operator_username` VARCHAR(64) NOT NULL DEFAULT '',
    `antenna_sn` VARCHAR(64) NOT NULL,
    `satellite_profile_id` INT NOT NULL,
    `test_timestamp` DATETIME DEFAULT CURRENT_TIMESTAMP,
    `duration_sec` INT NOT NULL DEFAULT 0,
    `overall_result` ENUM('PASS', 'FAIL', 'ABORTED') NOT NULL,
    `fail_reason` TEXT DEFAULT NULL,
    `avg_delta_db` DECIMAL(5, 2) DEFAULT 0.00,
    `max_delta_db` DECIMAL(5, 2) DEFAULT 0.00,
    `last_drx_device_time` VARCHAR(64) DEFAULT NULL,
    `test_end_time` DATETIME DEFAULT NULL,
    `dt_reserved_ms` BIGINT DEFAULT 0,
    `notes` TEXT DEFAULT NULL,
    FOREIGN KEY (`work_order_id`) REFERENCES `work_orders`(`id`) ON DELETE SET NULL,
    FOREIGN KEY (`satellite_profile_id`) REFERENCES `satellite_profiles`(`id`),
    INDEX `idx_antenna_sn` (`antenna_sn`),
    INDEX `idx_timestamp` (`test_timestamp`),
    INDEX `idx_station` (`station_id`),
    INDEX `idx_test_runs_sn_time` (`antenna_sn`, `test_timestamp`),
    INDEX `idx_test_runs_wo` (`work_order_id`),
    INDEX `idx_test_runs_result` (`overall_result`)
) ENGINE=InnoDB;

-- 6. Test Run Channels (Detailed signal results for each channel in a test run)
CREATE TABLE IF NOT EXISTS `test_run_channels` (
    `id` BIGINT AUTO_INCREMENT PRIMARY KEY,
    `test_run_id` BIGINT NOT NULL,
    `channel_no` INT NOT NULL,
    `channel_name` VARCHAR(64) NOT NULL,
    `frequency_mhz` INT NOT NULL,
    `symbol_rate` INT NOT NULL,
    `polarization` ENUM('V', 'H') NOT NULL,
    `dut_cn_avg` DECIMAL(5, 2) NOT NULL DEFAULT 0.00,
    `dut_pow_avg` DECIMAL(6, 2) NOT NULL DEFAULT 0.00,
    `ref_cn_avg` DECIMAL(5, 2) NOT NULL DEFAULT 0.00,
    `ref_pow_avg` DECIMAL(6, 2) NOT NULL DEFAULT 0.00,
    `delta_db` DECIMAL(5, 2) NOT NULL DEFAULT 0.00,
    `carrier_locked` TINYINT(1) NOT NULL DEFAULT 0,
    `video_locked` TINYINT(1) NOT NULL DEFAULT 0,
    `audio_locked` TINYINT(1) NOT NULL DEFAULT 0,
    `tolerance_db` DECIMAL(4, 2) NOT NULL DEFAULT 1.50,
    `drx_device_time` VARCHAR(64) DEFAULT NULL,
    `sample_host_time` DATETIME DEFAULT NULL,
    `dt_reserved_ms` BIGINT DEFAULT 0,
    `result` ENUM('PASS', 'FAIL', 'WARNING') NOT NULL,
    FOREIGN KEY (`test_run_id`) REFERENCES `test_runs`(`id`) ON DELETE CASCADE,
    INDEX `idx_run_channel` (`test_run_id`, `channel_no`)
) ENGINE=InnoDB;

-- 7. System Settings (Key-value settings with simple English descriptions)
CREATE TABLE IF NOT EXISTS `system_settings` (
    `setting_key` VARCHAR(64) PRIMARY KEY,
    `setting_value` VARCHAR(255) NOT NULL,
    `description` VARCHAR(255) DEFAULT NULL,
    `updated_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP
) ENGINE=InnoDB;

-- 8. Reference Lock Table (Shared lock queue for Reference DRX receiver)
CREATE TABLE IF NOT EXISTS `reference_lock` (
    `id` INT PRIMARY KEY DEFAULT 1,
    `locked_by_station` INT NOT NULL DEFAULT 0,      -- 0: Free, 1: Station 1, 2: Station 2, 3: Station 3
    `current_channel_no` INT NOT NULL DEFAULT 0,     -- Active channel number (1 to 10)
    `lock_acquired_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    `heartbeat_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP -- Last keep-alive time
) ENGINE=InnoDB;

INSERT INTO `reference_lock` (`id`, `locked_by_station`, `current_channel_no`)
VALUES (1, 0, 0)
ON DUPLICATE KEY UPDATE `id`=`id`;

-- 9. Golden Sample Reference Pool (Saved reference measurements from Host PC)
CREATE TABLE IF NOT EXISTS `golden_frequencies` (
    `id` INT AUTO_INCREMENT PRIMARY KEY,
    `label` VARCHAR(100) NOT NULL DEFAULT '',
    `frequency_mhz` INT NOT NULL DEFAULT 0,
    `symbol_rate` INT NOT NULL DEFAULT 0,
    `polarization` ENUM('V', 'H') NOT NULL DEFAULT 'V',
    `golden_cn_db` DECIMAL(5, 2) NOT NULL DEFAULT 0.00,
    `golden_pow_dbm` DECIMAL(6, 2) NOT NULL DEFAULT 0.00,
    `tolerance_db` DECIMAL(4, 2) NOT NULL DEFAULT 1.50,
    `duration_sec` INT NOT NULL DEFAULT 0,
    `sample_data` MEDIUMTEXT DEFAULT NULL,
    `updated_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP
) ENGINE=InnoDB;

-- 10. Active Network Nodes (Live Host servers and Station clients on the network)
CREATE TABLE IF NOT EXISTS `cluster_nodes` (
    `id` INT AUTO_INCREMENT PRIMARY KEY,
    `node_type` ENUM('HOST_SERVER', 'STATION_CLIENT', 'WEB_SERVICE') NOT NULL DEFAULT 'HOST_SERVER',
    `node_name` VARCHAR(100) NOT NULL,
    `ip_address` VARCHAR(45) NOT NULL,
    `port` INT NOT NULL DEFAULT 45454,
    `status` ENUM('ONLINE', 'BUSY', 'OFFLINE') NOT NULL DEFAULT 'ONLINE',
    `active_test_sn` VARCHAR(64) DEFAULT NULL,
    `details` VARCHAR(255) DEFAULT NULL,
    `last_seen` TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    UNIQUE KEY `uk_node_ip_port` (`node_type`, `ip_address`, `port`)
) ENGINE=InnoDB;

-- 11. System Logs (Application and Web Console event logs)
CREATE TABLE IF NOT EXISTS `system_logs` (
    `id` BIGINT AUTO_INCREMENT PRIMARY KEY,
    `log_time` TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    `log_level` VARCHAR(16) NOT NULL DEFAULT 'INFO',
    `source` VARCHAR(64) NOT NULL DEFAULT 'SYSTEM',
    `message` TEXT NOT NULL
) ENGINE=InnoDB;
