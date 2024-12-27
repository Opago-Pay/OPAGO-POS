#include "app.h"

std::string pin = "";
std::string qrcodeDatafallback = "";
std::string keysBuffer = "";
const std::string keyBufferCharList = "0123456789";
bool correctPin = false;
std::string pinBuffer = "";
int incorrectPinAttempts = 0;

const std::string keyPressed;

static unsigned long lastKeyAddedTime = 0;
const unsigned long KEY_ADD_DELAY = 210; // Rate limit in milliseconds

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

void appTask(void* pvParameters) {
    Serial.println("App task started");
    static unsigned long lastPopTime = 0;
    static int popCount = 0;
    int signal;
    
    while(1) {
        power::loop();
        //handleSleepMode();
        if (!jsonRpc::hasPinConflict() || !jsonRpc::inUse()) {
            logger::loop();
            jsonRpc::loop();
        }
        const std::string currentScreen = screen::getCurrentScreen();
        if (currentScreen == "") {
            keysBuffer = "";
            screen::showEnterAmountScreen(keysToAmount(keysBuffer));
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
                    qrcodeData = "";
                    qrcodeDatafallback = "";
                    pin = util::generateRandomPin();
                    if (config::getString("fiatCurrency") == "sat") {
                        Serial.println("Dividing");
                        amount = amount / 100;
                    }

                    int retryCount = 0;
                    const int MAX_RETRIES = 3;
                    bool isRetrying = false;

                    while (true) {
                        if (offlineMode || config::getString("callbackUrl") == "https://opago-pay.com/getstarted" || 
                            config::getString("apiKey.key") == "BueokH4o3FmhWmbvqyqLKz") {
                            // In offline mode or demo mode
                            if (config::getString("callbackUrl") == "https://opago-pay.com/getstarted" || 
                                config::getString("apiKey.key") == "BueokH4o3FmhWmbvqyqLKz") {
                                // Demo mode - show setup link
                                screen::showPaymentQRCodeScreen("https://opago-pay.com/getstarted");
                            } else {
                                // Regular offline mode - proceed with offline QR code
                                std::string signedUrl = util::createLnurlPay(amount, pin);
                                qrcodeDatafallback = config::getString("uriSchemaPrefix") + 
                                                   util::toUpperCase(util::lnurlEncode(signedUrl));
                                screen::showPaymentQRCodeScreen(qrcodeDatafallback);
                            }
                            break;
                        }

                        // Online mode with retry logic
                        if (!isRetrying) {
                            screen::showSand();
                            std::string signedUrl = util::createLnurlPay(amount, pin);
                            qrcodeData = requestInvoice(signedUrl);
                            if (qrcodeData.empty()) {
                                retryCount++;
                                if (!onlineStatus) {
                                    // Lost WiFi connection - wait in nowifi screen
                                    screen::showNowifi();
                                    isRetrying = true;
                                    continue;
                                } else if (retryCount < MAX_RETRIES) {
                                    // Server request failed but we have WiFi - brief nowifi and retry
                                    screen::showNowifi();
                                    vTaskDelay(pdMS_TO_TICKS(1200));
                                    continue;  // Go back to showing sand and trying again
                                } else {
                                    // Max retries reached - stay on nowifi screen
                                    screen::showNowifi();
                                    isRetrying = true;
                                    continue;
                                }
                            }
                            screen::showPaymentQRCodeScreen(qrcodeData);
                            std::string paymentHash = fetchPaymentHash(qrcodeData);
                            logger::write("Payment hash: " + paymentHash, "debug");
                            if (waitForPaymentOrCancel(paymentHash, config::getString("apiKey.key"), qrcodeData)) {
                                screen::showSuccess();
                                TickType_t startTime = xTaskGetTickCount();
                                while ((xTaskGetTickCount() - startTime) < pdMS_TO_TICKS(4200)) {
                                    if (getTouch() == "*") {
                                        break;
                                    }
                                    vTaskDelay(pdMS_TO_TICKS(50));
                                }
                                keysBuffer = "";  // Reset buffer
                                amount = 0;       // Reset amount
                                screen::showEnterAmountScreen(0);
                            } else {
                                screen::showX();
                                vTaskDelay(pdMS_TO_TICKS(2100));
                                keysBuffer = "";  // Reset buffer
                                amount = 0;       // Reset amount
                                screen::showEnterAmountScreen(0);
                            }
                            break;
                        } else {
                            // We're in retry mode (after three failures or lost connection)
                            const std::string keyPressed = keypad::getPressedKey();
                            if (keyPressed == "*") {
                                screen::showX();
                                keysBuffer = "";  // Reset buffer
                                amount = 0;       // Reset amount
                                vTaskDelay(pdMS_TO_TICKS(2100));
                                esp_restart();
                            } else if (keyPressed == "#" || onlineStatus) {  // Also retry if WiFi is back
                                // Try again
                                retryCount = 0;
                                isRetrying = false;
                                continue;
                            }
                            // Stay on nowifi screen waiting for user input or WiFi reconnection
                            vTaskDelay(pdMS_TO_TICKS(100));
                        }
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
                pinBuffer = "";
                screen::showPaymentPinScreen(pinBuffer);
            } else if (keyPressed == "*" || getLongTouch('*', 210)) { 
                screen::showX();
                vTaskDelay(pdMS_TO_TICKS(2100));
                screen::showHomeScreen();
            } else if (keyPressed == "1") {
                screen::adjustContrast(-10);// decrease contrast
            } else if (keyPressed == "4") {
                screen::adjustContrast(10);// increase contrast
            }
        } else if (currentScreen == "paymentPin") {
            if (keyPressed == "#" || keyPressed == "*") {
                if (!qrcodeDatafallback.empty()) {
                    screen::showPaymentQRCodeScreen(qrcodeDatafallback);
                } else {
                    screen::showPaymentQRCodeScreen(qrcodeData);
                }
            } else if (keyPressed == "0" || keyPressed == "1" || keyPressed == "2" || keyPressed == "3" || keyPressed == "4" || keyPressed == "5" || keyPressed == "6" || keyPressed == "7" || keyPressed == "8" || keyPressed == "9") {
                unsigned long currentTime = millis();
                if (currentTime - lastKeyAddedTime >= KEY_ADD_DELAY) {
                    pinBuffer += keyPressed;
                    lastKeyAddedTime = currentTime;
                    if (pinBuffer.length() == 4) {
                        if (pinBuffer == pin) {
                            screen::showSuccess();
                            pinBuffer = "";
                            TickType_t startTime = xTaskGetTickCount();
                            while ((xTaskGetTickCount() - startTime) < pdMS_TO_TICKS(4200)) {
                                if (getTouch() == "*") {
                                    break;
                                }
                                vTaskDelay(pdMS_TO_TICKS(50)); // Check for input every 50ms
                            }
                            screen::showHomeScreen();
                        } else {
                            screen::showX();
                            pinBuffer = "";
                            vTaskDelay(pdMS_TO_TICKS(2100));
                            if (++incorrectPinAttempts >= 5) {
                                pinBuffer = "";
                                incorrectPinAttempts = 0;
                                vTaskDelay(pdMS_TO_TICKS(2100));
                                screen::showHomeScreen();
                            } else {
                                vTaskDelay(pdMS_TO_TICKS(420));
                                screen::showPaymentPinScreen(pinBuffer);
                            }
                        }
                    }
                    else {
                        screen::showPaymentPinScreen(pinBuffer);
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
        taskYIELD();
    } 
} 