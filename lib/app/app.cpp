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

// Screen transition flag to prevent double-processing of keys during transitions
bool isTransitioningScreens = false;
unsigned long transitionStartTime = 0;

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
                // NOTE: Do NOT cleanup payment flow here - let the success screen transition handle it
                // This prevents race condition where cleanup happens before screen transition completes
                // Cleanup will be handled in main loop when success -> enterAmount transition is detected
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
    static std::string lastScreenState = "";
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
            // Note: getTouch() result will be used in the main keyPressed processing below
            
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
        
                    // Handle screen transitions and cleanup
        if (lastScreenState == "success" && currentScreen == "enterAmount") {
            logger::write("[app] Success screen transitioned to enterAmount - handling cleanup", "debug");
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
            
            // Cleanup payment flow if we were in one
            if (isInPaymentFlow) {
                cleanupPaymentFlow();
                isInPaymentFlow = false;
                keysBuffer = "";  // Reset buffer
                amount = 0;       // Reset amount
            }
        }
        
        // Handle successful payment completion - screen task now handles timing
        if (currentScreen == "success" && !isInPaymentFlow) {
            // Check for skip input and trigger early transition if * is pressed
            if (getTouch() == "*") {
                logger::write("[app] Skip requested for success screen", "debug");
                screen::triggerEarlyTransition();
            }
            // Note: Auto-transition after 4.2 seconds is now handled by screen task
        }
        
        // Handle X screen transitions
        if (lastScreenState == "X" && currentScreen == "enterAmount") {
            logger::write("[app] X screen transitioned to enterAmount - checking cleanup", "debug");
            // Cleanup payment flow if we were in one (fallback safety net)
            if (isInPaymentFlow) {
                logger::write("[app] Fallback cleanup: Payment flow still active after X screen", "warning");
                cleanupPaymentFlow();
                isInPaymentFlow = false;
                keysBuffer = "";  // Reset buffer
                amount = 0;       // Reset amount
            }
            // Note: RF shutdown should already be handled by immediate cleanup when * was pressed
        }
        
        // CRITICAL: Ensure RF-safe mode is always disabled when returning to amount entry
        // This prevents race conditions where RF-safe mode might remain enabled
        if (currentScreen == "enterAmount" && lastScreenState != "enterAmount") {
            logger::write("[app] Transitioning to amount entry - ensuring RF-safe mode is disabled", "debug");
            extern void setRFSafeMode(bool enable);
            setRFSafeMode(false);
            suppressTouchDuringNFC(false);
            // Restore normal touch sensitivity for amount entry
            cap.setThresholds(3, 5);
            logger::write("[app] RF-safe mode disabled and normal touch sensitivity restored for amount entry", "info");
        }
        // Use cap touch system for all key input (includes RF-safe mode validation)
        const std::string keyPressed = getTouch();
        if (keyPressed != "") {
            logger::write("[app] Key received from cap touch: '" + keyPressed + "' on screen: " + currentScreen, "info");
            lastActivityTime = millis();
            
            // CRITICAL: Skip key processing if we're transitioning between screens
            if (isTransitioningScreens) {
                logger::write("[app] Skipping key processing during screen transition", "debug");
                continue;
            }
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
                    
                    // CRITICAL: Extended delay to let screen task process the PIN screen message
                    // This prevents timing gaps where button presses are processed with old screen logic
                    vTaskDelay(pdMS_TO_TICKS(200));
                    
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
                    
                    // CRITICAL: Ensure RF is turned off before cleanup
                    if (config::getBool("nfcEnabled") && nfcTaskHandle != NULL) {
                        logger::write("[app] Initiating RF shutdown sequence for payment cancellation", "info");
                        
                        bool rfShutdownSuccess = false;
                        unsigned long shutdownStartTime = millis();
                        const unsigned long SHUTDOWN_TIMEOUT_MS = 5000; // 5 seconds timeout
                        
                        while (!rfShutdownSuccess && (millis() - shutdownStartTime) < SHUTDOWN_TIMEOUT_MS) {
                            // Send shutdown signals
                            xEventGroupClearBits(nfcEventGroup, (1 << 0));
                            xEventGroupSetBits(nfcEventGroup, (1 << 1));
                            
                            // Wait for confirmation with shorter timeout for retry
                            EventBits_t uxBits = xEventGroupWaitBits(appEventGroup, (1 << 1), pdFALSE, pdFALSE, pdMS_TO_TICKS(500));
                            if ((uxBits & (1 << 1)) != 0) {
                                logger::write("[app] RF shutdown confirmed for payment cancellation", "info");
                                vTaskSuspend(nfcTaskHandle);
                                rfShutdownSuccess = true;
                            } else {
                                logger::write("[app] RF shutdown attempt failed, retrying...", "warning");
                                vTaskDelay(pdMS_TO_TICKS(100)); // Brief delay before retry
                            }
                        }
                        
                        if (!rfShutdownSuccess) {
                            logger::write("[app] CRITICAL: RF shutdown failed after 5 seconds for payment cancellation", "error");
                        }
                    }
                    
                    cleanupPaymentFlow();
                    isInPaymentFlow = false;
                    keysBuffer = "";  // Clear amount buffer immediately
                    amount = 0;       // Reset amount immediately
                    screen::showX();
                    // Note: X screen timing (2.1s) is now handled by screen task
                } else {
                    // Legacy behavior - clear buffer for clean state
                    keysBuffer = "";  // Clear amount buffer immediately
                    amount = 0;       // Reset amount immediately
                    screen::showX();
                    // Note: X screen timing (2.1s) is now handled by screen task
                }
            } else if (keyPressed == "1") {
                screen::adjustContrast(-10);// decrease contrast
            } else if (keyPressed == "4") {
                screen::adjustContrast(10);// increase contrast
            }
        } else if (currentScreen == "paymentPin") {
                        if (keyPressed == "#") {
                // # key: Return to payment QR code screen (keep payment flow active if it exists)
                logger::write("[app] # pressed on PIN screen - returning to QR code", "info");
                
                // CRITICAL: Set transition flag to prevent double-processing
                isTransitioningScreens = true;
                transitionStartTime = millis();
                
                // Disable PIN entry mode
                setPinEntryMode(false);
                
                if (isInPaymentFlow) {
                    logger::write("[app] In payment flow - showing currentPaymentLNURL: " + currentPaymentLNURL, "info");
                    
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
                    logger::write("[app] Not in payment flow - showing legacy qrcodeData: " + qrcodeData, "info");
                    screen::showPaymentQRCodeScreen(qrcodeData);
                }
                
                // CRITICAL: Extended delay to ensure screen transition completes
                vTaskDelay(pdMS_TO_TICKS(200));
                
                // CRITICAL: Clear transition flag after screen change is complete
                isTransitioningScreens = false;
                
                logger::write("[app] Screen transition complete - key processing re-enabled", "debug");
            } else if (keyPressed == "*") {
                // * key: Delete last digit from PIN buffer, or return to QR if buffer is empty
                if (!pinBuffer.empty()) {
                    // Delete last digit from PIN buffer
                    pinBuffer.pop_back();
                    screen::showPaymentPinScreen(pinBuffer);
                    logger::write("[app] PIN digit deleted: " + std::to_string(pinBuffer.length()) + "/4 digits", "debug");
                } else {
                    // PIN buffer is empty - return to payment QR code screen
                    logger::write("[app] * pressed on empty PIN buffer - returning to QR code", "info");
                    
                    // CRITICAL: Set transition flag to prevent double-processing
                    isTransitioningScreens = true;
                    transitionStartTime = millis();
                    
                    // Disable PIN entry mode
                    setPinEntryMode(false);
                    
                    if (isInPaymentFlow) {
                        logger::write("[app] In payment flow - showing currentPaymentLNURL: " + currentPaymentLNURL, "info");
                        
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
                        logger::write("[app] Not in payment flow - showing legacy qrcodeData: " + qrcodeData, "info");
                        screen::showPaymentQRCodeScreen(qrcodeData);
                    }
                    
                    // CRITICAL: Extended delay to ensure screen transition completes
                    vTaskDelay(pdMS_TO_TICKS(200));
                    
                    // CRITICAL: Clear transition flag after screen change is complete
                    isTransitioningScreens = false;
                    
                    logger::write("[app] Screen transition complete - key processing re-enabled", "debug");
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
                            // Note: Success screen timing (4.2s intended, 2.1s min) is now handled by screen task
                            // The screen task will automatically transition or handle early transitions via triggerEarlyTransition()
                        } else {
                            incorrectPinAttempts++;
                            screen::showX();
                            pinBuffer = "";
                            
                            if (incorrectPinAttempts >= 5) {
                                // 5 failed attempts - show long X (4200ms, abortable after 1200ms) then reset
                                logger::write("[app] 5 PIN attempts failed - showing long X and resetting payment", "info");
                                setPinEntryMode(false);
                                incorrectPinAttempts = 0;
                                
                                // Show long X manually (4200ms)
                                vTaskDelay(pdMS_TO_TICKS(4200));
                                
                                // Reset payment flow completely
                                if (isInPaymentFlow) {
                                    cleanupPaymentFlow();
                                    isInPaymentFlow = false;
                                    keysBuffer = "";
                                    amount = 0;
                                }
                                
                                screen::showEnterAmountScreen(0);
                                logger::write("[app] Payment reset to amount entry after 5 failed PIN attempts", "info");
                            } else {
                                // 1-4 failed attempts - show X (2100ms) then return to PIN entry
                                logger::write("[app] Incorrect PIN attempt " + std::to_string(incorrectPinAttempts) + "/5", "info");
                                
                                // Show X for 2100ms manually
                                vTaskDelay(pdMS_TO_TICKS(2100));
                                screen::showPaymentPinScreen(pinBuffer);
                                
                                // CRITICAL: Extended delay to let screen task process the PIN screen message
                                vTaskDelay(pdMS_TO_TICKS(200));
                                
                                logger::write("[app] Returned to PIN entry after incorrect attempt", "info");
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
                if (nfcEnabled) {
                    // Turning NFC off
                    config::saveConfiguration("nfcEnabled", "false");
                    logger::write("[app] NFC disabled via menu", "info");
                } else {
                    // Turning NFC on - must disable offline mode
                    config::saveConfiguration("nfcEnabled", "true");
                    config::saveConfiguration("offlineMode", "false");
                    logger::write("[app] NFC enabled via menu - offline mode automatically disabled", "info");
                }
                vTaskDelay(pdMS_TO_TICKS(210));
                esp_restart();  
            } else if (keyPressed == "2") {
                bool currentOfflineMode = config::getBool("offlineMode");
                if (currentOfflineMode) {
                    // Turning offline mode off
                    config::saveConfiguration("offlineMode", "false");
                    logger::write("[app] Offline mode disabled via menu", "info");
                } else {
                    // Turning offline mode on - must disable NFC
                    config::saveConfiguration("offlineMode", "true");
                    config::saveConfiguration("nfcEnabled", "false");
                    logger::write("[app] Offline mode enabled via menu - NFC automatically disabled", "info");
                }
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
                // Note: Success screen will show for 2.1s min, 4.2s intended via screen task
                // Brief delay to let user see success before restart
                vTaskDelay(pdMS_TO_TICKS(1000));
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
                // Note: Success screen will show for 2.1s min, 4.2s intended via screen task
                // Brief delay to let user see success before restart
                vTaskDelay(pdMS_TO_TICKS(1000));
                esp_restart();  // Reboot to apply new sensitivity setting
            } else if (keyPressed >= "0" && keyPressed <= "9" && pinBuffer.length() < 3) {
                pinBuffer += keyPressed;
                screen::showSensitivityInputScreen(pinBuffer);
            }
        }
        
        // Update last screen state for transition detection
        lastScreenState = currentScreen;
        
        // Reset transition flag after each loop cycle (safety net)
        if (isTransitioningScreens && keyPressed == "") {
            // Only reset if no key was pressed this cycle and timeout exceeded
            if (millis() - transitionStartTime > 500) { // Safety timeout
                isTransitioningScreens = false;
                logger::write("[app] Transition flag reset by safety timeout", "debug");
            }
        }
        
        // Consistent fast timing for all screens to ensure responsive touch detection
        if (currentScreen != "paymentPin") {
            vTaskDelay(pdMS_TO_TICKS(10)); // 10ms delay for consistent touch responsiveness
        }
    } 
} 