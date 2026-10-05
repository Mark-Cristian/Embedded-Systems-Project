<?php
declare(strict_types=1);

require __DIR__ . '/_lib.php';
init_api();
require_method('POST');

$body = get_json_body();
$device_id = device_id_from_request($body);

$current_pin = isset($body['current_pin']) ? trim((string)$body['current_pin']) : '';
$new_pin = isset($body['new_pin']) ? trim((string)$body['new_pin']) : '';

if (!preg_match('/^\d{4,12}$/', $current_pin) || !preg_match('/^\d{4,12}$/', $new_pin)) {
  respond_error('invalid_pin', 'PIN must be 4–12 digits', 400);
}
if ($current_pin === $new_pin) {
  respond_error('invalid_pin', 'New PIN must be different from current PIN', 400);
}

try {
  $pdo = db();

  // Load active PIN
  $stmt = $pdo->prepare('
    SELECT id, pin_hash, is_default
    FROM keypad_pins
    WHERE device_id = ? AND is_active = 1
    ORDER BY created_at DESC
    LIMIT 1
  ');
  $stmt->execute([$device_id]);
  $row = $stmt->fetch();
  if (!$row) respond_error('pin_not_configured', 'No active PIN found', 500);

  if (!password_verify($current_pin, $row['pin_hash'])) {
    respond_error('pin_incorrect', 'Current PIN is incorrect', 401);
  }

  $new_hash = password_hash($new_pin, PASSWORD_BCRYPT, ['cost' => 10]);
  if (!$new_hash) respond_error('hash_failed', 'Failed to hash PIN', 500);

  $pdo->beginTransaction();

  // Deactivate previous active pins (keep history)
  $pdo->prepare('
    UPDATE keypad_pins
    SET is_active = 0, replaced_at = UTC_TIMESTAMP(), is_default = 0
    WHERE device_id = ? AND is_active = 1
  ')->execute([$device_id]);

  // Insert new pin
  $ins = $pdo->prepare('
    INSERT INTO keypad_pins (device_id, pin_hash, is_default, is_active, created_at)
    VALUES (?, ?, 0, 1, UTC_TIMESTAMP())
  ');
  $ins->execute([$device_id, $new_hash]);

  $pdo->commit();

  respond_ok(['changed' => true]);
} catch (Throwable $e) {
  if (isset($pdo) && $pdo->inTransaction()) $pdo->rollBack();
  respond_error('server_error', 'Failed to update PIN', 500);
}
