<?php
declare(strict_types=1);

require __DIR__ . '/_lib.php';
init_api();
require_method('GET');

$device_id = device_id_from_request();

try {
  $pdo = db();

  // Build fixed 7-day window (today and previous 6 days), in UTC
  $tz = new DateTimeZone('UTC');
  $today = new DateTimeImmutable('now', $tz);
  $start = $today->sub(new DateInterval('P6D'));

  $days = [];
  for ($i = 0; $i < 7; $i++) {
    $d = $start->add(new DateInterval('P' . $i . 'D'));
    $key = $d->format('Y-m-d');
    $days[$key] = [
      'date' => $key,
      'label' => $d->format('D'), // Mon/Tue/...
      'access' => ['granted' => 0, 'denied' => 0],
      'alerts' => ['forced_entry' => 0, 'lockout' => 0, 'multiple_failed_attempts' => 0],
    ];
  }

  // Access logs counts
  $stmt = $pdo->prepare('
    SELECT DATE(event_at) AS d,
      SUM(CASE WHEN result = "granted" THEN 1 ELSE 0 END) AS granted,
      SUM(CASE WHEN result = "denied" THEN 1 ELSE 0 END) AS denied
    FROM access_logs
    WHERE device_id = ?
      AND event_at >= (UTC_DATE() - INTERVAL 6 DAY)
    GROUP BY DATE(event_at)
  ');
  $stmt->execute([$device_id]);
  while ($r = $stmt->fetch()) {
    $k = $r['d'];
    if (!isset($days[$k])) continue;
    $days[$k]['access']['granted'] = (int)$r['granted'];
    $days[$k]['access']['denied'] = (int)$r['denied'];
  }

  // Alerts counts
  $stmt2 = $pdo->prepare('
    SELECT DATE(created_at) AS d,
      SUM(CASE WHEN type = "forced_entry" THEN 1 ELSE 0 END) AS forced_entry,
      SUM(CASE WHEN type = "lockout" THEN 1 ELSE 0 END) AS lockout,
      SUM(CASE WHEN type = "multiple_failed_attempts" THEN 1 ELSE 0 END) AS multiple_failed_attempts
    FROM alerts
    WHERE device_id = ?
      AND created_at >= (UTC_DATE() - INTERVAL 6 DAY)
    GROUP BY DATE(created_at)
  ');
  $stmt2->execute([$device_id]);
  while ($r = $stmt2->fetch()) {
    $k = $r['d'];
    if (!isset($days[$k])) continue;
    $days[$k]['alerts']['forced_entry'] = (int)$r['forced_entry'];
    $days[$k]['alerts']['lockout'] = (int)$r['lockout'];
    $days[$k]['alerts']['multiple_failed_attempts'] = (int)$r['multiple_failed_attempts'];
  }

  // Return in chronological order
  $out = array_values($days);

  
  // -------------------------
  // Quick stats for "today" in the user's local timezone (provided by browser)
  // tz_offset_min matches JS Date().getTimezoneOffset()
  // (minutes to add to local time to get UTC; e.g. Philippines = -480)
  // -------------------------
  $tz_off_min = clamp_int($_GET['tz_offset_min'] ?? 0, -840, 840, 0);

  $nowUtc = new DateTimeImmutable('now', new DateTimeZone('UTC'));
  $nowLocal = $nowUtc->modify(sprintf('%+d minutes', -$tz_off_min));
  $startLocal = $nowLocal->setTime(0, 0, 0);
  $endLocal = $startLocal->add(new DateInterval('P1D'));

  // Convert local day window back to UTC for DB queries
  $startUtc = $startLocal->modify(sprintf('%+d minutes', $tz_off_min));
  $endUtc   = $endLocal->modify(sprintf('%+d minutes', $tz_off_min));
  $startUtcStr = $startUtc->format('Y-m-d H:i:s');
  $endUtcStr   = $endUtc->format('Y-m-d H:i:s');

  // Successful / Failed attempts today
  $q1 = $pdo->prepare('SELECT COUNT(*) FROM access_logs WHERE device_id = ? AND result = "granted" AND event_at >= ? AND event_at < ?');
  $q1->execute([$device_id, $startUtcStr, $endUtcStr]);
  $success_today = (int)$q1->fetchColumn();

  $q2 = $pdo->prepare('SELECT COUNT(*) FROM access_logs WHERE device_id = ? AND result = "denied" AND event_at >= ? AND event_at < ?');
  $q2->execute([$device_id, $startUtcStr, $endUtcStr]);
  $failed_today = (int)$q2->fetchColumn();

  // Alerts today
  $q3 = $pdo->prepare('SELECT COUNT(*) FROM alerts WHERE device_id = ? AND created_at >= ? AND created_at < ?');
  $q3->execute([$device_id, $startUtcStr, $endUtcStr]);
  $alerts_today = (int)$q3->fetchColumn();

  // Last successful access: prefer today, fall back to most recent overall
  $last_access_at = null;
  $q4 = $pdo->prepare('SELECT event_at FROM access_logs WHERE device_id = ? AND result = "granted" AND event_at >= ? AND event_at < ? ORDER BY event_at DESC LIMIT 1');
  $q4->execute([$device_id, $startUtcStr, $endUtcStr]);
  $last_access_at = $q4->fetchColumn();

  if (!$last_access_at) {
    $q5 = $pdo->prepare('SELECT event_at FROM access_logs WHERE device_id = ? AND result = "granted" ORDER BY event_at DESC LIMIT 1');
    $q5->execute([$device_id]);
    $last_access_at = $q5->fetchColumn();
  }

  // Device "last seen" (heartbeat) from latest status history
  $last_seen_at = null;
  $q6 = $pdo->prepare('SELECT changed_at FROM device_status_history WHERE device_id = ? ORDER BY changed_at DESC LIMIT 1');
  $q6->execute([$device_id]);
  $last_seen_at = $q6->fetchColumn();

  $wifi_rssi = null;
  $battery_percent = null;
  $q7 = $pdo->prepare('SELECT wifi_rssi, battery_percent FROM device_status_current WHERE device_id = ? LIMIT 1');
  $q7->execute([$device_id]);
  $cur = $q7->fetch();
  if ($cur) {
    $wifi_rssi = $cur['wifi_rssi'] !== null ? (int)$cur['wifi_rssi'] : null;
    $battery_percent = $cur['battery_percent'] !== null ? (int)$cur['battery_percent'] : null;
  }

  respond_ok([
    'device_id' => $device_id,
    'days' => $out,
    'stats' => [
      'success_today' => $success_today,
      'failed_today' => $failed_today,
      'alerts_today' => $alerts_today,
      'last_access_at' => to_iso($last_access_at ? (string)$last_access_at : null),
      'window_start_utc' => to_iso($startUtcStr),
      'window_end_utc' => to_iso($endUtcStr),
    ],
    'device' => [
      'last_seen_at' => to_iso($last_seen_at ? (string)$last_seen_at : null),
      'wifi_rssi' => $wifi_rssi,
      'battery_percent' => $battery_percent,
    ],
  ]);
} catch (Throwable $e) {
  respond_error('server_error', 'Failed to fetch dashboard summary', 500);
}
