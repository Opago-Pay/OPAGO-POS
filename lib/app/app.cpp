#include "app.h"

std::string pin = "";
std::string qrcodeDatafallback = "";
std::string keysBuffer = "";
const std::string keyBufferCharList = "0123456789";
bool correctPin = false;
std::string pinBuffer = "";
int incorrectPinAttempts = 0;

const std::string keyPressed;

void appendToKeyBuffer(const std::string &key) {
	if (keyBufferCharList.find(key) != std::string::npos) {
		keysBuffer += key;
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
    screen::showHomeScreen();
    screen::showStatusSymbols(power::getBatteryPercent());
    vTaskDelay(pdMS_TO_TICKS(420));
    static unsigned long lastPopTime = 0;
    static int popCount = 0;
    static unsigned long lastUpdate = 0;
    int signal;
    while(1) {
        power::loop();
        if (millis() - lastUpdate > 2100) {
            screen::showStatusSymbols(power::getBatteryPercent());
            lastUpdate = millis();
        }
        //handleSleepMode();
        if (!jsonRpc::hasPinConflict() || !jsonRpc::inUse()) {
            logger::loop();
            jsonRpc::loop();
        }
        const std::string currentScreen = screen::getCurrentScreen();
        //logger::write("Current Screen: " + currentScreen);
        if (currentScreen == "") {
            screen::showHomeScreen();
            keysBuffer = "";
            screen::showEnterAmountScreen(keysToAmount(keysBuffer));
        }
        const std::string keyPressed = keypad::getPressedKey();
        if (keyPressed != "") {
            logger::write("Key pressed: " + keyPressed, "debug");
            lastActivityTime = millis();
        }
        if (currentScreen == "home") {
            Serial.println("Home Screen");
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
                    if (config::getString("fiatCurrency") == "sat") { //sat currency in LNbits for some reason returns bits. This needs to be removed once bug is fixed. 
                        Serial.println("Dividing");
                        amount = amount / 100;
                    } 
                    Serial.println("Amount: " + String(amount));
                    Serial.println("New Amount: " + String(amount));
                    std::string signedUrl = util::createLnurlPay(amount, pin);
                    std::string encoded = util::lnurlEncode(signedUrl);
                    qrcodeData += config::getString("uriSchemaPrefix");
                    qrcodeData += util::toUpperCase(encoded);
                    qrcodeDatafallback = qrcodeData;
                    if (onlineStatus) {
                        //if WiFi is connected, we can fetch the invoice from the server
                        screen::showSand();
                        std::string paymentHash = "";
                        bool paymentMade = false;
                        qrcodeData = requestInvoice(signedUrl);
                        if (qrcodeData.empty()) {
                            keysBuffer = "";
                            logger::write("Server connection failed. Falling back to offline mode.");
                            offlineMode = true;
                            screen::showPaymentQRCodeScreen(qrcodeDatafallback);
                            logger::write("Payment request shown: \n" + signedUrl);
                            logger::write("QR Code data: \n" + qrcodeDatafallback, "debug");
                        } else {
                            keysBuffer = "";
                            screen::showPaymentQRCodeScreen(qrcodeData);
                            logger::write("Payment request shown: \n" + qrcodeData);
                            paymentHash = fetchPaymentHash(qrcodeData);
                            logger::write("Payment hash: " + paymentHash, "debug");
                            paymentMade = waitForPaymentOrCancel(paymentHash, config::getString("apiKey.key"), qrcodeData);
                            if (!paymentMade) { 
                                screen::showX();
                                offlineMode = false;
                                vTaskDelay(pdMS_TO_TICKS(2100));
                                keysBuffer = "";
                                screen::showHomeScreen();
                            } else {
                                screen::showSuccess();
                                offlineMode = false;
                                TickType_t startTime = xTaskGetTickCount();
                                while ((xTaskGetTickCount() - startTime) < pdMS_TO_TICKS(4200)) {
                                    if (getTouch() == "*") {
                                        break;
                                    }
                                    vTaskDelay(pdMS_TO_TICKS(50)); // Check for input every 50ms
                                }
                                keysBuffer = "";
                                screen::showHomeScreen();
                            }
                        }
                    } else {
                        offlineMode = true;
                        logger::write("Device is offline, displaying payment QR code...");
                        screen::showPaymentQRCodeScreen(qrcodeData);
                        logger::write("Payment request shown: \n" + signedUrl);
                        logger::write("QR Code data: \n" + qrcodeData, "debug");
                    }
                } else {
                    // Show the menu screen
                    screen::showMenu();

                    // Wait for user input
                    while (true) {
                        std::string keyPressed = keypad::getPressedKey(); // Assume getKeyPressed() is a function that returns the key pressed

                        if (keyPressed == "1") {
                            if (WiFi.getMode() != WIFI_AP) {
                                screen::showEnterAmountScreen(0);
                                startAccessPoint();
                                logger::write("Access Point started.");
                            } else {
                                screen::showEnterAmountScreen(0);
                                stopAccessPoint();
                                offlineMode = false;
                                logger::write("Access Point stopped.");
                            }
                            break; // Exit the loop after handling the key press
                        } else if (keyPressed == "2") {
                            // Toggle the offlineOnly flag
                            offlineOnly = !offlineOnly;
                            onlineStatus = false;
                            logger::write("offlineOnly flag toggled to: " + std::to_string(offlineOnly));
                            screen::showEnterAmountScreen(0);
                            break; // Exit the loop after handling the key press
                        } else if (keyPressed == "*") {
                            // Exit the loop if 'x' is pressed
                            screen::showEnterAmountScreen(0);
                            break;
                        }
                        vTaskDelay(pdMS_TO_TICKS(50)); // Check for input every 50ms
                    }
                }
            } else if (keysBuffer.size() < maxNumKeysPressed) {
                if (keyPressed != "0" || keysBuffer != "") {
                    appendToKeyBuffer(keyPressed);
                    logger::write("keysBuffer = " + keysBuffer);
                    screen::showEnterAmountScreen(keysToAmount(keysBuffer));
                }
            }
        } else if (currentScreen == "paymentQRCode") {
            if (keyPressed == "#") {
                pinBuffer = "";
                screen::showPaymentPinScreen(pinBuffer);
            } else if (keyPressed == "*" || getLongTouch('*', 210)) { 
                screen::showX();
                offlineMode = false;
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
                pinBuffer += keyPressed;
                if (pinBuffer.length() == 4) {
                    if (pinBuffer == pin) {
                        screen::showSuccess();
                        offlineMode = false;
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
                            offlineMode = false;
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
        taskYIELD();
    } 
} 