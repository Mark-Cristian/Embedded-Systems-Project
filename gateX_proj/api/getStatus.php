<?php
declare(strict_types=1);

require __DIR__ . '/_lib.php';
init_api();
require_method('GET');

// device_id can be provided as a query param (?device_id=1) or falls back to defaults
$device_id = device_id_from_request([]);

try {
  $pdo = db();

  $stmt = $pdo->prepare('
    SELECT lock_state, door_state, system_state, updated_by, battery_percent, wifi_rssi
    FROM device_status_current
    WHERE device_id = ?
    LIMIT 1
  ');
  $stmt->execute([$device_id]);
  $row = $stmt->fetch();

  $lastStmt = $pdo->prepare('
    SELECT changed_at
    FROM device_status_history
    WHERE device_id = ?
    ORDER BY changed_at DESC
    LIMIT 1
  ');
  $lastStmt->execute([$device_id]);
  $last = $lastStmt->fetch();

  $data = [
    'device_id' => $device_id,
    'lock_state' => $row ? (string)$row['lock_state'] : null,
    'door_state' => $row ? (string)$row['door_state'] : null,
    'system_state' => $row ? (string)$row['system_state'] : null,
    'updated_by' => $row ? (string)$row['updated_by'] : null,
    'battery_percent' => $row && $row['battery_percent'] !== null ? (int)$row['battery_percent'] : null,
    'wifi_rssi' => $row && $row['wifi_rssi'] !== null ? (int)$row['wifi_rssi'] : null,
    'updated_at' => $last && $last['changed_at'] !== null ? to_iso((string)$last['changed_at']) : null,
  ];

  respond_ok($data);
} catch (Throwable $e) {
  respond_error('server_error', 'Failed to get status', 500);
}
