#pragma once

#include <DNSServer.h>
#include <WebServer.h>

#include "BuddySettings.h"

class BuddyPortal {
  public:
    explicit BuddyPortal(BuddySettings &settings) : settings_(settings) {}
    void begin(bool setupAccessPoint);
    void loop();
    bool active() const { return active_; }
    bool setupMode() const { return setupMode_; }
    const String &accessPointName() const { return apName_; }
    const String &adminPassword() const { return adminPassword_; }
    String url() const;

  private:
    BuddySettings &settings_;
    WebServer server_{80};
    DNSServer dns_;
    String apName_;
    String adminPassword_;
    bool active_ = false;
    bool setupMode_ = false;
    uint32_t restartAt_ = 0;

    bool authorize();
    void configureRoutes();
    void sendConfig();
    void sendModels();
    void saveConfig();
    static String jsonEscape(const String &value);
};
