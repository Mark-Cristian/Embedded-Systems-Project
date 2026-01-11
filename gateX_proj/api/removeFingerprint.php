<?php
declare(strict_types=1);

require __DIR__ . '/_lib.php';
init_api();
require_method('POST');

$body = get_json_body();
$device_id = device_id_from_request($body);

$user_code = isset($body['user_code']) ? trim((string)$body['user_code']) : '';
$deactivate_user = $body['deactivate_user'] ?? false;

if ($user_code === '' || mb_strlen($user_code) > 32) {
  respond_error('invalid_user_code', 'user_code is required', 400);
}
if (!is_bool($deactivate_user)) {
  $deactivate_user = ($deactivate_user === 1 || $deactivate_user === '1');
}

try {
  $pdo = db();
  $pdo->beginTransaction();

  $uStmt = $pdo->prepare('SELECT id FROM users WHERE user_code = ? LIMIT 1');
  $uStmt->execute([$user_code]);
  $u = $uStmt->fetch();
  if (!$u) {
    $pdo->rollBack();
    respond_error('not_found', 'User not found', 404);
  }
  $user_id = (int)$u['id'];

  // Delete fingerprint mapping for this user+device
  $del = $pdo->prepare('DELETE FROM fingerprints WHERE device_id = ? AND user_id = ?');
  $del->execute([$device_id, $user_id]);
  $removed = $del->rowCount();

  if ($removed <= 0) {
    $pdo->rollBack();
    respond_error('not_found', 'No fingerprint found for this user', 404);
  }

  if ($deactivate_user) {
    $pdo->prepare('UPDATE users SET is_active = 0 WHERE id = ?')->execute([$user_id]);
  }

  $pdo->commit();

  respond_ok([
    'removed' => true,
    'user_code' => $user_code,
    'device_id' => $device_id,
    'deactivated_user' => $deactivate_user,
  ]);
} catch (Throwable $e) {
  if (isset($pdo) && $pdo->inTransaction()) $pdo->rollBack();
  respond_error('server_error', 'Failed to remove fingerprint', 500);
}
