#ifndef DASHBOARD_H
#define DASHBOARD_H
#include <Arduino.h>

/*
 * dashboard.h
 * -----------
 * Two self-contained HTML pages, served by Section 1's WebServer
 * (account_pairing.cpp) only while the pairing portal is active:
 *
 *   customer_dashboard_html  -> served at  GET /
 *   admin_dashboard_html     -> served at  GET /admin?key=<ADMIN_PORTAL_KEY>
 *
 * Both call the SAME JSON API Section 1/3 already expose
 * (/info, /networks, /pair, /status) — this file adds no new
 * backend logic, just a UI in front of what already existed.
 * The admin page additionally calls three admin-only endpoints
 * (added in account_pairing.cpp): /admin/api/version,
 * /admin/api/fota-check, /admin/api/exit-ap-mode.
 *
 * PROGMEM: these strings live in flash, not RAM, since HTML pages
 * are much bigger than the JSON responses the rest of this project
 * sends — same reasoning as your reference dashboard_html.h.
 *
 * HONEST NOTE ON THE ADMIN GATE: "?key=..." is a shared-secret query
 * param, not real authentication — fine for a local AP with a small,
 * trusted audience, not something to expose beyond that. A genuine
 * login (or disabling the admin page over WiFi entirely and only
 * allowing it over BLE/serial) would be the real fix before this
 * goes anywhere more exposed.
 */

const char customer_dashboard_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Device Setup</title>
<style>
* { margin:0; padding:0; box-sizing:border-box; }
body { font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,Arial,sans-serif;
  background:#f5f6f8; color:#1a1a1a; min-height:100vh; padding:24px; line-height:1.5; }
.container { max-width:520px; margin:0 auto; }
.header { text-align:center; margin-bottom:24px; }
.header h1 { font-size:1.5rem; font-weight:600; }
.header p { color:#6b7280; font-size:0.85rem; margin-top:4px; }
.card { background:#ffffff; border:1px solid #e5e7eb; border-radius:10px;
  padding:20px; margin-bottom:16px; box-shadow:0 1px 2px rgba(0,0,0,0.04); }
.card h3 { font-size:1rem; font-weight:600; margin-bottom:14px; }
.row { display:flex; justify-content:space-between; padding:8px 0; border-bottom:1px solid #f0f0f0; font-size:0.85rem; }
.row:last-child { border-bottom:none; }
.row span:first-child { color:#6b7280; }
.form-group { margin-bottom:14px; }
label { display:block; font-size:0.8rem; font-weight:500; color:#374151; margin-bottom:6px; }
input, select { width:100%; padding:10px 12px; border:1px solid #d1d5db; border-radius:6px;
  font-size:0.875rem; background:#fff; color:#1a1a1a; }
input:focus, select:focus { outline:none; border-color:#3b82f6; box-shadow:0 0 0 3px rgba(59,130,246,0.1); }
button { width:100%; padding:11px; border:none; border-radius:6px; background:#3b82f6; color:#fff;
  font-weight:600; font-size:0.875rem; cursor:pointer; margin-top:6px; }
button:hover { background:#2563eb; }
button:disabled { opacity:0.5; cursor:not-allowed; }
.btn-secondary { background:#f3f4f6; color:#374151; }
.btn-secondary:hover { background:#e5e7eb; }
.status-connected { color:#059669; font-weight:600; }
.status-idle { color:#6b7280; }
.status-failed { color:#dc2626; font-weight:600; }
.msg { font-size:0.8rem; margin-top:10px; padding:8px 10px; border-radius:6px; display:none; }
.msg.show { display:block; }
.msg.ok { background:#ecfdf5; color:#059669; }
.msg.err { background:#fef2f2; color:#dc2626; }
</style>
</head>
<body>
<div class="container">
  <div class="header">
    <h1>Device Setup</h1>
    <p id="deviceUuid">Loading device info…</p>
  </div>

  <div class="card">
    <h3>Status</h3>
    <div class="row"><span>Connection</span><span id="statState" class="status-idle">idle</span></div>
    <div class="row"><span>Network</span><span id="statSsid">—</span></div>
  </div>

  <div class="card">
    <h3>AP Mode</h3>
    <p style="font-size:0.8rem;color:#6b7280;margin-bottom:12px;">The device can run its own WiFi network (for setup) and your home network at the same time. Use this to reconfigure WiFi without a reset.</p>
    <button class="btn-secondary" onclick="enterApMode()" id="enterBtn">Enter AP Mode</button>
    <div id="enterMsg" class="msg"></div>
    <button class="btn-secondary" onclick="exitApMode()" id="exitBtn" style="margin-top:8px;">Exit AP Mode</button>
    <div id="exitMsg" class="msg"></div>
  </div>

  <div class="card">
    <h3>Wi-Fi Network</h3>
    <div class="form-group">
      <label>Available Networks</label>
      <select id="networkSelect" onchange="onNetworkSelect()"><option>Scanning…</option></select>
    </div>
    <div class="form-group">
      <label>Wi-Fi Name (SSID)</label>
      <input type="text" id="ssid1" placeholder="Your Wi-Fi network name">
    </div>
    <div class="form-group">
      <label>Password</label>
      <input type="password" id="pass1" placeholder="Wi-Fi password">
    </div>
  </div>

  <div class="card">
    <h3>Your Details</h3>
    <div class="form-group"><label>Name</label><input type="text" id="name" placeholder="Full name"></div>
    <div class="form-group"><label>Email</label><input type="email" id="email" placeholder="you@example.com"></div>
    <div class="form-group"><label>Mobile</label><input type="text" id="mobile" placeholder="Phone number"></div>
  </div>

  <button onclick="submitPairing()" id="submitBtn">Connect Device</button>
  <div id="pairMsg" class="msg"></div>
</div>

<script>
const $ = id => document.getElementById(id);

async function loadInfo() {
  try {
    const r = await fetch('/info'); const d = await r.json();
    $('deviceUuid').textContent = d.model + ' · ' + d.uuid.slice(0, 8);
  } catch (e) { $('deviceUuid').textContent = 'Device info unavailable'; }
}

async function loadNetworks() {
  try {
    const r = await fetch('/networks'); const list = await r.json();
    const sel = $('networkSelect');
    sel.innerHTML = '<option value="">Select a network…</option>';
    list.forEach(n => {
      const o = document.createElement('option');
      o.value = n.ssid; o.textContent = n.ssid + ' (' + n.rssi + ' dBm)';
      sel.appendChild(o);
    });
    if (list.length === 0) setTimeout(loadNetworks, 2000); // scan still running
  } catch (e) { /* portal may be mid-scan, retry via polling below */ }
}

function onNetworkSelect() {
  const v = $('networkSelect').value;
  if (v) $('ssid1').value = v;
}

async function refreshStatus() {
  try {
    const r = await fetch('/status'); const d = await r.json();
    const el = $('statState');
    el.textContent = d.state;
    el.className = d.state === 'connected' ? 'status-connected' : (d.state === 'failed' ? 'status-failed' : 'status-idle');
    $('statSsid').textContent = d.ssid || '—';
  } catch (e) {}
}

function showMsg(text, ok) {
  const m = $('pairMsg');
  m.textContent = text;
  m.className = 'msg show ' + (ok ? 'ok' : 'err');
}

function showApMsg(id, text, ok) {
  const m = $(id);
  m.textContent = text;
  m.className = 'msg show ' + (ok ? 'ok' : 'err');
}

async function enterApMode() {
  $('enterBtn').disabled = true;
  try {
    const r = await fetch('/admin/api/enter-ap-mode', { method: 'POST' });
    const d = await r.json();
    showApMsg('enterMsg', r.ok ? 'AP mode is on — join "Smart Device" WiFi and open 192.168.4.1.' : (d.error || 'Failed.'), r.ok);
  } catch (e) { showApMsg('enterMsg', 'Request failed: ' + e.message, false); }
  $('enterBtn').disabled = false;
}

async function exitApMode() {
  $('exitBtn').disabled = true;
  try {
    const r = await fetch('/admin/api/exit-ap-mode', { method: 'POST' });
    const d = await r.json();
    showApMsg('exitMsg', r.ok ? 'AP mode turned off.' : (d.error || 'Failed.'), r.ok);
  } catch (e) { showApMsg('exitMsg', 'Request failed: ' + e.message, false); }
  $('exitBtn').disabled = false;
}

async function submitPairing() {
  const body = {
    ssid1: $('ssid1').value, pass1: $('pass1').value,
    name: $('name').value, email: $('email').value, mobile: $('mobile').value
  };
  if (!body.ssid1) { showMsg('Please choose or enter a Wi-Fi network name.', false); return; }

  $('submitBtn').disabled = true;
  try {
    const r = await fetch('/pair', { method: 'POST', headers: {'Content-Type':'application/json'}, body: JSON.stringify(body) });
    const d = await r.json();
    if (r.ok) {
      showMsg('Connecting… this page will stop responding once the device leaves AP mode. That\'s expected.', true);
    } else {
      showMsg(d.error || 'Could not submit — check the form and try again.', false);
      $('submitBtn').disabled = false;
    }
  } catch (e) {
    showMsg('Request failed: ' + e.message, false);
    $('submitBtn').disabled = false;
  }
}

loadInfo();
loadNetworks();
refreshStatus();
setInterval(refreshStatus, 3000);
</script>
</body>
</html>
)rawliteral";

const char admin_dashboard_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Device Admin</title>
<style>
* { margin:0; padding:0; box-sizing:border-box; }
body { font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,Arial,sans-serif;
  background:#f5f6f8; color:#1a1a1a; min-height:100vh; padding:24px; line-height:1.5; }
.container { max-width:560px; margin:0 auto; }
.header { text-align:center; margin-bottom:24px; }
.header h1 { font-size:1.5rem; font-weight:600; }
.badge { display:inline-block; background:#fef3c7; color:#92400e; font-size:0.7rem; font-weight:700;
  padding:3px 8px; border-radius:20px; margin-top:6px; letter-spacing:0.03em; }
.card { background:#ffffff; border:1px solid #e5e7eb; border-radius:10px;
  padding:20px; margin-bottom:16px; box-shadow:0 1px 2px rgba(0,0,0,0.04); }
.card h3 { font-size:1rem; font-weight:600; margin-bottom:14px; }
.card.admin-only { border-color:#fbbf24; }
.row { display:flex; justify-content:space-between; padding:8px 0; border-bottom:1px solid #f0f0f0; font-size:0.85rem; }
.row:last-child { border-bottom:none; }
.row span:first-child { color:#6b7280; }
.form-group { margin-bottom:14px; }
label { display:block; font-size:0.8rem; font-weight:500; color:#374151; margin-bottom:6px; }
input, select { width:100%; padding:10px 12px; border:1px solid #d1d5db; border-radius:6px;
  font-size:0.875rem; background:#fff; color:#1a1a1a; }
button { width:100%; padding:11px; border:none; border-radius:6px; background:#3b82f6; color:#fff;
  font-weight:600; font-size:0.875rem; cursor:pointer; margin-top:6px; }
button:hover { background:#2563eb; }
button:disabled { opacity:0.5; cursor:not-allowed; }
.btn-warn { background:#f59e0b; }
.btn-warn:hover { background:#d97706; }
.status-connected { color:#059669; font-weight:600; }
.status-idle { color:#6b7280; }
.status-failed { color:#dc2626; font-weight:600; }
.msg { font-size:0.8rem; margin-top:10px; padding:8px 10px; border-radius:6px; display:none; }
.msg.show { display:block; }
.msg.ok { background:#ecfdf5; color:#059669; }
.msg.err { background:#fef2f2; color:#dc2626; }
</style>
</head>
<body>
<div class="container">
  <div class="header">
    <h1>Device Admin</h1>
    <span class="badge">ADMIN ACCESS</span>
  </div>

  <div class="card">
    <h3>Status</h3>
    <div class="row"><span>Connection</span><span id="statState" class="status-idle">idle</span></div>
    <div class="row"><span>Network</span><span id="statSsid">—</span></div>
    <div class="row"><span>Firmware</span><span id="fwVersion">—</span></div>
  </div>

  <div class="card admin-only">
    <h3>Firmware Update (admin only)</h3>
    <p style="font-size:0.8rem;color:#6b7280;margin-bottom:12px;">Forces an immediate check against the FOTA server, instead of waiting for the periodic check.</p>
    <button class="btn-warn" onclick="triggerFotaCheck()" id="fotaBtn">Check for Updates Now</button>
    <div id="fotaMsg" class="msg"></div>
  </div>

  <div class="card">
    <h3>AP Mode</h3>
    <p style="font-size:0.8rem;color:#6b7280;margin-bottom:12px;">Reopens the pairing portal so WiFi credentials can be changed — no physical button or reset needed. Safe to click even while already connected normally.</p>
    <button class="btn-warn" onclick="enterApMode()" id="enterBtn">Enter AP Mode (Change WiFi)</button>
    <div id="enterMsg" class="msg"></div>
    <p style="font-size:0.8rem;color:#6b7280;margin:12px 0;">Once in AP mode, use this same page (reachable at 192.168.4.1) to submit new credentials, then exit below.</p>
    <button class="btn-warn" onclick="exitApMode()" id="exitBtn">Exit AP Mode / Resume Normal Operation</button>
    <div id="exitMsg" class="msg"></div>
  </div>

  <div class="card">
    <h3>Wi-Fi Network</h3>
    <div class="form-group">
      <label>Available Networks</label>
      <select id="networkSelect" onchange="onNetworkSelect()"><option>Scanning…</option></select>
    </div>
    <div class="form-group"><label>Wi-Fi Name (SSID)</label><input type="text" id="ssid1"></div>
    <div class="form-group"><label>Password</label><input type="password" id="pass1"></div>
  </div>

  <div class="card">
    <h3>Owner Details</h3>
    <div class="form-group"><label>Name</label><input type="text" id="name"></div>
    <div class="form-group"><label>Email</label><input type="email" id="email"></div>
    <div class="form-group"><label>Mobile</label><input type="text" id="mobile"></div>
  </div>

  <button onclick="submitPairing()" id="submitBtn">Connect Device</button>
  <div id="pairMsg" class="msg"></div>
</div>

<script>
const $ = id => document.getElementById(id);
const ADMIN_KEY = new URLSearchParams(window.location.search).get('key') || '';

function adminUrl(path) {
  return path + (path.includes('?') ? '&' : '?') + 'key=' + encodeURIComponent(ADMIN_KEY);
}

async function loadVersion() {
  try {
    const r = await fetch(adminUrl('/admin/api/version'));
    const d = await r.json();
    $('fwVersion').textContent = d.version || 'unknown';
  } catch (e) { $('fwVersion').textContent = 'unavailable'; }
}

async function loadNetworks() {
  try {
    const r = await fetch('/networks'); const list = await r.json();
    const sel = $('networkSelect');
    sel.innerHTML = '<option value="">Select a network…</option>';
    list.forEach(n => {
      const o = document.createElement('option');
      o.value = n.ssid; o.textContent = n.ssid + ' (' + n.rssi + ' dBm)';
      sel.appendChild(o);
    });
    if (list.length === 0) setTimeout(loadNetworks, 2000);
  } catch (e) {}
}

function onNetworkSelect() { const v = $('networkSelect').value; if (v) $('ssid1').value = v; }

async function refreshStatus() {
  try {
    const r = await fetch('/status'); const d = await r.json();
    const el = $('statState');
    el.textContent = d.state;
    el.className = d.state === 'connected' ? 'status-connected' : (d.state === 'failed' ? 'status-failed' : 'status-idle');
    $('statSsid').textContent = d.ssid || '—';
  } catch (e) {}
}

function showMsg(id, text, ok) {
  const m = $(id);
  m.textContent = text;
  m.className = 'msg show ' + (ok ? 'ok' : 'err');
}

async function triggerFotaCheck() {
  $('fotaBtn').disabled = true;
  try {
    const r = await fetch(adminUrl('/admin/api/fota-check'), { method: 'POST' });
    const d = await r.json();
    showMsg('fotaMsg', r.ok ? 'Update check triggered — watch the serial log.' : (d.error || 'Failed.'), r.ok);
  } catch (e) { showMsg('fotaMsg', 'Request failed: ' + e.message, false); }
  $('fotaBtn').disabled = false;
}

async function enterApMode() {
  $('enterBtn').disabled = true;
  try {
    const r = await fetch(adminUrl('/admin/api/enter-ap-mode'), { method: 'POST' });
    const d = await r.json();
    showMsg('enterMsg', r.ok ? 'Entering AP mode — join "Smart Device" WiFi and open 192.168.4.1 to change credentials.' : (d.error || 'Failed.'), r.ok);
  } catch (e) { showMsg('enterMsg', 'Request failed: ' + e.message, false); }
  $('enterBtn').disabled = false;
}

async function exitApMode() {
  $('exitBtn').disabled = true;
  try {
    const r = await fetch(adminUrl('/admin/api/exit-ap-mode'), { method: 'POST' });
    const d = await r.json();
    showMsg('exitMsg', r.ok ? 'Resuming normal operation — this page will stop responding shortly. That\'s expected.' : (d.error || 'Failed.'), r.ok);
  } catch (e) { showMsg('exitMsg', 'Request failed: ' + e.message, false); }
}

async function submitPairing() {
  const body = { ssid1: $('ssid1').value, pass1: $('pass1').value,
    name: $('name').value, email: $('email').value, mobile: $('mobile').value };
  if (!body.ssid1) { showMsg('pairMsg', 'Please choose or enter a Wi-Fi network name.', false); return; }
  $('submitBtn').disabled = true;
  try {
    const r = await fetch('/pair', { method:'POST', headers:{'Content-Type':'application/json'}, body: JSON.stringify(body) });
    const d = await r.json();
    showMsg('pairMsg', r.ok ? 'Connecting…' : (d.error || 'Failed.'), r.ok);
    if (!r.ok) $('submitBtn').disabled = false;
  } catch (e) { showMsg('pairMsg', 'Request failed: ' + e.message, false); $('submitBtn').disabled = false; }
}

loadVersion();
loadNetworks();
refreshStatus();
setInterval(refreshStatus, 3000);
</script>
</body>
</html>
)rawliteral";

#endif // DASHBOARD_H
