#ifndef OPAGO_WIFI_H
#define OPAGO_WIFI_H

#include <WiFi.h>
#include <ArduinoJson.h>
#include <vector>
#include <algorithm>
#include "config.h"
#include "logger.h"

extern bool onlineStatus;
extern SemaphoreHandle_t wifiSemaphore;
extern TaskHandle_t wifiTaskHandle;
extern bool offlineOnly;

// Structure to hold network information
struct NetworkInfo {
    String ssid;
    uint8_t bssid[6];
    int32_t rssi;
    std::string password;
};

bool isConnectedToWiFi();
void WiFiEventHandler(WiFiEvent_t event);
String scanForSSIDs();
void WiFiTask(void* pvParameters);

#endif // OPAGO_WIFI_H