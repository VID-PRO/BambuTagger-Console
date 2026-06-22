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
body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;
background:#0d1117;color:#c9d1d9;min-height:100vh;padding-bottom:28px}
header{background:#161b22;border-bottom:1px solid #30363d;padding:12px 24px}
header .logo{display:flex;align-items:center;gap:10px}
header .logo img{width:28px;height:28px;flex-shrink:0;border-radius:4px}
header h1{font-size:18px;color:#58a6ff}
nav{background:#161b22;border-bottom:1px solid #30363d;display:flex;gap:0}
nav a{padding:10px 20px;color:#8b949e;text-decoration:none;font-size:14px;
border-bottom:2px solid transparent;cursor:pointer}
nav a:hover{color:#c9d1d9}
nav a.active{color:#58a6ff;border-bottom-color:#58a6ff}
.container{max-width:960px;margin:0 auto;padding:20px}
#printers{display:grid;grid-template-columns:repeat(auto-fill,minmax(380px,1fr));gap:12px}
.card{background:#161b22;border:1px solid #30363d;border-radius:6px;padding:16px}
.card-header{display:flex;justify-content:space-between;align-items:center;margin-bottom:12px}
.card-header h2{font-size:16px;color:#f0f6fc}
.badge{font-size:11px;padding:2px 10px;border-radius:10px;font-weight:600;text-transform:uppercase}
.badge.idle{background:rgba(139,148,158,0.15);color:#8b949e}
.badge.running{background:rgba(63,185,80,0.15);color:#3fb950}
.badge.pause{background:rgba(210,153,34,0.15);color:#d29922}
.badge.finish{background:rgba(88,166,255,0.15);color:#58a6ff}
.badge.failed{background:rgba(218,54,51,0.15);color:#da3633}
.badge.prepare{background:rgba(163,113,247,0.15);color:#a371f7}
.badge.unknown{background:rgba(139,148,158,0.15);color:#8b949e}
.card-body{display:flex;gap:16px}
.thumb{width:100px;height:100px;border-radius:6px;overflow:hidden;background:#1c2128;flex-shrink:0;border:1px solid #30363d}
.thumb img{width:100%;height:100%;object-fit:cover}
.thumb .placeholder{width:100%;height:100%;display:flex;align-items:center;justify-content:center;color:#484f58;font-size:11px;text-align:center;padding:4px}
.info{flex:1;min-width:0}
.info-row{display:flex;justify-content:space-between;padding:3px 0;font-size:13px}
.info-row .lbl{color:#8b949e}
.info-row .val{color:#c9d1d9;font-weight:600}
.progress-bar{height:6px;background:#1c2128;border-radius:3px;overflow:hidden;margin:8px 0}
.progress-bar .fill{height:100%;background:#238636;border-radius:3px;transition:width .5s}
.remain-row{font-size:20px;font-weight:700;color:#f0f6fc;padding:2px 0;display:flex;align-items:center;gap:6px}
.remain-row .lbl{font-size:13px;font-weight:400;color:#8b949e}
.layer{font-size:12px;color:#484f58;margin-top:2px}
.no-printers{text-align:center;padding:48px;color:#484f58;font-size:16px}
footer{position:fixed;bottom:0;left:0;right:0;text-align:center;padding:6px;
font-size:10px;color:#484f58;background:#0d1117;border-top:1px solid #30363d}
footer a{color:#484f58;text-decoration:none}
footer a:hover{color:#c9d1d9}
</style>
</head>
<body>
<header><div class="logo"><img src="/Logo/bambutagger.png" alt=""><h1>BambuTagger Console</h1></div></header>
<nav>
<a href="/" class="active">Dashboard</a>
<a href="/config/wifi">Settings</a>
<a href="/update">Firmware</a>
</nav>
<div class="container">
<div id="printers"><div class="no-printers">Loading printer data...</div></div>
</div>
<footer>&copy; 2026 by <a href="https://www.bambutagger.de" target="_blank">BambuTagger</a></footer>
<script>
var _thumbGen=[];
function esc(s){const d=document.createElement('div');d.appendChild(document.createTextNode(s));return d.innerHTML}
function cardHtml(idx){
 var c=document.createElement('div');c.className='card';c.dataset.idx=idx;
 c.innerHTML='<div class="card-header"><h2></h2><span class="badge unknown"></span></div>'
  +'<div class="card-body">'
  +'<div class="thumb"><div class="placeholder">NO<br>THUMB</div></div>'
  +'<div class="info">'
  +'<div class="info-row"><span class="lbl">Nozzle</span><span class="val" data-f="nozzle"></span></div>'
  +'<div class="info-row"><span class="lbl">Bed</span><span class="val" data-f="bed"></span></div>'
  +'<div class="info-row"><span class="lbl">Chamber</span><span class="val" data-f="chamber"></span></div>'
  +'<div class="info-row"><span class="lbl">Speed</span><span class="val" data-f="speed"></span></div>'
  +'<div class="info-row"><span class="lbl">Signal</span><span class="val" data-f="signal"></span></div>'
  +'<div class="info-row"><span class="lbl">SD</span><span class="val" data-f="sd"></span></div>'
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
 info.querySelector('[data-f="nozzle"]').textContent=s.temp_nozzle.toFixed(0)+'\u00b0'+(s.temp_nozzle_t?' / '+s.temp_nozzle_t.toFixed(0)+'\u00b0':'');
 info.querySelector('[data-f="bed"]').textContent=s.temp_bed.toFixed(0)+'\u00b0'+(s.temp_bed_t?' / '+s.temp_bed_t.toFixed(0)+'\u00b0':'');
 info.querySelector('[data-f="chamber"]').textContent=s.temp_chamber.toFixed(0)+'\u00b0';
 info.querySelector('[data-f="speed"]').textContent=s.speed_pct+'%';
 info.querySelector('[data-f="signal"]').textContent=s.wifi_signal?s.wifi_signal+'dBm':'--';
 info.querySelector('[data-f="sd"]').textContent=s.sd_present?'Yes':'No';
 info.querySelector('.fill').style.width=(s.progress||0)+'%';
 info.querySelector('.job-name').textContent=s.job_name?esc(s.job_name):'';
 info.querySelector('.time').textContent=(s.remaining_min||0)+'m';
 info.querySelector('.layer').textContent=s.layer_total>0?'Layer '+s.layer_cur+'/'+s.layer_total:'';
 var thumb=c.querySelector('.thumb');
 var gen=s.thumb_gen||0;
 var prevGen=_thumbGen[s.idx]||-1;
 if(s.has_thumb && gen!==prevGen){
  _thumbGen[s.idx]=gen;
  var img=thumb.querySelector('img');
  if(img){img.src='/api/thumb?idx='+s.idx+'&t='+gen;}
  else{thumb.innerHTML='<img src="/api/thumb?idx='+s.idx+'&t='+gen+'" alt="thumb">';}
 }else if(!s.has_thumb && !thumb.querySelector('img')){
  _thumbGen[s.idx]=-1;
  thumb.innerHTML='<div class="placeholder">NO<br>THUMB</div>';}
}
function ensureCards(p){
 var c=document.getElementById('printers');
 if(!p.length){c.innerHTML='<div class="no-printers">No printers configured</div>';return}
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
