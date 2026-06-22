#pragma once

#include <WebServer.h>
#include <ArduinoJson.h>
#include "logo_png.h"

#define WEB_DASHBOARD_MAX_PRINTERS 4
#define WEB_THUMB_MAX_SIZE (32 * 1024)

struct WebPrinterStatus {
    char name[32]      = {};
    char state[16]     = {};
    char job_name[64]  = {};
    uint8_t progress   = 0;
    int  remaining_min = 0;
    float temp_nozzle   = 0.f;
    float temp_nozzle_t = 0.f;
    float temp_bed      = 0.f;
    float temp_bed_t    = 0.f;
    float temp_chamber  = 0.f;
    int  layer_cur     = 0;
    int  layer_total   = 0;
    uint8_t speed_pct  = 100;
    int  wifi_signal   = 0;
    bool sd_present    = false;
    bool has_thumb     = false;
    uint8_t thumb_gen  = 0;
};

extern WebPrinterStatus g_web_status[WEB_DASHBOARD_MAX_PRINTERS];
extern int              g_web_printer_count;
extern uint8_t         *g_web_thumb_buf[WEB_DASHBOARD_MAX_PRINTERS];
extern size_t           g_web_thumb_sz[WEB_DASHBOARD_MAX_PRINTERS];

// ── HTML dashboard page (PROGMEM) ──────────────────────────────────────
// Incremental DOM update: JavaScript creates/reuses card elements and
// updates values in-place instead of replacing innerHTML every cycle.
static const char DASHBOARD_HTML[] PROGMEM = R"raw(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>BambuTagger Console</title>
<link rel="icon" href="/Logo/bambutagger.png">
<style>
*{margin:0;padding:0;box-sizing:border-box}
body{background:#1a1a2e;color:#eaeaea;font-family:Arial,sans-serif;min-height:100vh}
nav{background:#16213e;padding:8px 16px;display:flex;align-items:center;gap:24px;flex-wrap:wrap}
.nav-brand{display:flex;align-items:center;gap:8px;font-size:18px;font-weight:700;color:#90caf9}
.nav-brand img{width:32px;height:32px;border-radius:4px}
.nav-links{display:flex;gap:4px}
.nav-links a{padding:6px 14px;border-radius:6px;color:#8899aa;text-decoration:none;font-size:14px}
.nav-links a:hover{background:#0f3460;color:#eaeaea}
.nav-links a.active{background:#1db95433;color:#1db954;font-weight:600}
.container{padding:16px}
#printers{display:grid;grid-template-columns:repeat(auto-fill,minmax(360px,1fr));gap:12px}
.card{background:#16213e;border-radius:12px;padding:16px}
.card-header{display:flex;justify-content:space-between;align-items:center;margin-bottom:12px}
.card-header h2{font-size:17px;color:#eaeaea}
.badge{font-size:11px;padding:3px 10px;border-radius:4px;font-weight:700;text-transform:uppercase}
.badge.idle{background:#37474f;color:#90a4ae}
.badge.running{background:#1b5e20;color:#a5d6a7}
.badge.pause{background:#e65100;color:#ffcc80}
.badge.finish{background:#1565c0;color:#90caf9}
.badge.failed{background:#b71c1c;color:#ef9a9a}
.badge.prepare{background:#4a148c;color:#ce93d8}
.badge.unknown{background:#37474f;color:#90a4ae}
.card-body{display:flex;gap:16px}
.thumb{width:100px;height:100px;border-radius:8px;overflow:hidden;background:#0f3460;flex-shrink:0}
.thumb img{width:100%;height:100%;object-fit:cover}
.thumb .placeholder{width:100%;height:100%;display:flex;align-items:center;justify-content:center;color:#556677;font-size:11px;text-align:center;padding:4px}
.info{flex:1;min-width:0}
.info-row{display:flex;justify-content:space-between;padding:2px 0;font-size:13px;border-bottom:1px solid #1a1a2e}
.info-row:last-child{border-bottom:none}
.info-row .lbl{color:#8899aa}
.info-row .val{color:#eaeaea;font-weight:600}
.progress-bar{height:8px;background:#0f3460;border-radius:4px;overflow:hidden;margin:6px 0}
.progress-bar .fill{height:100%;background:linear-gradient(90deg,#1db954,#2ecc71);border-radius:4px;transition:width .5s}
.remain-row{font-size:20px;font-weight:700;color:#eaeaea;padding:2px 0;display:flex;align-items:center;gap:6px}
.remain-row .lbl{font-size:13px;font-weight:400;color:#8899aa}
.layer{font-size:12px;color:#556677;margin-top:2px}
.no-printers{text-align:center;padding:48px;color:#556677;font-size:16px}
.footer{margin-top:24px;text-align:center;color:#556677;font-size:11px}
</style>
</head>
<body>
<nav>
 <div class="nav-brand"><img src="/Logo/bambutagger.png" alt=""><span>BambuTagger Console</span></div>
  <div class="nav-links"><a href="/" class="active">Dashboard</a><a href="/config/wifi">Settings</a><a href="/update">Firmware</a></div>
</nav>
<div class="container">
<div id="printers"><div class="no-printers">⏳ Waiting for printer data...</div></div>
<div class="footer"><span id="ts"></span></div>
</div>
<script>
// Track last thumb_gen per printer so we re-fetch when thumbnail changes
var _thumbGen=[];
function esc(s){const d=document.createElement('div');d.appendChild(document.createTextNode(s));return d.innerHTML}
function cardHtml(idx){
 var c=document.createElement('div');c.className='card';c.dataset.idx=idx;
 c.innerHTML='<div class="card-header"><h2></h2><span class="badge unknown"></span></div>'
  +'<div class="card-body">'
  +'<div class="thumb"><div class="placeholder">NO<br>THUMB</div></div>'
  +'<div class="info">'
  +'<div class="info-row" data-f="nozzle"><span class="lbl">Nozzle</span><span class="val"></span></div>'
  +'<div class="info-row" data-f="bed"><span class="lbl">Bed</span><span class="val"></span></div>'
  +'<div class="info-row" data-f="chamber"><span class="lbl">Chamber</span><span class="val"></span></div>'
  +'<div class="info-row" data-f="speed"><span class="lbl">Speed</span><span class="val"></span></div>'
  +'<div class="info-row" data-f="signal"><span class="lbl">Signal</span><span class="val"></span></div>'
  +'<div class="info-row" data-f="sd"><span class="lbl">SD</span><span class="val"></span></div>'
  +'<div class="progress-bar"><div class="fill" style="width:0%"></div></div>'
  +'<div class="remain-row"><span class="lbl job-name" style="flex:1;min-width:0"></span><span class="time" style="margin-left:auto"></span></div>'
  +'<div class="layer"></div>'
  +'</div></div>';
 return c;
}
function updateCard(s){
 var c=document.querySelector('.card[data-idx="'+s.idx+'"]');
 if(!c)return;
 var st=(s.state||'unknown').toLowerCase();
 c.querySelector('.card-header h2').textContent=s.name||'Printer '+(s.idx+1);
 var b=c.querySelector('.badge');b.className='badge '+st;b.textContent=s.state||'?';
 var info=c.querySelector('.info');
 info.querySelector('[data-f="nozzle"] .val').textContent=s.temp_nozzle.toFixed(0)+'\u00b0'+(s.temp_nozzle_t?' / '+s.temp_nozzle_t.toFixed(0)+'\u00b0':'');
 info.querySelector('[data-f="bed"] .val').textContent=s.temp_bed.toFixed(0)+'\u00b0'+(s.temp_bed_t?' / '+s.temp_bed_t.toFixed(0)+'\u00b0':'');
 info.querySelector('[data-f="chamber"] .val').textContent=s.temp_chamber.toFixed(0)+'\u00b0';
 info.querySelector('[data-f="speed"] .val').textContent=s.speed_pct+'%';
 info.querySelector('[data-f="signal"] .val').textContent=s.wifi_signal?s.wifi_signal+'dBm':'--';
 info.querySelector('[data-f="sd"] .val').textContent=s.sd_present?'Yes':'No';
 info.querySelector('.fill').style.width=(s.progress||0)+'%';
 info.querySelector('.job-name').textContent=s.job_name?esc(s.job_name):'';
 info.querySelector('.time').textContent=(s.remaining_min||0)+'m';
 info.querySelector('.layer').textContent=s.layer_total>0?'Layer '+s.layer_cur+'/'+s.layer_total:'';
  // Thumbnail — only update src when has_thumb or thumb_gen changes.
  // The MQTT callback resets g_web_status without a mutex, so a race
  // between the HTTP handler and the callback can transiently report
  // has_thumb=false.  To avoid flickering the placeholder, we never
  // remove an existing <img> — once shown it stays until a new gen
  // provides a replacement.
  var thumb=c.querySelector('.thumb');
  var gen=s.thumb_gen||0;
  var prevGen=_thumbGen[s.idx]||-1;
  if(s.has_thumb && gen!==prevGen){
   _thumbGen[s.idx]=gen;
   thumb.innerHTML='<img src="/api/thumb?idx='+s.idx+'&t='+gen+'" alt="thumb">';
  }else if(!s.has_thumb && !thumb.querySelector('img')){
   _thumbGen[s.idx]=-1;
   thumb.innerHTML='<div class="placeholder">NO<br>THUMB</div>';
  }
}
function ensureCards(p){
 var c=document.getElementById('printers');
 if(!p.length){c.innerHTML='<div class="no-printers">No printers configured</div>';return}
 // Only rebuild if count changed — preserves existing <img> elements
 // (thumbnails) across the 2-second poll cycle.
 var cur=c.querySelectorAll('.card').length;
 if(cur==p.length) return;
 c.innerHTML='';
 for(var i=0;i<p.length;i++) c.appendChild(cardHtml(i));
}
async function refresh(){
 try{
  var r=await fetch('/api/status');
  if(!r.ok)return;
  var d=await r.json();
  var p=d.printers||[];
  document.getElementById('ts').textContent=new Date().toLocaleTimeString();
  ensureCards(p);
  for(var i=0;i<p.length;i++) updateCard(p[i]);
 }catch(e){}
}
setInterval(refresh,2000);refresh();
</script>
</body>
</html>
)raw";

// ── Handlers ───────────────────────────────────────────────────────────
static void _handle_api_status(WebServer &server) {
    JsonDocument doc;
    JsonArray printers = doc["printers"].to<JsonArray>();
    for (int i = 0; i < g_web_printer_count; i++) {
        JsonObject p = printers.add<JsonObject>();
        p["idx"]           = i;
        p["name"]          = g_web_status[i].name;
        p["state"]         = g_web_status[i].state;
        p["job_name"]      = g_web_status[i].job_name;
        p["progress"]      = g_web_status[i].progress;
        p["remaining_min"] = g_web_status[i].remaining_min;
        p["temp_nozzle"]   = g_web_status[i].temp_nozzle;
        p["temp_nozzle_t"] = g_web_status[i].temp_nozzle_t;
        p["temp_bed"]      = g_web_status[i].temp_bed;
        p["temp_bed_t"]    = g_web_status[i].temp_bed_t;
        p["temp_chamber"]  = g_web_status[i].temp_chamber;
        p["layer_cur"]     = g_web_status[i].layer_cur;
        p["layer_total"]   = g_web_status[i].layer_total;
        p["speed_pct"]     = g_web_status[i].speed_pct;
        p["wifi_signal"]   = g_web_status[i].wifi_signal;
        p["sd_present"]    = g_web_status[i].sd_present;
        p["has_thumb"]     = g_web_status[i].has_thumb;
        p["thumb_gen"]     = g_web_status[i].thumb_gen;
    }
    String json;
    serializeJson(doc, json);
    server.setContentLength(json.length());
    server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    server.send(200, "application/json", json);
}

static void _handle_api_thumb(WebServer &server) {
    int idx = server.arg("idx").toInt();
    if (idx < 0 || idx >= g_web_printer_count || !g_web_thumb_buf[idx] || g_web_thumb_sz[idx] == 0) {
        server.send(404, "text/plain", "");
        return;
    }
    server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    server.send_P(200, "image/png", (const char *)g_web_thumb_buf[idx], g_web_thumb_sz[idx]);
}

static void _handle_logo(WebServer &server) {
    server.send_P(200, "image/png", (const char *)LOGO_PNG, LOGO_PNG_SZ);
}

static void _handle_dashboard(WebServer &server) {
    server.send_P(200, "text/html", DASHBOARD_HTML, sizeof(DASHBOARD_HTML) - 1);
}

inline void register_dashboard_routes(WebServer &server) {
    server.on("/",                 HTTP_GET, [&server]() { _handle_dashboard(server); });
    server.on("/api/status",       HTTP_GET, [&server]() { _handle_api_status(server); });
    server.on("/api/thumb",        HTTP_GET, [&server]() { _handle_api_thumb(server); });
    server.on("/Logo/bambutagger.png", HTTP_GET, [&server]() { _handle_logo(server); });
}
