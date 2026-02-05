<?php
require_once "db.php";

$interval = isset($_POST["interval_min"]) ? intval($_POST["interval_min"]) : 3;
$limit    = isset($_POST["charge_limit"]) ? intval($_POST["charge_limit"]) : 70;

if (!in_array($interval, [1,3,5], true)) $interval = 3;
if (!in_array($limit, [70,100], true))   $limit = 70;

$stmt = $mysqli->prepare("UPDATE config SET interval_min=?, charge_limit=? WHERE id=1");
$stmt->bind_param("ii", $interval, $limit);
$stmt->execute();

header("Location: settings.php");
exit;
