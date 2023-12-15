#include <WiFi.h>
#include "config.h"
#include <ESPAsyncWebServer.h> 

extern bool onlineStatus;
extern SemaphoreHandle_t wifiSemaphore;
extern AsyncWebServer server;
extern bool serverStarted;

void connectToWiFi(const char* ssid, const char* password);
bool isConnectedToWiFi();
void WiFiTask(void* pvParameters);
