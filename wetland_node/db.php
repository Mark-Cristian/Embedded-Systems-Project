<?php
$DB_HOST = "127.0.0.1";
$DB_USER = "root";
$DB_PASS = "";         // XAMPP default
$DB_NAME = "wetland_node";

$mysqli = new mysqli($DB_HOST, $DB_USER, $DB_PASS, $DB_NAME);
if ($mysqli->connect_errno) {
  http_response_code(500);
  die("DB connect failed");
}
$mysqli->set_charset("utf8mb4");
?>
