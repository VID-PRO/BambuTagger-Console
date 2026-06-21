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
static const char _PORTAL_PAGE_TMPL[] PROGMEM = R"html(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>BambuTagger-Console</title>
<style>
  *{box-sizing:border-box;margin:0;padding:0}
  body{background:#1a1a2e;color:#eaeaea;font-family:Arial,sans-serif;
       min-height:100vh;display:flex;align-items:center;justify-content:center;padding:16px}
  .card{background:#16213e;border-radius:12px;padding:28px;width:100%;max-width:440px;
        box-shadow:0 4px 24px rgba(0,0,0,.5)}
  h1{color:#1db954;font-size:1.4em;margin-bottom:4px}
  .sub{color:#8899aa;font-size:.85em;margin-bottom:22px}
  h2{color:#90caf9;font-size:.9em;letter-spacing:.05em;text-transform:uppercase;
     margin:18px 0 8px;padding-bottom:4px;border-bottom:1px solid #1e3a5f}
  label{display:block;color:#8899aa;font-size:.8em;margin-bottom:3px}
  input{display:block;width:100%;padding:10px 12px;margin-bottom:12px;
        background:#0f3460;color:#eaeaea;border:1px solid #2d3561;border-radius:6px;
        font-size:.95em;outline:none;transition:border .2s}
  input:focus{border-color:#1db954}
  button{display:block;width:100%;padding:13px;margin-top:6px;
         background:#1db954;color:#fff;border:none;border-radius:8px;
         font-size:1.05em;font-weight:bold;cursor:pointer;transition:background .2s}
  button:hover{background:#17a845}
  .hint{color:#6c757d;font-size:.78em;margin-top:-9px;margin-bottom:12px}
</style>
<link rel="icon" href="data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAACAAAAAgAgMAAAAOFJJnAAABhWlDQ1BJQ0MgcHJvZmlsZQAAKJF9kb9Lw0AcxV9bS6VUHawg4pChOrWLijiWKhbBQmkrtOpgcukvaNKQpLg4Cq4FB38sVh1cnHV1cBUEwR8g/gHipOgiJX4vKbSI8eC4D+/uPe7eAd5WjSlGXxxQVFPPJBNCvrAqBF7hRxAjGERUZIaWyi7m4Dq+7uHh612MZ7mf+3MMyEWDAR6BOM403STeIJ7dNDXO+8RhVhFl4nPiqE4XJH7kuuTwG+eyzV6eGdZzmXniMLFQ7mGph1lFV4hniCOyolK+N++wzHmLs1JrsM49+QtDRXUly3Wa40hiCSmkIUBCA1XUYCJGq0qKgQztJ1z8Y7Y/TS6JXFUwciygDgWi7Qf/g9/dGqXpKScplAD8L5b1MQEEdoF207K+jy2rfQL4noErteuvt4C5T9KbXS1yBAxtAxfXXU3aAy53gNEnTdRFW/LR9JZKwPsZfVMBGL4FgmtOb519nD4AOepq+QY4OAQmy5S97vLu/t7e/j3T6e8HrYRyvp7c8c0AAAAJUExURXIA83m/boC9efRkY8YAAAABdFJOUwBA5thmAAAAAWJLR0QAiAUdSAAAAL1JREFUGNNNkLEKg0AMhv8GHO52H0FR36SbCJHD6XASn+Lazb1XHG8R1Kds7kqLgZAvGZL/D3CJbXCp1sxTAs/cx5HmvWErUJhvYgsA9QJv6AAvzUqeXe5Au5qPNrMyL4GXslCuAppbK4B6AhkUDr6PUDpYBQiUgZw2Bs02KJsn4KVjgWLh+8idQbawVTwaqGeERwttfZv1coKMXrMgR2lFVcX9f2GIm5PUwpBL4omPOdlJBpNT9bOM87y+5AM/WTesHvLO9wAAAABJRU5ErkJggg==" type="image/svg+xml" />
</head>
<body>
<div class="card">
  <h1><img style="vertical-align:middle;" src="data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAACAAAAAgCAMAAABEpIrGAAAA51BMVEVMaXF8wXEAAAB5vW5jrFt7wHB8wnF+1nd6vm96vm9oo15bkVN0tWlqpmB6v28gMx1Ld0MrVDBDazximllvrWRknVtVhU5bkFJ1t2tbkFNXjFB4u21nol13uWxxsWdqpmB4u21sqmNnol1xsGdxsWc2VzBwsGZglVdqpmBXik5tq2NUhEpppF9elVVurGRztGhvrWRjm1pjnFtztGlysmd4u21sqWI3XDNwsGZysmhppF92uWt0tGlknFpZjlFbkVN40nh9wnJ+xXOByHV7wHCByXaDy3eCyneDzXiAx3SJ1HyH0XqN3IFujF/lAAAAQXRSTlMA+gH5Afz9Av77gDjWe/kHHAQUZ7lXJ0HzPSH8bO3RivOeXqW3DchEkTKFLXRIseLDTV7KwOeWIZKup93rlnpTCMyoTDkAAAAJcEhZcwAALiMAAC4jAXilP3YAAALRSURBVHicZZPncuM4EISbAYGkGCRROeccncMGggAoSn7/59mSfL711fW/wXw1NYPqBj5lMwBRrfzzZ3nzBICZ+I8YQ6kc17nSWsn6dB1dX773EfrvqaLEIMQIZJrV3Qm+EQxeV4uAplpwLrWigTh1vb8EQ8VSAdXWflmZ1Q5+XM+olVrDL4KhQhMrtearr8Uit68sKsqfBEPDSixdWMD+Gmmjep8FNJhdCRulQmpl4wnsoudu7+/HvVoIhKMsyN8i2GBY6iC7nwCzPTmfPz7O52R6AIovmaWfryOaHZk8NGG6/Gx0B6476Brn5DFEqZDLfhWAnwWiDCxPadwoXjcIa3F+GQCVJMiWgHmXi0IIbHXPBMIoCgG4agqY97dOs0+UjyJKtSJK7t37e7cXoTgroYh2Lq0FDlLyBhhjNqqFy1mI83m3uNYM1RYVQ6zzpH7Fr+d8dNzVqv32cVu+aE52ycmFq0T3V6Gw7zXQnC9QORab8yYavX1hV47TbH4DjjpVmsdVYHC5xCaqMdcqvbTHKpvDT5P3RV86Dj11GsUHGhglr3OijsOtzVToHiqJbD29aEdyK32ovpwvd09vmSUlT++e6lS1sbJIOlxxyqV0TqPo9TVanhwpOUkPNUHkBmFBqDF8TR3Ok04ETN4E545xesRAyU4JeNak5cF3dMIJ3wCrFiGJlj1U+1KNAHgWSccmvG09yUUFqMk8qcczYKQcubn+z0gH+RrMjGplawQ8y3YtMhnaIsjGN0NVHxJCKmBgOIiXgRqC2QwVh9DgZlyGoSCUuCbYbxyFXuM3s9EmNEjXX6b0U8NQU8+0gWkBsE0vFjQ4Pf61tZ8LSzlTd/ar0zp6/p4oR6i5af9jc5Nh2NGGkWtJkyThWRoYul/+nk+G5pbrxDACxwkMIrSzrYL9G4MbgcZgx3OdZTpv7R69/+X7GuZJo/zj9fXHcRF+1jf9AXw/ZhEkocSxAAAAAElFTkSuQmCC" alt="bambutagger_resize" />&nbsp;BambuTagger-Console</h1>
  <p class="sub">WiFi &amp; printer configuration</p>
  <form method="post" action="/save">
    <h2>WiFi</h2>
    <label>SSID</label>
    <input name="ssid" placeholder="Your WiFi network name" required autocomplete="off" %%SSID%%>
    <label>Password</label>
    <input name="pass" type="password" placeholder="WiFi password" autocomplete="off" %%PASS%%>

    <h2>Bambu Printer 1</h2>
    <label>IP Address</label>
    <input name="ip" placeholder="e.g. 192.168.1.100" required %%IP%%>
    <p class="hint">Printer: Settings &#8594; Network</p>
    <label>Serial Number</label>
    <input name="serial" placeholder="e.g. 01S00C123456789" required %%SERIAL%%>
    <p class="hint">Printer: Settings &#8594; About</p>
    <label>Access Code</label>
    <input name="code" placeholder="8-digit code" required %%CODE%%>
    <p class="hint">Printer: Settings &#8594; Network &#8594; Access Code</p>

    <button type="submit">Save &amp; Reboot</button>
  </form>
  <div style="margin-top:16px;padding-top:14px;border-top:1px solid #1e3a5f;text-align:center">
    <a href="/update" style="color:#90caf9;font-size:.85em;text-decoration:none"
       onmouseover="this.style.textDecoration='underline'"
       onmouseout="this.style.textDecoration='none'">&#9654; Firmware Update</a>
  </div>
</div>
</body>
</html>
)html";

static const char _PORTAL_SAVED_HTML[] PROGMEM = R"html(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>BambuTagger-Console</title>
<style>
  body{background:#1a1a2e;color:#eaeaea;font-family:Arial,sans-serif;
       min-height:100vh;display:flex;align-items:center;justify-content:center;text-align:center}
  .card{background:#16213e;border-radius:12px;padding:40px 32px}
  h1{color:#1db954;font-size:2em;margin-bottom:10px}
  p{color:#8899aa}
</style>
<link rel="icon" href="data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAACAAAAAgAgMAAAAOFJJnAAABhWlDQ1BJQ0MgcHJvZmlsZQAAKJF9kb9Lw0AcxV9bS6VUHawg4pChOrWLijiWKhbBQmkrtOpgcukvaNKQpLg4Cq4FB38sVh1cnHV1cBUEwR8g/gHipOgiJX4vKbSI8eC4D+/uPe7eAd5WjSlGXxxQVFPPJBNCvrAqBF7hRxAjGERUZIaWyi7m4Dq+7uHh612MZ7mf+3MMyEWDAR6BOM403STeIJ7dNDXO+8RhVhFl4nPiqE4XJH7kuuTwG+eyzV6eGdZzmXniMLFQ7mGph1lFV4hniCOyolK+N++wzHmLs1JrsM49+QtDRXUly3Wa40hiCSmkIUBCA1XUYCJGq0qKgQztJ1z8Y7Y/TS6JXFUwciygDgWi7Qf/g9/dGqXpKScplAD8L5b1MQEEdoF207K+jy2rfQL4noErteuvt4C5T9KbXS1yBAxtAxfXXU3aAy53gNEnTdRFW/LR9JZKwPsZfVMBGL4FgmtOb519nD4AOepq+QY4OAQmy5S97vLu/t7e/j3T6e8HrYRyvp7c8c0AAAAJUExURXIA83m/boC9efRkY8YAAAABdFJOUwBA5thmAAAAAWJLR0QAiAUdSAAAAL1JREFUGNNNkLEKg0AMhv8GHO52H0FR36SbCJHD6XASn+Lazb1XHG8R1Kds7kqLgZAvGZL/D3CJbXCp1sxTAs/cx5HmvWErUJhvYgsA9QJv6AAvzUqeXe5Au5qPNrMyL4GXslCuAppbK4B6AhkUDr6PUDpYBQiUgZw2Bs02KJsn4KVjgWLh+8idQbawVTwaqGeERwttfZv1coKMXrMgR2lFVcX9f2GIm5PUwpBL4omPOdlJBpNT9bOM87y+5AM/WTesHvLO9wAAAABJRU5ErkJggg==" type="image/svg+xml" />
</head>
<body>
<div class="card">
  <h1>&#10003; Saved!</h1>
  <p>Settings stored. Device is rebooting&hellip;</p>
  <p style="margin-top:12px;font-size:.85em">You can reconnect after a few seconds.</p>
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
  body{background:#1a1a2e;color:#eaeaea;font-family:Arial,sans-serif;
       min-height:100vh;display:flex;align-items:center;justify-content:center;padding:16px}
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
  .back-link{display:inline-block;margin-top:16px;color:#90caf9;font-size:.85em;text-decoration:none}
  .back-link:hover{text-decoration:underline}
  #status{display:none;margin-top:14px;padding:12px;border-radius:6px;font-size:.9em;text-align:center}
  #status.info{display:block;background:#0f3460;color:#90caf9}
  #status.ok{display:block;background:#0f3460;color:#1db954}
  #status.err{display:block;background:#3d1a1a;color:#e74c3c}
</style>
</head>
<body>
<div class="card">
  <h1>Firmware Update</h1>
  <p class="sub">Install the latest release from GitHub</p>
  <div class="ver">Current: <span class="old" id="cur-ver">%%VER%%</span></div>
  <div class="ver">Latest:  <span class="new" id="lat-ver">&mdash;</span></div>
  <div id="status"></div>
  <button id="up-btn">Install Latest Version</button>
  <a class="back-link" href="/">&larr; Back to Configuration</a>
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
      }else{
        st.className='err'; st.textContent=x.responseText;
        btn.disabled=false; btn.textContent='Try Again';
      }
    };
    x.onerror=function(){
      st.className='ok';
      st.textContent='Update initiated \u2014 device is rebooting.';
      btn.textContent='Rebooting\u2026';
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
<title>BambuTagger-Console — Update</title>
<style>
  body{background:#1a1a2e;color:#eaeaea;font-family:Arial,sans-serif;
       min-height:100vh;display:flex;align-items:center;justify-content:center;text-align:center}
  .card{background:#16213e;border-radius:12px;padding:40px 32px}
  h1{color:#1db954;font-size:2em;margin-bottom:10px}
  p{color:#8899aa}
</style>
</head>
<body>
<div class="card">
  <h1>&#10003; Update Complete!</h1>
  <p>Firmware flashed successfully. Rebooting&hellip;</p>
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
  body{background:#1a1a2e;color:#eaeaea;font-family:Arial,sans-serif;
       min-height:100vh;display:flex;align-items:center;justify-content:center;text-align:center}
  .card{background:#16213e;border-radius:12px;padding:40px 32px}
  h1{color:#e74c3c;font-size:1.6em;margin-bottom:10px}
  p{color:#8899aa}
</style>
</head>
<body>
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

static String _portal_build_page() {
    // Read current NVS values for pre-filling in STA mode
    Preferences prefs;
    prefs.begin("bambu_mon", true);
    String ssid   = prefs.getString("wifi_ssid",  "");
    String pass   = prefs.getString("wifi_pass",  "");
    String ip     = prefs.getString("bam_ip",     "");
    String serial = prefs.getString("bam_serial", "");
    String code   = prefs.getString("bam_code",   "");
    prefs.end();

    String page = FPSTR(_PORTAL_PAGE_TMPL);
    page.replace("%%SSID%%",   ssid.length()   ? "value=\"" + ssid   + "\"" : "");
    page.replace("%%PASS%%",   pass.length()   ? "value=\"" + pass   + "\"" : "");
    page.replace("%%IP%%",     ip.length()     ? "value=\"" + ip     + "\"" : "");
    page.replace("%%SERIAL%%", serial.length() ? "value=\"" + serial + "\"" : "");
    page.replace("%%CODE%%",   code.length()   ? "value=\"" + code   + "\"" : "");
    return page;
}

static void _portal_handle_save() {
    String ssid   = _portal_server.arg("ssid");
    String pass   = _portal_server.arg("pass");
    String ip     = _portal_server.arg("ip");
    String serial = _portal_server.arg("serial");
    String code   = _portal_server.arg("code");

    if (ssid.length() == 0 || ip.length() == 0) {
        _portal_server.send(400, "text/plain", "SSID and IP are required.");
        return;
    }

    Preferences prefs;
    prefs.begin("bambu_mon", false);
    prefs.putString("wifi_ssid",  ssid);
    prefs.putString("wifi_pass",  pass);
    prefs.putString("bam_ip",     ip);
    prefs.putString("bam_serial", serial);
    prefs.putString("bam_code",   code);
    prefs.end();

    log_i("Portal: credentials saved — rebooting");

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
    _portal_server.on("/", HTTP_GET, []() {
        String page = _portal_build_page();
        _portal_server.send(200, "text/html; charset=utf-8", page);
    });
    _portal_server.on("/save", HTTP_POST, _portal_handle_save);
    _portal_server.on("/update",      HTTP_GET,  _portal_handle_update_page);
    _portal_server.on("/update",      HTTP_POST, _portal_handle_update_github);
    _portal_server.on("/api/release", HTTP_GET,  _portal_handle_api_release);
    // Captive-portal catch-all (Android / iOS auto-redirect)
    _portal_server.onNotFound([]() {
        _portal_server.sendHeader("Location", "http://192.168.4.1/", true);
        _portal_server.send(302, "text/html",
            "<html><body><a href=\"http://192.168.4.1/\">Configure</a></body></html>");
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
