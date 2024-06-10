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
    int n = WiFi.scanNetworks();
    DynamicJsonDocument doc(1024);
    JsonArray ssidArray = doc.to<JsonArray>();

    for (int i = 0; i < n; ++i) {
        ssidArray.add(WiFi.SSID(i));
    }

    String result;
    serializeJson(ssidArray, result);

    // Delete scan results after we are done with them
    WiFi.scanDelete();

    return result;
}

void startAccessPoint() {
    // Suspend the WiFi task to prevent conflicts while the AP is on
    if(wifiTaskHandle != NULL) {
        offlineOnly = true;
    }

    WiFi.mode(WIFI_AP);
    uint32_t hash = 0;
    for (int i = 0; i < WiFi.macAddress().length(); ++i) {
        hash = hash * 31 + WiFi.macAddress()[i];
    }
    String apName = "OPAGO-POS-" + String(hash).substring(0, 6);
    WiFi.softAP(apName.c_str());

    Serial.print("AP Mode Started. SSID: ");
    Serial.println(apName);

    dnsServer.start(53, "*", WiFi.softAPIP());
    Serial.println("DNS server started");

    startWebServer();
}

void stopAccessPoint() {
    // Stop the DNS server
    dnsServer.stop();
    Serial.println("DNS server stopped");

    // Stop the web server
    if (serverStarted) {
        server.end();
        serverStarted = false;
        Serial.println("Web server stopped.");
    }

    // Disconnect the soft AP
    WiFi.softAPdisconnect(true);
    Serial.println("AP Mode Stopped.");

    // Resume the WiFi task if it was suspended
    if(wifiTaskHandle != NULL) {
        offlineOnly = false;
        //vTaskResume(wifiTaskHandle);
        //Serial.println("WiFi task resumed.");
    }
}

void startWebServer() {
    if(!serverStarted) {
        server.begin(); // Start the web server
        serverStarted = true;
        Serial.println("Web server started.");

        // Captive portal detection for various devices
        // iOS
        server.on("/hotspot-detect.html", HTTP_GET, [](AsyncWebServerRequest *request){
            request->send(204, "text/plain");
        });
        // macOS
        server.on("/library/test/success.html", HTTP_GET, [](AsyncWebServerRequest *request){
            request->send(204, "text/plain");
        });
        // Android
        server.on("/generate_204", HTTP_GET, [](AsyncWebServerRequest *request){
            request->send(204, "text/plain");
        });
        // Windows
        server.on("/ncsi.txt", HTTP_GET, [](AsyncWebServerRequest *request){
            request->send(200, "text/plain", "Microsoft NCSI");
        });

        // Redirect all not found requests to the root page to handle captive portal functionality
        server.onNotFound([](AsyncWebServerRequest *request) {
            if (request->method() == HTTP_GET) {
                request->redirect("/");
            } else {
                request->send(404, "text/plain", "Not found");
            }
        });

        // Serve the configuration page
        server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
            String ssids = scanForSSIDs();
            String page = "<!DOCTYPE html><html><head><title>OPAGO POS Configuration</title><meta name='viewport' content='width=device-width, initial-scale=1, maximum-scale=1, user-scalable=no'><meta name='apple-mobile-web-app-capable' content='yes'></head><body style='display: flex; justify-content: center; align-items: center; height: 100vh; margin: 0;'>"
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

            configurations["contrastLevel"] = request->getParam("contrast", true)->value();
            configurations["logLevel"] = request->getParam("logLevel", true)->value();
            configurations["primarySSID"] = request->getParam("wifiSSID", true)->value();
            configurations["primaryPassword"] = request->getParam("wifiPwd", true)->value();
            configurations["secondarySSID"] = request->getParam("wifiSSID2", true)->value();
            configurations["secondaryPassword"] = request->getParam("wifiPwd2", true)->value();

            // Save configurations to file or EEPROM
            // saveConfigurations(configurations);

            // Restart the device to apply new configurations
            ESP.restart();
        });

        // Handle other routes and functionalities as needed
    }
}

void WiFiTask(void* pvParameters) {
    std::string ssid = config::getString("wifiSSID");
    std::string pwd = config::getString("wifiPwd");
    std::string ssid2 = config::getString("wifiSSID2");
    std::string pwd2 = config::getString("wifiPwd2");

    IPAddress ip(0, 0, 0, 0);
    IPAddress gateway(0, 0, 0, 0);
    IPAddress subnet(0, 0, 0, 0);
    WiFi.config(ip, gateway, subnet);

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

    // Check if both SSIDs are blank
    if (ssid.empty() && ssid2.empty()) {
        Serial.println("Both SSIDs are blank. Suspending WiFi task.");
        vTaskSuspend(NULL); // Suspend the task indefinitely
    }

    unsigned long backoffDelay = 1000; // Initial backoff delay of 1 second
    const unsigned long maxBackoffDelay = 60000; // Maximum delay of 60 seconds
    unsigned long connectionStartTime = 0; // Time when connection attempt started
    const unsigned long connectionTimeout = 600000; 

    auto connectToBestNetwork = [&]() {
        // Main loop for WiFi connection attempts
        do {
            WiFi.disconnect(true);  // Reset the WiFi module
            vTaskDelay(pdMS_TO_TICKS(1000)); // Wait for a second after reset

            // To address memory exhaustion, free the memory allocated for previous scans before starting a new scan
            WiFi.scanDelete(); // Free memory used by previous scan

            // Scan for available networks to find the BSSID with the strongest signal
            int n = WiFi.scanNetworks();
            struct {
                String ssid;
                uint8_t bssid[6]; 
                int rssi = -1000; 
                std::string pwd;
            } bestNetwork[2]; 

            for (int i = 0; i < n; ++i) {
                String currentSSID = WiFi.SSID(i);
                int currentRSSI = WiFi.RSSI(i);
                // Print all discovered BSSIDs and their signal strength
                Serial.print("Discovered SSID: ");
                Serial.print(currentSSID);
                Serial.print(" with RSSI: ");
                Serial.println(currentRSSI);

                // Convert std::string to String for comparison
                if (currentSSID == String(ssid.c_str()) && currentRSSI > bestNetwork[0].rssi) {
                    bestNetwork[0].ssid = currentSSID;
                    memcpy(bestNetwork[0].bssid, WiFi.BSSID(i), sizeof(bestNetwork[0].bssid));
                    bestNetwork[0].rssi = currentRSSI;
                    bestNetwork[0].pwd = pwd;
                } else if (currentSSID == String(ssid2.c_str()) && currentRSSI > bestNetwork[1].rssi) {
                    bestNetwork[1].ssid = currentSSID;
                    memcpy(bestNetwork[1].bssid, WiFi.BSSID(i), sizeof(bestNetwork[1].bssid));
                    bestNetwork[1].rssi = currentRSSI;
                    bestNetwork[1].pwd = pwd2;
                }
            }

            // Sort the bestNetwork array based on RSSI in descending order
            if (bestNetwork[0].rssi < bestNetwork[1].rssi) {
                std::swap(bestNetwork[0], bestNetwork[1]);
            }

            // Try to connect to the networks in the order of their signal strength
            for (int i = 0; i < 2; ++i) {
                if (bestNetwork[i].rssi != -1000) { // A network was found
                    Serial.print("Trying to connect to WiFi: ");
                    Serial.println(bestNetwork[i].ssid);
                    Serial.print("With RSSI: ");
                    Serial.println(bestNetwork[i].rssi);
                    
                    int connectionAttempts = 0;
                    while (WiFi.status() != WL_CONNECTED && connectionAttempts < 6) {
                        WiFi.begin(bestNetwork[i].ssid.c_str(), bestNetwork[i].pwd.c_str(), 0, bestNetwork[i].bssid);
                        
                        unsigned long startTime = millis();
                        while (WiFi.status() != WL_CONNECTED && millis() - startTime < backoffDelay) {
                            vTaskDelay(pdMS_TO_TICKS(100)); // Wait for 100ms before checking again
                        }
                        
                        if (WiFi.status() == WL_CONNECTED) {
                            Serial.print("\nConnected to WiFi! IP Address: ");
                            Serial.println(WiFi.localIP());
                            onlineStatus = true;
                            backoffDelay = 1000; // Reset backoff delay on successful connection
                            break; // Exit the loop if connected
                        } else {
                            Serial.print("\nUnable to connect to WiFi. Status code: ");
                            Serial.print(WiFi.status());
                            Serial.println(" (0: WL_IDLE_STATUS, 1: WL_NO_SSID_AVAIL, 2: WL_SCAN_COMPLETED, 3: WL_CONNECTED, 4: WL_CONNECT_FAILED, 5: WL_CONNECTION_LOST, 6: WL_DISCONNECTED)");
                            onlineStatus = false;
                            
                            // Check the specific error code and handle accordingly
                            if (WiFi.status() == WL_CONNECT_FAILED) {
                                Serial.println("Connection failed. Retrying with increased delay...");
                                backoffDelay = min(backoffDelay * 2, maxBackoffDelay); // Exponential backoff
                            }
                        }
                        
                        connectionAttempts++;
                    }
                    
                    if (onlineStatus) break; // Exit the loop if connected
                    
                    // If 6 attempts failed, rescan for best networks
                    if (connectionAttempts >= 6) {
                        Serial.println("6 connection attempts failed. Rescanning for best networks...");
                        break;
                    }
                }
            }

            if (!onlineStatus) {
                Serial.println("No known SSIDs found or unable to connect. Please check the configuration.");
                vTaskDelay(pdMS_TO_TICKS(backoffDelay)); // Wait for the backoff delay before retrying
            }

            // Check if connection timeout has been reached
            if (!onlineStatus && millis() - connectionStartTime > connectionTimeout) {
                Serial.println("Connection timeout reached. Checking once every minute.");
                vTaskDelay(pdMS_TO_TICKS(60000)); // Check once every minute
            }
        } while (!onlineStatus); // Keep trying until connected
    };

    connectionStartTime = millis(); // Start the connection timer

    // Main task loop
    while (true) {
        if (offlineOnly) {
            // If offlineOnly is true, idle and wait for it to turn false
            Serial.println("Offline mode enabled. Idling...");
            while (offlineOnly) {
                vTaskDelay(pdMS_TO_TICKS(420)); 
            }
            Serial.println("Offline mode disabled. Attempting to connect...");
        }

        // Attempt to connect to the best network
        connectToBestNetwork();

        // Once connected, maintain the connection
        while (!offlineOnly) {
            if (WiFi.status() != WL_CONNECTED) {
                onlineStatus = false; // Update status to trigger reconnection
                Serial.println("Connection lost. Attempting to reconnect...");
                connectionStartTime = millis(); // Reset the connection timer
                connectToBestNetwork(); // Attempt to reconnect
            }
            vTaskDelay(pdMS_TO_TICKS(10000)); // Check connection status every 10 seconds
        }
    }
}
