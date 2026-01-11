<?php
declare(strict_types=1);

require __DIR__ . '/_lib.php';
init_api();
require_method('GET');

$device_id = device_id_from_request();

$limit  = clamp_int($_GET['limit'] ?? 15, 1, 100, 15);
$offset = clamp_int($_GET['offset'] ?? 0, 0, 1000000, 0);

try {
  $pdo = db();

  // ✅ Use ONLY named params (no mixing)
  $sql = '
    SELECT
      l.method,
      l.result,
      l.event_at,
      COALESCE(u.display_name, l.username_snapshot, "Unknown") AS username
    FROM access_logs l
    LEFT JOIN users u ON u.id = l.user_id
    WHERE l.device_id = :device_id
    ORDER BY l.event_at DESC
    LIMIT :lim OFFSET :off
  ';

  $stmt = $pdo->prepare($sql);

  $stmt->bindValue(':device_id', $device_id, PDO::PARAM_INT);
  $stmt->bindValue(':lim', $limit, PDO::PARAM_INT);
  $stmt->bindValue(':off', $offset, PDO::PARAM_INT);

  $stmt->execute();

  $logs = [];
  while ($r = $stmt->fetch()) {
    $logs[] = [
      'username' => $r['username'],
      'method' => $r['method'],
      'result' => $r['result'],
      'event_at' => to_iso($r['event_at']),
    ];
  }

  respond_ok(['logs' => $logs, 'limit' => $limit, 'offset' => $offset]);

} catch (Throwable $e) {
  respond_error('server_error', 'Failed to fetch logs', 500);
}
