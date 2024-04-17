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
    Serial.println("Scanning for SSIDs...");
    DynamicJsonDocument doc(2048);
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

    vTaskDelay(pdMS_TO_TICKS(210));
    startWebServer();
}

void startWebServer() {
    if(!serverStarted) {
        // Suspend the WiFi task to prevent conflicts while the web server is running
        if(wifiTaskHandle != NULL) {
            vTaskSuspend(wifiTaskHandle);
            Serial.println("WiFi task suspended.");
        }

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
                          "<label for='wifiSSID' style='grid-column: 1; text-align: left;'>Primary WiFi SSID:</label>"
                          "<select id='wifiSSID' name='wifiSSID' style='grid-column: 2; width: 100%; background-color: #ffb100;'>"
                          "<option value=''>--select WIFI--</option>"
                          "</select>"
                          "<input type='text' id='wifiSSIDCustom' name='wifiSSIDCustom' placeholder='Or enter SSID' style='grid-column: 2; width: 100%;'>"
                          "<label for='wifiPwd' style='grid-column: 1; text-align: left;'>Primary WiFi Password:</label>"
                          "<input type='password' id='wifiPwd' name='wifiPwd' style='grid-column: 2; width: 100%;'>"
                          "<label for='wifiSSID2' style='grid-column: 1; text-align: left;'>Secondary WiFi SSID:</label>"
                          "<select id='wifiSSID2' name='wifiSSID2' style='grid-column: 2; width: 100%; background-color: #ffb100;'>"
                          "<option value=''>--select WIFI--</option>"
                          "</select>"
                          "<input type='text' id='wifiSSID2Custom' name='wifiSSID2Custom' placeholder='Or enter SSID' style='grid-column: 2; width: 100%;'>"
                          "<label for='wifiPwd2' style='grid-column: 1; text-align: left;'>Secondary WiFi Password:</label>"
                          "<input type='password' id='wifiPwd2' name='wifiPwd2' style='grid-column: 2; width: 100%;'>"
                          "<input type='submit' value='Save' style='grid-column: 1 / span 2; margin-top: 10px; background-color: #ffb100;'>"
                          "</form>"
                          "</div>"
                          "<script>const ssids = " + ssids + "; const selectPrimary = document.getElementById('wifiSSID'); const selectSecondary = document.getElementById('wifiSSID2'); ssids.forEach(ssid => { const optionPrimary = document.createElement('option'); optionPrimary.value = ssid; optionPrimary.text = ssid; selectPrimary.appendChild(optionPrimary); const optionSecondary = document.createElement('option'); optionSecondary.value = ssid; optionSecondary.text = ssid; selectSecondary.appendChild(optionSecondary); });</script>"
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
            String wifiSSID2 = request->arg("wifiSSID2Custom").isEmpty() ? request->arg("wifiSSID2") : request->arg("wifiSSID2Custom");
            if(wifiSSID != "" && wifiSSID != "--select WIFI--") { // Check if a valid primary SSID is selected or entered
                configurations["wifiSSID"] = wifiSSID.c_str();
                configurations["wifiPwd"] = request->arg("wifiPwd").c_str();
            }
            if(wifiSSID2 != "" && wifiSSID2 != "--select WIFI--") { // Check if a valid secondary SSID is selected or entered
                configurations["wifiSSID2"] = wifiSSID2.c_str();
                configurations["wifiPwd2"] = request->arg("wifiPwd2").c_str();
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

            // Resume the WiFi task 
            if(wifiTaskHandle != NULL) {
                vTaskResume(wifiTaskHandle);
                Serial.println("WiFi task resumed.");
            }
        });
    }
}

void WiFiTask(void* pvParameters) {
    std::string ssid = config::getString("wifiSSID");
    std::string pwd = config::getString("wifiPwd");
    std::string ssid2 = config::getString("wifiSSID2");
    std::string pwd2 = config::getString("wifiPwd2");

    // Set the Hostname 
    uint32_t hash = 0;
    for (int i = 0; i < WiFi.macAddress().length(); ++i) {
        hash = hash * 31 + WiFi.macAddress()[i];
    }
    String hostName = "OPAGO-POS-" + String(hash).substring(0, 6);
    WiFi.setHostname(hostName.c_str());

    // Trim leading and trailing whitespaces from SSID
    ssid.erase(0, ssid.find_first_not_of(" \t\n\r\f\v")); // Left trim
    ssid.erase(ssid.find_last_not_of(" \t\n\r\f\v") + 1); // Right trim
    ssid2.erase(0, ssid2.find_first_not_of(" \t\n\r\f\v")); // Left trim
    ssid2.erase(ssid2.find_last_not_of(" \t\n\r\f\v") + 1); // Right trim

    unsigned long backoffDelay; // Declare backoffDelay here for scope visibility
    const unsigned long maxBackoffDelay = 32000; // Maximum delay of 32 seconds

    auto connectToBestNetwork = [&]() {
        // Main loop for WiFi connection attempts
        do {
            backoffDelay = 1000; // Reset initial delay of 1 second for each connection attempt
            WiFi.disconnect(true);  // Reset the WiFi module
            vTaskDelay(pdMS_TO_TICKS(1000)); // Wait for a second after reset

            // To address memory exhaustion, free the memory allocated for previous scans before starting a new scan
            WiFi.scanDelete(); // Free memory used by previous scan

            // Scan for available networks to find the BSSID with the strongest signal
            int n = WiFi.scanNetworks();
            struct {
                String ssid;
                uint8_t bssid[6]; // Corrected to the actual size of a BSSID
                int rssi = -1000; // Start with a very low RSSI
                std::string pwd;
            } bestNetwork;

            for (int i = 0; i < n; ++i) {
                String currentSSID = WiFi.SSID(i);
                int currentRSSI = WiFi.RSSI(i);
                // Print all discovered BSSIDs and their signal strength
                Serial.print("Discovered SSID: ");
                Serial.print(currentSSID);
                Serial.print(" with RSSI: ");
                Serial.println(currentRSSI);

                // Convert std::string to String for comparison
                if ((currentSSID == String(ssid.c_str()) || currentSSID == String(ssid2.c_str())) && currentRSSI > bestNetwork.rssi) {
                    bestNetwork.ssid = currentSSID;
                    memcpy(bestNetwork.bssid, WiFi.BSSID(i), sizeof(bestNetwork.bssid));
                    bestNetwork.rssi = currentRSSI;
                    bestNetwork.pwd = (currentSSID == String(ssid.c_str())) ? pwd : pwd2;
                }
            }

            if (bestNetwork.rssi != -1000) { // A network was found
                Serial.print("Trying to connect to WiFi: ");
                Serial.println(bestNetwork.ssid);
                WiFi.begin(bestNetwork.ssid.c_str(), bestNetwork.pwd.c_str(), 0, bestNetwork.bssid);

                while (WiFi.status() != WL_CONNECTED && backoffDelay <= maxBackoffDelay) {
                    vTaskDelay(pdMS_TO_TICKS(backoffDelay));
                    backoffDelay = min(backoffDelay * 2, maxBackoffDelay); // Exponential backoff
                    Serial.print(".");
                }

                if (WiFi.status() == WL_CONNECTED) {
                    Serial.print("\nConnected to WiFi! IP Address: ");
                    Serial.println(WiFi.localIP());
                    onlineStatus = true;
                } else {
                    Serial.print("\nUnable to connect to WiFi. Status code: ");
                    Serial.print(WiFi.status());
                    Serial.println(")");
                    onlineStatus = false;
                }
            } else {
                Serial.println("No known SSIDs found. Please check the configuration.");
                onlineStatus = false;
            }
        } while (!onlineStatus); // Keep trying until connected
    };

    connectToBestNetwork(); // Initial connection attempt

    // Once connected, maintain the connection
    while (true) {
        if (WiFi.status() != WL_CONNECTED) {
            onlineStatus = false; // Update status to trigger reconnection
            Serial.println("Connection lost. Attempting to reconnect...");
            connectToBestNetwork(); // Attempt to reconnect
        }
        vTaskDelay(pdMS_TO_TICKS(10000)); // Check connection status every 10 seconds
    }
}
