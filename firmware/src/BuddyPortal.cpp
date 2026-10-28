#include "BuddyPortal.h"

#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <WiFi.h>

namespace {
const char kPage[] PROGMEM = R"HTML(
<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>PocketBuddy Settings</title><style>
:root{color-scheme:dark;--bg:#080b13;--card:#121827;--line:#29334a;--ink:#f5f7ff;--muted:#9da8bf;--accent:#78e6d0;--accent2:#a78bfa;--input:#0b1020;--danger:#ff8b9b}*{box-sizing:border-box}body{margin:0;min-height:100vh;background:radial-gradient(circle at 15% 0,#203d4d 0,transparent 35%),radial-gradient(circle at 90% 10%,#35245a 0,transparent 32%),var(--bg);color:var(--ink);font:15px/1.5 system-ui,-apple-system,sans-serif}main{width:min(100%,820px);margin:auto;padding:24px 16px 48px}.hero{display:flex;align-items:center;gap:14px;margin-bottom:18px}.face{display:grid;place-items:center;width:58px;height:58px;border-radius:19px;background:linear-gradient(145deg,var(--accent),var(--accent2));color:#071019;font-size:29px;box-shadow:0 14px 40px #0007}.hero h1{margin:0;font-size:27px;letter-spacing:-.5px}.hero p{margin:4px 0 0;color:var(--muted)}.status{margin-left:auto;padding:7px 11px;border:1px solid var(--line);border-radius:99px;background:#0b1020;color:var(--accent);font-size:12px;font-weight:750}.card{margin:14px 0;padding:20px;border:1px solid var(--line);border-radius:22px;background:linear-gradient(145deg,#182137,var(--card));box-shadow:0 18px 46px #0005}.card h2{margin:0 0 4px;font-size:19px}.card>p{margin:0 0 17px;color:var(--muted)}.grid{display:grid;grid-template-columns:1fr 1fr;gap:14px}.full{grid-column:1/-1}label{display:block;color:#dbe3f5;font-size:13px;font-weight:700}small{display:block;margin-top:5px;color:var(--muted);font-weight:400}input,textarea,select{width:100%;margin-top:7px;border:1px solid var(--line);border-radius:12px;padding:11px 12px;background:var(--input);color:var(--ink);font:inherit;outline:0}input:focus,textarea:focus,select:focus{border-color:var(--accent);box-shadow:0 0 0 3px #78e6d018}textarea{min-height:110px;resize:vertical}.modelrow{display:flex;gap:8px;align-items:stretch}.modelrow select{flex:1}.refresh{margin-top:7px;border:1px solid var(--line);border-radius:12px;padding:0 13px;background:#182137;color:var(--ink);font-weight:750;cursor:pointer;white-space:nowrap}.actions{display:flex;align-items:center;gap:11px;margin-top:18px}.save{border:0;border-radius:13px;padding:12px 18px;background:linear-gradient(135deg,var(--accent),#60a5fa);color:#071019;font-weight:850;cursor:pointer}.save:disabled{opacity:.6}.message{color:var(--muted)}.message.error{color:var(--danger)}.privacy{padding:13px 15px;border:1px solid var(--line);border-radius:15px;background:#0b1020;color:var(--muted);font-size:12px}.foot{text-align:center;color:var(--muted);font-size:12px;margin-top:22px}@media(max-width:650px){main{padding:16px 11px 35px}.grid{grid-template-columns:1fr}.full{grid-column:auto}.status{display:none}.card{padding:17px}.hero h1{font-size:23px}}
</style></head><body><main><header class="hero"><div class="face">●‿●</div><div><h1>PocketBuddy</h1><p>Connection, voice, and personality studio</p></div><div id="status" class="status">Loading…</div></header>
<form id="form"><section class="card"><h2>Network</h2><p>Where PocketBuddy lives and how it reaches your AI service.</p><div class="grid"><label>Wi-Fi SSID<input id="ssid" name="ssid" maxlength="32" required autocomplete="off"></label><label>Wi-Fi password<input name="wifiPassword" type="password" maxlength="63" placeholder="Leave blank to keep saved password" autocomplete="new-password"><small id="wifiState"></small></label><label class="full">OpenAI-compatible service URL<input id="serviceUrl" name="serviceUrl" type="url" maxlength="160" required placeholder="http://192.168.0.172:5810"><small>PocketBuddy uses the bridge's asynchronous chat and speech extensions.</small></label><label class="full">API key<input name="apiKey" type="password" maxlength="256" placeholder="Leave blank to keep saved key" autocomplete="new-password"><small id="keyState"></small></label></div></section>
<section class="card"><h2>Identity & personality</h2><p>These settings travel with each request and shape the companion.</p><div class="grid"><label>Buddy name<input id="buddyName" name="buddyName" maxlength="32" required></label><label>Owner name<input id="ownerName" name="ownerName" maxlength="32" required></label><label>Default model<div class="modelrow"><select id="model" name="model" required><option value="gemma4:e4b">gemma4:e4b</option></select><button id="refreshModels" class="refresh" type="button">Refresh</button></div><small id="modelState">Connect PocketBuddy to Wi-Fi to discover models.</small></label><label>Voice<input id="voice" name="voice" maxlength="64" required placeholder="af_sky"></label><label class="full">Personality bio<textarea id="personality" name="personality" maxlength="600" required></textarea></label><label>Device ID<input id="deviceId" name="deviceId" maxlength="80" required></label><label>Speaker volume <span id="volumeLabel">255</span><input id="volume" name="volume" type="range" min="0" max="255" value="255"></label></div></section>
<div class="privacy">Passwords and API keys are stored in PocketBuddy's device preferences. They are never returned to this page.</div><div class="actions"><button id="save" class="save">Save & restart</button><span id="message" class="message" role="status"></span></div></form><p class="foot">PocketBuddy local WebUI · pocketbuddy.local</p></main>
<script>
const $=id=>document.getElementById(id),form=$('form'),message=$('message'),save=$('save'),volume=$('volume'),model=$('model');let currentModel='gemma4:e4b';volume.oninput=()=>$('volumeLabel').textContent=volume.value;
function setModels(ids){const all=[...new Set([currentModel,...ids])].filter(Boolean).sort();model.replaceChildren(...all.map(id=>{const o=document.createElement('option');o.value=o.textContent=id;return o}));model.value=currentModel}
async function loadModels(){const state=$('modelState');state.textContent='Discovering models…';try{const r=await fetch('/api/models',{cache:'no-store'}),b=await r.json();if(!r.ok)throw Error(b.error||'Model discovery failed');setModels((b.data||[]).map(x=>x.id));state.textContent=`${b.data.length} models available. The selected model becomes the default.`}catch(e){setModels([]);state.textContent=e.message}}
async function load(){try{const r=await fetch('/api/config',{cache:'no-store'});if(!r.ok)throw Error();const c=await r.json();currentModel=c.model;setModels([]);for(const k of ['ssid','serviceUrl','buddyName','ownerName','voice','personality','deviceId','volume'])if($(k))$(k).value=c[k];$('volumeLabel').textContent=c.volume;$('wifiState').textContent=c.hasWifiPassword?'A password is saved.':'';$('keyState').textContent=c.hasApiKey?'An API key is saved.':'Required before PocketBuddy can talk to Dark Tower.';$('status').textContent=c.connected?'Online · '+c.ip:(c.setupMode?'Setup hotspot':'Offline');if(c.connected&&c.hasApiKey)loadModels()}catch(e){message.textContent='Could not read PocketBuddy settings.';message.className='message error'}}
$('refreshModels').onclick=loadModels;
form.onsubmit=async e=>{e.preventDefault();save.disabled=true;message.className='message';message.textContent='Saving…';try{const r=await fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(new FormData(form))});const b=await r.json();if(!r.ok)throw Error(b.error||'Save failed');message.textContent=b.serviceReady?'Saved. PocketBuddy is restarting…':'Wi-Fi saved. PocketBuddy will restart; add the API key after reconnecting.'}catch(e){message.textContent=e.message;message.className='message error';save.disabled=false}};load();
</script></body></html>
)HTML";

bool validHttpUrl(const String &value) {
    return value.startsWith("http://") || value.startsWith("https://");
}

bool validText(const String &value, size_t maxLength, bool required = true) {
    return (!required || !value.isEmpty()) && value.length() <= maxLength &&
           value.indexOf('\r') < 0 && value.indexOf('\n') < 0;
}
}  // namespace

String BuddyPortal::jsonEscape(const String &value) {
    String out;
    out.reserve(value.length() + 8);
    for (size_t i = 0; i < value.length(); ++i) {
        const char c = value[i];
        if (c == '\\' || c == '"') out += '\\';
        if (c == '\n') out += "\\n";
        else if (c != '\r') out += c;
    }
    return out;
}

bool BuddyPortal::authorize() {
    if (server_.authenticate("admin", adminPassword_.c_str())) return true;
    server_.requestAuthentication(BASIC_AUTH, "PocketBuddy", "Use the device portal password");
    return false;
}

void BuddyPortal::begin(bool setupAccessPoint) {
    const uint64_t chip = ESP.getEfuseMac();
    char suffix[5];
    char password[14];
    snprintf(suffix, sizeof(suffix), "%04X", static_cast<uint16_t>(chip));
    snprintf(password, sizeof(password), "Buddy%08X", static_cast<uint32_t>(chip));
    apName_ = String("PocketBuddy-") + suffix;
    adminPassword_ = password;
    setupMode_ = setupAccessPoint;

    if (setupMode_) {
        WiFi.mode(WIFI_AP_STA);
        WiFi.softAP(apName_.c_str(), adminPassword_.c_str());
        dns_.start(53, "*", WiFi.softAPIP());
    } else if (MDNS.begin("pocketbuddy")) {
        MDNS.addService("http", "tcp", 80);
    }
    configureRoutes();
    server_.begin();
    active_ = true;
    Serial.printf("PocketBuddy WebUI: %s\n", url().c_str());
    if (setupMode_) Serial.println("Setup fallback: http://192.168.4.1");
    else Serial.println("Friendly address: http://pocketbuddy.local");
    Serial.printf("WebUI login: admin / %s\n", adminPassword_.c_str());
}

String BuddyPortal::url() const {
    return setupMode_ ? "http://pocketbuddy" : String("http://") + WiFi.localIP().toString();
}

void BuddyPortal::loop() {
    if (!active_) return;
    if (setupMode_) dns_.processNextRequest();
    server_.handleClient();
    if (restartAt_ && static_cast<int32_t>(millis() - restartAt_) >= 0) ESP.restart();
}

void BuddyPortal::sendConfig() {
    if (!authorize()) return;
    String json;
    json.reserve(1200);
    json = "{\"ssid\":\"" + jsonEscape(settings_.wifiSsid) + "\"";
    json += ",\"serviceUrl\":\"" + jsonEscape(settings_.serviceUrl) + "\"";
    json += ",\"model\":\"" + jsonEscape(settings_.model) + "\"";
    json += ",\"buddyName\":\"" + jsonEscape(settings_.buddyName) + "\"";
    json += ",\"ownerName\":\"" + jsonEscape(settings_.ownerName) + "\"";
    json += ",\"personality\":\"" + jsonEscape(settings_.personality) + "\"";
    json += ",\"voice\":\"" + jsonEscape(settings_.voice) + "\"";
    json += ",\"deviceId\":\"" + jsonEscape(settings_.deviceId) + "\"";
    json += ",\"volume\":" + String(settings_.volume);
    json += ",\"hasWifiPassword\":" + String(settings_.wifiPassword.isEmpty() ? "false" : "true");
    json += ",\"hasApiKey\":" + String(settings_.apiKey.isEmpty() ? "false" : "true");
    json += ",\"serviceReady\":" + String(settings_.serviceReady() ? "true" : "false");
    json += ",\"connected\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false");
    json += ",\"setupMode\":" + String(setupMode_ ? "true" : "false");
    json += ",\"ip\":\"" + (WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : WiFi.softAPIP().toString()) + "\"}";
    server_.sendHeader("Cache-Control", "no-store");
    server_.send(200, "application/json", json);
}

void BuddyPortal::sendModels() {
    if (!authorize()) return;
    if (WiFi.status() != WL_CONNECTED) {
        server_.send(503, "application/json", "{\"error\":\"Connect PocketBuddy to Wi-Fi, save, and reopen this page to discover models.\"}");
        return;
    }
    HTTPClient http;
    http.setConnectTimeout(5000);
    http.setTimeout(8000);
    if (!http.begin(settings_.serviceUrl + "/v1/models")) {
        server_.send(502, "application/json", "{\"error\":\"Could not open the configured AI service.\"}");
        return;
    }
    if (!settings_.apiKey.isEmpty()) http.addHeader("Authorization", "Bearer " + settings_.apiKey);
    const int status = http.GET();
    const String body = http.getString();
    http.end();
    if (status != 200 || body.isEmpty() || body.length() > 16384) {
        server_.send(502, "application/json", "{\"error\":\"The AI service could not provide its model list.\"}");
        return;
    }
    server_.sendHeader("Cache-Control", "no-store");
    server_.send(200, "application/json", body);
}

void BuddyPortal::saveConfig() {
    if (!authorize()) return;
    String ssid = server_.arg("ssid");
    String serviceUrl = server_.arg("serviceUrl");
    String model = server_.arg("model");
    String buddyName = server_.arg("buddyName");
    String ownerName = server_.arg("ownerName");
    String personality = server_.arg("personality");
    String voice = server_.arg("voice");
    String deviceId = server_.arg("deviceId");
    ssid.trim(); serviceUrl.trim(); model.trim(); buddyName.trim(); ownerName.trim();
    personality.trim(); voice.trim(); deviceId.trim();
    while (serviceUrl.endsWith("/")) serviceUrl.remove(serviceUrl.length() - 1);
    const int volume = server_.arg("volume").toInt();
    const String wifiPassword = server_.arg("wifiPassword");
    const String apiKey = server_.arg("apiKey");

    if (!validText(ssid, 32) || !validHttpUrl(serviceUrl) || serviceUrl.length() > 160 ||
        !validText(model, 80) || !validText(buddyName, 32) || !validText(ownerName, 32) ||
        personality.isEmpty() || personality.length() > 600 || !validText(voice, 64) ||
        !validText(deviceId, 80) || volume < 0 || volume > 255 ||
        (!wifiPassword.isEmpty() && (wifiPassword.length() < 8 || wifiPassword.length() > 63)) ||
        apiKey.length() > 256) {
        server_.send(400, "application/json", "{\"ok\":false,\"error\":\"Check the field lengths and URL. Wi-Fi passwords need 8–63 characters.\"}");
        return;
    }

    settings_.wifiSsid = ssid;
    if (!wifiPassword.isEmpty()) settings_.wifiPassword = wifiPassword;
    settings_.serviceUrl = serviceUrl;
    if (!apiKey.isEmpty()) settings_.apiKey = apiKey;
    settings_.model = model;
    settings_.buddyName = buddyName;
    settings_.ownerName = ownerName;
    settings_.personality = personality;
    settings_.voice = voice;
    settings_.deviceId = deviceId;
    settings_.volume = static_cast<uint8_t>(volume);
    if (!settings_.wifiReady()) {
        server_.send(400, "application/json", "{\"ok\":false,\"error\":\"Wi-Fi SSID and password are required on first setup.\"}");
        return;
    }
    if (!settings_.save()) {
        server_.send(500, "application/json", "{\"ok\":false,\"error\":\"PocketBuddy could not write its device storage.\"}");
        return;
    }
    server_.send(200, "application/json", settings_.serviceReady()
        ? "{\"ok\":true,\"restarting\":true,\"serviceReady\":true}"
        : "{\"ok\":true,\"restarting\":true,\"serviceReady\":false}");
    restartAt_ = millis() + 900;
}

void BuddyPortal::configureRoutes() {
    server_.on("/", HTTP_GET, [this]() {
        if (!authorize()) return;
        server_.sendHeader("Cache-Control", "no-store");
        server_.sendHeader("X-Content-Type-Options", "nosniff");
        server_.sendHeader("Content-Security-Policy", "default-src 'self'; style-src 'unsafe-inline'; script-src 'unsafe-inline'; connect-src 'self'; base-uri 'none'; frame-ancestors 'none'");
        server_.send_P(200, "text/html; charset=utf-8", kPage);
    });
    server_.on("/api/config", HTTP_GET, [this]() { sendConfig(); });
    server_.on("/api/config", HTTP_POST, [this]() { saveConfig(); });
    server_.on("/api/models", HTTP_GET, [this]() { sendModels(); });
    server_.on("/generate_204", HTTP_GET, [this]() { server_.sendHeader("Location", "/"); server_.send(302); });
    server_.on("/hotspot-detect.html", HTTP_GET, [this]() { server_.sendHeader("Location", "/"); server_.send(302); });
    server_.onNotFound([this]() { server_.sendHeader("Location", "/"); server_.send(302); });
}
