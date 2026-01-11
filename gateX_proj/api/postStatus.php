<?php
declare(strict_types=1);

/**
 * ESP32-ready endpoint (optional for now):
 * POST current lock/door/system state to DB.
 * - Stateless
 * - Supports event_uuid for idempotency
 */

require __DIR__ . '/_lib.php';
init_api();
require_method('POST');

$body = get_json_body();
$device_id = device_id_from_request($body);

$lock_state = strtolower(trim((string)($body['lock_state'] ?? '')));
$door_state = strtolower(trim((string)($body['door_state'] ?? '')));
$system_state = strtolower(trim((string)($body['system_state'] ?? '')));

$allowedLock = ['locked','unlocked'];
$allowedDoor = ['open','closed'];
$allowedSys  = ['armed','disarmed'];

if (!in_array($lock_state, $allowedLock, true)) respond_error('invalid_lock_state', 'lock_state must be locked/unlocked', 400);
if (!in_array($door_state, $allowedDoor, true)) respond_error('invalid_door_state', 'door_state must be open/closed', 400);
if (!in_array($system_state, $allowedSys, true)) respond_error('invalid_system_state', 'system_state must be armed/disarmed', 400);

$event_uuid = isset($body['event_uuid']) && $body['event_uuid'] ? (string)$body['event_uuid'] : uuid_v4();

$battery_percent = $body['battery_percent'] ?? null;
$wifi_rssi = $body['wifi_rssi'] ?? null;

if ($battery_percent !== null) {
  $battery_percent = filter_var($battery_percent, FILTER_VALIDATE_INT);
  if ($battery_percent === false || $battery_percent < 0 || $battery_percent > 100) {
    respond_error('invalid_battery', 'battery_percent must be 0..100', 400);
  }
}
if ($wifi_rssi !== null) {
  $wifi_rssi = filter_var($wifi_rssi, FILTER_VALIDATE_INT);
  if ($wifi_rssi === false || $wifi_rssi < -120 || $wifi_rssi > 0) {
    respond_error('invalid_rssi', 'wifi_rssi must be between -120..0', 400);
  }
}

try {
  // If you enable device-key auth, require it here:
  // require_device_key($device_id);

  $pdo = db();
  $pdo->beginTransaction();

  // Ensure status row exists
  $pdo->prepare('INSERT IGNORE INTO device_status_current (device_id) VALUES (?)')->execute([$device_id]);

  // Update current
  $upd = $pdo->prepare('
    UPDATE device_status_current
    SET lock_state = ?, door_state = ?, system_state = ?, updated_by = "device",
        battery_percent = ?, wifi_rssi = ?
    WHERE device_id = ?
  ');
  $upd->execute([$lock_state, $door_state, $system_state, $battery_percent, $wifi_rssi, $device_id]);

  // Insert history (idempotent by event_uuid)
  $hist = $pdo->prepare('
    INSERT INTO device_status_history (device_id, event_uuid, lock_state, door_state, system_state, updated_by, battery_percent, wifi_rssi, changed_at)
    VALUES (?, ?, ?, ?, ?, "device", ?, ?, UTC_TIMESTAMP())
    ON DUPLICATE KEY UPDATE
      lock_state = VALUES(lock_state),
      door_state = VALUES(door_state),
      system_state = VALUES(system_state),
      battery_percent = VALUES(battery_percent),
      wifi_rssi = VALUES(wifi_rssi)
  ');
  $hist->execute([$device_id, $event_uuid, $lock_state, $door_state, $system_state, $battery_percent, $wifi_rssi]);

  $pdo->commit();

  respond_ok([
    'stored' => true,
    'device_id' => $device_id,
    'event_uuid' => $event_uuid,
  ]);
} catch (Throwable $e) {
  if (isset($pdo) && $pdo->inTransaction()) $pdo->rollBack();
  respond_error('server_error', 'Failed to store status', 500);
}
