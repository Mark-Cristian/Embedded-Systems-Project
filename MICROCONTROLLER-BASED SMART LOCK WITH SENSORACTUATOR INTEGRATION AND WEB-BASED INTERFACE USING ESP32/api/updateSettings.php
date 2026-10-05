<?php
declare(strict_types=1);

require __DIR__ . '/_lib.php';
init_api();
require_method('POST');

$body = get_json_body();
$device_id = device_id_from_request($body);

$lockout_seconds = $body['lockout_seconds'] ?? null;
$alarm_enabled = $body['alarm_enabled'] ?? null;

// Validate
$lockout_seconds = filter_var($lockout_seconds, FILTER_VALIDATE_INT);
if ($lockout_seconds === false) {
  respond_error('invalid_lockout', 'lockout_seconds must be an integer', 400);
}
$lockout_seconds = (int)$lockout_seconds;
if ($lockout_seconds < 30 || $lockout_seconds > 600) {
  respond_error('invalid_lockout', 'lockout_seconds must be between 30 and 600', 400);
}

if (!is_bool($alarm_enabled)) {
  // allow 0/1 from callers
  if ($alarm_enabled === 0 || $alarm_enabled === 1 || $alarm_enabled === '0' || $alarm_enabled === '1') {
    $alarm_enabled = (bool)$alarm_enabled;
  } else {
    respond_error('invalid_alarm', 'alarm_enabled must be boolean', 400);
  }
}

try {
  $pdo = db();
  $pdo->beginTransaction();

  // Ensure row exists
  $pdo->prepare('INSERT IGNORE INTO device_settings (device_id) VALUES (?)')->execute([$device_id]);

  // Update + bump version
  $stmt = $pdo->prepare('
    UPDATE device_settings
    SET lockout_seconds = ?, alarm_enabled = ?, settings_version = settings_version + 1
    WHERE device_id = ?
  ');
  $stmt->execute([$lockout_seconds, $alarm_enabled ? 1 : 0, $device_id]);

  $pdo->commit();

  // Return updated settings
  $stmt2 = $pdo->prepare('
    SELECT lockout_seconds, alarm_enabled, max_failed_attempts, settings_version, updated_at
    FROM device_settings WHERE device_id = ? LIMIT 1
  ');
  $stmt2->execute([$device_id]);
  $s = $stmt2->fetch();

  respond_ok([
    'device_id' => $device_id,
    'lockout_seconds' => (int)$s['lockout_seconds'],
    'alarm_enabled' => (bool)$s['alarm_enabled'],
    'max_failed_attempts' => (int)$s['max_failed_attempts'],
    'settings_version' => (int)$s['settings_version'],
    'updated_at' => to_iso($s['updated_at']),
  ]);
} catch (Throwable $e) {
  if (isset($pdo) && $pdo->inTransaction()) $pdo->rollBack();
  respond_error('server_error', 'Failed to update settings', 500);
}
