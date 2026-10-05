<?php
require_once "db.php";
$cfg = $mysqli->query("SELECT interval_min, charge_limit, updated_at FROM config WHERE id=1 LIMIT 1")->fetch_assoc();
$interval = intval($cfg["interval_min"] ?? 3);
$limit = intval($cfg["charge_limit"] ?? 70);
?>
<!doctype html>
<html>
<head>
<meta charset="utf-8"/>
<meta name="viewport" content="width=device-width, initial-scale=1"/>
<title>Wetland Node - Settings</title>
<style>
  body{font-family:system-ui,Arial,sans-serif;background:#f6f7fb;margin:16px}
  .wrap{max-width:720px;margin:0 auto}
  .card{background:#fff;border-radius:16px;padding:14px;box-shadow:0 3px 12px rgba(0,0,0,.06)}
  label{display:block;font-size:12px;opacity:.75;margin-top:10px;margin-bottom:6px}
  select,button{width:100%;padding:12px;border-radius:12px;border:1px solid #d7dbe8;background:#fff;font-size:14px}
  button{background:#111827;color:#fff;font-weight:900;border:none;cursor:pointer;margin-top:14px}
  a{display:inline-block;margin-top:12px;text-decoration:none;font-weight:800}
  .small{font-size:12px;opacity:.75}
</style>
</head>
<body>
<div class="wrap">
  <h2 style="margin:0 0 10px">Settings</h2>
  <div class="card">
    <div class="small">Last updated: <?= htmlspecialchars($cfg["updated_at"] ?? "N/A") ?></div>
    <form method="POST" action="set_config.php">
      <label>Logging interval</label>
      <select name="interval_min">
        <option value="1" <?= $interval===1?"selected":"" ?>>1 minute</option>
        <option value="3" <?= $interval===3?"selected":"" ?>>3 minutes</option>
        <option value="5" <?= $interval===5?"selected":"" ?>>5 minutes</option>
      </select>

      <label>Charge limit</label>
      <select name="charge_limit">
        <option value="70" <?= $limit===70?"selected":"" ?>>70% (stop early)</option>
        <option value="100" <?= $limit===100?"selected":"" ?>>100% (full charge)</option>
      </select>

      <button type="submit">Save Settings</button>
    </form>
  </div>

  <a href="dashboard.php">← Back to Dashboard</a>
</div>
</body>
</html>
