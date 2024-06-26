#include "payment.h"

uint8_t lastRenderedQRCode = 0;
bool lastConnectionLossState = false;

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
    // Initialize variables
    paymentisMade = false;
    bool keyPressed = false;
    static unsigned long lastUpdate = 0;
    lastRenderedQRCode = millis();
    EventBits_t uxBits;
    const EventBits_t uxAllBits = ( 1 << 0 ) | ( 1 << 1 );

    //decrease sensitivity to prevent interference
    cap.setThresholds(5, 5); //configure sensitivity

    // Resume NFC task and tell it to turn on
    vTaskResume(nfcTaskHandle); // Resume the NFC task
    xEventGroupClearBits(nfcEventGroup, (1 << 1)); // Clear turn off bit
    xEventGroupSetBits(nfcEventGroup, (1 << 0)); // Set power up bit

    // Main loop: wait for payment or cancellation
    while (!paymentisMade) {
        if (millis() - lastUpdate > 2100) {
            screen::showStatusSymbols(power::getBatteryPercent());
            lastUpdate = millis();
        }
        // Wait for bits from appEventGroup
        uxBits = xEventGroupWaitBits(appEventGroup, uxAllBits, pdFALSE, pdFALSE, pdMS_TO_TICKS(210));
        // Handle the received bits
        if ((uxBits & (1 << 0)) != 0) { //card has been detected
            logger::write("[payment] Checking if paid. No cancel check.", "info");
            paymentisMade = isPaymentMade(paymentHash, apiKey);
            vTaskDelay(pdMS_TO_TICKS(2100));
            continue;
        } else { //no card detected
            // Check if NFC task is not actively processing
            if ((uxBits & (1 << 0)) == 0 && screen::getCurrentScreen() != "paymentQRCode") {
                // Display the payment QR code
                screen::showPaymentQRCodeScreen(qrcodeData);
                lastRenderedQRCode = millis();
            }
            logger::write("[payment] Checking if paid.", "info");
            paymentisMade = isPaymentMade(paymentHash, apiKey);
            if (!paymentisMade) {
                logger::write("[payment] Checking for user cancellation or contrast adjustment", "debug");
                // Check for cancellation unless nfcSuccess or x screens are showing
                if (screen::getCurrentScreen() != "nfcSuccess" && screen::getCurrentScreen() != "x") {
                    keyPressed = getLongTouch('*', 210); // Check for cancellation
                    if (keyPressed) {
                        logger::write("[payment] Payment cancelled by user.", "info");
                        screen::showX();
                        // Ensure NFC shutdown before exiting
                        xEventGroupClearBits(nfcEventGroup, (1 << 0)); // Clear power up bit
                        xEventGroupSetBits(nfcEventGroup, (1 << 1)); // Set turn off bit
                        for (int i = 0; i < 10; ++i) { // Attempt confirmation up to 10 times
                            uxBits = xEventGroupWaitBits(appEventGroup, (1 << 1), pdFALSE, pdFALSE, pdMS_TO_TICKS(1000));
                            if ((uxBits & (1 << 1)) != 0) {
                                vTaskSuspend(nfcTaskHandle); // Suspend the NFC task
                                break;
                            } else {
                                vTaskDelay(pdMS_TO_TICKS(500)); // Short delay before retry
                            }
                        }
                        if ((uxBits & (1 << 1)) == 0) {
                            logger::write("[payment] Failed to confirm NFC shutdown, restarting ESP.", "error");
                            ESP.restart();
                        }
                        return false;
                    }
                }
                // Check for contrast adjustment
                std::string contrastKey = getTouch(); 
                if (contrastKey == "1") {
                    screen::adjustContrast(-10); // Decrease contrast
                    logger::write("[payment] Contrast decreased.", "info");
                } else if (contrastKey == "4") {
                    screen::adjustContrast(10); // Increase contrast
                    logger::write("[payment] Contrast increased.", "info");
                }
                taskYIELD();
            } else {
                logger::write("[payment] Payment made, signaling NFC shutdown.", "info");
                screen::showSuccess();
                // Signal nfcTask to shut down RF and enter Idle Mode
                xEventGroupClearBits(nfcEventGroup, (1 << 0)); // Clear power up bit
                xEventGroupSetBits(nfcEventGroup, (1 << 1)); // Set turn off bit

                // Wait for confirmation that NFC reader is off and nfcTask is in Idle Mode
                for (int i = 0; i < 10; ++i) { // Attempt confirmation up to 10 times
                    uxBits = xEventGroupWaitBits(appEventGroup, (1 << 1), pdFALSE, pdFALSE, pdMS_TO_TICKS(1000));
                    if ((uxBits & (1 << 1)) != 0) {
                        vTaskSuspend(nfcTaskHandle); // Suspend the NFC task
                        break;
                    } else {
                        // Signal nfcTask to shut down RF and enter Idle Mode
                        xEventGroupClearBits(nfcEventGroup, (1 << 0)); // Clear power up bit
                        xEventGroupSetBits(nfcEventGroup, (1 << 1)); // Set turn off bit
                        vTaskDelay(pdMS_TO_TICKS(500)); // Short delay before retry
                    }
                }

                if ((uxBits & (1 << 1)) == 0) {
                    logger::write("[payment] Failed to confirm NFC shutdown, restarting ESP.", "error");
                    ESP.restart();
                }
            }
        }
    }

    // Check whether payment has been made
    if (paymentisMade) {
        screen::showSuccess();
        logger::write("[payment] Payment has been made.", "info");
    }

    logger::write("[payment] Returning to App Loop", "debug");
    //increase sensitivity again
    cap.setThresholds(3, 5); //configure sensitivity
    connectionLoss = false; //reset flag
    return paymentisMade;
}
