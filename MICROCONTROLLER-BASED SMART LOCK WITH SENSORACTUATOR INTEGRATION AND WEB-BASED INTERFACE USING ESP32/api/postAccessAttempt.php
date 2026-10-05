<?php
declare(strict_types=1);

/**
 * ESP32-ready endpoint (optional for now):
 * POST an access attempt (fingerprint/keypad) to DB.
 * - Stateless
 * - event_uuid supports idempotency when WiFi is intermittent.
 */

require __DIR__ . '/_lib.php';
init_api();
require_method('POST');

$body = get_json_body();
$device_id = device_id_from_request($body);

$event_uuid = isset($body['event_uuid']) && $body['event_uuid'] ? (string)$body['event_uuid'] : uuid_v4();

$method = strtolower(trim((string)($body['method'] ?? '')));
$result = strtolower(trim((string)($body['result'] ?? '')));
$reason = isset($body['reason']) ? trim((string)$body['reason']) : null;

$allowedMethod = ['fingerprint','keypad','remote','system'];
$allowedResult = ['granted','denied'];

if (!in_array($method, $allowedMethod, true)) respond_error('invalid_method', 'method must be fingerprint/keypad/remote/system', 400);
if (!in_array($result, $allowedResult, true)) respond_error('invalid_result', 'result must be granted/denied', 400);

$user_code = isset($body['user_code']) ? trim((string)$body['user_code']) : '';
$username_snapshot = isset($body['username_snapshot']) ? trim((string)$body['username_snapshot']) : null;

// Optional timestamp (ISO 8601). If missing, store server UTC time.
$event_at = null;
if (!empty($body['event_at'])) {
  $t = strtotime((string)$body['event_at']);
  if ($t === false) respond_error('invalid_event_at', 'event_at must be ISO8601', 400);
  $event_at = gmdate('Y-m-d H:i:s', $t);
}

try {
  // If you enable device-key auth, require it here:
  // require_device_key($device_id);

  $pdo = db();

  // Resolve user_id if provided
  $user_id = null;
  if ($user_code !== '') {
    $u = $pdo->prepare('SELECT id, display_name FROM users WHERE user_code = ? LIMIT 1');
    $u->execute([$user_code]);
    $row = $u->fetch();
    if ($row) {
      $user_id = (int)$row['id'];
      if ($username_snapshot === null || $username_snapshot === '') {
        $username_snapshot = (string)$row['display_name'];
      }
    }
  }

  if ($username_snapshot === null || $username_snapshot === '') {
    $username_snapshot = $user_code !== '' ? $user_code : 'Unknown';
  }

  $sql = '
    INSERT INTO access_logs
      (device_id, event_uuid, user_id, username_snapshot, method, result, reason, source, event_at)
    VALUES
      (?, ?, ?, ?, ?, ?, ?, "device", COALESCE(?, UTC_TIMESTAMP()))
    ON DUPLICATE KEY UPDATE
      user_id = VALUES(user_id),
      username_snapshot = VALUES(username_snapshot),
      method = VALUES(method),
      result = VALUES(result),
      reason = VALUES(reason),
      event_at = VALUES(event_at)
  ';
  $stmt = $pdo->prepare($sql);
  $stmt->execute([
    $device_id,
    $event_uuid,
    $user_id,
    $username_snapshot,
    $method,
    $result,
    $reason,
    $event_at
  ]);

  respond_ok([
    'stored' => true,
    'device_id' => $device_id,
    'event_uuid' => $event_uuid,
  ], 201);
} catch (Throwable $e) {
  respond_error('server_error', 'Failed to store access attempt', 500);
}
