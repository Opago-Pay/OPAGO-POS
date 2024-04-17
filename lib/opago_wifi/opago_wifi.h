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
extern TaskHandle_t wifiTaskHandle;

void connectToWiFi(const char* ssid, const char* password);
bool isConnectedToWiFi();
void startWebServer();
void startAccessPoint();
String scanForSSIDs();
void WiFiTask(void* pvParameters);
