<?php
declare(strict_types=1);

/**
 * GateX API Config
 * ---------------
 * Copy-paste friendly. Update DB credentials for your environment.
 *
 * Folder structure expected:
 *   /index.html
 *   /settings.html
 *   /app.js
 *   /api/_config.php   <-- this file
 *   /api/*.php
 */

return [
  'db' => [
    'host' => 'localhost',
    'name' => 'gatex',
    'user' => 'root',
    'pass' => '',
    'charset' => 'utf8mb4',
  ],

  // Default device for single-door setups
  'defaults' => [
    'device_id' => 1,
  ],

  'fingerprint' => [
    // Typical optical sensor capacities are 100-200. Adjust later to match your module.
    'max_slots' => 127,
  ],

  'security' => [
    // DEV convenience: set true if you test frontend from file:// or another port.
    // For production, serve frontend + API from same origin and set this to false.
    'allow_cors_dev' => true,

    // If true, endpoints can require an ESP32 device key header.
    // You can enable this later; the web dashboard does NOT use sessions.
    'enable_device_key_auth' => false,

    // Header name used by ESP32 to send its key
    'device_key_header' => 'X-Device-Key',

    // Security: by default we do NOT return pin_hash to browser calls.
    // When hardware arrives, you will enable device key auth and let ESP32 request pin hash.
    'allow_pin_hash_to_web' => false,
  ],
];
