#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <PocketSDK.h>
#include <WiFi.h>
#include <esp_heap_caps.h>

#include "BuddyPortal.h"
#include "BuddySettings.h"
#include "EyeAssets.h"
#include "config.h"

namespace {
constexpr uint32_t kSampleRate = 16000;
constexpr size_t kRecordSeconds = 4;
constexpr size_t kSampleCount = kSampleRate * kRecordSeconds;
// Arduino HTTPClient stores this value in 16 bits.
constexpr uint16_t kRequestTimeoutMs = 60000;
constexpr uint32_t kJobTimeoutMs = 15 * 60 * 1000;

enum class Mood { Offline, Calm, Listening, Thinking, Happy, Concerned };

#pragma pack(push, 1)
struct WavHeader {
    char riff[4] = {'R', 'I', 'F', 'F'};
    uint32_t fileSizeMinus8 = 0;
    char wave[4] = {'W', 'A', 'V', 'E'};
    char fmt[4] = {'f', 'm', 't', ' '};
    uint32_t fmtSize = 16;
    uint16_t format = 1;
    uint16_t channels = 1;
    uint32_t sampleRate = kSampleRate;
    uint32_t byteRate = kSampleRate * 2;
    uint16_t blockAlign = 2;
    uint16_t bitsPerSample = 16;
    char data[4] = {'d', 'a', 't', 'a'};
    uint32_t dataSize = 0;
};
#pragma pack(pop)

int16_t *recording = nullptr;
Mood mood = Mood::Offline;
String faceCaption;
bool blinkActive = false;
bool thinkingLeft = true;
uint32_t nextBlinkAt = 0;
uint32_t blinkEndsAt = 0;
String serialLine;
BuddySettings settings;
BuddyPortal portal(settings);

bool isPlaceholder(const char *value) {
    return !value || !value[0] || String(value) == "CHANGE_ME";
}

void loadRuntimeConfig() {
    settings.load(isPlaceholder(BUDDY_WIFI_SSID) ? "" : BUDDY_WIFI_SSID,
                  isPlaceholder(BUDDY_WIFI_PASSWORD) ? "" : BUDDY_WIFI_PASSWORD,
                  BUDDY_BRIDGE_URL,
                  isPlaceholder(BUDDY_TOKEN) ? "" : BUDDY_TOKEN,
                  BUDDY_DEVICE_ID);
}

bool wifiConfigured() { return settings.wifiReady(); }
bool serviceConfigured() { return settings.serviceReady(); }

void printSetupHelp() {
    Serial.println();
    Serial.println("PocketBuddy first-boot setup (115200 baud)");
    Serial.println("Enter one setting per line:");
    Serial.println("WIFI_SSID=your network name");
    Serial.println("WIFI_PASSWORD=your password");
    Serial.println("BRIDGE_URL=http://192.168.0.172:5810");
    Serial.println("BUDDY_TOKEN=the Dark Tower buddy token");
    Serial.println("MODEL=gemma4:e4b");
    Serial.println("BUDDY_NAME=Ember");
    Serial.println("OWNER_NAME=Phil");
    Serial.println("PERSONALITY=a short personality bio");
    Serial.println("VOICE=af_sky");
    Serial.println("VOLUME=255");
    Serial.println("DEVICE_ID=phil-sticks3   (optional)");
    Serial.println("Then enter SAVE. Enter STATUS to check or CLEAR to erase.");
    Serial.println("Secrets are never echoed back.");
}

void saveRuntimeConfig() {
    settings.save();
}

void handleSetupLine(String line) {
    line.trim();
    if (line.isEmpty()) return;
    const int equals = line.indexOf('=');
    if (equals > 0) {
        String key = line.substring(0, equals);
        String value = line.substring(equals + 1);
        key.trim();
        value.trim();
        if (key == "WIFI_SSID") settings.wifiSsid = value;
        else if (key == "WIFI_PASSWORD") settings.wifiPassword = value;
        else if (key == "BRIDGE_URL") {
            settings.serviceUrl = value;
            while (settings.serviceUrl.endsWith("/")) settings.serviceUrl.remove(settings.serviceUrl.length() - 1);
        } else if (key == "BUDDY_TOKEN") settings.apiKey = value;
        else if (key == "MODEL") settings.model = value;
        else if (key == "BUDDY_NAME") settings.buddyName = value;
        else if (key == "OWNER_NAME") settings.ownerName = value;
        else if (key == "PERSONALITY") settings.personality = value;
        else if (key == "VOICE") settings.voice = value;
        else if (key == "VOLUME") {
            const int volume = value.toInt();
            if (volume < 0 || volume > 255) { Serial.println("VOLUME must be 0–255."); return; }
            settings.volume = static_cast<uint8_t>(volume);
        } else if (key == "DEVICE_ID") settings.deviceId = value;
        else { Serial.println("Unknown setting. Enter HELP for commands."); return; }
        Serial.println(key + " accepted.");
        return;
    }
    line.toUpperCase();
    if (line == "HELP") printSetupHelp();
    else if (line == "STATUS") {
        Serial.printf("Wi-Fi SSID: %s\n", settings.wifiSsid.isEmpty() ? "missing" : settings.wifiSsid.c_str());
        Serial.printf("Wi-Fi password: %s\n", settings.wifiPassword.isEmpty() ? "missing" : "set");
        Serial.printf("Service URL: %s\n", settings.serviceUrl.isEmpty() ? "missing" : settings.serviceUrl.c_str());
        Serial.printf("API key: %s\n", settings.apiKey.isEmpty() ? "missing" : "set");
        Serial.printf("Model: %s\n", settings.model.c_str());
        Serial.printf("Buddy: %s, owner: %s, voice: %s, volume: %u\n",
                      settings.buddyName.c_str(), settings.ownerName.c_str(),
                      settings.voice.c_str(), settings.volume);
        Serial.printf("Device ID: %s\n", settings.deviceId.c_str());
        if (portal.active()) Serial.printf("WebUI: %s (admin / %s)\n", portal.url().c_str(), portal.adminPassword().c_str());
    } else if (line == "CLEAR") {
        settings.clear();
        settings = BuddySettings();
        loadRuntimeConfig();
        Serial.println("Saved setup erased.");
        printSetupHelp();
    } else if (line == "SAVE") {
        if (!wifiConfigured()) {
            Serial.println("Wi-Fi setup incomplete. Enter STATUS to see what is missing.");
            return;
        }
        if (!settings.save()) { Serial.println("Could not write device storage."); return; }
        Serial.println(serviceConfigured() ? "Setup saved. Restarting PocketBuddy..."
                                           : "Wi-Fi saved; AI setup is still incomplete. Restarting...");
        delay(500);
        ESP.restart();
    } else Serial.println("Unknown command. Enter HELP for commands.");
}

void processSerialSetup() {
    while (Serial.available()) {
        const char c = static_cast<char>(Serial.read());
        if (c == '\n' || c == '\r') {
            if (!serialLine.isEmpty()) {
                handleSetupLine(serialLine);
                serialLine = "";
            }
        } else if (serialLine.length() < 512) serialLine += c;
    }
}

void drawWrappedCaption(const String &caption) {
    if (caption.isEmpty()) return;
    auto &d = Pocket.display();
    constexpr size_t kMaxChars = 36;
    constexpr uint8_t kMaxLines = 3;
    String remaining = caption;
    remaining.trim();
    d.setTextDatum(top_left);
    d.setTextColor(TFT_WHITE, TFT_BLACK);
    d.setTextSize(1);
    for (uint8_t lineIndex = 0; lineIndex < kMaxLines && !remaining.isEmpty(); ++lineIndex) {
        String line;
        if (remaining.length() <= kMaxChars) {
            line = remaining;
            remaining = "";
        } else {
            int split = remaining.lastIndexOf(' ', kMaxChars);
            if (split < 8) split = kMaxChars;
            line = remaining.substring(0, split);
            remaining = remaining.substring(split);
            remaining.trim();
        }
        if (lineIndex == kMaxLines - 1 && !remaining.isEmpty()) {
            if (line.length() > kMaxChars - 3) line.remove(kMaxChars - 3);
            line += "...";
        }
        d.drawString(line, 10, 82 + lineIndex * 14);
    }
}

void renderFace(bool closedFrame = false) {
    auto &d = Pocket.display();
    const uint8_t *png = eyes::kCalm;
    size_t length = eyes::kCalmLen;
    if (closedFrame) {
        png = mood == Mood::Happy ? eyes::kHappyClosed : eyes::kBlink;
        length = mood == Mood::Happy ? eyes::kHappyClosedLen : eyes::kBlinkLen;
    } else if (mood == Mood::Listening) {
        png = eyes::kListening; length = eyes::kListeningLen;
    } else if (mood == Mood::Thinking) {
        png = thinkingLeft ? eyes::kThinkingLeft : eyes::kThinkingRight;
        length = thinkingLeft ? eyes::kThinkingLeftLen : eyes::kThinkingRightLen;
    } else if (mood == Mood::Happy) {
        png = eyes::kHappyOpen; length = eyes::kHappyOpenLen;
    } else if (mood == Mood::Concerned) {
        png = eyes::kConcerned; length = eyes::kConcernedLen;
    } else if (mood == Mood::Offline) {
        png = eyes::kOffline; length = eyes::kOfflineLen;
    }
    d.fillScreen(TFT_BLACK);
    d.drawPng(png, length, 0, 0);
    drawWrappedCaption(faceCaption);
}

void drawFace(Mood nextMood, const String &caption = "") {
    const bool enteringThinking = nextMood == Mood::Thinking && mood != Mood::Thinking;
    mood = nextMood;
    faceCaption = caption;
    blinkActive = false;
    if (enteringThinking) thinkingLeft = (esp_random() & 1U) == 0;
    renderFace();
    nextBlinkAt = millis() + 3500 + (esp_random() % 2500);
}

void updateFaceAnimation() {
    const uint32_t now = millis();
    if (mood != Mood::Calm && mood != Mood::Happy) return;
    if (!blinkActive && static_cast<int32_t>(now - nextBlinkAt) >= 0) {
        blinkActive = true;
        blinkEndsAt = now + 140;
        renderFace(true);
    } else if (blinkActive && static_cast<int32_t>(now - blinkEndsAt) >= 0) {
        blinkActive = false;
        nextBlinkAt = now + 3500 + (esp_random() % 2500);
        renderFace();
    }
}

void drawSetupQr() {
    auto &d = Pocket.display();
    const String payload = "WIFI:T:WPA;S:" + portal.accessPointName() +
                           ";P:" + portal.adminPassword() + ";;";
    d.fillScreen(TFT_WHITE);
    d.qrcode(payload.c_str(), 2, 2, 132, 1, false);
    d.setTextDatum(top_left);
    d.setTextColor(TFT_BLACK, TFT_WHITE);
    d.setTextSize(1);
    d.drawString("Scan to join", 140, 5);
    d.drawString(portal.accessPointName(), 140, 21);
    d.drawString("Then open", 140, 43);
    d.drawString("pocketbuddy", 140, 57);
    d.drawString("Login: admin", 140, 79);
    d.drawString("Password:", 140, 95);
    d.drawString(portal.adminPassword(), 140, 109);
}

bool connectWifi() {
    if (!wifiConfigured()) {
        drawFace(Mood::Offline, "USB setup required");
        return false;
    }
    drawFace(Mood::Thinking, "Finding Dark Tower...");
    WiFi.mode(WIFI_STA);
    WiFi.begin(settings.wifiSsid.c_str(), settings.wifiPassword.c_str());
    const uint32_t started = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - started < 20000) {
        Pocket.update();
        processSerialSetup();
        portal.loop();
        updateFaceAnimation();
        delay(25);
    }
    const bool connected = WiFi.status() == WL_CONNECTED;
    drawFace(connected ? Mood::Calm : Mood::Offline,
             connected ? "Face: talk  M5: hello" : "Wi-Fi unavailable");
    return connected;
}

void addAuth(HTTPClient &http) {
    http.addHeader("Authorization", String("Bearer ") + settings.apiKey);
}

String headerSafe(String value) {
    value.replace("\r", " ");
    value.replace("\n", " ");
    return value;
}

void addProfileHeaders(HTTPClient &http) {
    http.addHeader("X-Buddy-Name", headerSafe(settings.buddyName));
    http.addHeader("X-Buddy-Owner", headerSafe(settings.ownerName));
    http.addHeader("X-Buddy-Model", headerSafe(settings.model));
    http.addHeader("X-Buddy-Voice", headerSafe(settings.voice));
    http.addHeader("X-Buddy-Personality", headerSafe(settings.personality));
}

bool findPcm(const uint8_t *wav, size_t length, const int16_t *&pcm,
             size_t &sampleCount, uint32_t &sampleRate) {
    if (length < 44 || memcmp(wav, "RIFF", 4) || memcmp(wav + 8, "WAVE", 4)) return false;
    size_t offset = 12;
    uint16_t format = 0, channels = 0, bits = 0;
    const uint8_t *data = nullptr;
    uint32_t dataBytes = 0;
    while (offset + 8 <= length) {
        uint32_t chunkSize;
        memcpy(&chunkSize, wav + offset + 4, 4);
        if (offset + 8 + chunkSize > length) return false;
        if (!memcmp(wav + offset, "fmt ", 4) && chunkSize >= 16) {
            memcpy(&format, wav + offset + 8, 2);
            memcpy(&channels, wav + offset + 10, 2);
            memcpy(&sampleRate, wav + offset + 12, 4);
            memcpy(&bits, wav + offset + 22, 2);
        } else if (!memcmp(wav + offset, "data", 4)) {
            data = wav + offset + 8;
            dataBytes = chunkSize;
        }
        offset += 8 + chunkSize + (chunkSize & 1U);
    }
    if (format != 1 || channels != 1 || bits != 16 || !data) return false;
    pcm = reinterpret_cast<const int16_t *>(data);
    sampleCount = dataBytes / sizeof(int16_t);
    return true;
}

bool playReply(const String &audioPath) {
    if (audioPath.isEmpty()) {
        Serial.println("Reply contained no audio path.");
        return false;
    }
    HTTPClient http;
    http.setTimeout(kRequestTimeoutMs);
    http.begin(settings.serviceUrl + audioPath);
    addAuth(http);
    const int status = http.GET();
    const int length = http.getSize();
    Serial.printf("Audio download: HTTP %d, %d bytes\n", status, length);
    if (status != 200 || length <= 0 || length > 1024 * 1024) {
        http.end();
        return false;
    }

    auto *wav = static_cast<uint8_t *>(heap_caps_malloc(length, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!wav) { http.end(); Serial.println("Audio allocation failed."); return false; }
    const size_t received = http.getStreamPtr()->readBytes(wav, length);
    http.end();
    if (received != static_cast<size_t>(length)) {
        Serial.printf("Short audio read: %u of %d bytes\n", static_cast<unsigned>(received), length);
        free(wav);
        return false;
    }

    M5.Mic.end();
    delay(40);
    const bool speakerReady = M5.Speaker.begin();
    M5.Speaker.setVolume(settings.volume);
    const bool queued = speakerReady && M5.Speaker.playWav(wav, received, 1, -1, true);
    Serial.printf("Speaker begin=%s, WAV queued=%s\n",
                  speakerReady ? "yes" : "no", queued ? "yes" : "no");
    if (queued) {
        while (M5.Speaker.isPlaying()) { Pocket.update(); processSerialSetup(); portal.loop(); delay(2); }
        delay(40);
    }
    M5.Speaker.end();
    delay(40);
    M5.Mic.begin();
    free(wav);
    return queued;
}

void showReply(JsonVariantConst json) {
    const String text = json["text"] | "No reply";
    const String expression = json["emotion"] | "calm";
    Mood next = Mood::Calm;
    if (expression == "happy") next = Mood::Happy;
    if (expression == "thinking") next = Mood::Thinking;
    if (expression == "concerned") next = Mood::Concerned;
    drawFace(next, text);
    if (!playReply(json["audioPath"] | "")) Serial.println("Reply audio playback failed.");
}

bool pollJob(const String &statusPath) {
    const uint32_t started = millis();
    uint32_t polls = 0;
    while (millis() - started < kJobTimeoutMs) {
        HTTPClient http;
        http.setTimeout(10000);
        http.begin(settings.serviceUrl + statusPath);
        addAuth(http);
        const int status = http.GET();
        const String body = http.getString();
        http.end();
        if (status != 200) {
            drawFace(Mood::Concerned, "Job check error " + String(status));
            return false;
        }
        JsonDocument job;
        if (deserializeJson(job, body)) {
            drawFace(Mood::Concerned, "Invalid job status");
            return false;
        }
        const String state = job["state"] | "";
        if (state == "complete") {
            showReply(job["result"]);
            return true;
        }
        if (state == "error") {
            drawFace(Mood::Concerned, String(job["error"] | "Dark Tower job failed"));
            return false;
        }
        String dots = "Dark Tower is thinking";
        const uint8_t dotCount = (polls++ % 3) + 1;
        for (uint8_t i = 0; i < dotCount; ++i) dots += '.';
        drawFace(Mood::Thinking, dots);
        const uint32_t waitStarted = millis();
        while (millis() - waitStarted < 3000) { Pocket.update(); processSerialSetup(); portal.loop(); updateFaceAnimation(); delay(10); }
    }
    drawFace(Mood::Concerned, "Dark Tower timed out");
    return false;
}

bool postJson(const String &text) {
    if (!serviceConfigured()) { drawFace(Mood::Concerned, "Finish AI setup in the WebUI"); return false; }
    if (WiFi.status() != WL_CONNECTED && !connectWifi()) return false;
    HTTPClient http;
    http.setTimeout(kRequestTimeoutMs);
    http.begin(settings.serviceUrl + "/v1/jobs/chat");
    http.addHeader("Content-Type", "application/json");
    addAuth(http);
    JsonDocument request;
    request["deviceId"] = settings.deviceId;
    request["text"] = text;
    JsonObject profile = request["profile"].to<JsonObject>();
    profile["name"] = settings.buddyName;
    profile["owner"] = settings.ownerName;
    profile["personality"] = settings.personality;
    profile["model"] = settings.model;
    profile["voice"] = settings.voice;
    String payload;
    serializeJson(request, payload);
    drawFace(Mood::Thinking, "Dark Tower is thinking...");
    const int status = http.POST(payload);
    const String body = http.getString();
    http.end();
    if (status != 202) { drawFace(Mood::Concerned, "Bridge error " + String(status)); return false; }
    JsonDocument accepted;
    if (deserializeJson(accepted, body)) { drawFace(Mood::Concerned, "Invalid bridge reply"); return false; }
    return pollJob(accepted["statusPath"] | "");
}

bool recordAndTalk() {
    if (!serviceConfigured()) { drawFace(Mood::Concerned, "Finish AI setup in the WebUI"); return false; }
    if (WiFi.status() != WL_CONNECTED && !connectWifi()) return false;
    drawFace(Mood::Listening, "Listening for 4 seconds...");
    M5.Speaker.end();
    M5.Mic.begin();
    if (!M5.Mic.record(recording, kSampleCount, kSampleRate, false)) {
        drawFace(Mood::Concerned, "Microphone failed");
        return false;
    }
    while (M5.Mic.isRecording()) { Pocket.update(); processSerialSetup(); portal.loop(); delay(2); }
    M5.Mic.end();

    WavHeader header;
    header.dataSize = kSampleCount * sizeof(int16_t);
    header.fileSizeMinus8 = sizeof(WavHeader) + header.dataSize - 8;
    const size_t wavSize = sizeof(WavHeader) + header.dataSize;
    auto *wav = static_cast<uint8_t *>(heap_caps_malloc(wavSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!wav) { M5.Mic.begin(); drawFace(Mood::Concerned, "Out of memory"); return false; }
    memcpy(wav, &header, sizeof(header));
    memcpy(wav + sizeof(header), recording, header.dataSize);

    drawFace(Mood::Thinking, "Dark Tower is thinking...");
    HTTPClient http;
    http.setTimeout(kRequestTimeoutMs);
    http.begin(settings.serviceUrl + "/v1/jobs/talk");
    http.addHeader("Content-Type", "audio/wav");
    http.addHeader("X-Device-Id", settings.deviceId);
    addProfileHeaders(http);
    addAuth(http);
    const int status = http.POST(wav, wavSize);
    free(wav);
    const String body = http.getString();
    http.end();
    M5.Mic.begin();
    if (status != 202) {
        drawFace(Mood::Concerned, status == 502 ? "Speech service not ready" : "Bridge error " + String(status));
        return false;
    }
    JsonDocument accepted;
    if (deserializeJson(accepted, body)) { drawFace(Mood::Concerned, "Invalid bridge reply"); return false; }
    return pollJob(accepted["statusPath"] | "");
}
}  // namespace

void setup() {
    pocketsdk::Config config;
    config.internalMicrophone = true;
    config.internalSpeaker = true;
    config.rotation = 3;
    config.brightness = 80;
    Pocket.begin(config);
    Serial.begin(115200);
    loadRuntimeConfig();
    recording = static_cast<int16_t *>(heap_caps_malloc(
        kSampleCount * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    M5.Speaker.end();
    M5.Mic.begin();
    if (!recording) drawFace(Mood::Concerned, "PSRAM allocation failed");
    else if (!wifiConfigured()) {
        printSetupHelp();
        portal.begin(true);
        drawSetupQr();
    } else {
        const bool connected = connectWifi();
        portal.begin(!connected);
        if (connected) drawFace(serviceConfigured() ? Mood::Calm : Mood::Concerned,
                                (serviceConfigured() ? "Face: talk  M5: hello  WebUI: "
                                                     : "Finish AI setup: WebUI ") + WiFi.localIP().toString());
        else drawSetupQr();
    }
}

void loop() {
    Pocket.update();
    processSerialSetup();
    portal.loop();
    updateFaceAnimation();
    if (!recording) { delay(20); return; }
    if (Pocket.buttons().pressed(pocketsdk::Button::Face)) recordAndTalk();
    if (Pocket.buttons().pressed(pocketsdk::Button::M5)) postJson("Hello! Please give me a short greeting.");
    delay(4);
}
