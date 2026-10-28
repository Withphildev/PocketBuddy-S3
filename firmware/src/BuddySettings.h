#pragma once

#include <Arduino.h>
#include <Preferences.h>

struct BuddySettings {
    String wifiSsid;
    String wifiPassword;
    String serviceUrl = "http://192.168.0.172:5810";
    String apiKey;
    String model = "gemma4:e4b";
    String buddyName = "Ember";
    String ownerName = "Phil";
    String personality = "A warm, curious little desktop companion who is concise, playful, and helpful.";
    String voice = "af_sky";
    String deviceId = "phil-sticks3";
    uint8_t volume = 255;

    bool wifiReady() const { return !wifiSsid.isEmpty() && !wifiPassword.isEmpty(); }
    bool serviceReady() const {
        return !serviceUrl.isEmpty() && !apiKey.isEmpty() && !model.isEmpty();
    }
    bool ready() const { return wifiReady() && serviceReady(); }

    void load(const char *defaultSsid, const char *defaultPassword,
              const char *defaultUrl, const char *defaultToken,
              const char *defaultDeviceId) {
        Preferences prefs;
        prefs.begin("buddy", true);
        wifiSsid = prefs.getString("ssid", defaultSsid);
        wifiPassword = prefs.getString("pass", defaultPassword);
        serviceUrl = prefs.getString("url", defaultUrl);
        apiKey = prefs.getString("token", defaultToken);
        model = prefs.getString("model", model);
        buddyName = prefs.getString("name", buddyName);
        ownerName = prefs.getString("owner", ownerName);
        personality = prefs.getString("bio", personality);
        voice = prefs.getString("voice", voice);
        deviceId = prefs.getString("device", defaultDeviceId);
        volume = static_cast<uint8_t>(prefs.getUChar("volume", volume));
        prefs.end();
        while (serviceUrl.endsWith("/")) serviceUrl.remove(serviceUrl.length() - 1);
    }

    bool save() const {
        Preferences prefs;
        if (!prefs.begin("buddy", false)) return false;
        bool ok = true;
        ok &= prefs.putString("ssid", wifiSsid) > 0;
        ok &= prefs.putString("pass", wifiPassword) > 0;
        ok &= prefs.putString("url", serviceUrl) > 0;
        if (apiKey.isEmpty()) prefs.remove("token");
        else ok &= prefs.putString("token", apiKey) > 0;
        ok &= prefs.putString("model", model) > 0;
        ok &= prefs.putString("name", buddyName) > 0;
        ok &= prefs.putString("owner", ownerName) > 0;
        ok &= prefs.putString("bio", personality) > 0;
        ok &= prefs.putString("voice", voice) > 0;
        ok &= prefs.putString("device", deviceId) > 0;
        ok &= prefs.putUChar("volume", volume) == sizeof(uint8_t);
        prefs.end();
        return ok;
    }

    void clear() {
        Preferences prefs;
        prefs.begin("buddy", false);
        prefs.clear();
        prefs.end();
    }
};
