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
// Firmware update page (upload .bin)
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
  h2{color:#90caf9;font-size:.9em;letter-spacing:.05em;text-transform:uppercase;
     margin:18px 0 8px;padding-bottom:4px;border-bottom:1px solid #1e3a5f}
  label{display:block;color:#8899aa;font-size:.8em;margin-bottom:3px}
  input[type=file]{display:block;width:100%;padding:12px;margin-bottom:12px;
        background:#0f3460;color:#eaeaea;border:1px solid #2d3561;border-radius:6px;
        font-size:.9em;outline:none}
  input[type=file]::file-selector-button{background:#1db954;color:#fff;border:none;
        padding:6px 14px;border-radius:4px;cursor:pointer;margin-right:10px}
  button{display:block;width:100%;padding:13px;margin-top:6px;
         background:#1db954;color:#fff;border:none;border-radius:8px;
         font-size:1.05em;font-weight:bold;cursor:pointer;transition:background .2s}
  button:hover{background:#17a845}
  button:disabled{background:#555;cursor:not-allowed}
  .back-link{display:inline-block;margin-top:16px;color:#90caf9;font-size:.85em;text-decoration:none}
  .back-link:hover{text-decoration:underline}
  #progress-wrap{display:none;margin-top:14px}
  #progress-bar{border-radius:10px;height:20px;background:#0f3460;overflow:hidden}
  #progress-fill{height:100%;width:0;background:#1db954;border-radius:10px;transition:width .3s;text-align:center;font-size:.7em;line-height:20px;color:#fff}
  #up-status{display:none;margin-top:12px;color:#8899aa;font-size:.85em;text-align:center}
</style>
</head>
<body>
<div class="card">
  <h1>Firmware Update</h1>
  <p class="sub">Upload a firmware (<code>.bin</code>) file</p>
  <form id="up-form" method="post" action="/update" enctype="multipart/form-data">
    <label>Firmware Binary</label>
    <input id="up-file" type="file" name="firmware" accept=".bin" required>
    <button id="up-btn" type="submit">Upload &amp; Update</button>
  </form>
  <div id="progress-wrap">
    <div id="progress-bar"><div id="progress-fill">0%</div></div>
  </div>
  <div id="up-status"></div>
  <a class="back-link" href="/">&larr; Back to Configuration</a>
</div>
<script>
(function(){
  var form=document.getElementById('up-form'),
      inp=document.getElementById('up-file'),
      btn=document.getElementById('up-btn'),
      pw=document.getElementById('progress-wrap'),
      pf=document.getElementById('progress-fill'),
      st=document.getElementById('up-status');
  form.onsubmit=function(e){
    e.preventDefault();
    if(!inp.files.length)return;
    btn.disabled=true; btn.textContent='Uploading…';
    pw.style.display='block'; st.style.display='block';
    st.textContent='Uploading firmware …';
    var x=new XMLHttpRequest();
    x.upload.addEventListener('progress',function(e){
      if(e.lengthComputable){var p=Math.round(e.loaded/e.total*100);pf.style.width=p+'%';pf.textContent=p+'%'}
    });
    x.addEventListener('load',function(){st.innerHTML=x.responseText;pf.style.width='100%';pf.textContent='100%'});
    x.addEventListener('error',function(){st.textContent='Upload failed';btn.disabled=false;btn.textContent='Upload & Update'});
    var fd=new FormData(form);x.open('POST','/update',true);x.send(fd);
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

// ── Firmware update handlers ─────────────────────────────────

static bool _update_ok = false;

static void _portal_handle_update_page() {
    _portal_server.send(200, "text/html; charset=utf-8", FPSTR(_UPDATE_PAGE_HTML));
}

static void _portal_handle_update_upload() {
    HTTPUpload &upload = _portal_server.upload();
    if (upload.status == UPLOAD_FILE_START) {
        log_i("Update: receiving %s", upload.filename.c_str());
        _update_ok = false;
        ota_display_begin();
        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
            log_e("Update.begin failed");
        }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
            log_e("Update.write failed at offset %u", upload.totalSize);
        }
        // Estimate progress against max ~2 MB firmware
        int pct = (upload.totalSize * 100) / 0x200000;
        if (pct > 99) pct = 99;
        char msg[48];
        snprintf(msg, sizeof(msg), "Receiving… %d KB", upload.totalSize / 1024);
        ota_display_progress(pct, msg);
    } else if (upload.status == UPLOAD_FILE_END) {
        _update_ok = Update.end(true);
        if (_update_ok) {
            log_i("Update success (%u bytes)", upload.totalSize);
            ota_display_progress(100, "Update complete! Rebooting…");
        } else {
            log_e("Update failed: %s", Update.errorString());
            ota_display_end();
        }
    } else if (upload.status == UPLOAD_FILE_ABORTED) {
        Update.end(false);
        _update_ok = false;
        ota_display_end();
        log_w("Update aborted");
    }
}

static void _portal_register_routes() {
    _portal_server.on("/", HTTP_GET, []() {
        String page = _portal_build_page();
        _portal_server.send(200, "text/html; charset=utf-8", page);
    });
    _portal_server.on("/save", HTTP_POST, _portal_handle_save);
    _portal_server.on("/update", HTTP_GET, _portal_handle_update_page);
    _portal_server.on("/update", HTTP_POST,
        []() {
            if (_update_ok) {
                _portal_server.send_P(200, "text/html; charset=utf-8", _UPDATE_OK_HTML);
                _portal_server.client().flush();
                delay(1000);
                ESP.restart();
            } else {
                String msg = Update.hasError() ? String(Update.errorString()) : "Unknown error";
                String html = FPSTR(_UPDATE_FAIL_HTML);
                html.replace("%%MSG%%", msg);
                _portal_server.send(200, "text/html; charset=utf-8", html);
            }
        },
        _portal_handle_update_upload
    );
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
