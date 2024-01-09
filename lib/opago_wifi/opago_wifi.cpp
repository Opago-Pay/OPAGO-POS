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

void startWebServer() {
    if(!serverStarted) {
        server.begin(); // Start the web server
        serverStarted = true;
        Serial.println("Web server started. You can view the full serial log output by dialing the IP address of this ESP32 via HTTP.");
    }
    // Handle client requests
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        String message = "";
        File file = SPIFFS.open("/opago.log", FILE_READ); // Open the log file
        if (file) {
            while(file.available()) { // While there is more to read
                message += file.readString(); // Append the log file content to the message
            }
            file.close();
        }
        request->send(200, "text/plain", message); // Send the full log file content as a plain text response
    });
    vTaskDelay(1); // Yield the task to allow other tasks to run
}

void WiFiTask(void* pvParameters) {
    std::string ssid = config::getString("wifiSSID");
    std::string pwd = config::getString("wifiPwd");
    int retryCount = 0;
    while(true) {
        if(!ssid.empty() && ssid.find_first_not_of(' ') != std::string::npos) {
            connectToWiFi(ssid.c_str(), pwd.c_str());
            if(WiFi.status() == WL_CONNECTED) {
                onlineStatus = true;
                retryCount = 0;
                break;
            }
        }
        else {
            onlineStatus = false;
            vTaskDelete(NULL);
        }
        retryCount++;
        if(retryCount == 21) {
            Serial.println("Failed to connect to WiFi after 21 attempts. Trying less often now.");
            vTaskDelay(60000 / portTICK_PERIOD_MS); // Delay for a minute
            retryCount = 0;
        }
    }
    vTaskSuspend(NULL); // pause this task once it's done
}

