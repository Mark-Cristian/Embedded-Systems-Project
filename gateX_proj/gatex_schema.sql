-- GateX Smart Lock (MySQL) - Production-ready schema + mock seed data
-- Compatible with MySQL 8.x (and generally 5.7+ due to JSON type)
-- IMPORTANT: This file creates a database named `gatex`.
-- If you already have a DB, remove the CREATE DATABASE / USE lines and run the CREATE TABLE + INSERT sections only.

CREATE DATABASE IF NOT EXISTS gatex
  CHARACTER SET utf8mb4
  COLLATE utf8mb4_unicode_ci;

USE gatex;

-- Strong defaults
SET sql_mode = 'STRICT_ALL_TABLES,NO_ZERO_DATE,NO_ZERO_IN_DATE,ERROR_FOR_DIVISION_BY_ZERO,NO_ENGINE_SUBSTITUTION';
SET time_zone = '+00:00';

-- =========================
-- 1) Devices (future-proof: multiple doors / ESP32 units)
-- =========================
CREATE TABLE IF NOT EXISTS devices (
  id INT UNSIGNED NOT NULL AUTO_INCREMENT,
  device_uid CHAR(36) NOT NULL,          -- UUID or stable device identifier
  name VARCHAR(80) NOT NULL,
  location VARCHAR(120) NULL,
  api_key_hash VARCHAR(255) NULL,        -- optional (enable later for ESP32 auth)
  is_active TINYINT(1) NOT NULL DEFAULT 1,
  created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (id),
  UNIQUE KEY uq_devices_uid (device_uid)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- =========================
-- 2) Users (sidebar dropdown; future multi-user roles)
-- =========================
CREATE TABLE IF NOT EXISTS users (
  id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  user_code VARCHAR(32) NOT NULL,        -- e.g., 2025-001 (used by UI)
  display_name VARCHAR(120) NOT NULL,
  role ENUM('admin','member','viewer') NOT NULL DEFAULT 'member',
  avatar_data_url MEDIUMTEXT NULL,       -- optional (base64 data URL)
  is_active TINYINT(1) NOT NULL DEFAULT 1,
  created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (id),
  UNIQUE KEY uq_users_user_code (user_code),
  KEY idx_users_active (is_active)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- =========================
-- 3) Fingerprints (mapping: sensor slot -> user)
-- =========================
CREATE TABLE IF NOT EXISTS fingerprints (
  id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  device_id INT UNSIGNED NOT NULL,
  user_id BIGINT UNSIGNED NOT NULL,
  sensor_slot SMALLINT UNSIGNED NOT NULL,    -- ESP32/sensor slot index (1..N)
  enrolled_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  meta JSON NULL,                             -- future: template hash, algo version, etc.
  PRIMARY KEY (id),
  UNIQUE KEY uq_fp_device_slot (device_id, sensor_slot),
  KEY idx_fp_user (user_id),
  CONSTRAINT fk_fp_device FOREIGN KEY (device_id) REFERENCES devices(id)
    ON DELETE CASCADE ON UPDATE CASCADE,
  CONSTRAINT fk_fp_user FOREIGN KEY (user_id) REFERENCES users(id)
    ON DELETE RESTRICT ON UPDATE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- =========================
-- 4) Keypad PINs (hashed; history kept)
-- =========================
CREATE TABLE IF NOT EXISTS keypad_pins (
  id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  device_id INT UNSIGNED NOT NULL,
  pin_hash VARCHAR(255) NOT NULL,          -- bcrypt hash (never store plaintext)
  is_default TINYINT(1) NOT NULL DEFAULT 0,
  is_active TINYINT(1) NOT NULL DEFAULT 1,
  created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  replaced_at TIMESTAMP NULL DEFAULT NULL,
  updated_by_user_id BIGINT UNSIGNED NULL,
  PRIMARY KEY (id),
  KEY idx_pin_device_active (device_id, is_active),
  CONSTRAINT fk_pin_device FOREIGN KEY (device_id) REFERENCES devices(id)
    ON DELETE CASCADE ON UPDATE CASCADE,
  CONSTRAINT fk_pin_user FOREIGN KEY (updated_by_user_id) REFERENCES users(id)
    ON DELETE SET NULL ON UPDATE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- =========================
-- 5) Device Settings (ESP32 will GET this)
-- =========================
CREATE TABLE IF NOT EXISTS device_settings (
  device_id INT UNSIGNED NOT NULL,
  lockout_seconds SMALLINT UNSIGNED NOT NULL DEFAULT 60,     -- UI: 30/60/300/600
  alarm_enabled TINYINT(1) NOT NULL DEFAULT 1,
  max_failed_attempts TINYINT UNSIGNED NOT NULL DEFAULT 3,   -- future
  settings_version INT UNSIGNED NOT NULL DEFAULT 1,          -- increments on change
  created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (device_id),
  CONSTRAINT fk_settings_device FOREIGN KEY (device_id) REFERENCES devices(id)
    ON DELETE CASCADE ON UPDATE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- =========================
-- 6) Device Status (Real-time panel reads this)
-- =========================
CREATE TABLE IF NOT EXISTS device_status_current (
  device_id INT UNSIGNED NOT NULL,
  lock_state ENUM('locked','unlocked') NOT NULL DEFAULT 'locked',
  door_state ENUM('open','closed') NOT NULL DEFAULT 'closed',
  system_state ENUM('armed','disarmed') NOT NULL DEFAULT 'disarmed',
  updated_by ENUM('device','web','mock','system') NOT NULL DEFAULT 'mock',
  battery_percent TINYINT UNSIGNED NULL,
  wifi_rssi SMALLINT NULL,
  updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (device_id),
  CONSTRAINT fk_status_device FOREIGN KEY (device_id) REFERENCES devices(id)
    ON DELETE CASCADE ON UPDATE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS device_status_history (
  id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  device_id INT UNSIGNED NOT NULL,
  event_uuid CHAR(36) NOT NULL,
  lock_state ENUM('locked','unlocked') NOT NULL,
  door_state ENUM('open','closed') NOT NULL,
  system_state ENUM('armed','disarmed') NOT NULL,
  updated_by ENUM('device','web','mock','system') NOT NULL,
  battery_percent TINYINT UNSIGNED NULL,
  wifi_rssi SMALLINT NULL,
  changed_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (id),
  UNIQUE KEY uq_status_event (event_uuid),
  KEY idx_status_device_time (device_id, changed_at),
  CONSTRAINT fk_statushist_device FOREIGN KEY (device_id) REFERENCES devices(id)
    ON DELETE CASCADE ON UPDATE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- =========================
-- 7) Access Logs (Dashboard + Logs Table)
-- =========================
CREATE TABLE IF NOT EXISTS access_logs (
  id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  device_id INT UNSIGNED NOT NULL,
  event_uuid CHAR(36) NOT NULL,  -- idempotency for ESP32 retries
  user_id BIGINT UNSIGNED NULL,  -- null for unknown
  username_snapshot VARCHAR(120) NULL,  -- store display name at time of event
  method ENUM('fingerprint','keypad','remote','system') NOT NULL,
  result ENUM('granted','denied') NOT NULL,
  reason VARCHAR(255) NULL,
  source ENUM('device','web','mock','system') NOT NULL DEFAULT 'mock',
  event_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  meta JSON NULL,
  PRIMARY KEY (id),
  UNIQUE KEY uq_access_event (event_uuid),
  KEY idx_access_device_time (device_id, event_at),
  KEY idx_access_result_time (result, event_at),
  KEY idx_access_method_time (method, event_at),
  CONSTRAINT fk_access_device FOREIGN KEY (device_id) REFERENCES devices(id)
    ON DELETE CASCADE ON UPDATE CASCADE,
  CONSTRAINT fk_access_user FOREIGN KEY (user_id) REFERENCES users(id)
    ON DELETE SET NULL ON UPDATE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- =========================
-- 8) Alerts (Alerts page + Dashboard)
-- =========================
CREATE TABLE IF NOT EXISTS alerts (
  id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  device_id INT UNSIGNED NOT NULL,
  event_uuid CHAR(36) NOT NULL,
  type ENUM('forced_entry','lockout','multiple_failed_attempts','tamper','door_held_open','system') NOT NULL,
  severity ENUM('info','warning','critical') NOT NULL DEFAULT 'info',
  message VARCHAR(255) NOT NULL,
  is_active TINYINT(1) NOT NULL DEFAULT 0,
  source ENUM('device','web','mock','system') NOT NULL DEFAULT 'mock',
  created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  cleared_at TIMESTAMP NULL DEFAULT NULL,
  meta JSON NULL,
  PRIMARY KEY (id),
  UNIQUE KEY uq_alert_event (event_uuid),
  KEY idx_alert_device_time (device_id, created_at),
  KEY idx_alert_type_time (type, created_at),
  KEY idx_alert_active (device_id, type, is_active),
  CONSTRAINT fk_alert_device FOREIGN KEY (device_id) REFERENCES devices(id)
    ON DELETE CASCADE ON UPDATE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- ==========================================
-- Mock seed data (safe to delete later)
-- ==========================================

-- Device (single door for now)
INSERT INTO devices (id, device_uid, name, location)
VALUES (1, '00000000-0000-0000-0000-000000000001', 'GateX Main Door', 'Front Entrance')
ON DUPLICATE KEY UPDATE
  name = VALUES(name),
  location = VALUES(location);

-- Settings for device 1
INSERT INTO device_settings (device_id, lockout_seconds, alarm_enabled, max_failed_attempts, settings_version)
VALUES (1, 60, 1, 3, 1)
ON DUPLICATE KEY UPDATE
  lockout_seconds = VALUES(lockout_seconds),
  alarm_enabled = VALUES(alarm_enabled),
  max_failed_attempts = VALUES(max_failed_attempts);

-- Current status
INSERT INTO device_status_current (device_id, lock_state, door_state, system_state, updated_by, battery_percent, wifi_rssi)
VALUES (1, 'locked', 'closed', 'disarmed', 'mock', 92, -55)
ON DUPLICATE KEY UPDATE
  lock_state = VALUES(lock_state),
  door_state = VALUES(door_state),
  system_state = VALUES(system_state),
  updated_by = VALUES(updated_by),
  battery_percent = VALUES(battery_percent),
  wifi_rssi = VALUES(wifi_rssi);

-- Users
INSERT INTO users (id, user_code, display_name, role, is_active)
VALUES
  (1, '2025-001', 'Juan Dela Cruz', 'member', 1),
  (2, '2025-002', 'Maria Santos',  'member', 1),
  (3, 'ADMIN',    'GateX Admin',   'admin',  1)
ON DUPLICATE KEY UPDATE
  display_name = VALUES(display_name),
  role = VALUES(role),
  is_active = VALUES(is_active);

-- Fingerprints (3 slots)
INSERT INTO fingerprints (device_id, user_id, sensor_slot, meta)
VALUES
  (1, 1, 1, JSON_OBJECT('simulated', true)),
  (1, 2, 2, JSON_OBJECT('simulated', true)),
  (1, 3, 3, JSON_OBJECT('simulated', true))
ON DUPLICATE KEY UPDATE
  user_id = VALUES(user_id);

-- Default keypad PIN = 1234 (bcrypt hash). Marked default and active.
-- NOTE: You can (and should) change this via Settings -> Change PIN in the web UI.
INSERT INTO keypad_pins (device_id, pin_hash, is_default, is_active)
VALUES
  (1, '$2y$10$ULxUEctYfpyWHCRtw5CF1ee4iM2WCJiJ07jyi.WiLGzyboQK5GGt2', 1, 1);

-- Mock access logs (last 7 days)
INSERT INTO access_logs (device_id, event_uuid, user_id, username_snapshot, method, result, reason, source, event_at)
VALUES
  (1, UUID(), 1, 'Juan Dela Cruz', 'fingerprint', 'granted', NULL, 'mock', DATE_SUB(UTC_TIMESTAMP(), INTERVAL 6 DAY) + INTERVAL 9 HOUR),
  (1, UUID(), 2, 'Maria Santos',  'keypad',      'granted', NULL, 'mock', DATE_SUB(UTC_TIMESTAMP(), INTERVAL 6 DAY) + INTERVAL 18 HOUR),
  (1, UUID(), NULL, 'Unknown',    'keypad',      'denied',  'Wrong PIN', 'mock', DATE_SUB(UTC_TIMESTAMP(), INTERVAL 5 DAY) + INTERVAL 20 HOUR),
  (1, UUID(), 1, 'Juan Dela Cruz','fingerprint', 'granted', NULL, 'mock', DATE_SUB(UTC_TIMESTAMP(), INTERVAL 5 DAY) + INTERVAL 21 HOUR),
  (1, UUID(), NULL, 'Unknown',    'fingerprint', 'denied',  'No match', 'mock', DATE_SUB(UTC_TIMESTAMP(), INTERVAL 4 DAY) + INTERVAL 7 HOUR),
  (1, UUID(), 2, 'Maria Santos',  'fingerprint', 'granted', NULL, 'mock', DATE_SUB(UTC_TIMESTAMP(), INTERVAL 4 DAY) + INTERVAL 8 HOUR),
  (1, UUID(), NULL, 'Unknown',    'keypad',      'denied',  'Wrong PIN', 'mock', DATE_SUB(UTC_TIMESTAMP(), INTERVAL 3 DAY) + INTERVAL 19 HOUR),
  (1, UUID(), NULL, 'Unknown',    'keypad',      'denied',  'Wrong PIN', 'mock', DATE_SUB(UTC_TIMESTAMP(), INTERVAL 3 DAY) + INTERVAL 19 HOUR + INTERVAL 1 MINUTE),
  (1, UUID(), NULL, 'Unknown',    'keypad',      'denied',  'Wrong PIN', 'mock', DATE_SUB(UTC_TIMESTAMP(), INTERVAL 3 DAY) + INTERVAL 19 HOUR + INTERVAL 2 MINUTE),
  (1, UUID(), 3, 'GateX Admin',   'remote',      'granted', 'Remote unlock', 'mock', DATE_SUB(UTC_TIMESTAMP(), INTERVAL 2 DAY) + INTERVAL 10 HOUR),
  (1, UUID(), NULL, 'Unknown',    'fingerprint', 'denied',  'No match', 'mock', DATE_SUB(UTC_TIMESTAMP(), INTERVAL 1 DAY) + INTERVAL 22 HOUR),
  (1, UUID(), 1, 'Juan Dela Cruz','fingerprint', 'granted', NULL, 'mock', DATE_SUB(UTC_TIMESTAMP(), INTERVAL 1 DAY) + INTERVAL 23 HOUR),
  (1, UUID(), 2, 'Maria Santos',  'keypad',      'granted', NULL, 'mock', UTC_TIMESTAMP() - INTERVAL 2 HOUR),
  (1, UUID(), NULL, 'Unknown',    'keypad',      'denied',  'Wrong PIN', 'mock', UTC_TIMESTAMP() - INTERVAL 55 MINUTE);

-- Mock alerts
INSERT INTO alerts (device_id, event_uuid, type, severity, message, is_active, source, created_at)
VALUES
  (1, UUID(), 'lockout', 'warning', 'Lockout triggered after 3 failed attempts.', 0, 'mock', DATE_SUB(UTC_TIMESTAMP(), INTERVAL 3 DAY) + INTERVAL 19 HOUR + INTERVAL 3 MINUTE),
  (1, UUID(), 'multiple_failed_attempts', 'warning', 'Multiple failed access attempts detected.', 0, 'mock', DATE_SUB(UTC_TIMESTAMP(), INTERVAL 1 DAY) + INTERVAL 22 HOUR),
  (1, UUID(), 'forced_entry', 'critical', 'Door opened while locked (forced entry).', 0, 'mock', DATE_SUB(UTC_TIMESTAMP(), INTERVAL 5 DAY) + INTERVAL 2 HOUR);

