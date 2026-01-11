<?php
declare(strict_types=1);

require __DIR__ . '/_lib.php';
init_api();
require_method('GET');

$device_id = device_id_from_request();

$type = isset($_GET['type']) ? trim((string)$_GET['type']) : '';
$limit  = clamp_int($_GET['limit'] ?? 25, 1, 200, 25);
$offset = clamp_int($_GET['offset'] ?? 0, 0, 1000000, 0);

$allowedTypes = ['forced_entry','lockout','multiple_failed_attempts','tamper','door_held_open','system'];

try {
  $pdo = db();

  // Build WHERE using ONLY named placeholders (no mixing)
  $where = 'WHERE a.device_id = :device_id';

  if ($type !== '') {
    if (!in_array($type, $allowedTypes, true)) {
      respond_error('invalid_type', 'Invalid alert type', 400);
    }
    $where .= ' AND a.type = :type';
  }

  $sql = "
    SELECT a.type, a.severity, a.message, a.is_active, a.created_at, a.cleared_at, a.source
    FROM alerts a
    $where
    ORDER BY a.created_at DESC
    LIMIT :lim OFFSET :off
  ";

  $stmt = $pdo->prepare($sql);

  // Bind named params
  $stmt->bindValue(':device_id', $device_id, PDO::PARAM_INT);
  if ($type !== '') {
    $stmt->bindValue(':type', $type, PDO::PARAM_STR);
  }
  $stmt->bindValue(':lim', $limit, PDO::PARAM_INT);
  $stmt->bindValue(':off', $offset, PDO::PARAM_INT);

  $stmt->execute();

  $alerts = [];
  while ($r = $stmt->fetch()) {
    $alerts[] = [
      'type' => $r['type'],
      'severity' => $r['severity'],
      'message' => $r['message'],
      'is_active' => (bool)$r['is_active'],
      'source' => $r['source'],
      'created_at' => to_iso($r['created_at']),
      'cleared_at' => to_iso($r['cleared_at']),
    ];
  }

  respond_ok(['alerts' => $alerts, 'limit' => $limit, 'offset' => $offset]);

} catch (Throwable $e) {
  respond_error('server_error', 'Failed to fetch alerts', 500);
}
