<?php
declare(strict_types=1);

/**
 * GateX API Shared Library
 * - PDO only
 * - JSON input/output
 * - Defensive error handling
 */

function cfg(): array {
  static $cfg = null;
  if ($cfg === null) {
    $cfg = require __DIR__ . '/_config.php';
  }
  return $cfg;
}

function init_api(): void {
  $c = cfg();

  header('Content-Type: application/json; charset=utf-8');
  header('Cache-Control: no-store, no-cache, must-revalidate, max-age=0');
  header('Pragma: no-cache');

  // DEV CORS (optional)
  if (!empty($c['security']['allow_cors_dev'])) {
    header('Access-Control-Allow-Origin: *');
    header('Access-Control-Allow-Headers: Content-Type, Accept, ' . ($c['security']['device_key_header'] ?? 'X-Device-Key'));
    header('Access-Control-Allow-Methods: GET, POST, OPTIONS');

    if (($_SERVER['REQUEST_METHOD'] ?? '') === 'OPTIONS') {
      http_response_code(204);
      exit;
    }
  }
}

function respond_ok($data = null, int $status = 200): void {
  http_response_code($status);
  echo json_encode(['ok' => true, 'data' => $data], JSON_UNESCAPED_SLASHES);
  exit;
}

function respond_error(string $code, string $message, int $status = 400, $details = null): void {
  http_response_code($status);

  $payload = [
    'ok' => false,
    'error' => [
      'code' => $code,
      'message' => $message,
    ],
  ];

  // DEV ONLY: show PHP error details
  if ($details !== null) {
    $payload['error']['details'] = $details;
  }

  echo json_encode($payload, JSON_UNESCAPED_SLASHES);
  exit;
}


function require_method(string $method): void {
  $m = $_SERVER['REQUEST_METHOD'] ?? '';
  if ($m !== $method) {
    respond_error('method_not_allowed', "Use $method", 405);
  }
}

function get_json_body(): array {
  $raw = file_get_contents('php://input');
  if ($raw === false) return [];
  $raw = trim($raw);
  if ($raw === '') return [];
  $data = json_decode($raw, true);
  if (!is_array($data)) {
    respond_error('bad_json', 'Invalid JSON body', 400);
  }
  return $data;
}

function db(): PDO {
  static $pdo = null;
  if ($pdo instanceof PDO) return $pdo;

  $c = cfg()['db'];
  $dsn = sprintf(
    'mysql:host=%s;dbname=%s;charset=%s',
    $c['host'],
    $c['name'],
    $c['charset'] ?? 'utf8mb4'
  );

  try {
    $pdo = new PDO($dsn, $c['user'], $c['pass'], [
      PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION,
      PDO::ATTR_DEFAULT_FETCH_MODE => PDO::FETCH_ASSOC,
      PDO::ATTR_EMULATE_PREPARES => false,
    ]);
  } catch (Throwable $e) {
    respond_error('db_connect_failed', 'Database connection failed', 500);
  }

  return $pdo;
}

function to_iso(?string $mysqlTs): ?string {
  if (!$mysqlTs) return null;
  $t = strtotime($mysqlTs . ' UTC');
  if ($t === false) return null;
  return gmdate('c', $t); // ISO 8601 in UTC
}

function clamp_int($v, int $min, int $max, int $fallback): int {
  $iv = filter_var($v, FILTER_VALIDATE_INT);
  if ($iv === false) return $fallback;
  $iv = (int)$iv;
  if ($iv < $min) return $min;
  if ($iv > $max) return $max;
  return $iv;
}

function device_id_from_request(array $body = []): int {
  $default = (int)(cfg()['defaults']['device_id'] ?? 1);
  $v = $body['device_id'] ?? ($_GET['device_id'] ?? $default);
  $v = filter_var($v, FILTER_VALIDATE_INT);
  if ($v === false || (int)$v <= 0) {
    respond_error('invalid_device_id', 'Invalid device_id', 400);
  }
  return (int)$v;
}

function get_header_value(string $headerName): ?string {
  $key = 'HTTP_' . strtoupper(str_replace('-', '_', $headerName));
  return $_SERVER[$key] ?? null;
}

/**
 * Optional ESP32 device-key auth. Disabled by default in _config.php.
 * When enabled, store a bcrypt hash of the key in devices.api_key_hash.
 */
function require_device_key(int $device_id): void {
  $sec = cfg()['security'] ?? [];
  if (empty($sec['enable_device_key_auth'])) return; // auth disabled

  $headerName = $sec['device_key_header'] ?? 'X-Device-Key';
  $key = get_header_value($headerName);
  if (!$key) {
    respond_error('missing_device_key', 'Missing device key', 401);
  }

  $pdo = db();
  $stmt = $pdo->prepare('SELECT api_key_hash FROM devices WHERE id = ? AND is_active = 1');
  $stmt->execute([$device_id]);
  $row = $stmt->fetch();

  if (!$row || empty($row['api_key_hash'])) {
    respond_error('device_key_not_configured', 'Device key not configured on server', 403);
  }

  if (!password_verify($key, $row['api_key_hash'])) {
    respond_error('invalid_device_key', 'Invalid device key', 403);
  }
}


function uuid_v4(): string {
  $data = random_bytes(16);
  // set version to 0100
  $data[6] = chr((ord($data[6]) & 0x0f) | 0x40);
  // set bits 6-7 to 10
  $data[8] = chr((ord($data[8]) & 0x3f) | 0x80);
  $hex = bin2hex($data);
  return sprintf(
    '%s-%s-%s-%s-%s',
    substr($hex, 0, 8),
    substr($hex, 8, 4),
    substr($hex, 12, 4),
    substr($hex, 16, 4),
    substr($hex, 20, 12)
  );
}
