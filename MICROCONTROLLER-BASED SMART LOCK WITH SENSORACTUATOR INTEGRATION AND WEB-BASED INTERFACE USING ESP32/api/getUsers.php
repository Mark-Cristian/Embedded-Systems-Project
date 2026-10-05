<?php
declare(strict_types=1);

require __DIR__ . '/_lib.php';
init_api();
require_method('GET');

try {
  $pdo = db();
  $stmt = $pdo->query('
    SELECT user_code AS id, display_name AS name, avatar_data_url AS photo
    FROM users
    WHERE is_active = 1
    ORDER BY (role = "admin") DESC, display_name ASC
  ');

  $users = [];
  while ($r = $stmt->fetch()) {
    $users[] = [
      'id' => $r['id'],
      'name' => $r['name'],
      'photo' => $r['photo'],
    ];
  }

  respond_ok(['users' => $users]);
} catch (Throwable $e) {
  respond_error('server_error', 'Failed to fetch users', 500);
}
