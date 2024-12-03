#include "opago_wifi.h"

// WiFi connection timeout 
const unsigned long WIFI_CONNECT_TIMEOUT = 10000;
unsigned long lastConnectAttemptTime = 0;

void logWiFiStatus(const char* message, const char* ssid = nullptr, int8_t rssi = 0) {
    String logMessage = "WiFi: ";
    logMessage += message;
    if (ssid) {
        logMessage += " SSID: ";
        logMessage += ssid;
    }
    if (rssi != 0) {
        logMessage += " RSSI: ";
        logMessage += String(rssi);
    }
    logger::write(logMessage.c_str(), "info");
}

bool isConnectedToWiFi() {
    bool isConnected = false;
    if(xSemaphoreTake(wifiSemaphore, portMAX_DELAY) == pdTRUE) {
        isConnected = WiFi.status() == WL_CONNECTED;
        xSemaphoreGive(wifiSemaphore);
    } else {
        logWiFiStatus("Failed to take wifi semaphore, aborting status check.");
    }
    return isConnected;
}

bool connectToWiFi(const char* ssid, const char* password) {
    if (xSemaphoreTake(wifiSemaphore, portMAX_DELAY) == pdTRUE) {
        WiFi.persistent(false);
        WiFi.mode(WIFI_STA);
        WiFi.setAutoReconnect(true);

        logWiFiStatus("Connecting to", ssid);
        WiFi.begin(ssid, password);

        unsigned long startAttemptTime = millis();

        while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < WIFI_CONNECT_TIMEOUT) {
            delay(100);
        }

        if (WiFi.status() == WL_CONNECTED) {
            logWiFiStatus("Connected to", ssid, WiFi.RSSI());
            xSemaphoreGive(wifiSemaphore);
            return true;
        } else {
            logWiFiStatus("Failed to connect to", ssid);
            xSemaphoreGive(wifiSemaphore);
            return false;
        }
    } else {
        logWiFiStatus("Failed to take WiFi semaphore");
        return false;
    }
}

void WiFiEventHandler(WiFiEvent_t event) {
    switch(event) {
        case SYSTEM_EVENT_STA_GOT_IP:
            logWiFiStatus("Got IP address", WiFi.SSID().c_str(), WiFi.RSSI());
            onlineStatus = true;
            break;
        case SYSTEM_EVENT_STA_DISCONNECTED:
            logWiFiStatus("Disconnected from", WiFi.SSID().c_str());
            onlineStatus = false;
            break;
        default:
            break;
    }
}

void WiFiTask(void* pvParameters) {
    std::string ssid = config::getString("wifiSSID");
    std::string pwd = config::getString("wifiPwd");

    if (ssid.empty()) {
        logWiFiStatus("No WiFi credentials available. Suspending WiFi task.");
        vTaskSuspend(NULL);
    }

    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.setMinSecurity(WIFI_AUTH_WEP);

    uint32_t hash = 0;
    for (char c : WiFi.macAddress()) {
        hash = hash * 31 + c;
    }
    String hostName = "OPAGO-POS-" + String(hash).substring(0, 6);
    WiFi.setHostname(hostName.c_str());

    WiFi.onEvent(WiFiEventHandler);
    unsigned long backoffTime = 1000;

    while (true) {
        if (offlineOnly) {
            Serial.println("Offline mode enabled. Disconnecting from WiFi...");
            WiFi.disconnect(true);
            onlineStatus = false;
            while (offlineOnly) {
                vTaskDelay(pdMS_TO_TICKS(1000));
            }
            Serial.println("Offline mode disabled. Attempting to connect...");
            backoffTime = 1000;  // Reset backoff time when exiting offline mode
        } else {
            unsigned long currentTime = millis();

            if (!onlineStatus && currentTime - lastConnectAttemptTime >= backoffTime) {
                lastConnectAttemptTime = currentTime;
                
                connectToWiFi(ssid.c_str(), pwd.c_str());
                if (WiFi.status() == WL_CONNECTED) {
                    onlineStatus = true;
                    backoffTime = 1000;  // Reset backoff time on successful connection
                } else {
                    // If we couldn't connect, increase backoff time
                    backoffTime = std::min(backoffTime * 2, (unsigned long)300000);  // Max 5 minutes
                }
            }

            if (WiFi.status() != WL_CONNECTED) {
                onlineStatus = false;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(1000));  // Check status every second
    }
}

String scanForSSIDs() {
    WiFi.scanNetworks(true);
    int n = WiFi.scanComplete();
    while (n == WIFI_SCAN_RUNNING) {
        delay(100);
        n = WiFi.scanComplete();
    }

    DynamicJsonDocument doc(1024);
    JsonArray ssidArray = doc.to<JsonArray>();

    for (int i = 0; i < n; ++i) {
        ssidArray.add(WiFi.SSID(i));
    }

    String result;
    serializeJson(ssidArray, result);

    WiFi.scanDelete();

    return result;
}

