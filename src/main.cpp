#include "FS.h"
#include "esp_task_wdt.h"
#include <SD.h>
#include "SPIFFS.h"
#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include "FreeRTOSConfig.h"
#include "ArduinoJson.h"
#include "opago_spiffs.h"
#include "logger.h"
#include "json-rpc.h"
#include "config.h"
#include "util.h"
#include "opago_wifi.h"
#include "power.h"
#include "screen.h"
#include "screen_tft.h"
#include "keypad.h"
#include "nfc.h"
#include "cap_touch.h"
#include "Adafruit_MPR121.h"
#include "app.h"

#define MPR_SDA   32
#define MPR_SCL   33
#define MPR_IRQ   35

//GLOBAL VARIABLES

//WEB Server
AsyncWebServer server(80); // Create a web server on port 80
bool serverStarted = false; // Declare serverStarted as a global variable
DNSServer dnsServer;

//TASK Handles
TaskHandle_t appTaskHandle = NULL;
TaskHandle_t nfcTaskHandle = NULL;
TaskHandle_t wifiTaskHandle = NULL;

//SLEEP MODE
unsigned int sleepModeDelay = 600000;
unsigned long lastActivityTime = 0;
bool isFakeSleeping = false;

//GROUP Handles
EventGroupHandle_t nfcEventGroup = xEventGroupCreate();
EventGroupHandle_t appEventGroup = xEventGroupCreate();

//WIFI TASK
bool onlineStatus = false;
bool connectionLoss = false; //indicates that the device lost connection during online payment
bool offlineMode = false; //indicate to the device that offline mode is on, so incorrect pin entry doesn't cause inconsistent states
bool offlineOnly = false; //flag for offline only mode
SemaphoreHandle_t wifiSemaphore = xSemaphoreCreateMutex();;

//TFT SCREEN
TFT_eSPI tft = TFT_eSPI();  
int rightMargin = 31; 
int screenWidth = 320 - rightMargin;
std::string currentScreen = "";

//CAP touch
Adafruit_MPR121 cap = Adafruit_MPR121();

//NFC variables
String lnurlwNFC = "";
bool initFlagNFC = false;

//PAYMENT variables
double amount = 0;
uint16_t amountCentsDivisor = 1;
unsigned short maxNumKeysPressed = 12;
std::string qrcodeData = "";
bool paymentisMade = false;

void initBoot() {
    //init the serial port
    Serial.begin(115200);
    esp_task_wdt_init(60, true); //increase watchdog timer

    //DANGER The following two lines may look innocent and superflous, but if you remove them, you will get an next to undebuggable display error.
    pinMode(2, OUTPUT);
    digitalWrite(2, HIGH);

    opago_spiffs::init();
    config::init();
    logger::init();
    logger::write(firmwareName + ": Firmware version = " + firmwareVersion + ", commit hash = " + firmwareCommitHash);
    logger::write(config::getConfigurationsAsString());
    power::init();
    jsonRpc::init();
    screen::init();
    keypad::init();
    const unsigned short fiatPrecision = config::getUnsignedShort("fiatPrecision");
    amountCentsDivisor = std::pow(10, fiatPrecision);
    if (fiatPrecision > 0) {
        maxNumKeysPressed--;
    }

    TJpgDec.setJpgScale(1);
    TJpgDec.setSwapBytes(true);

    //init Cap Touch
    Serial.println("MPR ...");
    Wire1.begin(MPR_SDA, MPR_SCL);
    scanDevices(&Wire1);
    Wire1.beginTransmission(0x5A);

    if (Wire1.endTransmission() == 0) {
        // Default address is 0x5A, if tied to 3.3V its 0x5B
        // If tied to SDA its 0x5C and if SCL then 0x5D
        if (!cap.begin(0x5A, &Wire1, 127, 127)) {
            Serial.println("MPR121 not found, check wiring?");
            while (1);
        }
        cap.setThresholds(3, 5); //configure sensitivity
        Serial.println("MPR121 found!");

    } else {
        Serial.println("Didn't find MPR121!");
        delay(1000);
    }

    //init WiFi
    logger::write("Initializing WiFi ...");
    if (xTaskCreate(WiFiTask, "Wifi Connect Task", 5000, NULL, 1, &wifiTaskHandle) != pdPASS) {
        Serial.println("Failed to create wifi task");
    }

}

void setup() {
    initBoot();
    if (nfcEventGroup == NULL) {
        Serial.println("Failed to create NFC event group");
    } else {
        xEventGroupClearBits(nfcEventGroup, (1 << 0) | (1 << 1)); // Clear power up bit and idle bit 1
        xEventGroupSetBits(nfcEventGroup, (1 << 2)); // Set long idle signal 2
        if(xTaskCreate(nfcTask, "NFC Task", 8000, NULL, 2, &nfcTaskHandle) != pdPASS) {
            Serial.println("Failed to create NFC task");
        }
    }
    
    if(xTaskCreate(appTask, "App Task", 8000, NULL, 2, &appTaskHandle) != pdPASS) {
        Serial.println("Failed to create App task");
    }
    vTaskSuspend(appTaskHandle); // Create the task in suspended state

    // Check if appEventGroup is correctly initialized
    if (appEventGroup == NULL) {
        Serial.println("Failed to create App event group");
        esp_restart();
    }

    vTaskResume(appTaskHandle); 
}

void loop() {
    if (serverStarted) {
        dnsServer.processNextRequest();
        vTaskDelay(pdMS_TO_TICKS(1)); 
    } else {
        vTaskDelay(pdMS_TO_TICKS(2100)); 
    }
}


extern "C" void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName) 
{
    Serial.printf("Stack overflow in task %s\n", pcTaskName);
    esp_restart();
}
