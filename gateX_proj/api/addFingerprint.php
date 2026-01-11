<?php
declare(strict_types=1);

require __DIR__ . '/_lib.php';
init_api();
require_method('POST');

$body = get_json_body();
$device_id = device_id_from_request($body);

$name = isset($body['name']) ? trim((string)$body['name']) : '';
$user_code = isset($body['user_code']) ? trim((string)$body['user_code']) : '';
$photo_data_url = isset($body['photo_data_url']) ? (string)$body['photo_data_url'] : null;

// Enrollment flow support:
//  - Web dashboard sends {simulate:true} to create a *pending* enrollment record.
//  - ESP32 sends {simulate:false} after physically enrolling the finger to finalize (sets enrolled_at).
// Backward compatible: if simulate is omitted, we default to simulate=true (previous behavior).
$simulate = true;
if (array_key_exists('simulate', $body)) {
  $raw = $body['simulate'];
  if (is_bool($raw)) {
    $simulate = $raw;
  } elseif (is_int($raw)) {
    $simulate = ($raw !== 0);
  } elseif (is_string($raw)) {
    $v = strtolower(trim($raw));
    $simulate = !in_array($v, ['0', 'false', 'no', 'off'], true);
  } else {
    $simulate = (bool)$raw;
  }
}

// Basic validation
if ($name === '' || mb_strlen($name) > 120) {
  respond_error('invalid_name', 'name is required (max 120 chars)', 400);
}
if ($user_code === '' || mb_strlen($user_code) > 32 || !preg_match('/^[A-Za-z0-9][A-Za-z0-9._-]{0,31}$/', $user_code)) {
  respond_error('invalid_user_code', 'user_code is required (1–32 chars: letters/numbers/._-)', 400);
}

if ($photo_data_url !== null && $photo_data_url !== '') {
  // Limit payload size (data URL can get huge). Adjust if you need.
  if (strlen($photo_data_url) > 2_000_000) { // ~2MB
    respond_error('photo_too_large', 'photo_data_url too large', 413);
  }
  if (strpos($photo_data_url, 'data:image/') !== 0) {
    respond_error('invalid_photo', 'photo_data_url must be a data:image/* URL', 400);
  }
} else {
  $photo_data_url = null;
}

$maxSlots = (int)(cfg()['fingerprint']['max_slots'] ?? 127);

try {
  $pdo = db();
  $pdo->beginTransaction();

  // Find or create user
  $stmt = $pdo->prepare('SELECT id, is_active FROM users WHERE user_code = ? LIMIT 1');
  $stmt->execute([$user_code]);
  $u = $stmt->fetch();

  if ($u) {
    $user_id = (int)$u['id'];

    // If fingerprint record already exists for this user/device, treat it as idempotent:
    //  - simulate=true  => return existing mapping (pending or enrolled)
    //  - simulate=false => finalize (set enrolled_at if missing, mark simulated=false)
    $fpExistingStmt = $pdo->prepare('SELECT id, sensor_slot, enrolled_at, meta FROM fingerprints WHERE device_id = ? AND user_id = ? LIMIT 1');
    $fpExistingStmt->execute([$device_id, $user_id]);
    $fpExisting = $fpExistingStmt->fetch();
    if ($fpExisting) {
      $fp_id = (int)$fpExisting['id'];
      $slot = (int)$fpExisting['sensor_slot'];

      // Reactivate/update profile
      $upd = $pdo->prepare('
        UPDATE users
        SET display_name = ?, avatar_data_url = COALESCE(?, avatar_data_url), is_active = 1
        WHERE id = ?
      ');
      $upd->execute([$name, $photo_data_url, $user_id]);

      if ($simulate === false) {
        $final = $pdo->prepare('UPDATE fingerprints SET meta = JSON_OBJECT("simulated", false), enrolled_at = COALESCE(enrolled_at, UTC_TIMESTAMP()) WHERE id = ?');
        $final->execute([$fp_id]);
      }

      $pdo->commit();

      respond_ok([
        'fingerprint_id' => $fp_id,
        'device_id' => $device_id,
        'user_code' => $user_code,
        'display_name' => $name,
        'sensor_slot' => $slot,
      ], 200);
    }

    // Reactivate/update profile (no fingerprint mapping yet)
    $upd = $pdo->prepare('
      UPDATE users
      SET display_name = ?, avatar_data_url = COALESCE(?, avatar_data_url), is_active = 1
      WHERE id = ?
    ');
    $upd->execute([$name, $photo_data_url, $user_id]);
  } else {
    $ins = $pdo->prepare('
      INSERT INTO users (user_code, display_name, role, avatar_data_url, is_active)
      VALUES (?, ?, "member", ?, 1)
    ');
    $ins->execute([$user_code, $name, $photo_data_url]);
    $user_id = (int)$pdo->lastInsertId();
  }

  // Determine next available sensor_slot (smallest free slot in 1..maxSlots)
  $slotStmt = $pdo->prepare('SELECT sensor_slot FROM fingerprints WHERE device_id = ? ORDER BY sensor_slot ASC');
  $slotStmt->execute([$device_id]);
  $used = [];
  while ($r = $slotStmt->fetch()) {
    $used[(int)$r['sensor_slot']] = true;
  }

  $slot = null;
  for ($i = 1; $i <= $maxSlots; $i++) {
    if (empty($used[$i])) { $slot = $i; break; }
  }
  if ($slot === null) {
    $pdo->rollBack();
    respond_error('capacity_reached', 'Fingerprint capacity reached for this device', 409);
  }

  // Create fingerprint mapping
  // simulate=true  => pending enrollment (enrolled_at NULL)
  // simulate=false => finalized enrollment (enrolled_at NOW)
  if ($simulate === true) {
    $fpIns = $pdo->prepare('
      INSERT INTO fingerprints (device_id, user_id, sensor_slot, meta, enrolled_at)
      VALUES (?, ?, ?, JSON_OBJECT("simulated", true), NULL)
    ');
    $fpIns->execute([$device_id, $user_id, $slot]);
  } else {
    $fpIns = $pdo->prepare('
      INSERT INTO fingerprints (device_id, user_id, sensor_slot, meta, enrolled_at)
      VALUES (?, ?, ?, JSON_OBJECT("simulated", false), UTC_TIMESTAMP())
    ');
    $fpIns->execute([$device_id, $user_id, $slot]);
  }

  $fp_id = (int)$pdo->lastInsertId();

  $pdo->commit();

  respond_ok([
    'fingerprint_id' => $fp_id,
    'device_id' => $device_id,
    'user_code' => $user_code,
    'display_name' => $name,
    'sensor_slot' => $slot,
  ], 201);
} catch (Throwable $e) {
  if (isset($pdo) && $pdo->inTransaction()) $pdo->rollBack();
  respond_error('server_error', 'Failed to add fingerprint', 500);
}
