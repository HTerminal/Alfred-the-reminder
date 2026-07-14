// Web config: a small HTTP server that shows/edits the schedule and uploads
// sounds. State is persisted to /config.json (see schedule.cpp).
#include "webconfig.h"
#include "config.h"
#include "schedule.h"
#include "imu.h"
#include "ringlog.h"
#include <Arduino.h>
#include <time.h>
#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

static WebServer server(80);
static File      uploadFile;
static volatile bool scheduleDirty = false;
static volatile bool testPending   = false;
static volatile bool sleepPending  = false;
static volatile bool rebootPending = false;
static volatile bool wifiResetPending = false;

// ------------------------------ the page ------------------------------
static const char PAGE[] PROGMEM = R"HTML(<!doctype html><html><head>
<meta name=viewport content="width=device-width,initial-scale=1"><title>Reminder Config</title>
<style>
:root{--bg:#0b0c0f;--card:#16181d;--line:#2a2d34;--fg:#eaecef;--dim:#98a0ac;--acc:#3ea36a;--acc2:#2b8a5b;--dng:#c0483f}
*{box-sizing:border-box}
body{font-family:system-ui,-apple-system,'Segoe UI',Roboto,sans-serif;background:var(--bg);color:var(--fg);margin:0;line-height:1.45;-webkit-text-size-adjust:100%}
.wrap{max-width:760px;margin:0 auto;padding:18px 16px 40px}
h1{font-size:22px;margin:4px 0 2px}
.sub{color:var(--dim);font-size:13px;margin:0 0 16px}
.card{background:var(--card);border:1px solid var(--line);border-radius:14px;padding:16px;margin:14px 0}
.card h2{font-size:16px;margin:0 0 3px}
.hint{color:var(--dim);font-size:12.5px;margin:0 0 12px}
.scroll{overflow-x:auto;-webkit-overflow-scrolling:touch}
table{width:100%;border-collapse:collapse;min-width:440px}
th{color:var(--dim);font-weight:600;font-size:11px;letter-spacing:.05em;text-transform:uppercase;text-align:left;padding:4px 6px}
td{padding:6px;border-top:1px solid var(--line);vertical-align:middle}
input,select{background:#0f1116;color:var(--fg);border:1px solid #363b44;border-radius:9px;padding:9px 10px;font-size:16px;width:100%;min-height:42px}
input[type=time]{min-width:120px}
input[type=number]{max-width:130px}
.btn{background:var(--acc2);color:#fff;border:0;border-radius:10px;padding:11px 16px;font-size:15px;font-weight:600;min-height:44px;cursor:pointer}
.btn.ghost{background:#23262e;color:var(--fg)}
.x{background:var(--dng);color:#fff;border:0;border-radius:8px;width:36px;height:36px;font-size:15px;cursor:pointer}
.row{display:flex;gap:10px;flex-wrap:wrap;align-items:center;margin-top:12px}
.fld{display:block;font-size:12.5px;color:var(--dim);margin:12px 0 5px}
.fitem{display:flex;align-items:center;gap:10px;padding:8px 0;border-top:1px solid var(--line);font-size:14px}
.fitem span{flex:1;word-break:break-all}
.msg{color:var(--acc);font-size:14px}
.savebar{position:sticky;bottom:0;background:linear-gradient(transparent,var(--bg) 30%);padding:14px 0 4px;margin-top:8px}
@media(max-width:520px){.wrap{padding:14px 12px 40px}.card{padding:13px}}
</style></head><body><div class=wrap>
<h1>&#9200; Reminder Config</h1>
<div class=sub>Reminders, sleep schedule, sounds &amp; icons &mdash; all saved on the device.</div>

<div class=card>
 <h2>Reminders</h2>
 <p class=hint>Each fires daily &mdash; plays a chime, then the chosen sound.</p>
 <div class=scroll><table id=tbl><thead><tr><th>Time</th><th>Name</th><th>Sound</th><th>Icon</th><th></th></tr></thead><tbody></tbody></table></div>
 <div class=row><button class="btn ghost" onclick=addRow()>+ Add reminder</button></div>
</div>

<div class=card>
 <h2>Sleep schedule</h2>
 <p class=hint>Windows set the display/power mode by time of day; uncovered hours stay <b>On</b>.
  <b>On</b> = screen on &middot; <b>Display off</b> = screen dark, still catches rings/alerts/taps &middot;
  <b>Deep sleep</b> = lowest power, wakes only for a timed reminder (no ring, no tap).</p>
 <div class=scroll><table id=ptbl><thead><tr><th>From</th><th>To</th><th>Mode</th><th></th></tr></thead><tbody></tbody></table></div>
 <div class=row><button class="btn ghost" onclick=addPwr()>+ Add window</button></div>
</div>

<div class=card>
 <h2>Behavior</h2>
 <label class=fld style="display:flex;align-items:center;gap:10px;color:var(--fg);font-size:15px">
  <input type=checkbox id=wake style="width:auto;min-height:0;transform:scale(1.4)"> Wake-up mode</label>
 <p class=hint style=margin:2px 0 8px>Silent &mdash; the screen shows <b>"Awake?"</b> until you <b>tap</b> it. On tap, the 1st reminder fires and the whole day's reminders shift so they line up from your wake time (keeping the gaps you set). Turn off to go back to your fixed times.</p>
 <label class=fld>Ring duration (doorbell) &mdash; seconds</label>
 <input type=number id=ring min=2 max=180 step=1>
 <label class=fld>Low battery &mdash; force Display-off at/below this % <small>(0 = off)</small></label>
 <input type=number id=boff min=0 max=100 step=1>
 <label class=fld>Low battery &mdash; force Deep-sleep at/below this % <small>(0 = off; lower than the above)</small></label>
 <input type=number id=bdeep min=0 max=100 step=1>
 <p class=hint style=margin-top:6px>On battery the board can't truly deep-sleep (it would power off), so "deep-sleep" here is a low-power light-sleep that keeps the clock and reminders alive.</p>
 <label class=fld>WiFi</label>
 <select id=wifi>
  <option value=0>Always on &mdash; web always reachable</option>
  <option value=1>Sync time at boot, then off &mdash; saves power, still rings; web only during the first ~60s after boot</option>
 </select>
 <p class=hint style=margin-top:6px>In "sync then off", WiFi connects once, sets the clock, then leaves the network. Reminders run from the RTC and ESP-NOW rings still work. To reach this page again, reboot the device (it's reachable for ~60s at boot) or after each Save.</p>
 <p class=hint style=margin-top:6px>Wi-Fi is set up from your phone &mdash; there's no password stored in the firmware. To move the device to a different network, use <b>Reset Wi-Fi</b>: it forgets the current network and reopens the <b>&ldquo;Alfred-Setup&rdquo;</b> hotspot so you can pick a new one.</p>
 <label class=fld>Tap to dismiss &mdash; drag left for a lighter tap</label>
 <input type=range id=tap min=0.2 max=1.5 step=0.01 oninput="tapv.textContent=(+this.value).toFixed(2)+' g'">
 <p class=hint style=margin-top:8px>Selected: <b id=tapv></b> &nbsp;&middot;&nbsp; Live tap force: <b id=jerk style=color:var(--acc)>0.00</b> g
  &mdash; tap the device and set the slider just below your peak.</p>
 <div class=row><button class="btn ghost" onclick=testAlert()>&#9654; Test alert on device</button>
  <button class="btn ghost" onclick=sleepNow()>&#128164; Deep sleep now (test)</button>
  <button class="btn ghost" onclick=reboot()>&#8635; Reboot</button>
  <button class="btn ghost" onclick=wifiReset()>&#128246; Reset Wi-Fi</button><span class=msg id=tmsg></span></div>
</div>

<div class=card>
 <h2>Sounds</h2>
 <p class=hint>16&nbsp;kHz mono WAV plays best.</p>
 <div class=row><input type=file id=f accept=".wav,.mp3" style=flex:1;min-width:180px><button class=btn onclick=upload()>Upload</button></div>
 <div id=sounds></div>
</div>

<div class=card>
 <h2>Icon images</h2>
 <p class=hint>PNG/JPG &mdash; resized in your browser, then pick it in a reminder's Icon column.</p>
 <div class=row><input type=file id=img accept="image/*" style=flex:1;min-width:180px><button class=btn onclick=uploadImg()>Upload</button><span class=msg id=imsg></span></div>
 <div id=images></div>
</div>

<div class=card>
 <h2>Ring log</h2>
 <p class=hint>Last 100 doorbell rings, newest first (device local time).
  <a href="#" onclick="loadRings();return false" style=color:var(--acc)>Refresh</a></p>
 <div id=ringlog></div>
</div>

<div class=savebar><div class=row><button class=btn style=flex:1 onclick=save()>&#128190; Save all</button><span class=msg id=msg></span></div></div>
</div>
<script>
let SOUNDS=[],ICONS=[],IMAGES=[];const MODES=['On','Display off','Deep sleep'];
function o(list,sel){return list.map(x=>`<option ${x==sel?'selected':''}>${x}</option>`).join('')}
function pad(n){return('0'+n).slice(-2)}
function m2t(m){return pad(Math.floor(m/60))+':'+pad(m%60)}
function t2m(t){let p=(t||'0:0').split(':');return (+p[0])*60+(+p[1])}
function addRow(r){r=r&&r.h!=undefined?r:{h:8,m:0,name:'New reminder',sound:'',icon:''};
 let tr=document.createElement('tr');
 tr.innerHTML=`<td><input type=time value=${pad(r.h)}:${pad(r.m)}></td>
  <td><input value="${(r.name||'').replace(/"/g,'&quot;')}"></td>
  <td><select><option value="">(chime only)</option>${o(SOUNDS,(r.sound||'').replace('/sounds/',''))}</select></td>
  <td><select><option value="" ${!r.icon?'selected':''}>(no image, big text)</option>${o(ICONS,r.icon)}</select></td>
  <td><button class=x onclick="this.closest('tr').remove()">&#10005;</button></td>`;
 document.querySelector('#tbl tbody').appendChild(tr)}
function addPwr(w){w=w&&w.s!=undefined?w:{s:1320,e:480,m:1};
 let tr=document.createElement('tr');
 tr.innerHTML=`<td><input type=time value=${m2t(w.s)}></td>
  <td><input type=time value=${m2t(w.e)}></td>
  <td><select>${MODES.map((x,i)=>`<option value=${i} ${i==w.m?'selected':''}>${x}</option>`).join('')}</select></td>
  <td><button class=x onclick="this.closest('tr').remove()">&#10005;</button></td>`;
 document.querySelector('#ptbl tbody').appendChild(tr)}
function fileRow(n,t){return `<div class=fitem><span>${n}</span><button class=x onclick="delFile('${t}','${n}')">&#10005;</button></div>`}
async function delFile(t,n){if(!confirm('Delete '+n+' from the device?'))return;await fetch('/api/delete',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({type:t,file:n})});load()}
async function load(){let s=await(await fetch('/api/state')).json();SOUNDS=s.sounds;ICONS=s.icons;IMAGES=s.images||[];
 let tb=document.querySelector('#tbl tbody');tb.innerHTML='';(s.reminders||[]).forEach(addRow);
 let pb=document.querySelector('#ptbl tbody');pb.innerHTML='';(s.power||[]).forEach(addPwr);
 tap.value=s.tapG||0.45;tapv.textContent=(+tap.value).toFixed(2)+' g';ring.value=s.ringSecs||10;
 boff.value=s.battOff!=undefined?s.battOff:10;bdeep.value=s.battDeep!=undefined?s.battDeep:0;
 wifi.value=s.wifiMode!=undefined?s.wifiMode:0;wake.checked=!!s.wakeMode;
 sounds.innerHTML=(SOUNDS.length?'':'<p class=hint>No sounds uploaded yet.</p>')+SOUNDS.map(x=>fileRow(x,'sound')).join('');
 images.innerHTML=(IMAGES.length?'':'<p class=hint>No images uploaded yet.</p>')+IMAGES.map(x=>fileRow(x,'image')).join('');
 loadRings()}
async function testAlert(){await fetch('/api/test',{method:'POST'});tmsg.textContent='✓ triggered on device';setTimeout(()=>tmsg.textContent='',2500)}
async function sleepNow(){if(!confirm('Deep sleep NOW for a test? It wakes itself in ~20s ON USB. On BATTERY the board POWERS OFF — press the PWR button to turn it back on.'))return;await fetch('/api/sleep',{method:'POST'});tmsg.textContent='💤 sleeping ~20s...'}
async function reboot(){if(!confirm('Reboot the device now?'))return;await fetch('/api/reboot',{method:'POST'});tmsg.textContent='↻ rebooting...'}
async function wifiReset(){if(!confirm('Forget the saved Wi-Fi and reopen the "Alfred-Setup" hotspot? Join it from your phone to pick a new network.'))return;await fetch('/api/wifireset',{method:'POST'});tmsg.textContent='📶 reopening Wi-Fi setup — join "Alfred-Setup"...'}
async function loadRings(){let r=await(await fetch('/api/ringlog')).json();let R=r.rings||[];
 ringlog.innerHTML=R.length?R.map(x=>`<div class=fitem><span>&#128276; ${x}</span></div>`).join(''):'<p class=hint>No rings recorded yet.</p>'}
async function save(){
 let rows=[...document.querySelectorAll('#tbl tbody tr')].map(tr=>{let e=tr.querySelectorAll('input,select');let p=e[0].value.split(':');
  let snd=e[2].value;return{h:+p[0],m:+p[1],name:e[1].value,sound:snd?'/sounds/'+snd:'',icon:e[3].value}});
 let pw=[...document.querySelectorAll('#ptbl tbody tr')].map(tr=>{let e=tr.querySelectorAll('input,select');
  return{s:t2m(e[0].value),e:t2m(e[1].value),m:+e[2].value}});
 let r=await fetch('/api/save',{method:'POST',headers:{'Content-Type':'application/json'},
  body:JSON.stringify({reminders:rows,power:pw,tapG:+tap.value,ringSecs:+ring.value,battOff:+boff.value,battDeep:+bdeep.value,wifiMode:+wifi.value,wakeMode:wake.checked?1:0})});
 msg.textContent=r.ok?'✓ saved':'✗ error';setTimeout(()=>msg.textContent='',2500)}
async function upload(){let f=document.getElementById('f').files[0];if(!f)return;
 let fd=new FormData();fd.append('f',f,f.name);await fetch('/api/upload',{method:'POST',body:fd});load()}
async function uploadImg(){let f=document.getElementById('img').files[0];if(!f)return;
 let nm=(f.name.replace(/\.[^.]+$/,'').replace(/[^a-z0-9]/gi,'')||'img').slice(0,24);
 let bm=await createImageBitmap(f);imsg.textContent=' converting...';
 for(let a of [[196,''],[104,'_h']]){let sz=a[0];
  let c=document.createElement('canvas');c.width=c.height=sz;let x=c.getContext('2d');
  x.fillStyle='#000';x.fillRect(0,0,sz,sz);
  let s=Math.min(sz/bm.width,sz/bm.height),w=bm.width*s,h=bm.height*s;
  x.drawImage(bm,(sz-w)/2,(sz-h)/2,w,h);let d=x.getImageData(0,0,sz,sz).data;
  let hdr=(4|(sz<<10)|(sz<<21))>>>0;let buf=new Uint8Array(4+sz*sz*2);
  buf[0]=hdr&255;buf[1]=(hdr>>>8)&255;buf[2]=(hdr>>>16)&255;buf[3]=(hdr>>>24)&255;let o=4;
  for(let i=0;i<sz*sz;i++){let v=((d[i*4]&0xF8)<<8)|((d[i*4+1]&0xFC)<<3)|(d[i*4+2]>>3);buf[o++]=v&255;buf[o++]=v>>8}
  let fd=new FormData();fd.append('f',new Blob([buf]),nm+a[1]+'.bin');
  await fetch('/api/upload',{method:'POST',body:fd})}
 imsg.textContent=' ✓ '+nm+'.bin';await load()}
setInterval(async()=>{try{let j=await(await fetch('/api/jerk')).json();
 let e=document.getElementById('jerk');if(e)e.textContent=j.jerk.toFixed(2)}catch(x){}},500);
load();
</script></body></html>)HTML";

// ------------------------------ handlers ------------------------------
static void handleRoot() { server.send_P(200, "text/html", PAGE); }

static void handleState() {
  JsonDocument doc;
  JsonArray rem = doc["reminders"].to<JsonArray>();
  for (int i = 0; i < g_scheduleCount; i++) {
    JsonObject o = rem.add<JsonObject>();
    o["h"] = g_schedule[i].hour; o["m"] = g_schedule[i].minute;
    o["name"] = g_schedule[i].name; o["sound"] = g_schedule[i].sound; o["icon"] = g_schedule[i].icon;
  }
  JsonArray snd = doc["sounds"].to<JsonArray>();
  File dir = LittleFS.open("/sounds");
  if (dir) { for (File e = dir.openNextFile(); e; e = dir.openNextFile()) { String n = e.name(); snd.add(n); } }
  JsonArray ic = doc["icons"].to<JsonArray>();
  char keys[192]; strncpy(keys, icon_keys(), sizeof(keys) - 1); keys[sizeof(keys) - 1] = 0;
  for (char *t = strtok(keys, ","); t; t = strtok(nullptr, ",")) ic.add(t);
  JsonArray im = doc["images"].to<JsonArray>();    // uploaded images, for the delete list
  File idir = LittleFS.open("/images");    // uploaded custom images (big ones only)
  if (idir) for (File e = idir.openNextFile(); e; e = idir.openNextFile()) {
    String n = e.name(); int s = n.lastIndexOf('/'); if (s >= 0) n = n.substring(s + 1);
    if (n.endsWith(".bin") && !n.endsWith("_h.bin")) { ic.add(n); im.add(n); }
  }
  doc["tapG"] = g_tapThreshold;

  JsonArray pw = doc["power"].to<JsonArray>();     // power/sleep windows
  for (int i = 0; i < g_powerCount; i++) {
    JsonObject o = pw.add<JsonObject>();
    o["s"] = g_power[i].start; o["e"] = g_power[i].end; o["m"] = g_power[i].mode;
  }
  doc["ringSecs"] = g_ringSecs;
  doc["battOff"]  = g_battDispOff;
  doc["battDeep"] = g_battDeepSleep;
  doc["wifiMode"] = g_wifiMode;
  doc["wakeMode"] = g_wakeupMode;

  String out; serializeJson(doc, out);
  server.send(200, "application/json", out);
}

static void handleSave() {
  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain"))) { server.send(400, "text/plain", "bad json"); return; }
  int n = 0;
  for (JsonObject o : doc["reminders"].as<JsonArray>()) {
    if (n >= MAX_REM) break;
    Rem &r = g_schedule[n++];
    r.hour = o["h"] | 0; r.minute = o["m"] | 0;
    strncpy(r.name,  o["name"]  | "",     sizeof(r.name)  - 1); r.name[sizeof(r.name) - 1]   = 0;
    strncpy(r.sound, o["sound"] | "",     sizeof(r.sound) - 1); r.sound[sizeof(r.sound) - 1] = 0;
    strncpy(r.icon,  o["icon"]  | "milk", sizeof(r.icon)  - 1); r.icon[sizeof(r.icon) - 1]   = 0;
    r.accent = icon_accent(r.icon);
  }
  g_scheduleCount = n;
  if (!doc["tapG"].isNull()) {
    g_tapThreshold = doc["tapG"].as<float>();
    if (g_tapThreshold < TAP_MIN_G) g_tapThreshold = TAP_MIN_G;   // keep noise from reading as taps
  }

  if (doc["power"].is<JsonArray>()) {              // power/sleep windows
    g_powerCount = 0;
    for (JsonObject o : doc["power"].as<JsonArray>()) {
      if (g_powerCount >= MAX_PWIN) break;
      PowerWin &w = g_power[g_powerCount++];
      w.start = o["s"] | 0; w.end = o["e"] | 0; w.mode = o["m"] | 0;
    }
  }
  if (!doc["ringSecs"].isNull()) { g_ringSecs = doc["ringSecs"].as<int>(); if (g_ringSecs < 2) g_ringSecs = 2; }
  if (!doc["battOff"].isNull())  { g_battDispOff   = doc["battOff"].as<int>();  if (g_battDispOff   > 100) g_battDispOff   = 100; if (g_battDispOff   < 0) g_battDispOff   = 0; }
  if (!doc["battDeep"].isNull()) { g_battDeepSleep = doc["battDeep"].as<int>(); if (g_battDeepSleep > 100) g_battDeepSleep = 100; if (g_battDeepSleep < 0) g_battDeepSleep = 0; }
  if (!doc["wifiMode"].isNull()) { g_wifiMode = doc["wifiMode"].as<int>(); }
  if (!doc["wakeMode"].isNull()) { g_wakeupMode = doc["wakeMode"].as<int>(); }

  schedule_save();
  scheduleDirty = true;
  server.send(200, "text/plain", "ok");
}

static void handleJerk() {
  char buf[32];
  snprintf(buf, sizeof(buf), "{\"jerk\":%.2f}", imu_peak_jerk());
  server.send(200, "application/json", buf);
}

static void handleTest()  { testPending  = true; server.send(200, "text/plain", "ok"); }
static void handleSleep()  { sleepPending  = true; server.send(200, "text/plain", "ok"); }
static void handleReboot() { rebootPending = true; server.send(200, "text/plain", "ok"); }
static void handleWifiReset() { wifiResetPending = true; server.send(200, "text/plain", "ok"); }

// last 100 doorbell rings, newest first, formatted in the device's local time
static void handleRingLog() {
  JsonDocument doc;
  JsonArray a = doc["rings"].to<JsonArray>();
  for (int i = ringlog_count() - 1; i >= 0; i--) {
    time_t t = (time_t)ringlog_at(i);
    struct tm tm; localtime_r(&t, &tm);
    char buf[40]; strftime(buf, sizeof(buf), "%a %d %b %Y  %I:%M %p", &tm);
    a.add(buf);
  }
  String out; serializeJson(doc, out);
  server.send(200, "application/json", out);
}

static void handleUpload() {
  HTTPUpload &up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    String path = (up.filename.endsWith(".bin") ? "/images/" : "/sounds/") + up.filename;
    uploadFile = LittleFS.open(path, "w");
    Serial.printf("[web] upload %s\n", path.c_str());
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (uploadFile) uploadFile.write(up.buf, up.currentSize);
  } else if (up.status == UPLOAD_FILE_END) {
    if (uploadFile) uploadFile.close();
  }
}

// delete a sound or an uploaded image (plus its _h companion) from the filesystem
static void handleDelete() {
  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain"))) { server.send(400, "text/plain", "bad json"); return; }
  String type = doc["type"] | "";
  String file = doc["file"] | "";
  if (file.length() == 0 || file.indexOf('/') >= 0) { server.send(400, "text/plain", "bad name"); return; }
  bool ok = false;
  if (type == "sound") {
    ok = LittleFS.remove("/sounds/" + file);
  } else if (type == "image") {
    ok = LittleFS.remove("/images/" + file);
    String base = file; int dot = base.lastIndexOf('.'); if (dot >= 0) base = base.substring(0, dot);
    LittleFS.remove("/images/" + base + "_h.bin");     // also drop the 104px home version
  }
  Serial.printf("[web] delete %s/%s -> %s\n", type.c_str(), file.c_str(), ok ? "ok" : "miss");
  server.send(ok ? 200 : 404, "text/plain", ok ? "ok" : "not found");
}

// ------------------------------ api ------------------------------
void webconfig_begin() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/state", HTTP_GET, handleState);
  server.on("/api/jerk", HTTP_GET, handleJerk);
  server.on("/api/ringlog", HTTP_GET, handleRingLog);
  server.on("/api/test", HTTP_POST, handleTest);
  server.on("/api/sleep", HTTP_POST, handleSleep);
  server.on("/api/reboot", HTTP_POST, handleReboot);
  server.on("/api/wifireset", HTTP_POST, handleWifiReset);
  server.on("/api/save", HTTP_POST, handleSave);
  server.on("/api/delete", HTTP_POST, handleDelete);
  server.on("/api/upload", HTTP_POST, []() { server.send(200, "text/plain", "ok"); }, handleUpload);
  server.begin();
  Serial.print("[web] config server at http://");
  Serial.println(WiFi.localIP());
}

void webconfig_loop() { server.handleClient(); }

bool webconfig_schedule_dirty() {
  if (scheduleDirty) { scheduleDirty = false; return true; }
  return false;
}

bool webconfig_test_pending() {
  if (testPending) { testPending = false; return true; }
  return false;
}

bool webconfig_sleep_pending() {
  if (sleepPending) { sleepPending = false; return true; }
  return false;
}

bool webconfig_reboot_pending() {
  if (rebootPending) { rebootPending = false; return true; }
  return false;
}

bool webconfig_wifireset_pending() {
  if (wifiResetPending) { wifiResetPending = false; return true; }
  return false;
}
