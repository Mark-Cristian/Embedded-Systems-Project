<?php
declare(strict_types=1);

/**
 * ESP32-ready endpoint (optional for now):
 * POST an alert event to DB.
 * - Stateless
 * - event_uuid supports idempotency on retries.
 */

require __DIR__ . '/_lib.php';
init_api();
require_method('POST');

$body = get_json_body();
$device_id = device_id_from_request($body);

$event_uuid = isset($body['event_uuid']) && $body['event_uuid'] ? (string)$body['event_uuid'] : uuid_v4();

$type = strtolower(trim((string)($body['type'] ?? '')));
$severity = strtolower(trim((string)($body['severity'] ?? 'info')));
$message = trim((string)($body['message'] ?? ''));

$allowedTypes = ['forced_entry','lockout','multiple_failed_attempts','tamper','door_held_open','system'];
$allowedSeverity = ['info','warning','critical'];

if (!in_array($type, $allowedTypes, true)) respond_error('invalid_type', 'Invalid alert type', 400);
if (!in_array($severity, $allowedSeverity, true)) respond_error('invalid_severity', 'Invalid severity', 400);
if ($message === '' || mb_strlen($message) > 255) respond_error('invalid_message', 'message is required (max 255)', 400);

$is_active = $body['is_active'] ?? false;
if (!is_bool($is_active)) $is_active = ($is_active === 1 || $is_active === '1');

$created_at = null;
if (!empty($body['created_at'])) {
  $t = strtotime((string)$body['created_at']);
  if ($t === false) respond_error('invalid_created_at', 'created_at must be ISO8601', 400);
  $created_at = gmdate('Y-m-d H:i:s', $t);
}

$cleared_at = null;
if (!empty($body['cleared_at'])) {
  $t = strtotime((string)$body['cleared_at']);
  if ($t === false) respond_error('invalid_cleared_at', 'cleared_at must be ISO8601', 400);
  $cleared_at = gmdate('Y-m-d H:i:s', $t);
}

try {
  // If you enable device-key auth, require it here:
  // require_device_key($device_id);

  $pdo = db();

  $sql = '
    INSERT INTO alerts
      (device_id, event_uuid, type, severity, message, is_active, source, created_at, cleared_at)
    VALUES
      (?, ?, ?, ?, ?, ?, "device", COALESCE(?, UTC_TIMESTAMP()), ?)
    ON DUPLICATE KEY UPDATE
      type = VALUES(type),
      severity = VALUES(severity),
      message = VALUES(message),
      is_active = VALUES(is_active),
      cleared_at = VALUES(cleared_at),
      created_at = VALUES(created_at)
  ';
  $stmt = $pdo->prepare($sql);
  $stmt->execute([
    $device_id,
    $event_uuid,
    $type,
    $severity,
    $message,
    $is_active ? 1 : 0,
    $created_at,
    $cleared_at
  ]);

  respond_ok([
    'stored' => true,
    'device_id' => $device_id,
    'event_uuid' => $event_uuid,
  ], 201);
} catch (Throwable $e) {
  respond_error('server_error', 'Failed to store alert', 500);
}
