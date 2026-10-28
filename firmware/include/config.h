#pragma once

// Local secrets override these safe build-time placeholders. The local file is
// ignored by Git; copy config.local.h.example to config.local.h before flashing.
#if __has_include("config.local.h")
#include "config.local.h"
#else
#define BUDDY_WIFI_SSID "CHANGE_ME"
#define BUDDY_WIFI_PASSWORD "CHANGE_ME"
#define BUDDY_BRIDGE_URL "http://192.168.0.172:5810"
#define BUDDY_TOKEN "CHANGE_ME"
#define BUDDY_DEVICE_ID "phil-sticks3"
#endif
