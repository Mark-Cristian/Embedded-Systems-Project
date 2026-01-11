<?php
declare(strict_types=1);

require __DIR__ . '/_lib.php';
init_api();
require_method('GET');

try {
  $pdo = db();

  $user_code = isset($_GET['user_code']) ? trim((string)$_GET['user_code']) : '';
  $id = isset($_GET['id']) ? (int)$_GET['id'] : 0;

  if ($user_code === '' && $id <= 0) {
    respond_error('missing_param', 'Provide user_code or id', 400);
  }

  if ($user_code !== '') {
    $stmt = $pdo->prepare('
      SELECT id, user_code, display_name, role, avatar_data_url, is_active, created_at, updated_at
      FROM users
      WHERE user_code = ?
      LIMIT 1
    ');
    $stmt->execute([$user_code]);
  } else {
    $stmt = $pdo->prepare('
      SELECT id, user_code, display_name, role, avatar_data_url, is_active, created_at, updated_at
      FROM users
      WHERE id = ?
      LIMIT 1
    ');
    $stmt->execute([$id]);
  }

  $u = $stmt->fetch();
  if (!$u) respond_error('not_found', 'User not found', 404);

  respond_ok([
    'id' => (int)$u['id'],
    'user_code' => $u['user_code'],
    'display_name' => $u['display_name'],
    'role' => $u['role'],
    'avatar_data_url' => $u['avatar_data_url'],
    'is_active' => (bool)$u['is_active'],
    'created_at' => to_iso($u['created_at']),
    'updated_at' => to_iso($u['updated_at']),
  ]);
} catch (Throwable $e) {
  respond_error('server_error', 'Failed to fetch user', 500);
}
