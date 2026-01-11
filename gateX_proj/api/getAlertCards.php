<?php
declare(strict_types=1);

require __DIR__ . '/_lib.php';
init_api();
require_method('GET');

$device_id = device_id_from_request();

try {
  $pdo = db();

  // Forced entry: active right now + last seen
  $feActiveStmt = $pdo->prepare('
    SELECT COUNT(*) FROM alerts
    WHERE device_id = ? AND type = "forced_entry" AND is_active = 1
  ');
  $feActiveStmt->execute([$device_id]);
  $feActive = (int)$feActiveStmt->fetchColumn();

  $feLastStmt = $pdo->prepare('
    SELECT MAX(created_at) FROM alerts
    WHERE device_id = ? AND type = "forced_entry"
  ');
  $feLastStmt->execute([$device_id]);
  $feLast = $feLastStmt->fetchColumn();

  // Lockout: 7 day count + last
  $loCountStmt = $pdo->prepare('
    SELECT COUNT(*) FROM alerts
    WHERE device_id = ? AND type = "lockout"
      AND created_at >= (UTC_TIMESTAMP() - INTERVAL 7 DAY)
  ');
  $loCountStmt->execute([$device_id]);
  $loCount = (int)$loCountStmt->fetchColumn();

  $loLastStmt = $pdo->prepare('
    SELECT MAX(created_at) FROM alerts
    WHERE device_id = ? AND type = "lockout"
  ');
  $loLastStmt->execute([$device_id]);
  $loLast = $loLastStmt->fetchColumn();

  // Multiple failed attempts: 24h count + last
  $mfCountStmt = $pdo->prepare('
    SELECT COUNT(*) FROM alerts
    WHERE device_id = ? AND type = "multiple_failed_attempts"
      AND created_at >= (UTC_TIMESTAMP() - INTERVAL 1 DAY)
  ');
  $mfCountStmt->execute([$device_id]);
  $mfCount = (int)$mfCountStmt->fetchColumn();

  $mfLastStmt = $pdo->prepare('
    SELECT MAX(created_at) FROM alerts
    WHERE device_id = ? AND type = "multiple_failed_attempts"
  ');
  $mfLastStmt->execute([$device_id]);
  $mfLast = $mfLastStmt->fetchColumn();

  respond_ok([
    'device_id' => $device_id,
    'forced_entry' => [
      'active_count' => $feActive,
      'last_at' => to_iso(is_string($feLast) ? $feLast : null),
    ],
    'lockout' => [
      'count_7d' => $loCount,
      'last_at' => to_iso(is_string($loLast) ? $loLast : null),
    ],
    'multiple_failed_attempts' => [
      'count_24h' => $mfCount,
      'last_at' => to_iso(is_string($mfLast) ? $mfLast : null),
    ],
  ]);
} catch (Throwable $e) {
  respond_error('server_error', 'Failed to fetch alert cards', 500);
}
