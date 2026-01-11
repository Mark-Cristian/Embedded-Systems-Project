<?php
declare(strict_types=1);

require __DIR__ . '/_lib.php';
init_api();
require_method('POST');

$body = get_json_body();
$device_id = device_id_from_request($body);

$current_pin = isset($body['current_pin']) ? trim((string)$body['current_pin']) : '';

if (!preg_match('/^\d{4,12}$/', $current_pin)) {
  respond_error('invalid_pin', 'current_pin must be 4–12 digits', 400);
}

try {
  $pdo = db();
  $stmt = $pdo->prepare('
    SELECT pin_hash
    FROM keypad_pins
    WHERE device_id = ? AND is_active = 1
    ORDER BY created_at DESC
    LIMIT 1
  ');
  $stmt->execute([$device_id]);
  $row = $stmt->fetch();

  if (!$row) {
    respond_error('pin_not_configured', 'No active PIN found', 500);
  }

  $ok = password_verify($current_pin, $row['pin_hash']);
  if (!$ok) {
    respond_error('pin_incorrect', 'Current PIN is incorrect', 401);
  }

  respond_ok(['verified' => true]);
} catch (Throwable $e) {
  respond_error('server_error', 'Failed to verify PIN', 500);
}
