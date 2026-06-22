#pragma once
/**
 * wifi_portal.h — Captive-portal / AP setup + STA-mode config web server
 *
 * AP mode  (no credentials in NVS):
 *   Starts soft-AP "BambuTagger-Console" → open http://192.168.4.1
 *   Fill in WiFi + printer details → Save & Reboot.
 *
 * STA mode (after credentials saved):
 *   Starts a config server on port 80 at the device's WiFi IP.
 *   Visit http://<device-ip> to edit any setting and reboot.
 */

#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <Update.h>
#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "config.h"

// Forward-declared in main.cpp — show OTA progress on the display
void ota_display_begin();
void ota_display_progress(int percent, const char *msg);
void ota_display_end();

#define PORTAL_AP_SSID  "BambuTagger-Console"
#define PORTAL_AP_IP    "192.168.4.1"

// ─────────────────────────────────────────────────────────────
// Shared HTML template (AP setup + STA edit form)
// Placeholder %%VALS%% is replaced at runtime with pre-filled values
// ─────────────────────────────────────────────────────────────
// ── Shared styles for both settings pages ────────────────────
static const char _SETTINGS_STYLE[] PROGMEM = R"(
.subnav{display:flex;gap:0;margin-bottom:18px;border-bottom:1px solid #1e3a5f}
.subnav a{padding:8px 18px;color:#8899aa;text-decoration:none;font-size:13px;font-weight:600;
          border-bottom:2px solid transparent;margin-bottom:-1px;transition:all .2s}
.subnav a:hover{color:#eaeaea}
.subnav a.active{color:#1db954;border-bottom-color:#1db954}
)";

// ── WiFi settings page ────────────────────────────────────────
static const char _PORTAL_WIFI_TMPL[] PROGMEM = R"html(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>BambuTagger-Console — WiFi</title>
<link rel="icon" href="/Logo/bambutagger.png">
<style>
  *{box-sizing:border-box;margin:0;padding:0}
  body{background:#1a1a2e;color:#eaeaea;font-family:Arial,sans-serif;min-height:100vh}
  nav{background:#16213e;padding:8px 16px;display:flex;align-items:center;gap:24px;flex-wrap:wrap}
  .nav-brand{display:flex;align-items:center;gap:8px;font-size:18px;font-weight:700;color:#90caf9}
  .nav-brand img{width:32px;height:32px;border-radius:4px}
  .nav-links{display:flex;gap:4px}
  .nav-links a{padding:6px 14px;border-radius:6px;color:#8899aa;text-decoration:none;font-size:14px}
  .nav-links a:hover{background:#0f3460;color:#eaeaea}
  .nav-links a.active{background:#1db95433;color:#1db954;font-weight:600}
  .wrapper{display:flex;align-items:center;justify-content:center;padding:16px}
  .card{background:#16213e;border-radius:12px;padding:28px;width:100%;max-width:480px;
        box-shadow:0 4px 24px rgba(0,0,0,.5)}
  h2{color:#90caf9;font-size:.9em;letter-spacing:.05em;text-transform:uppercase;
     margin:0 0 16px;padding-bottom:4px;border-bottom:1px solid #1e3a5f}
  label{display:block;color:#8899aa;font-size:.8em;margin-bottom:3px}
  input{display:block;width:100%;padding:10px 12px;margin-bottom:10px;
        background:#0f3460;color:#eaeaea;border:1px solid #2d3561;border-radius:6px;
        font-size:.95em;outline:none;transition:border .2s}
  input:focus{border-color:#1db954}
  button{display:block;width:100%;padding:13px;margin-top:6px;
         background:#1db954;color:#fff;border:none;border-radius:8px;
         font-size:1.05em;font-weight:bold;cursor:pointer;transition:background .2s}
  button:hover{background:#17a845}
)html";
// (styles continued below _SETTINGS_STYLE)
static const char _PORTAL_WIFI_TMPL2[] PROGMEM = R"html(
</style>
</head>
<body>
<nav>
 <div class="nav-brand"><img src="/Logo/bambutagger.png" alt=""><span>BambuTagger Console</span></div>
 <div class="nav-links"><a href="/">Dashboard</a><a href="/config/wifi" class="active">Settings</a><a href="/update">Firmware</a></div>
</nav>
<div class="wrapper">
<div class="card">
  <div class="subnav"><a href="/config/wifi" class="active">WiFi</a><a href="/config/printers">Printers</a></div>
  <h2>WiFi Settings</h2>
  <form method="post" action="/save">
    <label>SSID</label>
    <input name="ssid" placeholder="Your WiFi network name" required autocomplete="off" %%SSID%%>
    <label>Password</label>
    <input name="pass" type="password" placeholder="WiFi password" autocomplete="off" %%PASS%%>
    <button type="submit">Save &amp; Reboot</button>
  </form>
</div>
</div>
</body>
</html>
)html";

// ── Printers settings page ───────────────────────────────────
static const char _PORTAL_PRINTERS_TMPL[] PROGMEM = R"html(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>BambuTagger-Console — Printers</title>
<link rel="icon" href="/Logo/bambutagger.png">
<style>
  *{box-sizing:border-box;margin:0;padding:0}
  body{background:#1a1a2e;color:#eaeaea;font-family:Arial,sans-serif;min-height:100vh}
  nav{background:#16213e;padding:8px 16px;display:flex;align-items:center;gap:24px;flex-wrap:wrap}
  .nav-brand{display:flex;align-items:center;gap:8px;font-size:18px;font-weight:700;color:#90caf9}
  .nav-brand img{width:32px;height:32px;border-radius:4px}
  .nav-links{display:flex;gap:4px}
  .nav-links a{padding:6px 14px;border-radius:6px;color:#8899aa;text-decoration:none;font-size:14px}
  .nav-links a:hover{background:#0f3460;color:#eaeaea}
  .nav-links a.active{background:#1db95433;color:#1db954;font-weight:600}
  .wrapper{display:flex;align-items:center;justify-content:center;padding:16px}
  .card{background:#16213e;border-radius:12px;padding:28px;width:100%;max-width:480px;
        box-shadow:0 4px 24px rgba(0,0,0,.5)}
  h2{color:#90caf9;font-size:.9em;letter-spacing:.05em;text-transform:uppercase;
     margin:0 0 16px;padding-bottom:4px;border-bottom:1px solid #1e3a5f}
  .printer-section{border:1px solid #1e3a5f;border-radius:8px;padding:12px;margin:8px 0}
  .printer-section h3{color:#eaeaea;font-size:.9em;margin:0 0 8px}
  label{display:block;color:#8899aa;font-size:.8em;margin-bottom:3px}
  input{display:block;width:100%;padding:10px 12px;margin-bottom:10px;
        background:#0f3460;color:#eaeaea;border:1px solid #2d3561;border-radius:6px;
        font-size:.95em;outline:none;transition:border .2s}
  input:focus{border-color:#1db954}
  button{display:block;width:100%;padding:13px;margin-top:6px;
         background:#1db954;color:#fff;border:none;border-radius:8px;
         font-size:1.05em;font-weight:bold;cursor:pointer;transition:background .2s}
  button:hover{background:#17a845}
  .subnav{display:flex;gap:0;margin-bottom:18px;border-bottom:1px solid #1e3a5f}
  .subnav a{padding:8px 18px;color:#8899aa;text-decoration:none;font-size:13px;font-weight:600;
            border-bottom:2px solid transparent;margin-bottom:-1px;transition:all .2s}
  .subnav a:hover{color:#eaeaea}
  .subnav a.active{color:#1db954;border-bottom-color:#1db954}
  .hint{color:#6c757d;font-size:.78em;margin-top:-7px;margin-bottom:10px}
  .printer-hide{display:none}
</style>
<script>
function togglePrinters(n){
  for(var i=1;i<=4;i++){
    var el=document.getElementById('printer-'+i);
    if(el) el.className='printer-section'+(i>n?' printer-hide':'');
  }
}
</script>
</head>
<body>
<nav>
 <div class="nav-brand"><img src="/Logo/bambutagger.png" alt=""><span>BambuTagger Console</span></div>
 <div class="nav-links"><a href="/">Dashboard</a><a href="/config/wifi" class="active">Settings</a><a href="/update">Firmware</a></div>
</nav>
<div class="wrapper">
<div class="card">
  <div class="subnav"><a href="/config/wifi">WiFi</a><a href="/config/printers" class="active">Printers</a></div>
  <h2>Printer Settings</h2>
  <form method="post" action="/save">
    <label style="display:inline">Number of printers:</label>
    <select name="bam_count" onchange="togglePrinters(parseInt(this.value))"
            style="display:inline-block;width:auto;padding:6px 10px;margin-bottom:12px;
                   background:#0f3460;color:#eaeaea;border:1px solid #2d3561;border-radius:6px;
                   font-size:.95em;outline:none">
      <option value="1" %%SEL1%%>1</option>
      <option value="2" %%SEL2%%>2</option>
      <option value="3" %%SEL3%%>3</option>
      <option value="4" %%SEL4%%>4</option>
    </select>

    %%PRINTERS%%

    <button type="submit">Save &amp; Reboot</button>
  </form>
</div>
<script>togglePrinters(%%COUNT%%);</script>
</body>
</html>
)html";

static const char _PORTAL_SAVED_HTML[] PROGMEM = R"html(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<meta http-equiv="refresh" content="15;url=/config">
<title>BambuTagger-Console</title>
<link rel="icon" href="/Logo/bambutagger.png">
<style>
  *{box-sizing:border-box;margin:0;padding:0}
  body{background:#1a1a2e;color:#eaeaea;font-family:Arial,sans-serif;min-height:100vh}
  nav{background:#16213e;padding:8px 16px;display:flex;align-items:center;gap:24px;flex-wrap:wrap}
  .nav-brand{display:flex;align-items:center;gap:8px;font-size:18px;font-weight:700;color:#90caf9}
  .nav-brand img{width:32px;height:32px;border-radius:4px}
  .nav-links{display:flex;gap:4px}
  .nav-links a{padding:6px 14px;border-radius:6px;color:#8899aa;text-decoration:none;font-size:14px}
  .wrapper{display:flex;align-items:center;justify-content:center;padding:40px 16px;text-align:center}
  .card{background:#16213e;border-radius:12px;padding:40px 32px}
  h1{color:#1db954;font-size:2em;margin-bottom:10px}
  p{color:#8899aa}
</style>
<script>setTimeout(function(){location.href='/config'},15000);</script>
</head>
<body>
<nav>
 <div class="nav-brand"><img src="/Logo/bambutagger.png" alt=""><span>BambuTagger Console</span></div>
 <div class="nav-links"><a href="/">Dashboard</a><a href="/config/wifi">Settings</a><a href="/update">Firmware</a></div>
</nav>
<div class="wrapper">
<div class="card">
  <h1>&#10003; Saved!</h1>
  <p>Settings stored. Device is rebooting&hellip;</p>
  <p style="margin-top:12px;font-size:.85em">Reconnecting automatically in 15 seconds&hellip;</p>
</div>
</div>
</body>
</html>
)html";

// ─────────────────────────────────────────────────────────────
// Firmware update page — fetches latest release from GitHub
// ─────────────────────────────────────────────────────────────
static const char _UPDATE_PAGE_HTML[] PROGMEM = R"html(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>BambuTagger-Console — Update</title>
<style>
  *{box-sizing:border-box;margin:0;padding:0}
  body{background:#1a1a2e;color:#eaeaea;font-family:Arial,sans-serif;min-height:100vh}
  nav{background:#16213e;padding:8px 16px;display:flex;align-items:center;gap:24px;flex-wrap:wrap}
  .nav-brand{display:flex;align-items:center;gap:8px;font-size:18px;font-weight:700;color:#90caf9}
  .nav-brand img{width:32px;height:32px;border-radius:4px}
  .nav-links{display:flex;gap:4px}
  .nav-links a{padding:6px 14px;border-radius:6px;color:#8899aa;text-decoration:none;font-size:14px}
  .nav-links a:hover{background:#0f3460;color:#eaeaea}
  .nav-links a.active{background:#1db95433;color:#1db954;font-weight:600}
  .wrapper{display:flex;align-items:center;justify-content:center;padding:32px 16px}
  .card{background:#16213e;border-radius:12px;padding:28px;width:100%;max-width:440px;
        box-shadow:0 4px 24px rgba(0,0,0,.5)}
  h1{color:#1db954;font-size:1.4em;margin-bottom:4px}
  .sub{color:#8899aa;font-size:.85em;margin-bottom:22px}
  .ver{color:#90caf9;font-size:.95em;margin:9px 0 4px}
  .ver span{color:#eaeaea;font-weight:bold}
  .ver .new{color:#1db954}
  .ver .old{color:#6c757d}
  button{display:block;width:100%;padding:13px;margin-top:16px;
         background:#1db954;color:#fff;border:none;border-radius:8px;
         font-size:1.05em;font-weight:bold;cursor:pointer;transition:background .2s}
  button:hover{background:#17a845}
  button:disabled{background:#555;cursor:not-allowed}
  #status{display:none;margin-top:14px;padding:12px;border-radius:6px;font-size:.9em;text-align:center}
  #status.info{display:block;background:#0f3460;color:#90caf9}
  #status.ok{display:block;background:#0f3460;color:#1db954}
  #status.err{display:block;background:#3d1a1a;color:#e74c3c}
</style>
</head>
<body>
<nav>
 <div class="nav-brand"><img src="/Logo/bambutagger.png" alt=""><span>BambuTagger Console</span></div>
  <div class="nav-links"><a href="/">Dashboard</a><a href="/config/wifi">Settings</a><a href="/update" class="active">Firmware</a></div>
</nav>
<div class="wrapper">
<div class="card">
  <h1>Firmware Update</h1>
  <p class="sub">Install the latest release from GitHub</p>
  <div class="ver">Current: <span class="old" id="cur-ver">%%VER%%</span></div>
  <div class="ver">Latest:  <span class="new" id="lat-ver">&mdash;</span></div>
  <div id="status"></div>
  <button id="up-btn">Install Latest Version</button>
</div>
</div>
<script>
(function(){
  var btn=document.getElementById('up-btn'),
      st=document.getElementById('status'),
      lv=document.getElementById('lat-ver');
  btn.textContent='Checking…'; btn.disabled=true;
  st.className='info'; st.textContent='Checking for latest release…';
  var x=new XMLHttpRequest();
  x.open('GET','/api/release',true);
  x.onload=function(){
    if(x.status==200){
      var r=JSON.parse(x.responseText);
      lv.textContent=r.tag;
      if(r.update){
        st.className='info'; st.textContent='Version '+r.tag+' is available!';
        btn.textContent='Install Version '+r.tag;
        btn.disabled=false;
      }else{
        st.className='ok'; st.textContent='Already up to date.';
        btn.textContent='Re-install '+r.tag;
        btn.disabled=false;
      }
    }else{
      st.className='err'; st.textContent='Failed to check for updates.';
      btn.textContent='Install Latest Version';
      btn.disabled=false;
    }
  };
  x.onerror=function(){
    st.className='err'; st.textContent='Network error checking for updates.';
    btn.textContent='Install Latest Version';
    btn.disabled=false;
  };
  x.send();
  btn.onclick=function(){
    if(btn.disabled)return;
    btn.disabled=true; btn.textContent='Installing…';
    st.className='info'; st.textContent='Downloading firmware from GitHub…';
    var x=new XMLHttpRequest();
    x.open('POST','/update',true);
    x.onload=function(){
      if(x.responseText=='OK'){
        st.className='ok'; st.textContent='Update complete! Rebooting\u2026';
        btn.textContent='Rebooting\u2026';
        setTimeout(function(){location.href='/'},20000);
      }else{
        st.className='err'; st.textContent=x.responseText;
        btn.disabled=false; btn.textContent='Try Again';
      }
    };
    x.onerror=function(){
      st.className='ok';
      st.textContent='Update initiated \u2014 device is rebooting.';
      btn.textContent='Rebooting\u2026';
      setTimeout(function(){location.href='/'},20000);
    };
    x.send();
  };
})();
</script>
</div>
</body>
</html>
)html";

static const char _UPDATE_OK_HTML[] PROGMEM = R"html(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<meta http-equiv="refresh" content="20;url=/">
<title>BambuTagger-Console — Update</title>
<style>
  *{box-sizing:border-box;margin:0;padding:0}
  body{background:#1a1a2e;color:#eaeaea;font-family:Arial,sans-serif;min-height:100vh}
  nav{background:#16213e;padding:8px 16px;display:flex;align-items:center;gap:24px;flex-wrap:wrap}
  .nav-brand{display:flex;align-items:center;gap:8px;font-size:18px;font-weight:700;color:#90caf9}
  .nav-brand img{width:32px;height:32px;border-radius:4px}
  .nav-links{display:flex;gap:4px}
  .nav-links a{padding:6px 14px;border-radius:6px;color:#8899aa;text-decoration:none;font-size:14px}
  .nav-links a:hover{background:#0f3460;color:#eaeaea}
  .nav-links a.active{background:#1db95433;color:#1db954;font-weight:600}
  .wrapper{display:flex;align-items:center;justify-content:center;padding:40px 16px;text-align:center}
  .card{background:#16213e;border-radius:12px;padding:40px 32px}
  h1{color:#1db954;font-size:2em;margin-bottom:10px}
  p{color:#8899aa}
</style>
</head>
<body>
<nav>
 <div class="nav-brand"><img src="/Logo/bambutagger.png" alt=""><span>BambuTagger Console</span></div>
 <div class="nav-links"><a href="/">Dashboard</a><a href="/config/wifi">Settings</a><a href="/update" class="active">Firmware</a></div>
</nav>
<div class="wrapper">
<div class="card">
  <h1>&#10003; Update Complete!</h1>
  <p>Firmware flashed successfully. Rebooting&hellip;</p>
  <p style="margin-top:12px;font-size:.85em">Reconnecting automatically in 20 seconds&hellip;</p>
</div>
</div>
</body>
</html>
)html";

static const char _UPDATE_FAIL_HTML[] PROGMEM = R"html(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>BambuTagger-Console — Update</title>
<style>
  *{box-sizing:border-box;margin:0;padding:0}
  body{background:#1a1a2e;color:#eaeaea;font-family:Arial,sans-serif;min-height:100vh}
  nav{background:#16213e;padding:8px 16px;display:flex;align-items:center;gap:24px;flex-wrap:wrap}
  .nav-brand{display:flex;align-items:center;gap:8px;font-size:18px;font-weight:700;color:#90caf9}
  .nav-brand img{width:32px;height:32px;border-radius:4px}
  .nav-links{display:flex;gap:4px}
  .nav-links a{padding:6px 14px;border-radius:6px;color:#8899aa;text-decoration:none;font-size:14px}
  .nav-links a:hover{background:#0f3460;color:#eaeaea}
  .nav-links a.active{background:#1db95433;color:#1db954;font-weight:600}
  .wrapper{display:flex;align-items:center;justify-content:center;padding:40px 16px;text-align:center}
  .card{background:#16213e;border-radius:12px;padding:40px 32px}
  h1{color:#e74c3c;font-size:1.6em;margin-bottom:10px}
  p{color:#8899aa}
</style>
</head>
<body>
<nav>
 <div class="nav-brand"><img src="/Logo/bambutagger.png" alt=""><span>BambuTagger Console</span></div>
 <div class="nav-links"><a href="/">Dashboard</a><a href="/config/wifi">Settings</a><a href="/update" class="active">Firmware</a></div>
</nav>
<div class="wrapper">
<div class="card">
  <h1>&#10007; Update Failed</h1>
  <p>%%MSG%%</p>
  <p style="margin-top:12px;font-size:.85em">Please try again or flash via serial.</p>
</div>
</body>
</html>
)html";

// ─────────────────────────────────────────────────────────────
// Internal helpers
// ─────────────────────────────────────────────────────────────
static WebServer _portal_server(80);
static bool      _portal_active = false;
static bool      _sta_server_active = false;

// ── Build printer section HTML for one printer ─────────────────
static String _portal_printer_section(int idx, const String &ip,
                                       const String &serial, const String &code) {
    char buf[768];
    snprintf(buf, sizeof(buf),
        R"(<div class="printer-section" id="printer-%d">)" "\n"
        R"(  <h3>Printer %d</h3>)" "\n"
        R"(  <label>IP Address</label>)" "\n"
        R"(  <input name="ip_%d" placeholder="e.g. 192.168.1.100" value="%%IP%d%%">)" "\n"
        R"(  <p class="hint">Printer: Settings &#8594; Network</p>)" "\n"
        R"(  <label>Serial Number</label>)" "\n"
        R"(  <input name="serial_%d" placeholder="e.g. 01S00C123456789" value="%%SER%d%%">)" "\n"
        R"(  <p class="hint">Printer: Settings &#8594; About</p>)" "\n"
        R"(  <label>Access Code</label>)" "\n"
        R"(  <input name="code_%d" type="password" placeholder="8-digit code" value="%%COD%d%%">)" "\n"
        R"(  <p class="hint">Printer: Settings &#8594; Network &#8594; Access Code</p>)" "\n"
        R"(</div>)",
        idx + 1, idx + 1, idx, idx, idx, idx, idx, idx);
    return String(buf);
}

// ── Load printer section values from NVS ─────────────────────
static void _portal_load_printers(Preferences &prefs,
                                  String ip[MAX_PRINTERS],
                                  String serial[MAX_PRINTERS],
                                  String code[MAX_PRINTERS]) {
    int count = prefs.getInt("bam_count", 1);
    for (int i = 0; i < MAX_PRINTERS; i++) {
        char key[16];
        snprintf(key, sizeof(key), "bam_ip_%d", i);
        ip[i]     = prefs.getString(key, "");
        snprintf(key, sizeof(key), "bam_serial_%d", i);
        serial[i] = prefs.getString(key, "");
        snprintf(key, sizeof(key), "bam_code_%d", i);
        code[i]   = prefs.getString(key, "");
    }
    // Fallback: migrate old single-printer keys
    if (count <= 1 && ip[0].length() == 0) {
        ip[0]     = prefs.getString("bam_ip",     "");
        serial[0] = prefs.getString("bam_serial", "");
        code[0]   = prefs.getString("bam_code",   "");
    }
}

static String _portal_build_wifi_page() {
    Preferences prefs;
    prefs.begin("bambu_mon", true);
    String ssid = prefs.getString("wifi_ssid", "");
    String pass = prefs.getString("wifi_pass", "");
    prefs.end();

    String page = FPSTR(_PORTAL_WIFI_TMPL);
    page += FPSTR(_SETTINGS_STYLE);
    page += FPSTR(_PORTAL_WIFI_TMPL2);
    page.replace("%%SSID%%", ssid.length() ? "value=\"" + ssid + "\"" : "");
    page.replace("%%PASS%%", pass.length() ? "value=\"" + pass + "\"" : "");
    return page;
}

static String _portal_build_printers_page() {
    Preferences prefs;
    prefs.begin("bambu_mon", true);
    int count = prefs.getInt("bam_count", 1);
    String ip[MAX_PRINTERS], serial[MAX_PRINTERS], code[MAX_PRINTERS];
    _portal_load_printers(prefs, ip, serial, code);
    prefs.end();

    String printersHtml;
    for (int i = 0; i < MAX_PRINTERS; i++) {
        printersHtml += _portal_printer_section(i, ip[i], serial[i], code[i]);
    }

    String page = FPSTR(_PORTAL_PRINTERS_TMPL);
    page.replace("%%COUNT%%",  String(count));
    page.replace("%%SEL1%%",   count == 1 ? "selected" : "");
    page.replace("%%SEL2%%",   count == 2 ? "selected" : "");
    page.replace("%%SEL3%%",   count == 3 ? "selected" : "");
    page.replace("%%SEL4%%",   count == 4 ? "selected" : "");
    page.replace("%%PRINTERS%%", printersHtml);

    for (int i = 0; i < MAX_PRINTERS; i++) {
        char ph[16];
        snprintf(ph, sizeof(ph), "%%IP%d%%", i);
        page.replace(ph, ip[i]);
        snprintf(ph, sizeof(ph), "%%SER%d%%", i);
        page.replace(ph, serial[i]);
        snprintf(ph, sizeof(ph), "%%COD%d%%", i);
        page.replace(ph, code[i]);
    }
    return page;
}

static void _portal_handle_save() {
    String ssid = _portal_server.arg("ssid");
    String pass = _portal_server.arg("pass");

    if (ssid.length() == 0) {
        _portal_server.send(400, "text/plain", "SSID is required.");
        return;
    }

    Preferences prefs;
    prefs.begin("bambu_mon", false);

    // WiFi
    prefs.putString("wifi_ssid", ssid);
    prefs.putString("wifi_pass", pass);

    // Printer settings — only save if the form included printer fields
    String ip0 = _portal_server.arg("ip_0");
    if (ip0.length() > 0) {
        int count = _portal_server.arg("bam_count").toInt();
        if (count < 1 || count > MAX_PRINTERS) count = 1;
        prefs.putInt("bam_count", count);
        for (int i = 0; i < MAX_PRINTERS; i++) {
            char key[16];
            snprintf(key, sizeof(key), "bam_ip_%d", i);
            prefs.putString(key, _portal_server.arg("ip_" + String(i)));
            snprintf(key, sizeof(key), "bam_serial_%d", i);
            prefs.putString(key, _portal_server.arg("serial_" + String(i)));
            snprintf(key, sizeof(key), "bam_code_%d", i);
            prefs.putString(key, _portal_server.arg("code_" + String(i)));
        }
    }
    prefs.end();

    log_i("Portal: settings saved — rebooting");

    // Send response, flush TCP, then reboot
    _portal_server.send_P(200, "text/html; charset=utf-8", _PORTAL_SAVED_HTML);
    _portal_server.client().flush();
    _portal_server.client().stop();
    delay(1500);
    ESP.restart();
}

// ── Internal: fetch & parse latest release, return download URL & tag ──
// Returns true on success; downloadUrl and tag are filled in.
static bool _portal_get_latest_release(String &downloadUrl, String &tag) {
    WiFiClientSecure apiClient;
    apiClient.setInsecure();
    {
        HTTPClient apiHttp;
        apiHttp.setTimeout(10000);
        apiHttp.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
        if (!apiHttp.begin(apiClient, "https://api.github.com/repos/VID-PRO/BambuTagger-Console/releases/latest")) {
            return false;
        }
        apiHttp.addHeader("User-Agent", "BambuTagger-Console");
        apiHttp.addHeader("Accept", "application/vnd.github+json");
        int code = apiHttp.GET();
        if (code != 200) {
            apiHttp.end();
            return false;
        }
        String body = apiHttp.getString();
        apiHttp.end();

        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, body);
        if (err) return false;

        tag = doc["tag_name"].as<String>();
        if (tag.length() == 0) return false;

        JsonArray assets = doc["assets"].as<JsonArray>();
        for (JsonObject asset : assets) {
            const char *name = asset["name"].as<const char *>();
            if (name && strcmp(name, "BambuTagger-Console.ino.bin") == 0) {
                downloadUrl = asset["browser_download_url"].as<String>();
                break;
            }
        }
        if (downloadUrl.length() == 0) {
            downloadUrl = "https://github.com/VID-PRO/BambuTagger-Console/releases/download/"
                        + tag + "/BambuTagger-Console.ino.bin";
        }
    }
    return downloadUrl.length() > 0;
}

// ── Firmware update handlers ─────────────────────────────────

static void _portal_handle_update_page() {
    String page = FPSTR(_UPDATE_PAGE_HTML);
    page.replace("%%VER%%", APP_VERSION);
    _portal_server.send(200, "text/html; charset=utf-8", page);
}

static void _portal_handle_api_release() {
    String downloadUrl, tag;
    if (!_portal_get_latest_release(downloadUrl, tag)) {
        _portal_server.send(502, "application/json", R"({"error":"API request failed"})");
        return;
    }

    const char *ver = tag.c_str();
    while (*ver == 'v' || *ver == 'V') ++ver;

    bool update = false;
    int ma, mb;
    const char *a = ver, *b = APP_VERSION;
    while (*a && *b) {
        ma = mb = 0;
        while (*a >= '0' && *a <= '9') ma = ma * 10 + (*a++ - '0');
        while (*b >= '0' && *b <= '9') mb = mb * 10 + (*b++ - '0');
        if (ma != mb) { update = (ma > mb); break; }
        if (*a == '.' || *a == 'v') ++a;
        if (*b == '.' || *b == 'v') ++b;
    }
    if (!update) update = (*a && !*b);

    char json[256];
    snprintf(json, sizeof(json),
             R"({"tag":"%s","update":%s})",
             tag.c_str(), update ? "true" : "false");
    _portal_server.send(200, "application/json", json);
}

static void _portal_handle_update_github() {
    ota_display_begin();
    ota_display_progress(1, "Fetching latest release…");

    String downloadUrl, tag;
    if (!_portal_get_latest_release(downloadUrl, tag)) {
        _portal_server.send(200, "text/plain; charset=utf-8", "Error: Failed to fetch release info");
        ota_display_end();
        return;
    }

    ota_display_progress(5, "Downloading firmware from GitHub…");

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.setTimeout(30000);
    http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
    if (!http.begin(client, downloadUrl)) {
        _portal_server.send(200, "text/plain; charset=utf-8", "Error: Download connection failed");
        ota_display_end();
        return;
    }
    http.addHeader("User-Agent", "BambuTagger-Console");

    int code = http.GET();
    if (code != 200) {
        char buf[80];
        snprintf(buf, sizeof(buf), "Error: Download error (HTTP %d)", code);
        _portal_server.send(200, "text/plain; charset=utf-8", buf);
        http.end();
        ota_display_end();
        return;
    }

    int totalSize = http.getSize();
    WiFiClient *stream = http.getStreamPtr();

    uint8_t *buf = (uint8_t *)malloc(1024);
    if (!buf) {
        _portal_server.send(200, "text/plain; charset=utf-8", "Error: Memory allocation failed");
        http.end();
        ota_display_end();
        return;
    }

    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
        free(buf);
        _portal_server.send(200, "text/plain; charset=utf-8", "Error: Update init failed");
        http.end();
        ota_display_end();
        return;
    }

    int written  = 0;
    int last_pct = -1;

    while (http.connected() && (totalSize <= 0 || written < totalSize)) {
        int avail = stream->available();
        if (avail > 0) {
            int toRead = (avail > 1024) ? 1024 : avail;
            int read = stream->readBytes(buf, toRead);
            if (read <= 0) break;
            if (Update.write(buf, read) != read) {
                free(buf);
                Update.end(false);
                _portal_server.send(200, "text/plain; charset=utf-8", "Error: Flash write failed");
                http.end();
                ota_display_end();
                return;
            }
            written += read;

            if (totalSize > 0) {
                int pct = (written * 100) / totalSize;
                if (pct != last_pct && (pct % 10 == 0 || pct == 100)) {
                    last_pct = pct;
                    char msg[48];
                    snprintf(msg, sizeof(msg), "Downloading… %d%% (%d KB)", pct, written / 1024);
                    ota_display_progress(pct, msg);
                }
            }
        }
    }
    http.end();
    free(buf);

    ota_display_progress(95, "Flashing firmware…");

    if (!Update.end(true)) {
        char msg[96];
        snprintf(msg, sizeof(msg), "Update failed: %s", Update.errorString());
        _portal_server.send(200, "text/plain; charset=utf-8", msg);
        ota_display_end();
        return;
    }

    ota_display_progress(100, "Update complete! Rebooting…");
    _portal_server.send(200, "text/plain; charset=utf-8", "OK");
    _portal_server.client().flush();
    delay(1500);
    ESP.restart();
}

static void _portal_register_routes() {
    _portal_server.on("/config", HTTP_GET, []() {
        _portal_server.sendHeader("Location", "/config/wifi", true);
        _portal_server.send(302, "text/html",
            "<html><body><a href=\"/config/wifi\">WiFi Settings</a></body></html>");
    });
    _portal_server.on("/config/debug", HTTP_GET, []() {
        String raw = _portal_build_wifi_page();
        _portal_server.send(200, "text/plain; charset=utf-8", raw);
    });
    _portal_server.on("/config/wifi", HTTP_GET, []() {
        String page = _portal_build_wifi_page();
        _portal_server.send(200, "text/html; charset=utf-8", page);
    });
    _portal_server.on("/config/printers", HTTP_GET, []() {
        String page = _portal_build_printers_page();
        _portal_server.send(200, "text/html; charset=utf-8", page);
    });
    _portal_server.on("/save", HTTP_POST, _portal_handle_save);
    _portal_server.on("/update",      HTTP_GET,  _portal_handle_update_page);
    _portal_server.on("/update",      HTTP_POST, _portal_handle_update_github);
    _portal_server.on("/api/release", HTTP_GET,  _portal_handle_api_release);
    // Captive-portal catch-all (Android / iOS auto-redirect)
    _portal_server.onNotFound([]() {
        String loc;
        if (WiFi.getMode() == WIFI_AP) {
            loc = "http://192.168.4.1/config/wifi";
        } else {
            loc = "http://" + WiFi.localIP().toString() + "/";
        }
        _portal_server.sendHeader("Location", loc, true);
        _portal_server.send(302, "text/html",
            "<html><body><a href=\"" + loc + "\">Continue</a></body></html>");
    });
}

// ─────────────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────────────

// Check NVS: returns true if a non-empty SSID has been saved
inline bool wifi_portal_has_credentials() {
    Preferences prefs;
    prefs.begin("bambu_mon", true);
    bool has = prefs.isKey("wifi_ssid") &&
               prefs.getString("wifi_ssid").length() > 0;
    prefs.end();
    return has;
}

// AP mode: start soft-AP + captive portal web server
inline void wifi_portal_start() {
    WiFi.mode(WIFI_AP);
    WiFi.setHostname("BambuTagger-Console");
    WiFi.softAP(PORTAL_AP_SSID);
    log_i("Portal AP: SSID=%s  IP=%s", PORTAL_AP_SSID,
          WiFi.softAPIP().toString().c_str());

    _portal_register_routes();
    _portal_server.begin();
    _portal_active = true;
}

// STA mode: start config server on port 80 at the device's WiFi IP
// Call this after WiFi.isConnected() becomes true
inline void wifi_sta_server_start() {
    if (_sta_server_active) return;
    _portal_register_routes();   // same routes, same save handler
    _portal_server.begin();
    _sta_server_active = true;
    log_i("Config server running at http://%s", WiFi.localIP().toString().c_str());
}

// Call from loop() to service HTTP clients (AP or STA mode)
inline void wifi_portal_loop() {
    if (_portal_active || _sta_server_active)
        _portal_server.handleClient();
}

inline bool wifi_portal_active()     { return _portal_active; }
inline bool wifi_sta_server_active() { return _sta_server_active; }
