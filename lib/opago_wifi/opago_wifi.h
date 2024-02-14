#include <WiFi.h>
#include <ArduinoJson.h>
#include <AsyncTCP.h>
#include "config.h"
#include <ESPAsyncWebServer.h> 
#include <DNSServer.h>

extern bool onlineStatus;
extern SemaphoreHandle_t wifiSemaphore;
extern AsyncWebServer server;
extern bool serverStarted;
extern DNSServer dnsServer;

void connectToWiFi(const char* ssid, const char* password);
bool isConnectedToWiFi();
void startWebServer();
void startAccessPoint();
void WiFiTask(void* pvParameters);
