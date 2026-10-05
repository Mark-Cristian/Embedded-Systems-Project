<?php
require_once "db.php";

$cfg = $mysqli->query("SELECT interval_min, charge_limit, updated_at FROM config WHERE id=1 LIMIT 1")->fetch_assoc();
$interval = intval($cfg["interval_min"] ?? 3);
$limit = intval($cfg["charge_limit"] ?? 70);

$latest = $mysqli->query("SELECT * FROM logs ORDER BY id DESC LIMIT 1")->fetch_assoc();
$hist = $mysqli->query("SELECT id, created_at, soil_pct, flood, depth, batt_v, batt_pct, low_batt, wake, chg_en, servo
                        FROM logs ORDER BY id DESC LIMIT 30");
?>
<!doctype html>
<html>
<head>
<meta charset="utf-8"/>
<meta name="viewport" content="width=device-width, initial-scale=1"/>
<title>Wetland Node - Dashboard</title>
<style>
  body{font-family:system-ui,Arial,sans-serif;background:#f6f7fb;margin:16px}
  .wrap{max-width:1000px;margin:0 auto}
  .top{display:flex;justify-content:space-between;align-items:end;gap:12px;flex-wrap:wrap}
  .small{font-size:12px;opacity:.75}
  .grid{display:grid;grid-template-columns:repeat(3,1fr);gap:12px;margin-top:12px}
  .card{background:#fff;border-radius:16px;padding:14px;box-shadow:0 3px 12px rgba(0,0,0,.06)}
  .k{font-size:12px;opacity:.7;margin-bottom:6px}
  .v{font-size:22px;font-weight:800}
  .badge{display:inline-block;padding:4px 10px;border-radius:999px;font-size:12px;font-weight:700}
  .ok{background:#e7f7ec;color:#166534}
  .warn{background:#fff7e6;color:#92400e}
  .bad{background:#fee2e2;color:#991b1b}
  .meterWrap{display:flex;gap:16px;align-items:center}
  .meter{
    width:140px;height:140px;border-radius:50%;
    background: conic-gradient(#22c55e 0deg, #22c55e var(--deg), #e5e7eb var(--deg), #e5e7eb 360deg);
    display:flex;align-items:center;justify-content:center;
  }
  .meterInner{width:100px;height:100px;border-radius:50%;background:#fff;display:flex;flex-direction:column;align-items:center;justify-content:center}
  table{width:100%;border-collapse:collapse;background:#fff;border-radius:16px;overflow:hidden;box-shadow:0 3px 12px rgba(0,0,0,.06);margin-top:12px}
  th,td{padding:10px;border-bottom:1px solid #eef1f6;text-align:left;font-size:14px}
  th{background:#fafbff;font-size:12px;text-transform:uppercase;letter-spacing:.04em;opacity:.8}
  @media(max-width:900px){.grid{grid-template-columns:1fr}.meterWrap{flex-direction:column;align-items:flex-start}}
  a.btn{display:inline-block;padding:10px 12px;border-radius:12px;background:#111827;color:#fff;text-decoration:none;font-weight:800}
</style>
</head>
<body>
<div class="wrap">
  <div class="top">
    <div>
      <h2 style="margin:0">Wetland Monitoring Node</h2>
      <div class="small">Interval: <b><?= $interval ?></b> min · Charge limit: <b><?= $limit ?>%</b> · Updated: <?= htmlspecialchars($cfg["updated_at"] ?? "N/A") ?></div>
    </div>
    <div><a class="btn" href="settings.php">Settings</a></div>
  </div>

  <?php if ($latest):
    $soilPct = intval($latest["soil_pct"]);
    $deg = (int)round($soilPct * 3.6);
    $flood = intval($latest["flood"])===1;
    $depth = intval($latest["depth"])===1;
    $lowb  = intval($latest["low_batt"])===1;
    $chgEn = intval($latest["chg_en"])===1;
    $servo = intval($latest["servo"])===1;

    $riskBadge = $flood ? "warn" : "ok";
    $depthBadge = $depth ? "bad" : "ok";
    $battBadge = $lowb ? "bad" : "ok";
  ?>
  <div class="card" style="margin-top:12px">
    <div class="meterWrap">
      <div class="meter" style="--deg: <?= $deg ?>deg">
        <div class="meterInner">
          <div style="font-weight:900;font-size:22px;"><?= $soilPct ?>%</div>
          <div class="small">Flood risk</div>
        </div>
      </div>
      <div>
        <div class="small">Latest: <b><?= htmlspecialchars($latest["created_at"]) ?></b> · Wake: <b><?= htmlspecialchars($latest["wake"]) ?></b></div>
        <div style="margin-top:8px">
          <span class="badge <?= $riskBadge ?>"><?= $flood ? "WET / FLOODING" : "NORMAL" ?></span>
          <span class="badge <?= $depthBadge ?>"><?= $depth ? "DEPTH ≥5cm" : "DEPTH OK" ?></span>
          <span class="badge <?= $battBadge ?>"><?= $lowb ? "LOW BATTERY" : "BATTERY OK" ?></span>
          <span class="badge <?= $chgEn ? "ok" : "warn" ?>"><?= $chgEn ? "CHARGING ENABLED" : "CHARGING CUT (RELAY)" ?></span>
          <?php if ($servo): ?><span class="badge warn">SERVO MITIGATION</span><?php endif; ?>
        </div>
      </div>
    </div>
  </div>

  <div class="grid">
    <div class="card">
      <div class="k">Depth threshold (float)</div>
      <div class="v"><?= $depth ? "YES" : "NO" ?></div>
      <div class="small">Depth ≥ 5 cm triggers interrupt + wake</div>
    </div>
    <div class="card">
      <div class="k">Battery</div>
      <div class="v"><?= htmlspecialchars($latest["batt_v"]) ?>V (<?= intval($latest["batt_pct"]) ?>%)</div>
      <div class="small"><?= $lowb ? "Charge recommended." : "Normal." ?></div>
    </div>
    <div class="card">
      <div class="k">System mode</div>
      <div class="v"><?= htmlspecialchars($latest["mode"]) ?></div>
      <div class="small">Wi-Fi only during ACTIVE</div>
    </div>
  </div>

  <?php else: ?>
    <div class="card" style="margin-top:12px">No logs yet. Power ESP32 and ensure it can reach the server.</div>
  <?php endif; ?>

  <h3 style="margin:14px 0 6px">History (last 30)</h3>
  <table>
    <thead>
      <tr>
        <th>ID</th><th>Time</th><th>Soil%</th><th>Flood</th><th>Depth</th><th>Battery</th><th>Low</th><th>Wake</th><th>Chg</th><th>Servo</th>
      </tr>
    </thead>
    <tbody>
      <?php while($r = $hist->fetch_assoc()): ?>
      <tr>
        <td><?= intval($r["id"]) ?></td>
        <td><?= htmlspecialchars($r["created_at"]) ?></td>
        <td><?= intval($r["soil_pct"]) ?>%</td>
        <td><?= intval($r["flood"]) ? "YES" : "NO" ?></td>
        <td><?= intval($r["depth"]) ? "YES" : "NO" ?></td>
        <td><?= htmlspecialchars($r["batt_v"]) ?>V (<?= intval($r["batt_pct"]) ?>%)</td>
        <td><?= intval($r["low_batt"]) ? "LOW" : "OK" ?></td>
        <td><?= htmlspecialchars($r["wake"]) ?></td>
        <td><?= intval($r["chg_en"]) ? "EN" : "CUT" ?></td>
        <td><?= intval($r["servo"]) ? "YES" : "-" ?></td>
      </tr>
      <?php endwhile; ?>
    </tbody>
  </table>
</div>
</body>
</html>
