<?php
declare(strict_types=1);

require __DIR__ . '/_lib.php';
init_api();
require_method('GET');

$device_id = device_id_from_request();

$include_pin_hash = isset($_GET['include_pin_hash']) && (string)$_GET['include_pin_hash'] === '1';

try {
  $pdo = db();

  // Ensure settings row exists
  $pdo->prepare('
    INSERT IGNORE INTO device_settings (device_id) VALUES (?)
  ')->execute([$device_id]);

  $stmt = $pdo->prepare('
    SELECT lockout_seconds, alarm_enabled, max_failed_attempts, settings_version, updated_at
    FROM device_settings
    WHERE device_id = ?
    LIMIT 1
  ');
  $stmt->execute([$device_id]);
  $s = $stmt->fetch();
  if (!$s) respond_error('not_found', 'Device settings not found', 404);

  // Active PIN
  $pinStmt = $pdo->prepare('
    SELECT pin_hash, is_default, created_at
    FROM keypad_pins
    WHERE device_id = ? AND is_active = 1
    ORDER BY created_at DESC
    LIMIT 1
  ');
  $pinStmt->execute([$device_id]);
  $p = $pinStmt->fetch();

  $pin_is_default = $p ? (bool)$p['is_default'] : true;

  // Only return pin_hash if explicitly requested AND allowed
  $pin_hash = null;
  if ($include_pin_hash) {
    $sec = cfg()['security'] ?? [];
    if (!empty($sec['allow_pin_hash_to_web'])) {
      $pin_hash = $p ? (string)$p['pin_hash'] : null;
    } else {
      // If not allowed to web, require ESP32 device key (optional auth)
      require_device_key($device_id);
      $pin_hash = $p ? (string)$p['pin_hash'] : null;
    }
  }

  $out = [
    'device_id' => $device_id,
    'lockout_seconds' => (int)$s['lockout_seconds'],
    'alarm_enabled' => (bool)$s['alarm_enabled'],
    'max_failed_attempts' => (int)$s['max_failed_attempts'],
    'settings_version' => (int)$s['settings_version'],
    'updated_at' => to_iso($s['updated_at']),
    'pin_is_default' => $pin_is_default,
  ];

  if ($include_pin_hash) $out['pin_hash'] = $pin_hash;

  respond_ok($out);
} catch (Throwable $e) {
  respond_error('server_error', 'Failed to fetch settings', 500);
}
