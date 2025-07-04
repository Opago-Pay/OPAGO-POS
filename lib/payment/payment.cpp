#include "payment.h"

uint8_t lastRenderedQRCode = 0;
bool lastConnectionLossState = false;
std::string onlinePaymentHash = "";
unsigned long paymentStartTime = 0;

std::string parseCallbackUrl(const std::string &response) {
    DynamicJsonDocument doc(1024);
    deserializeJson(doc, response);

    std::string callbackUrl = "";

    if (doc["callback"].is<std::string>()) {
        callbackUrl = doc["callback"].as<std::string>();
    } else if (doc["callback"]["_url"].is<std::string>()) {
        callbackUrl = doc["callback"]["_url"].as<std::string>();
    }

    if (callbackUrl.empty()) {
        screen::showX();
        delay(2100);
        logger::write("Callback URL not found in response");
    }

    return callbackUrl;
}

std::string parseInvoice(const std::string &json) {
    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, json);

    if (error) {
        logger::write("Failed to parse JSON: " + std::string(error.c_str()));
        return "";
    }

    const char *invoice = doc["pr"];
    if (invoice) {
        return std::string(invoice);
    } else {
        logger::write("Invoice not found in response");
        return "";
    }
}

std::string requestInvoice(const std::string &url) {
    HTTPClient http;
    http.begin(url.c_str());
    int httpResponseCode = http.GET();
    logger::write("HTTP Response code: " + std::to_string(httpResponseCode));

    if (httpResponseCode == 200) {
        std::string response = http.getString().c_str();
        logger::write("HTTP Response: " + response);
        
        // Parse the callback URL from the response
        std::string callbackUrl = parseCallbackUrl(response);
        logger::write("Callback URL: " + callbackUrl);

        http.end();  // Close the first connection

        // Start the second request with the callback URL
        http.begin(callbackUrl.c_str());
        httpResponseCode = http.GET();

        if (httpResponseCode == 200) {
            response = http.getString().c_str();
            // Parse the invoice from the second response
            std::string invoice = parseInvoice(response);
            logger::write("Invoice: " + invoice);
            http.end();  // Close the connection
            return invoice;
        } else {
            // Handle error
            logger::write("Error on HTTP request for invoice: " + std::to_string(httpResponseCode));
        }

    } else {
        // Handle error
        logger::write("Error on HTTP request for callback URL: " + std::to_string(httpResponseCode));
    }

    http.end();
    return "";
}

std::string fetchPaymentHash(const std::string &lnurl) {
    HTTPClient http;
    // Use the complete URL as a single string argument for http.begin()
    http.begin("https://lnbits.opago-pay.com/api/v1/payments/decode");
    http.addHeader("Content-Type", "application/json");

    // Create the request body
    String requestBody = "{ \"data\": \"" + String(lnurl.c_str()) + "\" }";

    int httpResponseCode = http.POST(requestBody);
    if (httpResponseCode == 200) {
        std::string response = http.getString().c_str();
        logger::write("Response from server: " + response);
        logger::write("Payment Hash Fetch Response: " + std::string(response));
        // Parse the response
        DynamicJsonDocument doc(1024);
        DeserializationError error = deserializeJson(doc, response);

        if (error) {
            logger::write("Failed to parse JSON from decode response: " + std::string(error.c_str()));
            http.end();  // Close the connection
            return "";
        }
        
        // Get the payment hash
        const char* paymentHash = doc["payment_hash"];
        if (!paymentHash) {
            logger::write("Payment hash not found in decode response");
            http.end();  // Close the connection
            return "";
        }

        logger::write("Payment hash: " + std::string(paymentHash));
        http.end();  // Close the connection
        return std::string(paymentHash);
    } else {
        // Handle error
        logger::write("Error on HTTP request for decoding invoice: " + std::to_string(httpResponseCode));
        http.end();  // Close the connection
    }

    return "";
}

bool isPaymentMade(const std::string &paymentHash, const std::string &apiKey) {
    // Check if Wi-Fi is connected
    if (WiFi.status() != WL_CONNECTED) {
        logger::write("Wi-Fi is not connected");
        connectionLoss = true;
        onlineStatus = false;
        // Check if connection loss status has changed or 1 min has passed
        if (connectionLoss != lastConnectionLossState || millis() - lastRenderedQRCode >= 600000) {
            screen::showPaymentQRCodeScreen(qrcodeData);
            lastRenderedQRCode = millis();
            lastConnectionLossState = connectionLoss;
        }
        return false;
    } else {
        connectionLoss = false;
        onlineStatus = true;
        // Check if connection loss status has changed or 1 min has passed
        if (connectionLoss != lastConnectionLossState || millis() - lastRenderedQRCode >= 600000) {
            screen::showPaymentQRCodeScreen(qrcodeData);
            lastRenderedQRCode = millis();
            lastConnectionLossState = connectionLoss;
        }
    }

    // Check if paymentHash is not empty
    if (paymentHash.empty()) {
        logger::write("Payment hash is empty");
        connectionLoss = true;
        screen::showPaymentQRCodeScreen(qrcodeData);
        return false;
    }

    // Check if apiKey is not empty
    if (apiKey.empty()) {
        logger::write("API key is empty");
        connectionLoss = true;
        screen::showPaymentQRCodeScreen(qrcodeData);
        return false;
    }

    // Try to take the wifi semaphore before accessing wifi
    HTTPClient* http = nullptr;
    if(xSemaphoreTake(wifiSemaphore, portMAX_DELAY) == pdTRUE) {
        http = new HTTPClient();
        // Extract the domain from the config callback url
        std::string callbackUrl = config::getString("callbackUrl");
        size_t domainStart = callbackUrl.find("://") + 3;
        size_t domainEnd = callbackUrl.find("/", domainStart);
        std::string domain = callbackUrl.substr(domainStart, domainEnd - domainStart);

        // Create the url using the extracted domain
        std::string url = "https://" + domain + "/api/v1/payments/" + paymentHash;
        http->begin(url.c_str());

        http->addHeader("accept", "application/json");
        http->addHeader("X-Api-Key", apiKey.c_str());

        int httpResponseCode = http->GET();

        if (httpResponseCode == 200) {
            std::string response = http->getString().c_str();
            logger::write("HTTP Response: " + response);

            // Parse the response
            DynamicJsonDocument doc(1024);
            DeserializationError error = deserializeJson(doc, response);

            if (error) {
                logger::write("Failed to parse JSON from payment check response: " + std::string(error.c_str()));
                http->end();
                xSemaphoreGive(wifiSemaphore); // Release the wifi semaphore after accessing wifi
                delete http;
                return false;
            }

            // Get the payment status
            bool paid = doc["paid"];
            http->end();  // Close the connection
            xSemaphoreGive(wifiSemaphore); // Release the wifi semaphore after accessing wifi
            delete http;
            return paid;
        } else {
            // Handle error
            connectionLoss = true;
            onlineStatus = false;
            logger::write("Error on HTTP request for payment check: " + std::to_string(httpResponseCode));
        }

        http->end();
        xSemaphoreGive(wifiSemaphore); // Release the wifi semaphore after accessing wifi
    } else {
        logger::write("Failed to take wifi semaphore, aborting payment check.");
    }
    delete http;
    return false;
}

// waitForPaymentOrCancel
// This function waits for a payment to be made or cancelled. It checks various states and signals from the NFC task and the user.
// The function uses two event groups (appEventGroup and nfcEventGroup) to wait for and handle various events.
// The bits in the event groups represent the following states:
// appEventGroup:
// Bit 0 (1 << 0): Indicates that a card has been detected by nfcTask.
// Bit 1 (1 << 1): Confirmation bit for the successful shutdown of the RF module and transition into idle mode in nfcTask.
// nfcEventGroup:
// Bit 0 (1 << 0): Instructs nfcTask to power up or remain active.
// Bit 1 (1 << 1): Commands nfcTask to turn off the RF functionality and transition to idle mode. This bit is used particularly after a successful payment is processed.
bool waitForPaymentOrCancel(const std::string &paymentHash, const std::string &apiKey, const std::string &invoice) {
    paymentisMade = false;
    bool keyPressed = false;
    static unsigned long lastUpdate = 0;
    lastRenderedQRCode = millis();
    EventBits_t uxBits;
    const EventBits_t uxAllBits = ( 1 << 0 ) | ( 1 << 1 );
    bool lastConnectionState = onlineStatus;

    // Only configure NFC if it's enabled
    if (config::getBool("nfcEnabled")) {
        cap.setThresholds(5, 5);
        vTaskResume(nfcTaskHandle);
        xEventGroupClearBits(nfcEventGroup, (1 << 1));
        xEventGroupSetBits(nfcEventGroup, (1 << 0));
    }

    while (!paymentisMade) {
        // Check for connection state changes
        if (lastConnectionState != onlineStatus) {
            if (!onlineStatus) {
                screen::showNowifi();
            } else {
                screen::showPaymentQRCodeScreen(invoice);
            }
            lastConnectionState = onlineStatus;
        }

        // Check for payment if we're online
        if (onlineStatus) {
            logger::write("[payment] Checking if paid.", "info");
            paymentisMade = isPaymentMade(paymentHash, apiKey);
        }

        // Handle cancellation
        if (config::getBool("nfcEnabled")) {
            uxBits = xEventGroupWaitBits(appEventGroup, uxAllBits, pdFALSE, pdFALSE, pdMS_TO_TICKS(210));
            
            if ((uxBits & (1 << 0)) != 0) {
                logger::write("[payment] Card detected, checking payment", "info");
                vTaskDelay(pdMS_TO_TICKS(2100));
                continue;
            }
            
            keyPressed = getLongTouch('*', 210);
        } else {
            keyPressed = (getTouch() == "*");
        }

        if (keyPressed) {
            logger::write("[payment] Payment cancelled by user.", "info");
            screen::showX();
            
            // Only handle NFC shutdown if NFC is enabled
            if (config::getBool("nfcEnabled") && nfcTaskHandle != NULL) {
                xEventGroupClearBits(nfcEventGroup, (1 << 0));
                xEventGroupSetBits(nfcEventGroup, (1 << 1));
                
                // Wait for NFC shutdown confirmation
                uxBits = xEventGroupWaitBits(appEventGroup, (1 << 1), pdFALSE, pdFALSE, pdMS_TO_TICKS(1000));
                if ((uxBits & (1 << 1)) != 0) {
                    vTaskSuspend(nfcTaskHandle);
                }
            }
            return false;
        }

        // Check for contrast adjustment
        std::string contrastKey = getTouch(); 
        if (contrastKey == "1") {
            screen::adjustContrast(-10);
        } else if (contrastKey == "4") {
            screen::adjustContrast(10);
        }

        // Yield to prevent watchdog timer from triggering
        vTaskDelay(pdMS_TO_TICKS(10)); // Small delay to ensure proper yielding
    }

    // Payment successful
    if (paymentisMade) {
        screen::showSuccess();
        logger::write("[payment] Payment has been made.", "info");
        
        // Only handle NFC shutdown if NFC is enabled
        if (config::getBool("nfcEnabled") && nfcTaskHandle != NULL) {
            xEventGroupClearBits(nfcEventGroup, (1 << 0));
            xEventGroupSetBits(nfcEventGroup, (1 << 1));
            
            uxBits = xEventGroupWaitBits(appEventGroup, (1 << 1), pdFALSE, pdFALSE, pdMS_TO_TICKS(1000));
            if ((uxBits & (1 << 1)) != 0) {
                vTaskSuspend(nfcTaskHandle);
            }
        }
    }

    logger::write("[payment] Returning to App Loop", "debug");
    if (config::getBool("nfcEnabled")) {
        cap.setThresholds(3, 5); // Restore normal sensitivity
    }
    connectionLoss = false;
    return paymentisMade;
}

bool waitForPaymentWithFallback(const std::string &lnurlQR, const std::string &pin) {
    paymentisMade = false;
    bool keyPressed = false;
    lastRenderedQRCode = millis();
    EventBits_t uxBits;
    const EventBits_t uxAllBits = ( 1 << 0 ) | ( 1 << 1 );
    bool lastConnectionState = onlineStatus;
    unsigned long lastOnlineCheck = 0;
    const unsigned long ONLINE_CHECK_INTERVAL = 2000; // Check every 2 seconds
    
    // Show the LNURL QR immediately (same for both online/offline)
    screen::showPaymentQRCodeScreen(lnurlQR);
    logger::write("[payment] Showing LNURL QR code", "info");

    // Only configure NFC if it's enabled
    if (config::getBool("nfcEnabled")) {
        cap.setThresholds(5, 5);
        vTaskResume(nfcTaskHandle);
        xEventGroupClearBits(nfcEventGroup, (1 << 1));
        xEventGroupSetBits(nfcEventGroup, (1 << 0));
    }

    while (!paymentisMade) {
        // Check for connection state changes
        if (lastConnectionState != onlineStatus) {
            if (!onlineStatus) {
                logger::write("[payment] Connection lost", "info");
                screen::showNowifi();
                vTaskDelay(pdMS_TO_TICKS(1000));
                screen::showPaymentQRCodeScreen(lnurlQR);
            } else {
                logger::write("[payment] Connection restored", "info");
                screen::showPaymentQRCodeScreen(lnurlQR);
            }
            lastConnectionState = onlineStatus;
        }

        // Check for payment if we're online
        if (onlineStatus) {
            unsigned long currentTime = millis();
            if (currentTime - lastOnlineCheck >= ONLINE_CHECK_INTERVAL) {
                logger::write("[payment] Checking if LNURL payment received", "info");
                
                // Check POS status API for payment
                std::string posId = extractPosIdFromCallbackUrl();
                std::string apiKey = config::getString("apiKey.key");
                std::string returnedPin;
                
                if (!posId.empty()) {
                    bool paymentReceived = checkPosPaymentStatus(posId, apiKey, pin, returnedPin);
                    if (paymentReceived) {
                        logger::write("[payment] Payment confirmed via POS status API", "info");
                        if (!returnedPin.empty()) {
                            logger::write("[payment] Using API PIN for verification: " + returnedPin, "info");
                            // Update the global API PIN for verification
                            extern std::string apiReturnedPin;
                            apiReturnedPin = returnedPin;
                        }
                        paymentisMade = true;
                    }
                }
                
                lastOnlineCheck = currentTime;
            }
        }

        // Handle cancellation and PIN entry
        if (config::getBool("nfcEnabled")) {
            uxBits = xEventGroupWaitBits(appEventGroup, uxAllBits, pdFALSE, pdFALSE, pdMS_TO_TICKS(210));
            
            if ((uxBits & (1 << 0)) != 0) {
                logger::write("[payment] Card detected, checking payment", "info");
                vTaskDelay(pdMS_TO_TICKS(2100));
                continue;
            }
            
            keyPressed = getLongTouch('*', 210);
                         if (getTouch() == "#") {
                 // Switch to PIN entry mode
                 logger::write("[payment] Switching to PIN entry mode", "info");
                 
                 // Cleanup NFC if enabled
                 if (config::getBool("nfcEnabled") && nfcTaskHandle != NULL) {
                     xEventGroupClearBits(nfcEventGroup, (1 << 0));
                     xEventGroupSetBits(nfcEventGroup, (1 << 1));
                     uxBits = xEventGroupWaitBits(appEventGroup, (1 << 1), pdFALSE, pdFALSE, pdMS_TO_TICKS(1000));
                     if ((uxBits & (1 << 1)) != 0) {
                         vTaskSuspend(nfcTaskHandle);
                     }
                 }
                 
                 // No task cleanup needed - using direct API calls
                 
                 // Transition to PIN screen
                 screen::showPaymentPinScreen("");
                 return false; // Return false to indicate we've handled the transition
             }
        } else {
            keyPressed = (getTouch() == "*");
                         if (getTouch() == "#") {
                 // Switch to PIN entry mode
                 logger::write("[payment] Switching to PIN entry mode", "info");
                 
                 // No task cleanup needed - using direct API calls
                 
                 // Transition to PIN screen
                 screen::showPaymentPinScreen("");
                 return false; // Return false to indicate we've handled the transition
             }
        }

        if (keyPressed) {
            logger::write("[payment] Payment cancelled by user", "info");
            screen::showX();
            
            // Cleanup NFC if enabled
            if (config::getBool("nfcEnabled") && nfcTaskHandle != NULL) {
                xEventGroupClearBits(nfcEventGroup, (1 << 0));
                xEventGroupSetBits(nfcEventGroup, (1 << 1));
                uxBits = xEventGroupWaitBits(appEventGroup, (1 << 1), pdFALSE, pdFALSE, pdMS_TO_TICKS(1000));
                if ((uxBits & (1 << 1)) != 0) {
                    vTaskSuspend(nfcTaskHandle);
                }
            }
            
            // No task cleanup needed - using direct API calls
            
            return false;
        }

        // Check for contrast adjustment
        std::string contrastKey = getTouch(); 
        if (contrastKey == "1") {
            screen::adjustContrast(-10);
        } else if (contrastKey == "4") {
            screen::adjustContrast(10);
        }

        // Yield to prevent watchdog timer from triggering
        vTaskDelay(pdMS_TO_TICKS(10)); // Small delay to ensure proper yielding
    }

    // Payment successful
    if (paymentisMade) {
        screen::showSuccess();
        logger::write("[payment] Payment successful", "info");
        
        // Cleanup NFC if enabled
        if (config::getBool("nfcEnabled") && nfcTaskHandle != NULL) {
            xEventGroupClearBits(nfcEventGroup, (1 << 0));
            xEventGroupSetBits(nfcEventGroup, (1 << 1));
            uxBits = xEventGroupWaitBits(appEventGroup, (1 << 1), pdFALSE, pdFALSE, pdMS_TO_TICKS(1000));
            if ((uxBits & (1 << 1)) != 0) {
                vTaskSuspend(nfcTaskHandle);
            }
        }
        
        // No task cleanup needed - using direct API calls
    }

    logger::write("[payment] Returning to App Loop", "debug");
    if (config::getBool("nfcEnabled")) {
        cap.setThresholds(3, 5); // Restore normal sensitivity
    }
    connectionLoss = false;
    return paymentisMade;
}

// Task-based payment monitoring removed - using direct API calls instead

bool startUnifiedPaymentFlow(const double &amount, const std::string &pin) {
    logger::write("[payment] Starting unified payment flow v3.0.0", "info");
    
    // Always generate LNURL QR (same for both online/offline)
    std::string signedUrl = util::createLnurlPay(amount, pin);
    std::string lnurlQR = config::getString("uriSchemaPrefix") + 
                         util::toUpperCase(util::lnurlEncode(signedUrl));
    
    // Set the global qrcodeData for compatibility
    qrcodeData = lnurlQR;
    
    // Check if we're in demo mode
    if (config::getString("callbackUrl") == "https://opago-pay.com/getstarted" || 
        config::getString("apiKey.key") == "BueokH4o3FmhWmbvqyqLKz") {
        logger::write("[payment] Demo mode detected", "info");
        screen::showPaymentQRCodeScreen("https://opago-pay.com/getstarted");
        return true; // Demo mode doesn't need payment processing
    }
    
    // If we're in offline-only mode, skip online attempt
    if (offlineMode || !onlineStatus) {
        logger::write("[payment] Offline mode - showing LNURL QR only", "info");
        screen::showPaymentQRCodeScreen(lnurlQR);
        return waitForPaymentWithFallback(lnurlQR, pin);
    }
    
    // Online mode - payment status will be checked directly in waitForPaymentWithFallback
    logger::write("[payment] Online mode - will check POS status API directly", "info");
    
    // Wait for payment with fallback handling
    return waitForPaymentWithFallback(lnurlQR, pin);
}

// New refactored payment flow functions
PaymentState initializePaymentFlow(const double &amount, const std::string &pin, std::string &lnurlQR) {
    logger::write("[payment] Initializing payment flow v3.0.0", "info");
    
    // Clear any previous API PIN
    extern std::string apiReturnedPin;
    apiReturnedPin = "";
    
    // Reset payment start time for 60-minute timeout
    extern unsigned long paymentStartTime;
    paymentStartTime = millis();
    logger::write("[payment] Payment timeout set for 60 minutes", "info");
    
    // Always generate LNURL QR (same for both online/offline)
    std::string signedUrl = util::createLnurlPay(amount, pin);
    lnurlQR = config::getString("uriSchemaPrefix") + 
              util::toUpperCase(util::lnurlEncode(signedUrl));
    
    // Set the global qrcodeData for compatibility
    qrcodeData = lnurlQR;
    
    // Check if we're in demo mode
    if (config::getString("callbackUrl") == "https://opago-pay.com/getstarted" || 
        config::getString("apiKey.key") == "BueokH4o3FmhWmbvqyqLKz") {
        logger::write("[payment] Demo mode detected", "info");
        lnurlQR = "https://opago-pay.com/getstarted";
        return PaymentState::SHOWING_QR;
    }
    
    // Initialize NFC if enabled
    if (config::getBool("nfcEnabled")) {
        cap.setThresholds(5, 5);
        vTaskResume(nfcTaskHandle);
        xEventGroupClearBits(nfcEventGroup, (1 << 1));
        xEventGroupSetBits(nfcEventGroup, (1 << 0));
    }
    
    // If we're in offline-only mode, just show QR
    if (offlineMode || !onlineStatus) {
        logger::write("[payment] Offline mode - showing LNURL QR only", "info");
        return PaymentState::SHOWING_QR;
    }
    
    // Online mode - we'll check the POS status API directly in the main loop
    logger::write("[payment] Online mode - will check POS status API", "info");
    return PaymentState::MONITORING_PAYMENT;
}

PaymentState checkPaymentStatus(const std::string &lnurlQR, const std::string &pin) {
    // Check connection state
    static bool lastConnectionState = onlineStatus;
    if (lastConnectionState != onlineStatus) {
        lastConnectionState = onlineStatus;
        if (!onlineStatus) {
            return PaymentState::SHOWING_QR; // Will trigger no-wifi screen in app
        }
    }
    
    // Check if payment was already made
    if (paymentisMade) {
        return PaymentState::PAYMENT_SUCCESS;
    }
    
    // Check for 60-minute timeout (3600000 ms = 60 minutes)
    if (paymentStartTime > 0) {
        unsigned long currentTime = millis();
        const unsigned long PAYMENT_TIMEOUT_MS = 3600000; // 60 minutes
        
        // Handle millis() overflow (occurs every ~50 days)
        unsigned long elapsedTime;
        if (currentTime >= paymentStartTime) {
            elapsedTime = currentTime - paymentStartTime;
        } else {
            // millis() has overflowed
            elapsedTime = (ULONG_MAX - paymentStartTime) + currentTime + 1;
        }
        
        if (elapsedTime >= PAYMENT_TIMEOUT_MS) {
            logger::write("[payment] Payment timeout reached (60 minutes), cancelling payment", "info");
            paymentStartTime = 0; // Reset timeout
            return PaymentState::PAYMENT_CANCELLED;
        }
    }
    
    // Check for NFC card detection
    if (config::getBool("nfcEnabled")) {
        EventBits_t uxBits = xEventGroupWaitBits(appEventGroup, (1 << 0), pdFALSE, pdFALSE, 0);
        if ((uxBits & (1 << 0)) != 0) {
            logger::write("[payment] Card detected, checking payment", "info");
            vTaskDelay(pdMS_TO_TICKS(100)); // Brief delay for card processing
            return PaymentState::MONITORING_PAYMENT;
        }
    }
    
    // Check for payment via POS status API if we're online
    if (onlineStatus) {
        static unsigned long lastOnlineCheck = 0;
        const unsigned long ONLINE_CHECK_INTERVAL = 2100; // Check every 2.1 seconds
        
        unsigned long currentTime = millis();
        if (currentTime - lastOnlineCheck >= ONLINE_CHECK_INTERVAL) {
            logger::write("[payment] Checking POS status API for payment", "info");
            
            // Check POS status API for payment
            std::string posId = extractPosIdFromCallbackUrl();
            std::string apiKey = config::getString("apiKey.key");
            std::string returnedPin;
            
            if (!posId.empty() && !apiKey.empty()) {
                logger::write("[payment] Polling POS ID: " + posId, "debug");
                bool paymentReceived = checkPosPaymentStatus(posId, apiKey, pin, returnedPin);
                if (paymentReceived) {
                    logger::write("[payment] Payment confirmed via POS status API!", "info");
                    if (!returnedPin.empty()) {
                        logger::write("[payment] Using API PIN for verification: " + returnedPin, "info");
                        // Update the global API PIN for verification
                        extern std::string apiReturnedPin;
                        apiReturnedPin = returnedPin;
                    }
                    paymentisMade = true;
                    return PaymentState::PAYMENT_SUCCESS;
                }
            } else {
                logger::write("[payment] Cannot poll POS status - missing POS ID or API key", "error");
            }
            
            lastOnlineCheck = currentTime;
        }
    }
    
    return PaymentState::MONITORING_PAYMENT;
}

void cleanupPaymentFlow() {
    logger::write("[payment] Cleaning up payment flow", "info");
    
    // Clear API PIN
    extern std::string apiReturnedPin;
    apiReturnedPin = "";
    
    // Clean up NFC if enabled
    if (config::getBool("nfcEnabled") && nfcTaskHandle != NULL) {
        EventBits_t uxBits;
        xEventGroupClearBits(nfcEventGroup, (1 << 0));
        xEventGroupSetBits(nfcEventGroup, (1 << 1));
        uxBits = xEventGroupWaitBits(appEventGroup, (1 << 1), pdFALSE, pdFALSE, pdMS_TO_TICKS(1000));
        if ((uxBits & (1 << 1)) != 0) {
            vTaskSuspend(nfcTaskHandle);
        }
        cap.setThresholds(3, 5); // Restore normal sensitivity
    }
    
    // Reset payment state
    paymentisMade = false;
    connectionLoss = false;
    paymentStartTime = 0; // Reset timeout
}

bool isPaymentFlowActive() {
    return paymentisMade || (config::getBool("nfcEnabled") && nfcTaskHandle != NULL);
}

// New functions for POS status API integration
std::string extractPosIdFromCallbackUrl() {
    std::string callbackUrl = config::getString("callbackUrl");
    
    // Find the last slash in the URL
    size_t lastSlash = callbackUrl.find_last_of('/');
    if (lastSlash != std::string::npos && lastSlash < callbackUrl.length() - 1) {
        return callbackUrl.substr(lastSlash + 1);
    }
    
    logger::write("[payment] Failed to extract POS ID from callback URL: " + callbackUrl, "error");
    return "";
}

bool checkPosPaymentStatus(const std::string &posId, const std::string &apiKey, const std::string &expectedPin, std::string &returnedPin) {
    // Check if Wi-Fi is connected
    if (WiFi.status() != WL_CONNECTED) {
        logger::write("[payment] Wi-Fi not connected for POS status check", "debug");
        return false;
    }
    
    // Check if posId is not empty
    if (posId.empty()) {
        logger::write("[payment] POS ID is empty", "error");
        return false;
    }
    
    // Check if apiKey is not empty
    if (apiKey.empty()) {
        logger::write("[payment] API key is empty", "error");
        return false;
    }
    
    // Try to take the wifi semaphore before accessing wifi
    HTTPClient* http = nullptr;
    if(xSemaphoreTake(wifiSemaphore, portMAX_DELAY) == pdTRUE) {
        http = new HTTPClient();
        
        // Extract the domain from the config callback url
        std::string callbackUrl = config::getString("callbackUrl");
        size_t domainStart = callbackUrl.find("://") + 3;
        size_t domainEnd = callbackUrl.find("/", domainStart);
        std::string domain = callbackUrl.substr(domainStart, domainEnd - domainStart);
        
        // Create the status API URL
        std::string url = "https://" + domain + "/lnpos/api/v1/status/" + posId;
        logger::write("[payment] Calling POS status API: " + url, "info");
        http->begin(url.c_str());
        
        http->addHeader("accept", "application/json");
        http->addHeader("X-Api-Key", apiKey.c_str());
        
        int httpResponseCode = http->GET();
        
        logger::write("[payment] POS status API response code: " + std::to_string(httpResponseCode), "info");
        
        if (httpResponseCode == 200) {
            std::string response = http->getString().c_str();
            logger::write("[payment] POS status API response body: " + response, "info");
            
            // Parse the response
            DynamicJsonDocument doc(1024);
            DeserializationError error = deserializeJson(doc, response);
            
            if (error) {
                logger::write("[payment] Failed to parse JSON from POS status response: " + std::string(error.c_str()), "error");
                http->end();
                xSemaphoreGive(wifiSemaphore);
                delete http;
                return false;
            }
            
            // Get the payment status and PIN
            std::string status = doc["status"].as<std::string>();
            if (doc.containsKey("pin")) {
                returnedPin = doc["pin"].as<std::string>();
            }
            
            logger::write("[payment] Parsed status: '" + status + "', PIN: '" + returnedPin + "', Expected PIN: '" + expectedPin + "'", "info");
            
            http->end();
            xSemaphoreGive(wifiSemaphore);
            delete http;
            
            // Only consider payment successful if status is "paid" or "success" AND PIN matches
            if (status == "paid" || status == "success") {
                if (!returnedPin.empty() && returnedPin == expectedPin) {
                    logger::write("[payment] Payment confirmed with matching PIN", "info");
                    return true;
                } else if (returnedPin.empty()) {
                    logger::write("[payment] Payment confirmed but no PIN returned from API", "warning");
                    return true;
                } else {
                    logger::write("[payment] Payment status is '" + status + "' but PIN doesn't match. Expected: " + expectedPin + ", Got: " + returnedPin, "debug");
                    return false;
                }
            }
            
            return false;
            
        } else if (httpResponseCode == 404) {
            // Handle 404 as "still processing"
            logger::write("[payment] POS status: still processing (404)", "debug");
            http->end();
            xSemaphoreGive(wifiSemaphore);
            delete http;
            return false;
            
        } else if (httpResponseCode == 500) {
            // Handle 500 as "service error"
            logger::write("[payment] POS status: service error (500)", "error");
            http->end();
            xSemaphoreGive(wifiSemaphore);
            delete http;
            return false;
            
        } else {
            // Handle other errors
            logger::write("[payment] POS status API error: " + std::to_string(httpResponseCode), "error");
            http->end();
            xSemaphoreGive(wifiSemaphore);
            delete http;
            return false;
        }
    } else {
        logger::write("[payment] Failed to take wifi semaphore for POS status check", "error");
        delete http;
        return false;
    }
}

// pollPosStatusAPI function removed - using checkPosPaymentStatus directly
