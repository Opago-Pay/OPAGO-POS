#include "app.h"

std::string pin = "";
std::string keysBuffer = "";
const std::string keyBufferCharList = "0123456789";
bool correctPin = false;
std::string pinBuffer = "";
int incorrectPinAttempts = 0;

const std::string keyPressed;

static unsigned long lastKeyAddedTime = 0;
const unsigned long KEY_ADD_DELAY = 210; // Rate limit in milliseconds

// Payment flow state variables
PaymentState currentPaymentState = PaymentState::SHOWING_QR;
std::string currentPaymentLNURL = "";
std::string currentPaymentPin = "";
std::string apiReturnedPin = "";  // PIN returned from API for verification
bool isInPaymentFlow = false;

void appendToKeyBuffer(const std::string &key) {
	unsigned long currentTime = millis();
	if (currentTime - lastKeyAddedTime >= KEY_ADD_DELAY) {
		if (keyBufferCharList.find(key) != std::string::npos) {
			keysBuffer += key;
			lastKeyAddedTime = currentTime;
		}
	}
}

std::string leftTrimZeros(const std::string &keys) {
	return std::string(keys).erase(0, std::min(keys.find_first_not_of('0'), keys.size() - 1));
}

double keysToAmount(const std::string &t_keys) {
	if (t_keys == "") {
		return 0;
	}
	const std::string trimmed = leftTrimZeros(t_keys);
	double amount = std::stod(trimmed.c_str());
	if (amountCentsDivisor > 1) {
		amount = amount / amountCentsDivisor;
	}
	return amount;
}

void handleSleepMode() {
	if (sleepModeDelay > 0) {
		if (millis() - lastActivityTime > sleepModeDelay) {
			if (!isFakeSleeping) {
				// The battery does not charge while in deep sleep mode.
				// So let's just turn off the screen instead.
				screen::sleep();
				isFakeSleeping = true;
			}
			if (!power::isUSBPowered()) {
				cache::init();
				cache::save("pin", pin);
				cache::save("keysBuffer", keysBuffer);
				cache::save("qrcodeData", qrcodeData);
				cache::save("lastScreen", screen::getCurrentScreen());
				cache::end();
				//power::sleep();
			}
		} else if (isFakeSleeping) {
			screen::wakeup();
			const std::string lastScreen = screen::getCurrentScreen();
			if (lastScreen == "home") {
				screen::showHomeScreen();
			} else if (lastScreen == "enterAmount") {
				screen::showEnterAmountScreen(keysToAmount(keysBuffer));
			} else if (lastScreen == "paymentQRCode") {
				screen::showPaymentQRCodeScreen(qrcodeData);
			} else if (lastScreen == "paymentPin") {
				screen::showPaymentPinScreen(pin);
			}
			isFakeSleeping = false;
		}
	}
}

void handlePaymentFlow() {
    if (!isInPaymentFlow) {
        return;
    }
    
    PaymentState newState = checkPaymentStatus(currentPaymentLNURL, currentPaymentPin);
    
    if (newState != currentPaymentState) {
        currentPaymentState = newState;
        
        switch (currentPaymentState) {
            case PaymentState::PAYMENT_SUCCESS:
                logger::write("[app] Payment successful", "info");
                screen::showSuccess();
                cleanupPaymentFlow();
                isInPaymentFlow = false;
                // Will be handled in main loop to return to amount entry
                break;
                
            case PaymentState::SHOWING_QR:
                if (!onlineStatus) {
                    screen::showNowifi();
                } else {
                    screen::showPaymentQRCodeScreen(currentPaymentLNURL);
                }
                break;
                
            case PaymentState::MONITORING_PAYMENT:
                screen::showPaymentQRCodeScreen(currentPaymentLNURL);
                break;
                
            case PaymentState::PAYMENT_CANCELLED:
                logger::write("[app] Payment timeout - invoice expired after 60 minutes", "info");
                screen::showX();
                cleanupPaymentFlow();
                isInPaymentFlow = false;
                // Will be handled in main loop to return to amount entry
                break;
                
            case PaymentState::ERROR:
                logger::write("[app] Payment flow error", "error");
                screen::showX();
                cleanupPaymentFlow();
                isInPaymentFlow = false;
                break;
                
            default:
                break;
        }
    }
}

void appTask(void* pvParameters) {
    Serial.println("App task started");
    static unsigned long lastPopTime = 0;
    static int popCount = 0;
    int signal;
    
    while(1) {
        const std::string currentScreen = screen::getCurrentScreen();
        
        // OPTIMIZED: Ultra-lightweight loop during PIN entry for maximum responsiveness
        if (currentScreen == "paymentPin") {
            // Minimal processing during PIN entry - only essential tasks
            
            // Essential power management (lightweight)
            power::loop();
            
            // Skip heavy tasks during PIN entry for maximum responsiveness:
            // - Skip logger::loop() - only critical for background logging
            // - Skip jsonRpc operations - not needed during PIN input
            // - Skip handlePaymentFlow() - not relevant during PIN entry
            
            // Process PIN entry input immediately
            std::string keyPressed = getTouch();
            if (!keyPressed.empty()) {
                // PIN entry key processing is handled below in the main switch
            }
            
            // Ultra-fast yielding for maximum responsiveness
            vTaskDelay(pdMS_TO_TICKS(1)); // Minimal 1ms delay for ultra-responsiveness
        } else {
            // Normal processing for all other screens
            power::loop();
            //handleSleepMode();
            if (!jsonRpc::hasPinConflict() || !jsonRpc::inUse()) {
                logger::loop();
                jsonRpc::loop();
            }
            
            // Handle payment flow if active
            handlePaymentFlow();
            
            if (currentScreen == "") {
                keysBuffer = "";
                screen::showEnterAmountScreen(keysToAmount(keysBuffer));
            }
        }
        
        // Handle successful payment completion
        if (currentScreen == "success" && !isInPaymentFlow) {
            TickType_t startTime = xTaskGetTickCount();
            bool skipWait = false;
            while ((xTaskGetTickCount() - startTime) < pdMS_TO_TICKS(4200) && !skipWait) {
                if (getTouch() == "*") {
                    skipWait = true;
                }
                vTaskDelay(pdMS_TO_TICKS(50));
            }
            
            // CRITICAL: Ensure RF is turned off before returning to amount entry
            logger::write("[app] Payment success - ensuring RF is turned off before returning to amount entry", "info");
            if (config::getBool("nfcEnabled") && nfcTaskHandle != NULL) {
                logger::write("[app] Initiating RF shutdown sequence - trying for 10 seconds", "info");
                
                bool rfShutdownSuccess = false;
                unsigned long shutdownStartTime = millis();
                const unsigned long SHUTDOWN_TIMEOUT_MS = 10000; // 10 seconds
                
                while (!rfShutdownSuccess && (millis() - shutdownStartTime) < SHUTDOWN_TIMEOUT_MS) {
                    // Send shutdown signals
                    xEventGroupClearBits(nfcEventGroup, (1 << 0));
                    xEventGroupSetBits(nfcEventGroup, (1 << 1));
                    
                    // Wait for confirmation with shorter timeout for retry
                    EventBits_t uxBits = xEventGroupWaitBits(appEventGroup, (1 << 1), pdFALSE, pdFALSE, pdMS_TO_TICKS(500));
                    if ((uxBits & (1 << 1)) != 0) {
                        logger::write("[app] RF shutdown confirmed", "info");
                        vTaskSuspend(nfcTaskHandle);
                        rfShutdownSuccess = true;
                    } else {
                        logger::write("[app] RF shutdown attempt failed, retrying...", "warning");
                        vTaskDelay(pdMS_TO_TICKS(100)); // Brief delay before retry
                    }
                }
                
                if (!rfShutdownSuccess) {
                    logger::write("[app] CRITICAL: RF shutdown failed after 10 seconds - REBOOTING DEVICE", "error");
                    screen::showX(); // Show error briefly
                    vTaskDelay(pdMS_TO_TICKS(2000)); // Show error for 2 seconds
                    esp_restart(); // Force reboot to ensure clean state
                    return; // This won't execute but for safety
                }
                
                logger::write("[app] RF confirmed off - safe to return to amount entry", "info");
            }
            
            keysBuffer = "";  // Reset buffer
            amount = 0;       // Reset amount
            screen::showEnterAmountScreen(0);
        }
        const std::string keyPressed = keypad::getPressedKey();
        if (keyPressed != "") {
            logger::write("Key pressed: " + keyPressed, "debug");
            lastActivityTime = millis();
        }
        if (currentScreen == "home") {
            if (keyPressed == "") {
                keysBuffer = "";
                screen::showEnterAmountScreen(keysToAmount(keysBuffer));
            } else if (keyPressed == "0") {
                keysBuffer = "";
                screen::showEnterAmountScreen(keysToAmount(keysBuffer));
            } else if (keyPressed == "*") {
                keysBuffer = "";
                screen::showEnterAmountScreen(keysToAmount(keysBuffer));
            } else if (keyPressed == "#") {
                screen::showEnterAmountScreen(keysToAmount(keysBuffer));
            } else {
                if (keyPressed != "0" || keysBuffer != "") {
                    appendToKeyBuffer(keyPressed);
                }
                screen::showEnterAmountScreen(keysToAmount(keysBuffer));
            }
        } else if (currentScreen == "enterAmount") {
            if (keyPressed == "") {
                // Do nothing.
            } else if (keyPressed == "*") {
                unsigned long currentTime = millis();

                if (!keysBuffer.empty()) {
                    keysBuffer.pop_back(); // Remove the last digit from the buffer
                    popCount++;
                }

                if (currentTime - lastPopTime > 1000) {
                    popCount = 0; 
                }

                if (popCount > 1) { 
                    keysBuffer.clear(); 
                    popCount = 0; 
                }

                lastPopTime = currentTime;
                screen::showEnterAmountScreen(keysToAmount(keysBuffer));
            } else if (keyPressed == "#") {
                amount = keysToAmount(keysBuffer);
                if (amount > 0) {
                    // CRITICAL: Explicitly reset payment state before starting new payment
                    extern bool paymentisMade;
                    paymentisMade = false;
                    logger::write("[app] Explicitly reset paymentisMade to false before new payment", "info");
                    
                    // Initialize payment flow
                    qrcodeData = "";
                    pin = util::generateRandomPin();
                    if (config::getString("fiatCurrency") == "sat") {
                        Serial.println("Dividing");
                        amount = amount / 100;
                    }

                    // Start new payment flow v3.0.0
                    logger::write("Starting payment flow v3.0.0", "info");
                    currentPaymentState = initializePaymentFlow(amount, pin, currentPaymentLNURL);
                    currentPaymentPin = pin;
                    isInPaymentFlow = true;
                    
                    // Show initial payment screen
                    if (currentPaymentState == PaymentState::SHOWING_QR) {
                        screen::showPaymentQRCodeScreen(currentPaymentLNURL);
                    } else if (currentPaymentState == PaymentState::MONITORING_PAYMENT) {
                        screen::showPaymentQRCodeScreen(currentPaymentLNURL);
                    } else if (currentPaymentState == PaymentState::ERROR) {
                        screen::showX();
                        isInPaymentFlow = false;
                        keysBuffer = "";  // Reset buffer
                        amount = 0;       // Reset amount
                        screen::showEnterAmountScreen(0);
                    }
                } else {
                    // Show menu when amount is 0 and # is pressed
                    screen::showMenu();
                }
            } else if (keysBuffer.size() < maxNumKeysPressed) {
                unsigned long currentTime = millis();
                if (currentTime - lastKeyAddedTime >= KEY_ADD_DELAY) {
                    if (keyPressed != "0" || keysBuffer != "") {
                        appendToKeyBuffer(keyPressed);
                        logger::write("keysBuffer = " + keysBuffer);
                        screen::showEnterAmountScreen(keysToAmount(keysBuffer));
                    }
                }
            }
        } else if (currentScreen == "paymentQRCode") {
            if (keyPressed == "#") {
                if (isInPaymentFlow) {
                    // Switch to PIN entry mode during payment flow - need to shut down NFC for full keyboard access
                    logger::write("[app] Switching to PIN entry mode - shutting down NFC but keeping payment flow active", "info");
                    
                    // CRITICAL: Shut down NFC and restore full keyboard functionality for PIN entry
                    if (config::getBool("nfcEnabled") && nfcTaskHandle != NULL) {
                        logger::write("[app] Shutting down NFC for PIN entry mode", "info");
                        
                        // Immediately restore keyboard functionality
                        setRFSafeMode(false);
                        suppressTouchDuringNFC(false);
                        logger::write("[app] RF-safe mode disabled and touch restored immediately", "info");
                        
                        // Shut down NFC task with robust sequence
                        xEventGroupClearBits(nfcEventGroup, (1 << 0));
                        xEventGroupSetBits(nfcEventGroup, (1 << 1));
                        
                        // Wait for NFC shutdown with timeout
                        EventBits_t uxBits = xEventGroupWaitBits(appEventGroup, (1 << 1), pdFALSE, pdFALSE, pdMS_TO_TICKS(3000));
                        if ((uxBits & (1 << 1)) != 0) {
                            logger::write("[app] NFC shutdown confirmed for PIN entry", "info");
                            vTaskSuspend(nfcTaskHandle);
                        } else {
                            logger::write("[app] NFC shutdown timeout for PIN entry - forcing suspend", "warning");
                            vTaskSuspend(nfcTaskHandle);
                        }
                        
                        // Aggressively restore normal touch sensitivity and clear all interference
                        cap.setThresholds(2, 4); // Even more sensitive for PIN entry
                        vTaskDelay(pdMS_TO_TICKS(100)); // Allow settings to take effect
                        
                        // Clear all event groups to prevent interference
                        xEventGroupClearBits(appEventGroup, 0xFF);
                        xEventGroupClearBits(nfcEventGroup, 0xFF);
                        
                        logger::write("[app] Full keyboard optimized for PIN entry - enhanced sensitivity (2,4)", "info");
                    } else {
                        // Even if NFC disabled, optimize for PIN entry
                        cap.setThresholds(2, 4);
                        vTaskDelay(pdMS_TO_TICKS(50));
                        logger::write("[app] Touch sensitivity optimized for PIN entry", "info");
                    }
                    
                    // CRITICAL: Only transition to PIN screen AFTER NFC is fully shutdown
                    pinBuffer = "";
                    vTaskDelay(pdMS_TO_TICKS(200)); // Brief delay to ensure all shutdown is complete
                    
                    // Enable PIN entry mode for optimized responsiveness
                    setPinEntryMode(true);
                    
                    screen::showPaymentPinScreen(pinBuffer);
                    logger::write("[app] PIN entry screen displayed with optimized responsiveness", "info");
                    // Payment flow stays active - we can return to it
                } else {
                    // Legacy behavior for non-payment flow QR codes
                    pinBuffer = "";
                    screen::showPaymentPinScreen(pinBuffer);
                }
            } else if (keyPressed == "*") {
                if (isInPaymentFlow) {
                    // Cancel payment flow
                    logger::write("[app] Payment cancelled by user", "info");
                    
                    // CRITICAL: Immediately disable RF-safe mode for full keyboard access
                    extern void setRFSafeMode(bool enable);
                    setRFSafeMode(false);
                    suppressTouchDuringNFC(false);
                    logger::write("[app] RF-safe mode disabled and touch fully restored for cancellation", "info");
                    
                    cleanupPaymentFlow();
                    isInPaymentFlow = false;
                    screen::showX();
                    vTaskDelay(pdMS_TO_TICKS(2100));
                    keysBuffer = "";  // Reset buffer
                    amount = 0;       // Reset amount
                    screen::showEnterAmountScreen(0);
                } else {
                    // Legacy behavior
                    screen::showX();
                    vTaskDelay(pdMS_TO_TICKS(2100));
                    screen::showHomeScreen();
                }
            } else if (keyPressed == "1") {
                screen::adjustContrast(-10);// decrease contrast
            } else if (keyPressed == "4") {
                screen::adjustContrast(10);// increase contrast
            }
        } else if (currentScreen == "paymentPin") {
            if (keyPressed == "#") {
                // # key: Return to payment QR code screen (keep payment flow active if it exists)
                
                // Disable PIN entry mode
                setPinEntryMode(false);
                
                if (isInPaymentFlow) {
                    logger::write("[app] Returning to payment QR from PIN entry - restarting NFC if needed", "info");
                    
                    // CRITICAL: Restart NFC task since we shut it down for PIN entry
                    if (config::getBool("nfcEnabled") && nfcTaskHandle != NULL) {
                        logger::write("[app] Restarting NFC for payment flow", "info");
                        
                        // Resume NFC task
                        vTaskResume(nfcTaskHandle);
                        
                        // Restore payment mode sensitivity (less sensitive than PIN entry to reduce RF interference)
                        cap.setThresholds(5, 5); // Payment mode sensitivity
                        vTaskDelay(pdMS_TO_TICKS(100)); // Allow settings to take effect
                        
                        // Activate NFC for payment mode
                        xEventGroupClearBits(nfcEventGroup, (1 << 1));
                        xEventGroupSetBits(nfcEventGroup, (1 << 0));
                        
                        logger::write("[app] NFC restarted for payment flow with sensitivity (5,5)", "info");
                    } else {
                        // If NFC disabled, restore normal sensitivity
                        cap.setThresholds(3, 5);
                        vTaskDelay(pdMS_TO_TICKS(50));
                        logger::write("[app] Touch sensitivity restored to normal (3,5)", "info");
                    }
                    
                    screen::showPaymentQRCodeScreen(currentPaymentLNURL);
                } else {
                    screen::showPaymentQRCodeScreen(qrcodeData);
                }
            } else if (keyPressed == "*") {
                // * key: Delete last digit from PIN buffer, or return to QR if buffer is empty
                if (!pinBuffer.empty()) {
                    // Delete last digit from PIN buffer
                    pinBuffer.pop_back();
                    screen::showPaymentPinScreen(pinBuffer);
                    logger::write("[app] PIN digit deleted: " + std::to_string(pinBuffer.length()) + "/4 digits", "debug");
                } else {
                    // PIN buffer is empty - return to payment QR code screen
                    
                    // Disable PIN entry mode
                    setPinEntryMode(false);
                    
                    if (isInPaymentFlow) {
                        logger::write("[app] Returning to payment QR from empty PIN entry - restarting NFC if needed", "info");
                        
                        // CRITICAL: Restart NFC task since we shut it down for PIN entry
                        if (config::getBool("nfcEnabled") && nfcTaskHandle != NULL) {
                            logger::write("[app] Restarting NFC for payment flow", "info");
                            
                            // Resume NFC task
                            vTaskResume(nfcTaskHandle);
                            
                            // Restore payment mode sensitivity (less sensitive than PIN entry to reduce RF interference)
                            cap.setThresholds(5, 5); // Payment mode sensitivity
                            vTaskDelay(pdMS_TO_TICKS(100)); // Allow settings to take effect
                            
                            // Activate NFC for payment mode
                            xEventGroupClearBits(nfcEventGroup, (1 << 1));
                            xEventGroupSetBits(nfcEventGroup, (1 << 0));
                            
                            logger::write("[app] NFC restarted for payment flow with sensitivity (5,5)", "info");
                        } else {
                            // If NFC disabled, restore normal sensitivity
                            cap.setThresholds(3, 5);
                            vTaskDelay(pdMS_TO_TICKS(50));
                            logger::write("[app] Touch sensitivity restored to normal (3,5)", "info");
                        }
                        
                        screen::showPaymentQRCodeScreen(currentPaymentLNURL);
                    } else {
                        screen::showPaymentQRCodeScreen(qrcodeData);
                    }
                }
            } else if (keyPressed == "0" || keyPressed == "1" || keyPressed == "2" || keyPressed == "3" || keyPressed == "4" || keyPressed == "5" || keyPressed == "6" || keyPressed == "7" || keyPressed == "8" || keyPressed == "9") {
                unsigned long currentTime = millis();
                // Use same timing as amount entry for consistent user experience
                if (currentTime - lastKeyAddedTime >= KEY_ADD_DELAY) {
                    pinBuffer += keyPressed;
                    lastKeyAddedTime = currentTime;
                    logger::write("[app] PIN digit entered: " + std::to_string(pinBuffer.length()) + "/4 digits", "debug");
                    if (pinBuffer.length() == 4) {
                        if (pinBuffer == pin || pinBuffer == currentPaymentPin || pinBuffer == apiReturnedPin) {
                            // Disable PIN entry mode - PIN verification successful
                            setPinEntryMode(false);
                            
                            screen::showSuccess();
                            pinBuffer = "";
                            TickType_t startTime = xTaskGetTickCount();
                            while ((xTaskGetTickCount() - startTime) < pdMS_TO_TICKS(4200)) {
                                if (getTouch() == "*") {
                                    break;
                                }
                                vTaskDelay(pdMS_TO_TICKS(50)); // Check for input every 50ms
                            }
                            // Reset payment flow if we were in one
                            if (isInPaymentFlow) {
                                cleanupPaymentFlow();
                                isInPaymentFlow = false;
                                keysBuffer = "";  // Reset buffer
                                amount = 0;       // Reset amount
                                screen::showEnterAmountScreen(0);
                            } else {
                                screen::showHomeScreen();
                            }
                        } else {
                            screen::showX();
                            pinBuffer = "";
                            vTaskDelay(pdMS_TO_TICKS(2100));
                            if (++incorrectPinAttempts >= 5) {
                                // Disable PIN entry mode - too many failed attempts
                                setPinEntryMode(false);
                                
                                pinBuffer = "";
                                incorrectPinAttempts = 0;
                                vTaskDelay(pdMS_TO_TICKS(2100));
                                // Reset payment flow if we were in one
                                if (isInPaymentFlow) {
                                    keysBuffer = "";  // Reset buffer
                                    amount = 0;       // Reset amount
                                    screen::showEnterAmountScreen(0);
                                } else {
                                    screen::showHomeScreen();
                                }
                            } else {
                                vTaskDelay(pdMS_TO_TICKS(420));
                                screen::showPaymentPinScreen(pinBuffer);
                            }
                        }
                    }
                    else {
                        screen::showPaymentPinScreen(pinBuffer);
                        // Immediate feedback for responsiveness
                        logger::write("[app] PIN screen updated", "debug");
                    }
                }
            }
        } else if (currentScreen == "menu") {
            if (keyPressed == "1") {
                bool nfcEnabled = config::getBool("nfcEnabled");
                config::saveConfiguration("nfcEnabled", nfcEnabled ? "false" : "true");
                vTaskDelay(pdMS_TO_TICKS(210));
                esp_restart();  
            } else if (keyPressed == "2") {
                bool currentOfflineMode = config::getBool("offlineMode");
                config::saveConfiguration("offlineMode", currentOfflineMode ? "false" : "true");
                vTaskDelay(pdMS_TO_TICKS(210));
                esp_restart();  
            } else if (keyPressed == "3") {
                logger::write("Opening contrast input screen"); 
                pinBuffer = "";
                screen::showContrastInputScreen(pinBuffer);
            } else if (keyPressed == "4") {
                pinBuffer = "";
                screen::showSensitivityInputScreen(pinBuffer);
            } else if (keyPressed == "*") {
                screen::showEnterAmountScreen(0);
            }
        } else if (currentScreen == "contrastInput") {
            if (keyPressed == "*") {
                screen::showMenu();
            } else if (keyPressed == "#" && !pinBuffer.empty()) {
                int contrast = std::stoi(pinBuffer);
                if (contrast < 10) {
                    contrast = 10;  // Enforce minimum contrast of 10
                } else if (contrast > 100) {
                    contrast = 100;  // Enforce maximum contrast of 100
                }
                config::saveConfiguration("contrastLevel", std::to_string(contrast));
                screen::showSuccess();
                vTaskDelay(pdMS_TO_TICKS(2100));
                esp_restart();  // Restart to apply new contrast setting
            } else if (keyPressed >= "0" && keyPressed <= "9" && pinBuffer.length() < 3) {
                unsigned long currentTime = millis();
                if (currentTime - lastKeyAddedTime >= KEY_ADD_DELAY) {
                    pinBuffer += keyPressed;
                    lastKeyAddedTime = currentTime;
                    screen::showContrastInputScreen(pinBuffer);
                }
            }
        } else if (currentScreen == "sensitivityInput") {
            if (keyPressed == "*") {
                screen::showMenu();
            } else if (keyPressed == "#" && !pinBuffer.empty()) {
                int sensitivity = std::stoi(pinBuffer);
                if (sensitivity < 1) sensitivity = 1;
                if (sensitivity > 100) sensitivity = 100;
                config::saveConfiguration("touchSensitivity", std::to_string(sensitivity));
                screen::showSuccess();
                vTaskDelay(pdMS_TO_TICKS(2100));
                esp_restart();  // Reboot to apply new sensitivity setting
            } else if (keyPressed >= "0" && keyPressed <= "9" && pinBuffer.length() < 3) {
                pinBuffer += keyPressed;
                screen::showSensitivityInputScreen(pinBuffer);
            }
        }
        
        // Normal yielding for non-PIN screens (PIN entry has its own optimized timing above)
        if (currentScreen != "paymentPin") {
            taskYIELD(); // Normal yielding for other screens
        }
    } 
} 