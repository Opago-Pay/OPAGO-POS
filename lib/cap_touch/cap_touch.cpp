#include "cap_touch.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "config.h"
#include "Adafruit_MPR121.h"

// Global variables for key state
static char lastPressedKey = 0;
static bool isLongPress = false;
static unsigned long lastPressTime = 0;
static TaskHandle_t capTouchTaskHandle = NULL;
static int currentDebounceDelay = 210;  // Will be updated based on sensitivity
static int currentCapSensitivity = 10;  // Will be updated based on sensitivity
static SemaphoreHandle_t keyStateMutex = NULL;
static bool touchSuppressed = false;  // Flag to suppress touch during NFC operations
static bool rfSafeMode = false;      // Flag for RF-safe mode allowing only * and # with enhanced debouncing
static bool pinEntryMode = false;    // Flag for ultra-responsive PIN entry mode

// Rolling window data for stochastic analysis in RF-safe mode
static RollingWindowData rollingWindow;
static char rfSafeDetectedKey = 0;  // Result from rolling window analysis

// Non-blocking long press detection variables
static char longPressStartKey = 0;    // Key that started potential long press
static unsigned long longPressStartTime = 0;  // Time when potential long press started
static const unsigned long LONG_PRESS_DURATION = 300;  // 300ms for long press threshold

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
        
        // Log cap touch sampling when RF-safe mode is active or any touch detected
        if (currtouched > 0) {
            char detectedKeys[13] = {0}; // For up to 12 keys + null terminator
            int keyCount = 0;
            for (int i = 0; i < 12; i++) {
                if (currtouched & _BV(i)) {
                    detectedKeys[keyCount++] = button_values[i];
                }
            }
            if (keyCount > 0) {
                if (rfSafeMode) {
                    logger::write("[cap_touch] RF-safe sampling detected: " + std::string(detectedKeys) + 
                                  " (raw: 0x" + String(currtouched, HEX).c_str() + ")", "info");
                } else {
                    logger::write("[cap_touch] Normal sampling detected: " + std::string(detectedKeys) + 
                                  " (raw: 0x" + String(currtouched, HEX).c_str() + ")", "debug");
                }
            }
        }
        
                        for (int i = 0; i < 12; i++) {
            if (currtouched & _BV(i)) {
                char detectedKey = button_values[i];
                
                // Take mutex before updating shared variables
                if (xSemaphoreTake(keyStateMutex, portMAX_DELAY) == pdTRUE) {
                    if (rfSafeMode) {
                        // RF-safe mode: only process * and # with rolling window analysis
                        // NO DEBOUNCE - let rolling window accumulate samples continuously
                        if (detectedKey == '*' || detectedKey == '#') {
                            // Add sample to rolling window
                            rollingWindow.addSample(detectedKey, currentTime);
                            
                            // Analyze rolling window for decision
                            float keyProbability = rollingWindow.getKeyProbability(detectedKey, currentTime);
                            
                            // Get sample count for the target key in current window
                            int validSamples = 0;
                            int targetKeyCount = 0;
                            const unsigned long WINDOW_DURATION_MS = 300;
                            
                            for (int j = 0; j < (rollingWindow.windowFull ? RollingWindowData::WINDOW_SIZE : rollingWindow.currentIndex); j++) {
                                if (currentTime - rollingWindow.timeHistory[j] <= WINDOW_DURATION_MS) {
                                    validSamples++;
                                    if (rollingWindow.keyHistory[j] == detectedKey) {
                                        targetKeyCount++;
                                    }
                                }
                            }
                            
                            // Thresholds for decision making
                            const float INTENDED_PRESS_THRESHOLD = 0.15f;
                            const float STRONG_INTENTION_THRESHOLD = 0.60f;
                            const int MIN_SAMPLES_REQUIRED = 3;
                            
                            // Decision logic - RF-safe validation replaces debounce logic
                            if (validSamples >= MIN_SAMPLES_REQUIRED) {
                                if (keyProbability >= INTENDED_PRESS_THRESHOLD) {
                                    // Apply minimal debounce (50ms) only to prevent double-triggering
                                    if (currentTime - lastPressTime >= 50) {
                                        // Key press detected with sufficient confidence
                                        rfSafeDetectedKey = detectedKey;
                                        lastPressedKey = detectedKey;
                                        lastPressTime = currentTime;
                                        isLongPress = true; // RF-safe validation = intentional press
                                    
                                                                            // Clear the rolling window to prevent repeated detection
                                        rollingWindow.currentIndex = 0;
                                        rollingWindow.windowFull = false;
                                        
                                        if (keyProbability >= STRONG_INTENTION_THRESHOLD) {
                                            logger::write("[cap_touch] RF-safe STRONG intention: '" + std::string(1, detectedKey) + 
                                                          "' (prob: " + std::to_string(keyProbability * 100) + 
                                                          "%, samples: " + std::to_string(validSamples) + 
                                                          "/" + std::to_string(targetKeyCount) + ")", "info");
                                        } else {
                                            logger::write("[cap_touch] RF-safe key detected: '" + std::string(1, detectedKey) + 
                                                          "' (prob: " + std::to_string(keyProbability * 100) + 
                                                          "%, samples: " + std::to_string(validSamples) + 
                                                          "/" + std::to_string(targetKeyCount) + ")", "info");
                                        }
                                    } else {
                                        // Within debounce window - reject to prevent double-triggering
                                        logger::write("[cap_touch] RF-safe valid but within 50ms debounce window", "debug");
                                    }
                                } else {
                                    // Low confidence - likely RF interference
                                    logger::write("[cap_touch] RF interference rejected: '" + std::string(1, detectedKey) + 
                                                  "' (prob: " + std::to_string(keyProbability * 100) + 
                                                  "%, samples: " + std::to_string(validSamples) + 
                                                  "/" + std::to_string(targetKeyCount) + ")", "info");
                                }
                            } else if (validSamples < MIN_SAMPLES_REQUIRED) {
                                // Not enough samples yet - continue accumulating
                                logger::write("[cap_touch] RF-safe accumulating: '" + std::string(1, detectedKey) + 
                                              "' (samples: " + std::to_string(validSamples) + 
                                              "/" + std::to_string(MIN_SAMPLES_REQUIRED) + 
                                              ", prob: " + std::to_string(keyProbability * 100) + "%)", "debug");
                            }
                        }
                        // Ignore non-* and non-# keys in RF-safe mode
                    } else {
                        // Normal mode - apply traditional debounce with non-blocking long press detection
                        if (currentTime - lastPressTime >= currentDebounceDelay) {
                            lastPressedKey = detectedKey;
                            lastPressTime = currentTime;
                            isLongPress = false;
                            
                            // Start non-blocking long press timer for this key
                            longPressStartKey = detectedKey;
                            longPressStartTime = currentTime;
                        }
                    }
                    xSemaphoreGive(keyStateMutex);
                }
                break;  // Only process one button at a time
            }
        }
        
        // Non-blocking long press detection (only in normal mode)
        if (!rfSafeMode && longPressStartKey != 0) {
            if (xSemaphoreTake(keyStateMutex, portMAX_DELAY) == pdTRUE) {
                // Check if the same key is still being pressed after LONG_PRESS_DURATION
                if (currentTime - longPressStartTime >= LONG_PRESS_DURATION) {
                    // Check if the key is still being pressed
                    uint16_t currentTouched = cap.touched();
                    bool keyStillPressed = false;
                    
                    // Find the button index for the longPressStartKey
                    for (int i = 0; i < 12; i++) {
                        if (button_values[i] == longPressStartKey && (currentTouched & _BV(i))) {
                            keyStillPressed = true;
                            break;
                        }
                    }
                    
                    if (keyStillPressed && lastPressedKey == longPressStartKey) {
                        // Convert to long press
                        isLongPress = true;
                    }
                    
                    // Clear long press detection state
                    longPressStartKey = 0;
                    longPressStartTime = 0;
                } else if (lastPressedKey != longPressStartKey) {
                    // Different key was pressed, clear long press detection
                    longPressStartKey = 0;
                    longPressStartTime = 0;
                }
                xSemaphoreGive(keyStateMutex);
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(SAMPLE_PERIOD));
    }
}

std::string getTouch() {
    std::string result = "";
    if (xSemaphoreTake(keyStateMutex, portMAX_DELAY) == pdTRUE) {
        if (touchSuppressed) {
            // Complete touch suppression - no keys work
            xSemaphoreGive(keyStateMutex);
            return result;
        }
        
        // Log current mode status only in RF-safe mode
        if (rfSafeMode) {
            static unsigned long lastModeLog = 0;
            if (millis() - lastModeLog > 3000) { // Log every 3 seconds for better debugging
                logger::write("[cap_touch] Current mode - RF-safe: " + std::string(rfSafeMode ? "true" : "false") + 
                              ", suppressed: " + std::string(touchSuppressed ? "true" : "false") + 
                              ", lastKey: " + std::string(1, lastPressedKey ? lastPressedKey : '0'), "info");
                lastModeLog = millis();
            }
        }
        
        // Enhanced key reading with mode-specific logic
        if (lastPressedKey != 0) {
            bool shouldReturn = false;
            
            if (rfSafeMode) {
                // RF-safe mode: only return validated keys (treated as long presses)
                shouldReturn = isLongPress;
            } else {
                // Normal mode and PIN entry mode: return regular presses, not long presses
                // PIN entry gets responsiveness from CPU priority boost, not different logic
                shouldReturn = !isLongPress;
            }
            
            if (shouldReturn) {
                result = std::string(1, lastPressedKey);
                
                // Log only in RF-safe mode for reduced noise
                if (rfSafeMode) {
                    logger::write("[cap_touch] RF-safe key delivered to app: '" + result + 
                                  "' (validated via rolling window)", "info");
                }
                
                lastPressedKey = 0;  // Clear the key after reading
                isLongPress = false; // Reset long press state
            }
        } else if (rfSafeMode) {
            static unsigned long lastNoKeyLog = 0;
            if (millis() - lastNoKeyLog > 2000) { // Log every 2 seconds
                logger::write("[cap_touch] RF-safe mode active - no validated key available", "debug");
                lastNoKeyLog = millis();
            }
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

void setRFSafeMode(bool enable) {
    if (xSemaphoreTake(keyStateMutex, portMAX_DELAY) == pdTRUE) {
        rfSafeMode = enable;
        if (enable) {
            // Clear any pending touches when entering RF-safe mode
            lastPressedKey = 0;
            isLongPress = false;
            rfSafeDetectedKey = 0;
            
            // Reset rolling window for clean state
            rollingWindow.currentIndex = 0;
            rollingWindow.windowFull = false;
            logger::write("[cap_touch] RF-safe mode enabled with rolling stochastic window analysis", "info");
        } else {
            logger::write("[cap_touch] RF-safe mode disabled", "info");
        }
        xSemaphoreGive(keyStateMutex);
    }
}

void setPinEntryMode(bool enable) {
    if (xSemaphoreTake(keyStateMutex, portMAX_DELAY) == pdTRUE) {
        pinEntryMode = enable;
        if (enable) {
            // Clear any pending touches when entering PIN entry mode
            lastPressedKey = 0;
            isLongPress = false;
            logger::write("[cap_touch] PIN entry mode enabled - optimized CPU scheduling", "info");
            
            // CRITICAL: Boost App Task priority during PIN entry for maximum responsiveness
            extern TaskHandle_t appTaskHandle;
            if (appTaskHandle != NULL) {
                vTaskPrioritySet(appTaskHandle, configMAX_PRIORITIES - 2); // Highest priority except cap touch
                logger::write("[cap_touch] App Task priority boosted for PIN entry", "info");
            }
        } else {
            logger::write("[cap_touch] PIN entry mode disabled - restoring normal priorities", "info");
            
            // CRITICAL: Restore normal App Task priority
            extern TaskHandle_t appTaskHandle;
            if (appTaskHandle != NULL) {
                vTaskPrioritySet(appTaskHandle, 2); // Back to normal priority
                logger::write("[cap_touch] App Task priority restored to normal", "info");
            }
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