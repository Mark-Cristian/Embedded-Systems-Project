/* app.js - GateX Frontend Integration (DB + PHP APIs)
   - Preserves your existing HTML/CSS (no layout changes)
   - Uses plain JS (fetch) + DOM updates only
   - Optional fallback to localStorage if API is unreachable
*/

const API_BASE = "./api"; // folder next to your HTML files
const LS_KEY_USERS = "smartlock_users_v1"; // legacy fallback

// ---------------------------
// Tunables
// ---------------------------
// Consider device "online" if it has posted status within the last N seconds.
const ONLINE_STALE_S = 60;

// Polling intervals (ms)
const POLL_REALTIME_MS = 2000;
const POLL_LOGS_MS     = 2000;
const POLL_ALERTS_MS   = 2000;
const POLL_DASH_MS     = 3000;

// ---------------------------
// Poll helper (no overlap + pause when tab hidden)
// ---------------------------
const __pollRegistry = [];
document.addEventListener("visibilitychange", () => {
  if (!document.hidden) __pollRegistry.forEach(p => p.poke());
});

function startPoller(name, task, intervalMs) {
  let timer = null;
  let inFlight = false;
  let stopped = false;
  let hasWarned = false;

  const runOnce = async () => {
    if (stopped || document.hidden || inFlight) return;
    inFlight = true;
    try {
      await task();
      hasWarned = false;
    } catch (e) {
      if (!hasWarned) {
        console.warn(`${name} poll failed:`, e.message);
        hasWarned = true;
      }
      throw e;
    } finally {
      inFlight = false;
    }
  };

  const loop = async () => {
    try { await runOnce(); } catch { /* warning handled */ }
    if (stopped) return;
    timer = setTimeout(loop, intervalMs);
  };

  const handle = { poke: () => { runOnce().catch(() => {}); } };
  __pollRegistry.push(handle);

  loop();
  return {
    stop() {
      stopped = true;
      if (timer) clearTimeout(timer);
    }
  };
}

function rssiToSignal(rssi) {
  if (rssi === null || rssi === undefined || rssi === "") return "—";
  const v = Number(rssi);
  if (Number.isNaN(v)) return "—";
  if (v >= -60) return "Good";
  if (v >= -75) return "Fair";
  return "Poor";
}


/* ---------------------------
   Helpers
--------------------------- */
function byId(id) { return document.getElementById(id); }

function escapeHtml(str) {
  return String(str)
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;")
    .replaceAll('"', "&quot;")
    .replaceAll("'", "&#039;");
}

function fmtDateTime(iso) {
  if (!iso) return "—";
  const d = new Date(iso);
  if (Number.isNaN(d.getTime())) return "—";
  return d.toLocaleString(undefined, {
    month: "short",
    day: "2-digit",
    hour: "2-digit",
    minute: "2-digit",
  });
}

function fmtTimeOnly(iso) {
  if (!iso) return "—";
  const d = new Date(iso);
  if (Number.isNaN(d.getTime())) return "—";
  return d.toLocaleTimeString(undefined, { hour: "2-digit", minute: "2-digit" });
}

async function parseJsonResponse(res) {
  const text = await res.text();
  if (!text) return null;
  try { return JSON.parse(text); } catch { return null; }
}

async function apiGet(path, params = {}) {
  const url = new URL(`${API_BASE}/${path}`, window.location.href);

  // prevent stale caching (esp. on some hosts/proxies)
  url.searchParams.set("_ts", String(Date.now()));

  Object.entries(params).forEach(([k, v]) => {
    if (v !== undefined && v !== null && v !== "") url.searchParams.set(k, String(v));
  });

  const res = await fetch(url.toString(), {
    method: "GET",
    cache: "no-store",
    headers: { "Accept": "application/json" },
  });

  const json = await parseJsonResponse(res);
  if (!res.ok || !json || json.ok !== true) {
    const msg = json?.error?.message || `GET ${path} failed`;
    throw new Error(msg);
  }
  return json.data;
}

async function apiPost(path, body = {}) {
  const res = await fetch(`${API_BASE}/${path}`, {
    method: "POST",
    cache: "no-store",
    headers: { "Content-Type": "application/json", "Accept": "application/json" },
    body: JSON.stringify(body),
  });

  const json = await parseJsonResponse(res);
  if (!res.ok || !json || json.ok !== true) {
    const msg = json?.error?.message || `POST ${path} failed`;
    throw new Error(msg);
  }
  return json.data;
}

/* ---------------------------
   LocalStorage fallback users (legacy)
--------------------------- */
function loadUsersFallback() {
  try {
    const raw = localStorage.getItem(LS_KEY_USERS);
    if (!raw) return [{ id: "user-001", name: "User", photo: null }];
    const parsed = JSON.parse(raw);
    if (!Array.isArray(parsed) || parsed.length === 0) return [{ id: "user-001", name: "User", photo: null }];
    return parsed;
  } catch {
    return [{ id: "user-001", name: "User", photo: null }];
  }
}

function saveUsersFallback(users) {
  try { localStorage.setItem(LS_KEY_USERS, JSON.stringify(users)); } catch {}
}

/* ---------------------------
   User dropdown menu (Sidebar)
--------------------------- */
function renderUserMenu(users) {
  const list = byId("userMenuList");
  if (!list) return;

  list.innerHTML = "";
  users.forEach((u) => {
    const li = document.createElement("li");
    li.className = "user-menu__item";

    const avatar = document.createElement("span");
    avatar.className = "user-menu__avatar";
    if (u.photo) avatar.style.backgroundImage = `url(${u.photo})`;

    const meta = document.createElement("div");
    meta.className = "user-menu__meta";
    meta.innerHTML = `
      <div class="user-menu__name">${escapeHtml(u.name || "User")}</div>
      <div class="user-menu__id">${escapeHtml(u.id || "")}</div>
    `;

    li.appendChild(avatar);
    li.appendChild(meta);
    list.appendChild(li);
  });

  // Update top-left avatar + name to the first user (current user)
  const topAvatar = document.querySelector(".avatar");
  const topName = document.querySelector(".user__name");

  if (topName && users[0]?.name) topName.textContent = users[0].name;

  const photo = users[0]?.photo;
  if (topAvatar && photo) {
    topAvatar.style.backgroundImage = `url(${photo})`;
    topAvatar.style.backgroundSize = "cover";
    topAvatar.style.backgroundPosition = "center";
  }
}

function renderRemoveSelect(users) {
  const sel = byId("removeUserSelect");
  if (!sel) return;

  sel.innerHTML = "";
  users.forEach((u) => {
    const opt = document.createElement("option");
    opt.value = u.id;
    opt.textContent = `${u.name} (${u.id})`;
    sel.appendChild(opt);
  });
}

/* ---------------------------
   Modal helpers
--------------------------- */
function openModal(modalEl) {
  if (!modalEl) return;
  modalEl.classList.add("is-open");
  modalEl.setAttribute("aria-hidden", "false");

  const first = modalEl.querySelector("input, select, button, textarea");
  if (first) first.focus();
}

function closeModal(modalEl) {
  if (!modalEl) return;
  modalEl.classList.remove("is-open");
  modalEl.setAttribute("aria-hidden", "true");
}

function wireModalClose(modalEl) {
  if (!modalEl) return;

  // close on overlay click
  modalEl.addEventListener("click", (e) => {
    if (e.target === modalEl) closeModal(modalEl);
  });

  // close on ANY button with data-modal-close (X + Cancel)
  modalEl.querySelectorAll("[data-modal-close]").forEach((btn) => {
    btn.addEventListener("click", (e) => {
      e.preventDefault();
      closeModal(modalEl);
    });
  });

  // close on ESC
  document.addEventListener("keydown", (e) => {
    if (e.key === "Escape" && modalEl.classList.contains("is-open")) closeModal(modalEl);
  });
}

/* ---------------------------
   Photo -> dataURL
--------------------------- */
function fileToDataURL(file) {
  return new Promise((resolve, reject) => {
    const r = new FileReader();
    r.onload = () => resolve(r.result);
    r.onerror = reject;
    r.readAsDataURL(file);
  });
}

/* ---------------------------
   Page-specific integrations
--------------------------- */

// Dashboard charts (index.html)
async function initDashboard() {
  const grid = document.querySelector(".dash-grid");
  if (!grid) return;

  // Update status dot and related elements
  const updateStatus = (isOnline, statusText = null, wifiText = null, signalText = null) => {
    const dot = byId("statusDot");
    const statusEl = byId("statusText");
    const wifiEl = byId("wifiStatus");
    const signalEl = byId("signalStatus");

    if (dot) {
      if (isOnline) {
        dot.classList.remove("is-offline");
        dot.classList.add("is-online");
      } else {
        dot.classList.remove("is-online");
        dot.classList.add("is-offline");
      }
    }

    if (statusEl && statusText) statusEl.textContent = statusText;
    if (wifiEl && wifiText) wifiEl.textContent = wifiText;
    if (signalEl && signalText) signalEl.textContent = signalText;
  };

  // -------- Quick Stats helpers --------
  const setText = (id, val, fallback = "—") => {
    const el = byId(id);
    if (!el) return;
    el.textContent = (val === undefined || val === null || val === "") ? fallback : String(val);
  };

  const setStatsApiDown = () => {
    setText("statSuccess", "—");
    setText("statFailed", "—");
    setText("statAlerts", "—");
    setText("statLastAccessTime", "—");
    setText("statLastAccessAgo", "");
  };

  const computeMinsAgo = (iso) => {
    const d = new Date(iso);
    if (Number.isNaN(d.getTime())) return null;
    const diffMs = Date.now() - d.getTime();
    const mins = Math.max(0, Math.floor(diffMs / 60000));
    return mins;
  };

  const updateStats = (payload) => {
    const p = payload?.stats ? payload.stats : (payload || {});

    const success =
      p.success_today ?? p.successfulAttempts ?? p.successToday ?? p.success_count ?? p.success ?? p.ok_count;

    const failed =
      p.failed_today ?? p.failedAttempts ?? p.failedToday ?? p.failed_count ?? p.failed ?? p.denied_count;

    const alerts =
      p.alerts_today ?? p.alertsToday ?? p.alert_count ?? p.alerts ?? p.alerts_count;

    const lastIso =
      p.last_access_at ?? p.lastAccessAt ?? p.last_access_at ?? p.last_access_iso ?? p.lastAccessISO;

    // Numbers: show 0 if missing
    setText("statSuccess", (success ?? 0), "0");
    setText("statFailed", (failed ?? 0), "0");
    setText("statAlerts", (alerts ?? 0), "0");

    // Time + (x mins ago)
    if (lastIso) {
      setText("statLastAccessTime", fmtTimeOnly(lastIso), "—");
      const mins = computeMinsAgo(lastIso);
      setText("statLastAccessAgo", mins === null ? "" : `(${mins} mins ago)`, "");
    } else {
      setText("statLastAccessTime", "—", "—");
      setText("statLastAccessAgo", "", "");
    }
  };

  const tick = async () => {
    try {
      const data = await apiGet("getDashboard.php", { tz_offset_min: new Date().getTimezoneOffset() });
      const days = data.days || [];

      // Online/offline based on last status heartbeat
      const lastSeenIso = data.device?.last_seen_at ?? data.last_seen_at ?? null;
      let isOnline = false;
      if (lastSeenIso) {
        const t = new Date(lastSeenIso).getTime();
        if (!Number.isNaN(t)) {
          isOnline = (Date.now() - t) <= (ONLINE_STALE_S * 1000);
        }
      }

      const wifiText = isOnline ? "WiFi: Connected" : "WiFi: —";
      const signalText = isOnline ? `Signal: ${rssiToSignal(data.device?.wifi_rssi)}` : "Signal: —";
      updateStatus(isOnline, isOnline ? "Online" : "Offline", wifiText, signalText);

      updateStats(data.stats ?? data);

    const cards = document.querySelectorAll(".dash-card");
    if (cards.length < 2) return;

    // ---------- Chart 1: Access Attempts (Granted vs Denied) ----------
    const card1 = cards[0];
    const bars1 = card1.querySelectorAll(".chart-bars .bar");
    const maxAccess = Math.max(1, ...days.map(d => (d.access?.granted || 0) + (d.access?.denied || 0)));

    bars1.forEach((bar, i) => {
      const d = days[i] || {};
      const granted = d.access?.granted || 0;
      const denied = d.access?.denied || 0;

      const grantedPct = Math.round((granted / maxAccess) * 100);
      const deniedPct = Math.round((denied / maxAccess) * 100);

      const segDenied = bar.querySelector(".seg--denied");
      const segGranted = bar.querySelector(".seg--granted");
      if (segDenied) segDenied.style.setProperty("--h", String(deniedPct));
      if (segGranted) segGranted.style.setProperty("--h", String(grantedPct));

      // Lockout marker if lockout alerts occurred that day
      const lockoutCount = d.alerts?.lockout || 0;
      const existingMarker = bar.querySelector(".bar__marker");
      if (lockoutCount > 0) {
        if (!existingMarker) {
          const m = document.createElement("div");
          m.className = "bar__marker";
          m.title = "Lockout event";
          bar.insertBefore(m, bar.querySelector(".bar__label"));
        } else {
          existingMarker.title = "Lockout event";
        }
      } else if (existingMarker) {
        existingMarker.remove();
      }

      // Label override (Mon..Sun)
      const lbl = bar.querySelector(".bar__label");
      if (lbl && d.label) lbl.textContent = d.label;
    });

    // ---------- Chart 2: Alerts Frequency (Forced / Lockout / Failed) ----------
    const card2 = cards[1];
    const bars2 = card2.querySelectorAll(".chart-bars .bar");
    const maxAlerts = Math.max(1, ...days.map(d =>
      (d.alerts?.forced_entry || 0) +
      (d.alerts?.lockout || 0) +
      (d.alerts?.multiple_failed_attempts || 0)
    ));

    bars2.forEach((bar, i) => {
      const d = days[i] || {};
      const forced = d.alerts?.forced_entry || 0;
      const lockout = d.alerts?.lockout || 0;
      const failed = d.alerts?.multiple_failed_attempts || 0;

      const forcedPct = Math.round((forced / maxAlerts) * 100);
      const lockoutPct = Math.round((lockout / maxAlerts) * 100);
      const failedPct = Math.round((failed / maxAlerts) * 100);

      const segForced = bar.querySelector(".seg--forced");
      const segLockout = bar.querySelector(".seg--lockout");
      const segFailed = bar.querySelector(".seg--failed");

      if (segFailed) segFailed.style.setProperty("--h", String(failedPct));
      if (segLockout) segLockout.style.setProperty("--h", String(lockoutPct));
      if (segForced) segForced.style.setProperty("--h", String(forcedPct));

      const lbl = bar.querySelector(".bar__label");
      if (lbl && d.label) lbl.textContent = d.label;
    });



    } catch (e) {
      updateStatus(false, "Offline", "WiFi: —", "Signal: —");
      setStatsApiDown();
      throw e;
    }
  };

  startPoller("dashboard", tick, POLL_DASH_MS);

}

// Real-time status panel (realtime.html)
async function initRealtime() {
  const panel = document.querySelector(".panel-card");
  if (!panel) return;

  const setChipGroup = (groupEl, selectedText) => {
    if (!groupEl) return;
    const chips = Array.from(groupEl.querySelectorAll(".chip"));
    chips.forEach(ch => {
      const txt = ch.textContent.trim().toUpperCase();
      ch.classList.toggle("is-selected", txt === selectedText);
    });
  };

  const groups = panel.querySelectorAll(".chip-group");
  const lockGroup = groups[0] || null;
  const doorGroup = groups[1] || null;
  const sysGroup  = groups[2] || null;

  const hints = panel.querySelectorAll(".status-hint");

  const setAllHints = (text) => {
    hints.forEach((h) => { if (h) h.textContent = text; });
  };

  const tick = async () => {
    try {
      const s = await apiGet("getStatus.php");

      const lock = (s.lock_state || "locked").toUpperCase();     // LOCKED / UNLOCKED
      const door = (s.door_state || "closed").toUpperCase();     // OPEN / CLOSED
      const sys  = (s.system_state || "disarmed").toUpperCase(); // ARMED / DISARMED

      setChipGroup(lockGroup, lock);
      setChipGroup(doorGroup, door);
      setChipGroup(sysGroup, sys);

      const updated = s.updated_at ? fmtDateTime(s.updated_at) : "—";
      const src = (s.updated_by || "mock").toUpperCase();
      setAllHints(`Last update: ${updated} (${src})`);
    } catch (e) {
      setAllHints("API unavailable (showing last known status)");
      throw e;
      }
  };
  startPoller("realtime", tick, POLL_REALTIME_MS);
}

// Access logs page (logs.html)
async function initLogsPage() {
  const card = document.querySelector(".logs-card");
  if (!card) return;

  const emptyNote = card.querySelector(".logs-empty-note");

  const lockSvg = `
    <svg width="22" height="22" viewBox="0 0 24 24" class="svg">
      <path d="M7 11V8a5 5 0 0 1 10 0v3" />
      <path d="M6 11h12v10H6z" />
    </svg>
  `;
  const alertSvg = `
    <svg width="22" height="22" viewBox="0 0 24 24" class="svg">
      <path d="M12 3l10 18H2L12 3z" />
      <path d="M12 9v5" />
      <path d="M12 17h.01" />
    </svg>
  `;

  const clearExisting = () => {
    card.querySelectorAll(".log-item").forEach((el) => el.remove());
  };

  const renderLogs = (logs) => {
    clearExisting();

    if (!Array.isArray(logs) || logs.length === 0) {
      if (emptyNote) emptyNote.textContent = "No logs found yet.";
      return;
    }

    if (emptyNote) emptyNote.textContent = "";

    logs.forEach((log) => {
      const item = document.createElement("div");
      item.className = "log-item";

      const isGranted = (String(log.result || "").toLowerCase() === "granted");
      const icon = isGranted ? lockSvg : alertSvg;
      const statusClass = isGranted ? "log-status log-status--ok" : "log-status log-status--bad";
      const statusText = isGranted ? "Access Granted" : "Access Denied";
      const method = (log.method || "").toLowerCase();
      const methodLabel =
        method === "fingerprint" ? "Fingerprint" :
        method === "keypad" ? "Keypad" :
        method === "remote" ? "Remote" : "System";

      item.innerHTML = `
        <div class="log-left">
          <div class="log-icon" aria-hidden="true">${icon}</div>

          <div class="log-text">
            <div class="log-main">
              <span class="${statusClass}">${statusText}</span>
            </div>
            <div class="log-sub">${escapeHtml(log.username || "Unknown")}</div>
            <div class="log-sub">${escapeHtml(fmtDateTime(log.event_at))}</div>
          </div>
        </div>

        <div class="log-method">${methodLabel}</div>
      `;

      card.appendChild(item);
    });
  };
  let lastHead = null;

  const tick = async () => {
    try {
      const data = await apiGet("getLogs.php", { limit: 15 });
      const head = (data.logs && data.logs[0]) ? `${data.logs[0].event_at}|${data.logs[0].username}|${data.logs[0].result}|${data.logs[0].method}` : "";
      if (head && head === lastHead) return;
      lastHead = head;
      renderLogs(data.logs);
    } catch (e) {
      if (emptyNote) emptyNote.textContent = "API unavailable. Showing placeholder.";
      throw e;
    }
  };

  startPoller("logs", tick, POLL_LOGS_MS);
}

// Alerts page (alerts.html)
async function initAlertsPage() {
  const stack = document.querySelector(".alerts-stack");
  if (!stack) return;

  const cards = Array.from(stack.querySelectorAll(".alert-card"));

  const setCard = (titleText, statusText, lastText) => {
    const card = cards.find(c => (c.querySelector(".alert-title")?.textContent || "").trim() === titleText);
    if (!card) return;

    const lines = card.querySelectorAll(".alert-line");
    const statusLine = lines[0];
    const lastLine = lines[1];

    if (statusLine) {
      const v = statusLine.querySelector(".alert-v");
      if (v) v.textContent = statusText;
    }
    if (lastLine) {
      const v = lastLine.querySelector(".alert-v");
      if (v) v.textContent = lastText;
    }
  };
  const tick = async () => {
    const s = await apiGet("getAlertCards.php");
    // Forced Entry
    const feActive = (s.forced_entry?.active_count || 0) > 0;
    const feLast = fmtDateTime(s.forced_entry?.last_at);
    setCard(
      "Forced Entry",
      feActive ? "DETECTED" : "NONE DETECTED",
      feActive ? feLast : (s.forced_entry?.last_at ? feLast : "—")
    );

    // Lockout Events (7-day count)
    const loCount = s.lockout?.count_7d || 0;
    const loLast = s.lockout?.last_at ? fmtTimeOnly(s.lockout?.last_at) : "—";
    setCard("Lockout Events", `${loCount} EVENT${loCount === 1 ? "" : "S"}`, loLast);

    // Multiple Failed Attempts (24h count)
    const mfCount = s.multiple_failed_attempts?.count_24h || 0;
    const mfLast = s.multiple_failed_attempts?.last_at ? fmtTimeOnly(s.multiple_failed_attempts?.last_at) : "—";
    setCard("Multiple Failed Attempts", `${mfCount} ATTEMPT${mfCount === 1 ? "" : "S"}`, mfLast);
  };

  startPoller("alertCards", tick, POLL_ALERTS_MS);
}

// Settings page (settings.html)
async function initSettingsPage() {
  const wrap = document.querySelector(".settings-wrap");
  if (!wrap) return;

  const settingsCards = document.querySelectorAll(".settings-card");
  const pinCard = settingsCards[0];
  const lockCard = settingsCards[1];

  // ---- PIN card elements
  const pinInputs = pinCard ? pinCard.querySelectorAll("input.input") : [];
  const currentPinInput = pinInputs[0] || null;
  const newPinInput = pinInputs[1] || null;
  const pinBtn = pinCard ? pinCard.querySelector("button.btn") : null;
  const pinNote = pinCard ? pinCard.querySelector(".card-note") : null;

  // ---- Lockout + Alarm card elements
  const lockSelect = lockCard ? lockCard.querySelector("select.select") : null;
  const alarmToggle = lockCard ? lockCard.querySelector(".switch input[type='checkbox']") : null;
  const lockNote = lockCard ? lockCard.querySelector(".card-note") : null;

  // Enable controls (HTML has them disabled by default)
  [currentPinInput, newPinInput, pinBtn, lockSelect, alarmToggle].forEach((el) => {
    if (!el) return;
    el.disabled = false;
  });

  // Make PIN inputs numeric-friendly (doesn't change layout)
  if (currentPinInput) currentPinInput.inputMode = "numeric";
  if (newPinInput) newPinInput.inputMode = "numeric";

  // Set select option values based on label text (UI text is fixed)
  if (lockSelect) {
    Array.from(lockSelect.options).forEach((opt) => {
      const t = opt.textContent.trim().toLowerCase();
      if (t.includes("30")) opt.value = "30";
      else if (t.includes("1 minute")) opt.value = "60";
      else if (t.includes("5 minute")) opt.value = "300";
      else if (t.includes("10 minute")) opt.value = "600";
    });
  }

  // Load settings from API
  try {
    const s = await apiGet("getSettings.php");

    if (lockSelect) {
      const val = String(s.lockout_seconds ?? 60);
      const opt = Array.from(lockSelect.options).find(o => o.value === val);
      if (opt) lockSelect.value = val;
    }
    if (alarmToggle) alarmToggle.checked = !!s.alarm_enabled;

    if (pinNote) {
      pinNote.textContent = s.pin_is_default
        ? "Default PIN is still active. Please change it now."
        : "PIN is set. You can change it anytime.";
    }
  } catch (e) {
    console.warn("Settings API unavailable:", e.message);
    if (pinNote) pinNote.textContent = "API unavailable. PIN change disabled.";
    if (lockNote) lockNote.textContent = "API unavailable. Settings changes disabled.";
    [currentPinInput, newPinInput, pinBtn, lockSelect, alarmToggle].forEach((el) => { if (el) el.disabled = true; });
    return;
  }

  // Auto-save lockout + alarm (no extra Save button in UI)
  let saveTimer = null;
  const queueSave = () => {
    if (!lockSelect || !alarmToggle) return;
    if (saveTimer) clearTimeout(saveTimer);

    saveTimer = setTimeout(async () => {
      try {
        const lockoutSeconds = parseInt(lockSelect.value || "60", 10);
        const alarmEnabled = !!alarmToggle.checked;
        await apiPost("updateSettings.php", { lockout_seconds: lockoutSeconds, alarm_enabled: alarmEnabled });
        if (lockNote) lockNote.textContent = "Saved to database.";
      } catch (e) {
        if (lockNote) lockNote.textContent = `Save failed: ${e.message}`;
      }
    }, 350);
  };

  if (lockSelect) lockSelect.addEventListener("change", queueSave);
  if (alarmToggle) alarmToggle.addEventListener("change", queueSave);

  // PIN change
  if (pinBtn) {
    pinBtn.addEventListener("click", async () => {
      if (!currentPinInput || !newPinInput) return;

      const current_pin = (currentPinInput.value || "").trim();
      const new_pin = (newPinInput.value || "").trim();

      if (!/^\d{4,12}$/.test(current_pin) || !/^\d{4,12}$/.test(new_pin)) {
        if (pinNote) pinNote.textContent = "PIN must be 4–12 digits.";
        return;
      }

      if (current_pin === new_pin) {
        if (pinNote) pinNote.textContent = "New PIN must be different from current PIN.";
        return;
      }

      try {
        await apiPost("updatePin.php", { current_pin, new_pin });
        if (pinNote) pinNote.textContent = "PIN updated successfully.";
        currentPinInput.value = "";
        newPinInput.value = "";
      } catch (e) {
        if (pinNote) pinNote.textContent = `PIN update failed: ${e.message}`;
      }
    });
  }
}

/* ---------------------------
   Fingerprint modals (settings.html)
--------------------------- */
function initFingerprintModals(state) {
  const addBtn = byId("btnAddFp");
  const removeBtn = byId("btnRemoveFp");

  const addModal = byId("modalAddFp");
  const removeModal = byId("modalRemoveFp");

  if (addModal) wireModalClose(addModal);
  if (removeModal) wireModalClose(removeModal);

  if (addBtn && addModal) addBtn.addEventListener("click", () => {
    const st = byId("addFpStatus"); if (st) st.textContent = "";
    openModal(addModal);
  });
  if (removeBtn && removeModal) removeBtn.addEventListener("click", () => {
    const st = byId("removeFpStatus"); if (st) st.textContent = "";
    openModal(removeModal);
  });

  // Add fingerprint (create user + fingerprint in DB)
  const addForm = byId("addFpForm");
  const addName = byId("addUserName");
  const addId = byId("addUserId");
  const addPhoto = byId("addUserPhoto");
  const addPreview = byId("addPhotoPreview");
  const addStatus = byId("addFpStatus");

  if (addPhoto && addPreview) {
    addPhoto.addEventListener("change", async () => {
      const f = addPhoto.files?.[0];
      if (!f) {
        addPreview.style.backgroundImage = "";
        addPreview.classList.remove("has-photo");
        return;
      }
      const url = await fileToDataURL(f);
      addPreview.style.backgroundImage = `url(${url})`;
      addPreview.classList.add("has-photo");
    });
  }

  if (addForm) {
    addForm.addEventListener("submit", async (e) => {
      e.preventDefault();
      if (addStatus) addStatus.textContent = "";

      const name = (addName?.value || "").trim();
      const user_code = (addId?.value || "").trim();

      if (!name || !user_code) {
        if (addStatus) addStatus.textContent = "Please enter both Username and ID.";
        return;
      }

      // Optional photo
      let photo_data_url = null;
      const f = addPhoto?.files?.[0];
      if (f) photo_data_url = await fileToDataURL(f);

      try {
        await apiPost("addFingerprint.php", { name, user_code, photo_data_url, simulate: true });
        if (addStatus) addStatus.textContent = "Saved to database (mock enrollment).";

        // Refresh sidebar user list + remove select
        await state.refreshUsers();

        // reset form + preview
        addForm.reset();
        if (addPreview) {
          addPreview.style.backgroundImage = "";
          addPreview.classList.remove("has-photo");
        }

        setTimeout(() => closeModal(addModal), 250);
      } catch (err) {
        if (addStatus) addStatus.textContent = `Save failed: ${err.message}`;
      }
    });
  }

  // Remove fingerprint
  const removeForm = byId("removeFpForm");
  const removeSelect = byId("removeUserSelect");
  const removeStatus = byId("removeFpStatus");

  if (removeForm) {
    removeForm.addEventListener("submit", async (e) => {
      e.preventDefault();
      if (removeStatus) removeStatus.textContent = "";

      const user_code = removeSelect?.value;
      if (!user_code) {
        if (removeStatus) removeStatus.textContent = "Choose a user to remove.";
        return;
      }

      // Keep at least one user for UI continuity
      if ((state.users || []).length <= 1) {
        if (removeStatus) removeStatus.textContent = "You must keep at least one user.";
        return;
      }

      try {
        await apiPost("removeFingerprint.php", { user_code, simulate: true, deactivate_user: true });
        if (removeStatus) removeStatus.textContent = "Removed from database.";

        await state.refreshUsers();
        setTimeout(() => closeModal(removeModal), 250);
      } catch (err) {
        if (removeStatus) removeStatus.textContent = `Remove failed: ${err.message}`;
      }
    });
  }
}

/* ---------------------------
   Main init (runs on all pages)
--------------------------- */
document.addEventListener("DOMContentLoaded", async () => {
  // User dropdown toggle
  const userBtn = document.querySelector(".user");
  const userMenu = byId("userMenu");

  if (userBtn && userMenu) {
    userBtn.addEventListener("click", () => {
      userMenu.classList.toggle("is-open");
      userBtn.setAttribute("aria-expanded", userMenu.classList.contains("is-open") ? "true" : "false");
    });

    // close dropdown when clicking outside
    document.addEventListener("click", (e) => {
      if (!userMenu.classList.contains("is-open")) return;
      const clickedInside = userMenu.contains(e.target) || userBtn.contains(e.target);
      if (!clickedInside) userMenu.classList.remove("is-open");
    });
  }

  // Load users from API (fallback to localStorage if API fails)
  let users = [];

  const refreshUsers = async () => {
    try {
      const me = await apiGet("getMe.php"); // current user for sidebar label
      const u = await apiGet("getUsers.php");
      users = (u.users || []).map(x => ({ id: x.id, name: x.name, photo: x.photo || null }));

      // Put current user first in list if present
      const meIdx = users.findIndex(x => x.id === me.username);
      if (meIdx >= 0) {
        const meUser = users.splice(meIdx, 1)[0];
        meUser.name = me.display_name || meUser.name;
        meUser.photo = me.avatar_data_url || meUser.photo;
        users.unshift(meUser);
      } else {
        const topName = document.querySelector(".user__name");
        if (topName) topName.textContent = me.display_name || "User";
      }

      renderUserMenu(users);
      renderRemoveSelect(users);

      // Keep fallback updated (optional)
      saveUsersFallback(users);

      return true;
    } catch (e) {
      console.warn("Users API unavailable, using fallback:", e.message);
      users = loadUsersFallback();
      renderUserMenu(users);
      renderRemoveSelect(users);
      return false;
    }
  };

  await refreshUsers();

  // Wire fingerprint modals (only exists on settings.html, but safe to call)
  initFingerprintModals({
    get users() { return users; },
    refreshUsers,
  });

  // Page-specific init
  initDashboard();
  initRealtime();
  initLogsPage();
  initAlertsPage();
  initSettingsPage();
});
