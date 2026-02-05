<?php
header("Content-Type: application/json; charset=utf-8");
require_once "db.php";

$res = $mysqli->query("SELECT interval_min, charge_limit FROM config WHERE id=1 LIMIT 1");
if (!$res) { http_response_code(500); echo json_encode(["error"=>"db"]); exit; }
$row = $res->fetch_assoc();

$interval = $row ? intval($row["interval_min"]) : 3;
$limit    = $row ? intval($row["charge_limit"]) : 70;

if (!in_array($interval, [1,3,5], true)) $interval = 3;
if (!in_array($limit, [70,100], true))   $limit = 70;

echo json_encode([
  "interval_min" => $interval,
  "charge_limit" => $limit
]);
