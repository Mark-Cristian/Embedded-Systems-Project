<?php
declare(strict_types=1);

require __DIR__ . '/_lib.php';
init_api();
require_method('GET');

$device_id = device_id_from_request();

$limit  = clamp_int($_GET['limit'] ?? 100, 1, 500, 100);
$offset = clamp_int($_GET['offset'] ?? 0, 0, 1000000, 0);

try {
  $pdo = db();

  // IMPORTANT: with PDO::ATTR_EMULATE_PREPARES=false, LIMIT/OFFSET cannot be bound.
  // Safe because $limit/$offset are clamped integers.
  $sql = '
    SELECT
      u.user_code,
      u.display_name,
      f.sensor_slot,
      f.enrolled_at
    FROM fingerprints f
    INNER JOIN users u ON u.id = f.user_id
    WHERE f.device_id = ?
    ORDER BY f.sensor_slot ASC
    LIMIT ' . (int)$limit . ' OFFSET ' . (int)$offset . '
  ';

  $stmt = $pdo->prepare($sql);
  $stmt->execute([$device_id]);

  $fps = [];
  while ($r = $stmt->fetch()) {
    $fps[] = [
      'user_code'     => $r['user_code'],
      'display_name'  => $r['display_name'],
      'sensor_slot'   => (int)$r['sensor_slot'],
      'enrolled_at'   => to_iso($r['enrolled_at']),
    ];
  }

  respond_ok(['fingerprints' => $fps, 'limit' => $limit, 'offset' => $offset]);

} catch (Throwable $e) {
  error_log('getFingerprints ERROR: ' . $e->getMessage());
  respond_error('server_error', 'Failed to fetch fingerprints', 500);
}
