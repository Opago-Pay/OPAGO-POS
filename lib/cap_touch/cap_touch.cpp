#include "cap_touch.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "config.h"

// Global variables for key state
static char lastPressedKey = 0;
static bool isLongPress = false;
static unsigned long lastPressTime = 0;
static TaskHandle_t capTouchTaskHandle = NULL;
static int currentDebounceDelay = 210;  // Will be updated based on sensitivity
static int currentCapSensitivity = 10;  // Will be updated based on sensitivity
static SemaphoreHandle_t keyStateMutex = NULL;
static bool touchSuppressed = false;  // Flag to suppress touch during NFC operations

void updateSensitivity(int sensitivityPercent) {
    // Convert sensitivity (1-100) to cap sensitivity (20-1)
    // Higher sensitivity (100) = lower cap threshold (1)
    // Lower sensitivity (1) = higher cap threshold (20)
    currentCapSensitivity = 20 - ((sensitivityPercent - 1) * 19) / 99;
    
    // Convert sensitivity (1-100) to debounce delay (420ms-120ms)
    // Higher sensitivity (100) = lower debounce delay (120ms)
    // Lower sensitivity (1) = higher debounce delay (420ms)
    currentDebounceDelay = 420 - ((sensitivityPercent - 1) * 300) / 99;
    
    // If NFC is enabled, ensure minimum sensitivity
    if (config::getBool("nfcEnabled")) {
        currentCapSensitivity = std::min(5, currentCapSensitivity);
    }
    
    // Update cap touch thresholds
    cap.setThresholds(currentCapSensitivity, currentCapSensitivity);
}

void capTouchTask(void* parameter) {
    const int SAMPLE_PERIOD = 10;  // Sample every 10ms
    char button_values[12] = {'1', '2', '3', '4', '5', '6', '7', '8', '9', '*', '0', '#'};
    
    while (true) {
        uint16_t currtouched = cap.touched();
        unsigned long currentTime = millis();
        
        // Only process if enough time has passed since last press
        if (currentTime - lastPressTime >= currentDebounceDelay) {
            for (int i = 0; i < 12; i++) {
                if (currtouched & _BV(i)) {
                    // Take mutex before updating shared variables
                    if (xSemaphoreTake(keyStateMutex, portMAX_DELAY) == pdTRUE) {
                        lastPressedKey = button_values[i];
                        lastPressTime = currentTime;
                        isLongPress = false;
                        xSemaphoreGive(keyStateMutex);
                    }
                    
                    // Check for long press
                    int consistentReadings = 0;
                    for (int j = 0; j < 20; j++) {  // Check for 200ms
                        if (cap.touched() & _BV(i)) {
                            consistentReadings++;
                        }
                        vTaskDelay(pdMS_TO_TICKS(10));
                    }
                    
                    // If it was a long press, update the state
                    if (consistentReadings >= 18) {  // 90% consistency for long press
                        if (xSemaphoreTake(keyStateMutex, portMAX_DELAY) == pdTRUE) {
                            isLongPress = true;
                            xSemaphoreGive(keyStateMutex);
                        }
                    }
                    break;  // Only process one button at a time
                }
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(SAMPLE_PERIOD));
    }
}

std::string getTouch() {
    std::string result = "";
    if (xSemaphoreTake(keyStateMutex, portMAX_DELAY) == pdTRUE) {
        if (!touchSuppressed && lastPressedKey != 0 && !isLongPress) {
            result = std::string(1, lastPressedKey);
            lastPressedKey = 0;  // Clear the key after reading
        }
        xSemaphoreGive(keyStateMutex);
    }
    return result;
}

bool getLongTouch(char targetButton, unsigned long longTouchTime) {
    bool result = false;
    if (xSemaphoreTake(keyStateMutex, portMAX_DELAY) == pdTRUE) {
        if (!touchSuppressed && lastPressedKey == targetButton && isLongPress) {
            result = true;
            lastPressedKey = 0;  // Clear the key after reading
            isLongPress = false;
        }
        xSemaphoreGive(keyStateMutex);
    }
    return result;
}

bool getDebouncedLongTouch(char targetButton, unsigned long longTouchTime) {
    // Ultra-aggressive debouncing for NFC interference - require 5 consecutive positive readings
    bool validLongPress = false;
    int consecutivePositiveReadings = 0;
    const int REQUIRED_CONSECUTIVE = 5;
    const int DEBOUNCE_INTERVAL = 100; // 100ms between readings (was 50ms)
    
    // First check if touch is suppressed
    if (xSemaphoreTake(keyStateMutex, portMAX_DELAY) == pdTRUE) {
        if (touchSuppressed) {
            xSemaphoreGive(keyStateMutex);
            return false; // Return immediately if touch is suppressed
        }
        xSemaphoreGive(keyStateMutex);
    }
    
    for (int i = 0; i < REQUIRED_CONSECUTIVE; i++) {
        bool currentReading = false;
        
        if (xSemaphoreTake(keyStateMutex, portMAX_DELAY) == pdTRUE) {
            if (!touchSuppressed && lastPressedKey == targetButton && isLongPress) {
                currentReading = true;
                // Don't clear the key yet - wait for all readings to confirm
            }
            xSemaphoreGive(keyStateMutex);
        }
        
        if (currentReading) {
            consecutivePositiveReadings++;
        } else {
            // Reset count if we get a negative reading
            consecutivePositiveReadings = 0;
            break;
        }
        
        // Wait between readings to avoid interference spikes
        if (i < REQUIRED_CONSECUTIVE - 1) {
            vTaskDelay(pdMS_TO_TICKS(DEBOUNCE_INTERVAL));
        }
    }
    
    // Only consider it valid if we got all consecutive readings
    if (consecutivePositiveReadings >= REQUIRED_CONSECUTIVE) {
        if (xSemaphoreTake(keyStateMutex, portMAX_DELAY) == pdTRUE) {
            if (lastPressedKey == targetButton && isLongPress) {
                validLongPress = true;
                lastPressedKey = 0;  // Clear the key after confirmed reading
                isLongPress = false;
            }
            xSemaphoreGive(keyStateMutex);
        }
    }
    
    return validLongPress;
}

void suppressTouchDuringNFC(bool suppress) {
    if (xSemaphoreTake(keyStateMutex, portMAX_DELAY) == pdTRUE) {
        touchSuppressed = suppress;
        if (suppress) {
            // Clear any pending touches when suppressing
            lastPressedKey = 0;
            isLongPress = false;
        }
        xSemaphoreGive(keyStateMutex);
    }
}

void cap_touch_init() {
    // Create mutex for protecting shared variables
    keyStateMutex = xSemaphoreCreateMutex();
    if (keyStateMutex == NULL) {
        Serial.println("Failed to create key state mutex");
        return;
    }

    // Set initial sensitivity
    updateSensitivity(config::getUnsignedInt("touchSensitivity"));
    
    // Create the cap touch monitoring task with highest priority on Core 1
    BaseType_t result = xTaskCreatePinnedToCore(
        capTouchTask,
        "CapTouchTask",
        4096,
        NULL,
        configMAX_PRIORITIES - 1,  // Highest priority below system tasks
        &capTouchTaskHandle,
        1  // Pin to Core 1 with the app task
    );
    
    if (result != pdPASS) {
        Serial.println("Failed to create cap touch task");
    } else {
        Serial.println("Cap touch task created successfully on Core 1");
    }
}