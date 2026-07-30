#include "web_panel.h"

#include <ArduinoJson.h>
#include <DNSServer.h>
#include <M5Unified.h>
#include <WebServer.h>
#include <WiFi.h>

#include "panel_html.h"
#include "wifi_store.h"

namespace {

WebServer s_server(80);
TaskHandle_t s_task = nullptr;
bool s_running = false;

// --- 配网模式 ---
DNSServer s_dns;
volatile bool s_ap_mode = false;
volatile bool s_provisioned = false;   // 配网页保存成功后置位一次
char s_ap_ip[20] = {0};

// 快照与暂存都用同一把互斥锁保护 —— 两者都小、访问都不频繁（面板 1s 轮询一次），
// 一把锁足够，也省得考虑锁序。不用 portENTER_CRITICAL：结构 ~1.5KB，
// 在临界区里拷这么多会连中断一起挡太久。
SemaphoreHandle_t s_lock = nullptr;
panel::Snapshot s_snap;
panel::Pending  s_pending;

// GET /api/settings 的数据源：主循环发布，HTTP 任务只读。
struct SettingsView {
    uint8_t  volume = 0, brightness = 0, turn_limit = 0, tilt = 0;
    uint16_t idle_sec = 0;
    bool     motion = false, idle_wiggle = false, dance = false;
    char     voice[panel::VOICE_CAP] = {0};
    char     persona[panel::PERSONA_CAP] = {0};
} s_settings;

struct Lock {
    bool ok;
    Lock() : ok(s_lock && xSemaphoreTake(s_lock, pdMS_TO_TICKS(200)) == pdTRUE) {}
    ~Lock() { if (ok) xSemaphoreGive(s_lock); }
};

const char* stateName(uint8_t s) {
    switch (s) {
        case 0: return "sleep";
        case 1: return "connecting";
        case 2: return "ready";
        case 3: return "listening";
        case 4: return "thinking";
        case 5: return "speaking";
        default: return "unknown";
    }
}

void sendJson(int code, JsonDocument& doc) {
    String out;
    serializeJson(doc, out);
    s_server.send(code, "application/json; charset=utf-8", out);
}

// --- GET / ---
void handleRoot() {
    s_server.sendHeader("Content-Encoding", "gzip");
    s_server.send_P(200, "text/html; charset=utf-8",
                    (const char*)PANEL_HTML_GZ, PANEL_HTML_GZ_LEN);
}

// --- GET /api/state ---
void handleState() {
    panel::Snapshot snap;
    {
        Lock l;
        if (!l.ok) { s_server.send(503, "text/plain", "busy"); return; }
        snap = s_snap;
    }
    JsonDocument doc;
    doc["state"]        = stateName(snap.state);
    doc["wifi"]         = snap.wifi_up;
    doc["dashscope"]    = snap.ds_connected;
    doc["turns"]        = snap.turns;
    doc["turn_limit"]   = snap.turn_limit;
    doc["last_latency_ms"] = snap.last_latency_ms;
    doc["last_tokens"]  = snap.last_tokens;
    doc["underruns"]    = snap.underruns;
    doc["dry_ms"]       = snap.dry_ms;
    doc["battery_pct"]  = snap.battery_pct;
    doc["idle_remain_sec"] = snap.idle_remain_sec;
    doc["subtitle"]     = snap.subtitle;
    doc["last_user"]    = snap.last_user_text;
    doc["last_reply"]   = snap.last_reply_text;
    doc["rssi"]         = WiFi.RSSI();
    sendJson(200, doc);
}

// --- GET /api/settings ---
void handleGetSettings() {
    SettingsView v;
    {
        Lock l;
        if (!l.ok) { s_server.send(503, "text/plain", "busy"); return; }
        v = s_settings;
    }
    JsonDocument doc;
    doc["volume"]      = v.volume;
    doc["brightness"]  = v.brightness;
    doc["idle_sec"]    = v.idle_sec;
    doc["turn_limit"]  = v.turn_limit;
    doc["motion"]      = v.motion;
    doc["idle_wiggle"] = v.idle_wiggle;
    doc["tilt"]        = v.tilt;
    doc["dance"]       = v.dance;
    doc["voice"]       = v.voice;
    doc["persona"]     = v.persona;
    sendJson(200, doc);
}

// --- POST /api/settings ---
// 部分更新：只处理请求里出现的键。夹取后暂存，主循环下一 tick 应用并持久化。
void handlePostSettings() {
    JsonDocument req;
    if (deserializeJson(req, s_server.arg("plain"))) {
        JsonDocument err;
        err["error"] = "invalid json";
        sendJson(400, err);
        return;
    }

    panel::Pending p;
    if (req["volume"].is<long>())      p.setVolume(req["volume"].as<long>());
    if (req["brightness"].is<long>())  p.setBrightness(req["brightness"].as<long>());
    if (req["idle_sec"].is<long>())    p.setIdleSec(req["idle_sec"].as<long>());
    if (req["turn_limit"].is<long>())  p.setTurnLimit(req["turn_limit"].as<long>());
    if (req["tilt"].is<long>())        p.setTilt(req["tilt"].as<long>());
    if (req["motion"].is<bool>())      p.setMotion(req["motion"].as<bool>());
    if (req["idle_wiggle"].is<bool>()) p.setIdleWiggle(req["idle_wiggle"].as<bool>());
    if (req["dance"].is<bool>())       p.setDance(req["dance"].as<bool>());
    if (req["voice"].is<const char*>())   p.setVoice(req["voice"]);
    if (req["persona"].is<const char*>()) p.setPersona(req["persona"]);

    if (!p.any) {
        JsonDocument err;
        err["error"] = "no known keys";
        sendJson(400, err);
        return;
    }

    {
        Lock l;
        if (!l.ok) { s_server.send(503, "text/plain", "busy"); return; }
        // 合并进已有暂存（主循环还没来得及取走时不丢改动）
        if (p.has_volume)      s_pending.setVolume(p.volume);
        if (p.has_brightness)  s_pending.setBrightness(p.brightness);
        if (p.has_idle_sec)    s_pending.setIdleSec(p.idle_sec);
        if (p.has_turn_limit)  s_pending.setTurnLimit(p.turn_limit);
        if (p.has_tilt)        s_pending.setTilt(p.tilt);
        if (p.has_motion)      s_pending.setMotion(p.motion);
        if (p.has_idle_wiggle) s_pending.setIdleWiggle(p.idle_wiggle);
        if (p.has_dance)       s_pending.setDance(p.dance);
        if (p.has_voice)       s_pending.setVoice(p.voice);
        if (p.has_persona)     s_pending.setPersona(p.persona);
    }

    // 回夹取后的实际值，并标明哪些要下次会话才生效（spec 要求如实告知）。
    JsonDocument doc;
    doc["ok"] = true;
    JsonObject applied = doc["applied"].to<JsonObject>();
    if (p.has_volume)      applied["volume"] = p.volume;
    if (p.has_brightness)  applied["brightness"] = p.brightness;
    if (p.has_idle_sec)    applied["idle_sec"] = p.idle_sec;
    if (p.has_turn_limit)  applied["turn_limit"] = p.turn_limit;
    if (p.has_tilt)        applied["tilt"] = p.tilt;
    if (p.has_motion)      applied["motion"] = p.motion;
    if (p.has_idle_wiggle) applied["idle_wiggle"] = p.idle_wiggle;
    if (p.has_dance)       applied["dance"] = p.dance;
    if (p.has_voice)       applied["voice"] = p.voice;
    if (p.has_persona)     applied["persona_bytes"] = strlen(p.persona);
    if (p.has_voice || p.has_persona) {
        JsonArray d = doc["deferred"].to<JsonArray>();
        if (p.has_voice)   d.add("voice");
        if (p.has_persona) d.add("persona");
        doc["deferred_note"] = "下次唤醒建立会话时生效";
    }
    sendJson(200, doc);
}

// --- POST /api/action ---
void handleAction() {
    JsonDocument req;
    if (deserializeJson(req, s_server.arg("plain")) || !req["do"].is<const char*>()) {
        JsonDocument err;
        err["error"] = "expected {\"do\":\"dance|disconnect\"}";
        sendJson(400, err);
        return;
    }
    const char* what = req["do"];
    if (strcmp(what, "dance") != 0 && strcmp(what, "disconnect") != 0) {
        JsonDocument err;
        err["error"] = "unknown action";
        sendJson(400, err);
        return;
    }

    bool motion_on;
    {
        Lock l;
        if (!l.ok) { s_server.send(503, "text/plain", "busy"); return; }
        motion_on = s_settings.motion;
        s_pending.setAction(what);
    }

    JsonDocument doc;
    doc["ok"] = true;
    doc["queued"] = what;
    // spec：动作关闭时 dance 不驱动舵机，且要说明原因。
    if (strcmp(what, "dance") == 0 && !motion_on) {
        doc["ok"] = false;
        doc["note"] = "摇头跳舞总开关已关闭，舵机不会动";
    }
    sendJson(200, doc);
}

// --- 网络管理（xiaomi-wifi-provisioning） ---

// GET /api/networks —— 只回 SSID 与当前连接项。**永不回显密码**（spec 硬性要求）。
void handleGetNetworks() {
    JsonDocument doc;
    JsonArray arr = doc["networks"].to<JsonArray>();
    auto& st = wifi_store::device();
    const char* cur = wifi_store::deviceCurrentSsid();
    for (size_t i = 0; i < wifi_store::MAX_NETWORKS; ++i) {
        const auto* e = st.at(i);
        if (!e) break;
        JsonObject o = arr.add<JsonObject>();
        o["ssid"] = e->ssid;
        o["current"] = (cur && strcmp(cur, e->ssid) == 0);
        // 刻意不放密码字段
    }
    doc["count"] = st.count();
    doc["max"] = wifi_store::MAX_NETWORKS;
    sendJson(200, doc);
}

// POST /api/networks {ssid, password}
void handlePostNetworks() {
    JsonDocument req;
    if (deserializeJson(req, s_server.arg("plain")) || !req["ssid"].is<const char*>()) {
        JsonDocument err; err["error"] = "expected {\"ssid\":..,\"password\":..}";
        sendJson(400, err); return;
    }
    const char* ssid = req["ssid"];
    const char* pass = req["password"].is<const char*>()
                           ? req["password"].as<const char*>()
                           : "";

    auto r = wifi_store::device().put(ssid, pass);
    JsonDocument doc;
    switch (r) {
        case wifi_store::PutResult::Added:
        case wifi_store::PutResult::Updated:
            wifi_store::devicePersist();
            doc["ok"] = true;
            doc["result"] = (r == wifi_store::PutResult::Added) ? "added" : "updated";
            doc["count"] = wifi_store::device().count();
            s_provisioned = true;      // 配网模式下据此结束配网去连新网
            sendJson(200, doc);
            return;
        case wifi_store::PutResult::Full:
            doc["ok"] = false;
            doc["error"] = "已存满 5 个网络，请先删除一个再添加";
            sendJson(409, doc);
            return;
        default:
            doc["ok"] = false;
            doc["error"] = "SSID 为空或过长";
            sendJson(400, doc);
            return;
    }
}

// DELETE /api/networks {ssid}
void handleDeleteNetworks() {
    JsonDocument req;
    if (deserializeJson(req, s_server.arg("plain")) || !req["ssid"].is<const char*>()) {
        JsonDocument err; err["error"] = "expected {\"ssid\":..}";
        sendJson(400, err); return;
    }
    bool ok = wifi_store::device().remove(req["ssid"]);
    if (ok) wifi_store::devicePersist();
    JsonDocument doc;
    doc["ok"] = ok;
    if (!ok) doc["error"] = "没有这个网络";
    doc["count"] = wifi_store::device().count();
    sendJson(ok ? 200 : 404, doc);
}

// GET /api/networks/scan —— 扫描附近网络辅助选择。阻塞 ~2s，但跑在 HTTP 任务里，
// 主循环与音频不受影响（design 决策 10）。
void handleScanNetworks() {
    int n = WiFi.scanNetworks();
    JsonDocument doc;
    JsonArray arr = doc["found"].to<JsonArray>();
    for (int i = 0; i < n && i < 20; ++i) {
        JsonObject o = arr.add<JsonObject>();
        o["ssid"] = WiFi.SSID(i);
        o["rssi"] = WiFi.RSSI(i);
        o["open"] = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN);
    }
    WiFi.scanDelete();
    sendJson(200, doc);
}

// --- 配网门户页（AP 模式；小而全，不值得 gzip） ---
const char SETUP_PAGE[] PROGMEM = R"HTML(<!doctype html><html lang="zh-CN"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>小咪配网</title><style>
body{font-family:-apple-system,"PingFang SC",sans-serif;background:#faf8f5;color:#26221f;
margin:0;padding:22px;max-width:520px;margin:0 auto}
h1{font-size:1.3rem}label{display:block;margin:14px 0 5px;font-weight:600;font-size:.92rem}
input,select{width:100%;font:inherit;padding:9px;border:1px solid #e9e1d9;border-radius:9px;
box-sizing:border-box;background:#fff}
button{margin-top:16px;width:100%;padding:11px;font:inherit;font-weight:700;border:none;
border-radius:9px;background:#dd5389;color:#fff}
.hint{font-size:.8rem;color:#6e655d;margin-top:6px}
#msg{margin-top:14px;padding:10px;border-radius:9px;display:none}
.ok{background:#e7f5ee;border:1px solid #3a9e77;display:block!important}
.err{background:#fbe6e6;border:1px solid #b04a4a;display:block!important}
</style></head><body>
<h1>🐱 给小咪配网</h1>
<p class="hint">选一个 WiFi 并输入密码，小咪会记住它，以后到这儿自动连。</p>
<label>WiFi 名称</label>
<select id="sel"><option value="">扫描中……</option></select>
<div class="hint">找不到？在下面直接输入名称（隐藏网络也行）</div>
<input id="ssid" placeholder="或手动输入 WiFi 名称">
<label>密码</label>
<input id="pw" type="password" placeholder="开放网络请留空">
<button id="go">保存并连接</button>
<div id="msg"></div>
<script>
const $=i=>document.getElementById(i);
fetch("/api/networks/scan").then(r=>r.json()).then(d=>{
  const s=$("sel");s.innerHTML='<option value="">— 请选择 —</option>';
  (d.found||[]).sort((a,b)=>b.rssi-a.rssi).forEach(n=>{
    if(!n.ssid)return;const o=document.createElement("option");
    o.value=n.ssid;o.textContent=n.ssid+"  ("+n.rssi+"dBm"+(n.open?" 开放":"")+")";s.appendChild(o);});
}).catch(()=>{$("sel").innerHTML='<option value="">扫描失败，请手动输入</option>';});
$("sel").addEventListener("change",e=>{if(e.target.value)$("ssid").value=e.target.value;});
$("go").addEventListener("click",async()=>{
  const ssid=$("ssid").value||$("sel").value;const m=$("msg");
  if(!ssid){m.className="err";m.textContent="请先选择或输入 WiFi 名称";return;}
  m.className="";m.style.display="block";m.textContent="保存中……";
  try{
    const r=await fetch("/api/networks",{method:"POST",headers:{"Content-Type":"application/json"},
      body:JSON.stringify({ssid:ssid,password:$("pw").value})});
    const d=await r.json();
    if(d.ok){m.className="ok";m.textContent="保存成功！小咪正在连接 "+ssid+"，这个热点马上会关闭。";}
    else{m.className="err";m.textContent=d.error||"保存失败";}
  }catch(e){m.className="err";m.textContent="保存失败："+e.message;}
});
</script></body></html>)HTML";

void handleSetupPage() {
    s_server.send_P(200, "text/html; charset=utf-8", SETUP_PAGE);
}

void handleNotFound() {
    // 配网模式下把任意路径重定向到配网页 —— 各家系统的门户探测 URL 都不一样
    // （iOS /hotspot-detect.html、Android /generate_204、Windows /ncsi.txt…），
    // 一律 302 才能保证门户弹得出来；即使没弹，用户手打任意网址也会跳过来。
    if (s_ap_mode) {
        s_server.sendHeader("Location", String("http://") + s_ap_ip + "/", true);
        s_server.send(302, "text/plain", "");
        return;
    }
    s_server.send(404, "text/plain", "not found");
}

void webTask(void*) {
    // 根路径按模式分流：配网模式发配网页，正常模式发控制面板。
    s_server.on("/", HTTP_GET, []() {
        if (s_ap_mode) handleSetupPage(); else handleRoot();
    });
    s_server.on("/api/state", HTTP_GET, handleState);
    s_server.on("/api/settings", HTTP_GET, handleGetSettings);
    s_server.on("/api/settings", HTTP_POST, handlePostSettings);
    s_server.on("/api/action", HTTP_POST, handleAction);
    // 网络管理：面板与配网页共用同一套接口
    s_server.on("/api/networks", HTTP_GET, handleGetNetworks);
    s_server.on("/api/networks", HTTP_POST, handlePostNetworks);
    s_server.on("/api/networks", HTTP_DELETE, handleDeleteNetworks);
    s_server.on("/api/networks/scan", HTTP_GET, handleScanNetworks);
    s_server.onNotFound(handleNotFound);
    s_server.begin();
    Serial.printf("[panel] http://%s/ 已启动\n", WiFi.localIP().toString().c_str());
    for (;;) {
        s_server.handleClient();
        if (s_ap_mode) s_dns.processNextRequest();   // 强制门户
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

}  // namespace

bool webPanelStart() {
    if (s_running) return true;
    if (WiFi.status() != WL_CONNECTED) return false;
    if (!s_lock) s_lock = xSemaphoreCreateMutex();
    if (!s_lock) return false;
    // 优先级 2：低于播放任务（3），高于空闲 —— 面板永远不该抢音频的时间片。
    if (xTaskCreatePinnedToCore(webTask, "webPanel", 8192, nullptr, 2, &s_task, 0) != pdPASS) {
        Serial.println("[panel] 任务创建失败");
        return false;
    }
    s_running = true;
    return true;
}

bool webPanelRunning() { return s_running; }

const char* webPanelStartApMode(const char* ap_ssid, const char* ap_pass) {
    // 纯 AP 模式：先停 STA，不做 APSTA 并存（省射频与内存，也避免奇怪的状态）。
    WiFi.disconnect(true);
    WiFi.mode(WIFI_AP);
    if (!WiFi.softAP(ap_ssid, ap_pass)) {
        Serial.println("[panel] softAP 启动失败");
        return nullptr;
    }
    IPAddress ip = WiFi.softAPIP();
    strncpy(s_ap_ip, ip.toString().c_str(), sizeof(s_ap_ip) - 1);
    s_ap_ip[sizeof(s_ap_ip) - 1] = 0;

    s_dns.setErrorReplyCode(DNSReplyCode::NoError);
    s_dns.start(53, "*", ip);          // 所有域名解析到自己 → 手机自动弹门户
    s_ap_mode = true;
    s_provisioned = false;

    // HTTP 任务可能还没起过（从没连上过网时）——这里补起，AP 模式同样需要它。
    if (!s_running) {
        if (!s_lock) s_lock = xSemaphoreCreateMutex();
        if (s_lock && xTaskCreatePinnedToCore(webTask, "webPanel", 8192, nullptr, 2,
                                              &s_task, 0) == pdPASS) {
            s_running = true;
        }
    }
    Serial.printf("[panel] 配网热点 '%s' 已开，门户 http://%s/\n", ap_ssid, s_ap_ip);
    return s_ap_ip;
}

void webPanelStopApMode() {
    if (!s_ap_mode) return;
    s_ap_mode = false;
    s_dns.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    Serial.println("[panel] 配网热点已关闭");
}

bool webPanelInApMode() { return s_ap_mode; }

bool webPanelTakeProvisioned() {
    if (!s_provisioned) return false;
    s_provisioned = false;
    return true;
}

void webPanelPublish(const panel::Snapshot& snap) {
    if (!s_lock) return;
    Lock l;
    if (l.ok) s_snap = snap;
}

bool webPanelTakePending(panel::Pending* out) {
    if (!s_lock || !out) return false;
    Lock l;
    if (!l.ok) return false;
    return s_pending.take(out);
}

void webPanelPublishSettings(uint8_t volume, uint8_t brightness, uint16_t idle_sec,
                             uint8_t turn_limit, bool motion, bool idle_wiggle,
                             uint8_t tilt, bool dance, const char* voice,
                             const char* persona) {
    if (!s_lock) return;
    Lock l;
    if (!l.ok) return;
    s_settings.volume = volume;
    s_settings.brightness = brightness;
    s_settings.idle_sec = idle_sec;
    s_settings.turn_limit = turn_limit;
    s_settings.motion = motion;
    s_settings.idle_wiggle = idle_wiggle;
    s_settings.tilt = tilt;
    s_settings.dance = dance;
    strncpy(s_settings.voice, voice ? voice : "", panel::VOICE_CAP - 1);
    s_settings.voice[panel::VOICE_CAP - 1] = 0;
    strncpy(s_settings.persona, persona ? persona : "", panel::PERSONA_CAP - 1);
    s_settings.persona[panel::PERSONA_CAP - 1] = 0;
}
