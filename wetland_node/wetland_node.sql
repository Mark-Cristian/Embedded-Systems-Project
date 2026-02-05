CREATE DATABASE IF NOT EXISTS wetland_node
  CHARACTER SET utf8mb4 COLLATE utf8mb4_general_ci;
USE wetland_node;

-- Config: one row only
CREATE TABLE IF NOT EXISTS config (
  id TINYINT NOT NULL PRIMARY KEY,
  interval_min TINYINT NOT NULL DEFAULT 3,   -- 1 / 3 / 5
  charge_limit TINYINT NOT NULL DEFAULT 70,  -- 70 / 100
  updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP
);

INSERT INTO config (id, interval_min, charge_limit)
VALUES (1, 3, 70)
ON DUPLICATE KEY UPDATE interval_min=VALUES(interval_min), charge_limit=VALUES(charge_limit);

-- Logs
CREATE TABLE IF NOT EXISTS logs (
  id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
  created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,

  soil_raw INT NOT NULL,
  soil_pct TINYINT NOT NULL,
  flood TINYINT(1) NOT NULL,

  depth TINYINT(1) NOT NULL,

  batt_v DECIMAL(4,2) NOT NULL,
  batt_pct TINYINT NOT NULL,
  low_batt TINYINT(1) NOT NULL,

  mode VARCHAR(10) NOT NULL,            -- ACTIVE / IDLE / SLEEP
  wake VARCHAR(16) NOT NULL,            -- TIMER / EXT0 / POWERON / OTHER

  interval_min TINYINT NOT NULL,
  charge_limit TINYINT NOT NULL,

  chg_en TINYINT(1) NOT NULL,           -- relay state: charging enabled
  servo TINYINT(1) NOT NULL             -- servo actuated this cycle
);
