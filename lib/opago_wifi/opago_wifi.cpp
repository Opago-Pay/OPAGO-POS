#include "opago_wifi.h"

void connectToWiFi(const char* ssid, const char* password) {
    // Start connecting to WiFi
    if(xSemaphoreTake(wifiSemaphore, portMAX_DELAY) == pdTRUE) { // Try to take the wifi semaphore before accessing wifi
        WiFi.begin(ssid, password);

        Serial.print("Connecting to ");
        Serial.print(ssid); Serial.println(" ...");

        int i = 0;
        while (WiFi.status() != WL_CONNECTED) { // Wait for the Wi-Fi to connect
            if (i < 210) {
                delay(1000);
            } else if (i < 420) {
                delay(60000); // Delay of 1 minute after 210 tries
            } else {
                delay(90000); // Delay of 1.5 minutes after 420 tries
            }
            Serial.print(++i); Serial.print(' ');
            Serial.print("WiFi Status: "); Serial.println(WiFi.status());
            Serial.print("Signal strength: "); Serial.println(WiFi.RSSI());
        }

        Serial.println('\n');
        Serial.println("Connection established!");  
        Serial.print("IP address:\t");
        Serial.println(WiFi.localIP()); // Send the IP address of the ESP32 to the computer
        xSemaphoreGive(wifiSemaphore); // Release the wifi semaphore after accessing wifi
        taskYIELD();
    } else {
        Serial.println("Failed to take wifi semaphore, aborting connection attempt.");
        taskYIELD();
    }
}

bool isConnectedToWiFi() {
    bool isConnected = false;
    if(xSemaphoreTake(wifiSemaphore, portMAX_DELAY) == pdTRUE) { // Try to take the wifi semaphore before accessing wifi
        isConnected = WiFi.status() == WL_CONNECTED;
        xSemaphoreGive(wifiSemaphore); // Release the wifi semaphore after accessing wifi
    } else {
        Serial.println("Failed to take wifi semaphore, aborting status check.");
    }
    return isConnected;
}

String scanForSSIDs() {
    DynamicJsonDocument doc(1024);
    JsonArray ssids = doc.to<JsonArray>();

    int n = WiFi.scanNetworks();
    for (int i = 0; i < n; ++i) {
        ssids.add(WiFi.SSID(i));
    }

    String output;
    serializeJson(doc, output);
    return output;
}

void startAccessPoint() {
    WiFi.mode(WIFI_AP);
    uint32_t hash = 0;
    for (int i = 0; i < WiFi.macAddress().length(); ++i) {
        hash = hash * 31 + WiFi.macAddress()[i];
    }
    String apName = "OPAGO-POS-" + String(hash).substring(0, 6);
    WiFi.softAP(apName.c_str());

    Serial.print("AP Mode Started. SSID: ");
    Serial.println(apName);

    vTaskDelay(210);
    startWebServer();
}

void startWebServer() {
    if(!serverStarted) {
        dnsServer.start(53, "*", WiFi.softAPIP());
        server.begin(); // Start the web server
        serverStarted = true;
        Serial.println("Web server started.");

        server.onNotFound([](AsyncWebServerRequest *request) {
            request->redirect("/");
        });

        // Serve the configuration page
        server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
            String ssids = scanForSSIDs();
            String page = "<html><body style='display: flex; justify-content: center; align-items: center; height: 100vh; margin: 0;'>"
                          "<div style='width: 100%; max-width: 600px;'>"
                          "<h2 style='text-align: center;'>OPAGO POS Configuration</h2>"
                          "<form action='/saveConfig' method='POST' style='display: grid; grid-template-columns: 150px auto; gap: 10px;'>"
                          "<label for='contrast' style='grid-column: 1; text-align: left;'>Contrast:</label>"
                          "<input type='number' id='contrast' name='contrast' min='5' max='100' step='5' value='75' style='grid-column: 2; width: 100%;'>"
                          "<label for='logLevel' style='grid-column: 1; text-align: left;'>Log Level:</label>"
                          "<select id='logLevel' name='logLevel' style='grid-column: 2; width: 100%; background-color: #ffb100;'>"
                          "<option value='info'>Info</option>"
                          "<option value='debug'>Debug</option>"
                          "</select>"
                          "<label for='wifiSSID' style='grid-column: 1; text-align: left;'>WiFi SSID:</label>"
                          "<select id='wifiSSID' name='wifiSSID' style='grid-column: 2; width: 100%; background-color: #ffb100;'>"
                          "<option value=''>--select WIFI--</option>"
                          "</select>"
                          "<input type='text' id='wifiSSIDCustom' name='wifiSSIDCustom' placeholder='Or enter SSID' style='grid-column: 2; width: 100%;'>"
                          "<label for='wifiPwd' style='grid-column: 1; text-align: left;'>WiFi Password:</label>"
                          "<input type='password' id='wifiPwd' name='wifiPwd' style='grid-column: 2; width: 100%;'>"
                          "<input type='submit' value='Save' style='grid-column: 1 / span 2; margin-top: 10px; background-color: #ffb100;'>"
                          "</form>"
                          "</div>"
                          "<script>const ssids = " + ssids + "; const select = document.getElementById('wifiSSID'); ssids.forEach(ssid => { const option = document.createElement('option'); option.value = ssid; option.text = ssid; select.appendChild(option); });</script>"
                          "</body></html>";
            request->send(200, "text/html", page);
        });
        server.on("/saveConfig", HTTP_POST, [](AsyncWebServerRequest *request){
            request->send(200, "text/plain", "Configurations saved successfully");

            DynamicJsonDocument doc(1024);
            JsonObject configurations = doc.to<JsonObject>();

            configurations["contrastLevel"] = request->arg("contrast").c_str();
            configurations["logLevel"] = request->arg("logLevel").c_str();
            String wifiSSID = request->arg("wifiSSIDCustom").isEmpty() ? request->arg("wifiSSID") : request->arg("wifiSSIDCustom");
            if(wifiSSID != "" && wifiSSID != "--select WIFI--") { // Check if a valid SSID is selected or entered
                configurations["wifiSSID"] = wifiSSID.c_str();
                configurations["wifiPwd"] = request->arg("wifiPwd").c_str();
            }

            Serial.println("Saving configurations:");
            for (JsonPair kv : configurations) {
                Serial.printf("%s: %s\n", kv.key().c_str(), kv.value().as<String>().c_str());
            }

            if (config::saveConfigurations(configurations)) {
                Serial.println("Configurations saved successfully. Rebooting...");
                ESP.restart();
            } else {
                Serial.println("Failed to save configurations");
            }
        });
    }
}

void WiFiTask(void* pvParameters) {
    std::string ssid = config::getString("wifiSSID");
    std::string pwd = config::getString("wifiPwd");

    // Set the Hostname 
    uint32_t hash = 0;
    for (int i = 0; i < WiFi.macAddress().length(); ++i) {
        hash = hash * 31 + WiFi.macAddress()[i];
    }
    String hostName = "OPAGO-POS-" + String(hash).substring(0, 6);
    WiFi.setHostname(hostName.c_str());

    // Trim leading and trailing whitespaces from SSID
    const auto ssidStart = ssid.find_first_not_of(' ');
    const auto ssidEnd = ssid.find_last_not_of(' ');
    const auto ssidRange = ssidEnd - ssidStart + 1;
    if (ssidStart != std::string::npos) {
        ssid = ssid.substr(ssidStart, ssidRange);
    } else {
        ssid.clear(); // Clear ssid if it contains only whitespaces
    }

    int retryCount = 0;
    unsigned long delayTime = 1000; // Start with a 1 second delay

    while(true) {
        if(!ssid.empty()) {
            WiFi.disconnect(true);  // Reset the WiFi module
            delay(1000); // Wait for a second after reset
            WiFi.begin(ssid.c_str(), pwd.c_str());

            while (WiFi.status() != WL_CONNECTED) {
                delay(delayTime);
                Serial.print(".");
                retryCount++;
                if (retryCount % 5 == 0) {
                    delayTime = std::min(delayTime * 2, 60000UL); // Double the delay, up to 1 minute
                }
            }

            Serial.print("\nConnected to WiFi! IP Address: ");
            Serial.println(WiFi.localIP());
            onlineStatus = true;
            break; // Exit the loop once connected
        } else {
            Serial.println("SSID is empty or contains only whitespaces. Please check the configuration.");
            onlineStatus = false;
            break;
        }
    }
    vTaskSuspend(NULL);
}
