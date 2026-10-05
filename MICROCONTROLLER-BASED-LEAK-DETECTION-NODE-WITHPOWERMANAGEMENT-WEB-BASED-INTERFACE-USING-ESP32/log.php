<?php
require_once "db.php";

function need($k) {
  if (!isset($_POST[$k])) { http_response_code(400); die("Missing $k"); }
  return $_POST[$k];
}

$soil_raw   = intval(need("soil_raw"));
$soil_pct   = intval(need("soil_pct"));
$flood      = intval(need("flood"));
$depth      = intval(need("depth"));

$batt_v     = floatval(need("batt_v"));
$batt_pct   = intval(need("batt_pct"));
$low_batt   = intval(need("low_batt"));

$mode       = substr(need("mode"), 0, 10);
$wake       = substr(need("wake"), 0, 16);

$interval   = intval(need("interval_min"));
$limit      = intval(need("charge_limit"));

$chg_en     = intval(need("chg_en"));
$servo      = intval(need("servo"));

if ($soil_pct < 0) $soil_pct = 0;
if ($soil_pct > 100) $soil_pct = 100;
$flood = $flood ? 1 : 0;
$depth = $depth ? 1 : 0;
$low_batt = $low_batt ? 1 : 0;
$chg_en = $chg_en ? 1 : 0;
$servo = $servo ? 1 : 0;

if (!in_array($interval, [1,3,5], true)) $interval = 3;
if (!in_array($limit, [70,100], true))   $limit = 70;

$stmt = $mysqli->prepare(
  "INSERT INTO logs
   (soil_raw, soil_pct, flood, depth, batt_v, batt_pct, low_batt, mode, wake, interval_min, charge_limit, chg_en, servo)
   VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"
);

$stmt->bind_param(
  "iiiidii ssiiiii",
  $soil_raw, $soil_pct, $flood, $depth,
  $batt_v, $batt_pct, $low_batt,
  $mode, $wake,
  $interval, $limit, $chg_en, $servo
);

if (!$stmt->execute()) { http_response_code(500); die("Insert failed"); }
echo "OK";
