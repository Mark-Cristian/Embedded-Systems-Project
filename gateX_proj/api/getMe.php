<?php
declare(strict_types=1);

require __DIR__ . '/_lib.php';
init_api();
require_method('GET');

try {
  $pdo = db();

  // No auth yet: treat "current user" as first active admin, else first active user.
  $stmt = $pdo->query('
    SELECT user_code, display_name, avatar_data_url
    FROM users
    WHERE is_active = 1
    ORDER BY (role = "admin") DESC, id ASC
    LIMIT 1
  ');
  $u = $stmt->fetch();

  if (!$u) {
    // If DB has no users, return a safe fallback (UI will still render)
    respond_ok([
      'username' => 'User',
      'display_name' => 'User',
      'avatar_data_url' => null,
    ]);
  }

  respond_ok([
    'username' => $u['user_code'],
    'display_name' => $u['display_name'],
    'avatar_data_url' => $u['avatar_data_url'],
  ]);
} catch (Throwable $e) {
  respond_error('server_error', 'Failed to fetch current user', 500);
}
