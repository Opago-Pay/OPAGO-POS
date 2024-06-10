#include <WiFi.h>
#include <ArduinoJson.h>
#include <AsyncTCP.h>
#include "config.h"
#include <WebServer.h>
#include <ESPAsyncWebServer.h> 
#include <DNSServer.h>

extern bool onlineStatus;
extern SemaphoreHandle_t wifiSemaphore;
extern AsyncWebServer server;
extern bool serverStarted;
extern DNSServer dnsServer;
extern TaskHandle_t wifiTaskHandle;
extern bool offlineOnly;

void connectToWiFi(const char* ssid, const char* password);
bool isConnectedToWiFi();
void startWebServer();
void startAccessPoint();
void stopAccessPoint();
String scanForSSIDs();
void WiFiTask(void* pvParameters);
