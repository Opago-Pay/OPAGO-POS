#include "nfc.h"

bool isRfOff = false;

void printTaskState(TaskHandle_t taskHandle) {
    eTaskState taskState = eTaskGetState(taskHandle);

    Serial.print("Task state: ");

    switch(taskState) {
        case eReady:
            logger::write("Task is ready to run", "debug");
            break;
        case eRunning:
            logger::write("Task is currently running", "debug");
            break;
        case eBlocked:
            logger::write("Task is blocked", "debug");
            break;
        case eSuspended:
            logger::write("Task is suspended", "debug");
            break;
        case eDeleted:
            logger::write("Task is being deleted", "debug");
            break;
        default:
            logger::write("Unknown state", "debug");
            break;
    }
}

bool initNFC(PN532_I2C** pn532_i2c, Adafruit_PN532** nfc, PN532** pn532, NfcAdapter** nfcAdapter) {
    pinMode(2, OUTPUT);
    digitalWrite(2, HIGH);
    // Initialize the IRQ pin as input
    pinMode(NFC_IRQ, INPUT);
    // Initialize the RST pin as output
    pinMode(NFC_RST, OUTPUT);
    // Set the RST pin to HIGH to reset the module
    digitalWrite(NFC_RST, HIGH);
    vTaskDelay(21);
    // Set the RST pin to LOW to finish the reset
    digitalWrite(NFC_RST, LOW);
    Serial.println("[nfcTask] Initializing NFC ...");
    // Initialize the I2C bus with the correct SDA and SCL pins
    Wire.begin(NFC_SDA, NFC_SCL);
    Wire.setClock(40000);
    // Initialize the PN532_I2C object with the initialized Wire object
    *pn532_i2c = new PN532_I2C(Wire);
    // Initialize the PN532 object with the initialized PN532_I2C object
    *pn532 = new PN532(**pn532_i2c);
    //initialize NFC Adapter object
    *nfcAdapter = new NfcAdapter(**pn532_i2c);

    // Initialize the Adafruit_PN532 object with the initialized PN532_I2C object
    *nfc = new Adafruit_PN532(NFC_IRQ, NFC_RST);

    // Use the  pointer to call begin() and SAMConfig()
    // Try to initialize the NFC reader
    (*pn532)->begin();
    (*pn532)->SAMConfig();

    scanDevices(&Wire);
    int error = Wire.endTransmission();
    if (error == 0) {
        uint32_t versiondata = (*pn532)->getFirmwareVersion();
        if (! versiondata) {
            Serial.println("[nfcTask] Didn't find PN53x board");
            return false;
        }
        // Got ok data, print it out!
        String message = "[nfcTask] Found chip PN5" + String((versiondata >> 24) & 0xFF, HEX);
        Serial.println(message.c_str());
        message = "[nfcTask] Firmware ver. " + String((versiondata >> 16) & 0xFF, DEC);
        Serial.println(message.c_str());
        message = "[nfcTask] Firmware ver. " + String((versiondata >> 8) & 0xFF, DEC);
        Serial.println(message.c_str());
        setRFoff(true, *pn532_i2c);
        return true;
    } else {
        String message = "[nfcTask] Wire error: " + String(error);
        Serial.println(message.c_str());
        return false;
    } 
}

void setRFoff(bool turnOff, PN532_I2C* pn532_i2c) {
    uint8_t commandRFoff[3] = {0x32, 0x01, 0x00}; // RFConfiguration command to turn off the RF field
    uint8_t commandRFon[7] = { 0x02, 0x02, 0x00, 0xD4, 0x02, 0x2A, 0x00 };
    // Check the desired state
    if (turnOff && !isRfOff) {
        logger::write("[nfcTask] Powering down RF", "debug");
        vTaskDelay(21);
        // Try to turn off RF
        if (pn532_i2c->writeCommand(commandRFoff, sizeof(commandRFoff)) == 0) {
            // If RF is successfully turned off, set the flag
            logger::write("[nfcTask] RF is off", "debug");
            isRfOff = true;
            logger::write("[nfcTask] RF is off - Flag set", "debug");
        } else {
            logger::write("[nfcTask] Error powering down RF", "debug");
        }
    } else if (!turnOff && isRfOff) {
        logger::write("[nfcTask] Powering up RF", "debug");
        // Try to turn on RF
        vTaskDelay(21);
        if (pn532_i2c->writeCommand(commandRFon, sizeof(commandRFon)) == 0) {
            // If RF is successfully turned on, clear the flag
            logger::write("[nfcTask] RF is on", "debug");
            setRFPower(pn532_i2c, 190); //increase power to max
            isRfOff = false;
        } else {
            logger::write("[nfcTask] Error powering up RF", "debug");
            ESP.restart();
        }
    } else {
        logger::write("[nfcTask] RF already in desired state", "debug");
    }
}

bool setRFPower(PN532_I2C* pn532_i2c, int power, int gain, int modulation, int miller) {
    // Ensure power is within the valid range (0x00 to 0xFF)
    if (power < 0) power = 0;
    if (power > 255) power = 255;

    // Ensure gain is within the valid range (0x00 to 0x7F)
    if (gain < 0) gain = 0;
    if (gain > 0x7F) gain = 0x7F;

    // Ensure modulation is within the valid range (0x00 to 0x03)
    if (modulation < 0) modulation = 0;
    if (modulation > 0x03) modulation = 0x03;

    // Ensure miller is within the valid range (0x00 to 0x0F)
    if (miller < 0) miller = 0;
    if (miller > 0x0F) miller = 0x0F;

    // Create the command array
    uint8_t command[] = {0x32, 0x05, static_cast<uint8_t>(power)};

    // Send the command to the PN532 and check if it was successful
    int result = pn532_i2c->writeCommand(command, sizeof(command));

    // Log the result
    if (result == 0) {
        logger::write(("Successfully set RF power to " + String(power)).c_str(), "debug");
    } else {
        logger::write("Failed to set RF power", "debug");
    }

    // Set modulation index
    uint8_t modulationCommand[] = {0x32, 0x02, static_cast<uint8_t>(modulation)};
    result = pn532_i2c->writeCommand(modulationCommand, sizeof(modulationCommand));
    if (result == 0) {
        logger::write(("Successfully set modulation index to " + String(modulation)).c_str(), "debug");
    } else {
        logger::write("Failed to set modulation index", "debug");
    }

    // Set number of Miller coding pulses
    uint8_t millerCommand[] = {0x32, 0x03, static_cast<uint8_t>(miller)};
    result = pn532_i2c->writeCommand(millerCommand, sizeof(millerCommand));
    if (result == 0) {
        logger::write(("Successfully set Miller coding pulses to " + String(miller)).c_str(), "debug");
    } else {
        logger::write("Failed to set Miller coding pulses", "debug");
    }

    // Set RxGain in RFCfg register
    uint8_t rxGainCommand[] = {0x32, 0x0A, static_cast<uint8_t>(gain)};
    result = pn532_i2c->writeCommand(rxGainCommand, sizeof(rxGainCommand));
    if (result == 0) {
        logger::write(("Successfully set RxGain to " + String(gain, HEX)).c_str(), "debug");
    } else {
        logger::write("Failed to set RxGain", "debug");
    }

    // Return true if all commands were successful, false otherwise
    return (result == 0);
}

void scanDevices(TwoWire *w)
{
    uint8_t err, addr;
    int nDevices = 0;
    uint32_t start = 0;
    for (addr = 1; addr < 127; addr++) {
        start = millis();
        w->beginTransmission(addr); delay(2);
        err = w->endTransmission();
        delay(10);
        if (err == 0) {
            nDevices++;
            String message = "[nfcTask] I2C device found at address 0x";
            if (addr < 16) {
                message += "0";
            }
            message += String(addr, HEX);
            logger::write(message.c_str(), "debug");
            break;

        } else if (err == 4) {
            String message = "[nfcTask] Unknown error at address 0x";
            if (addr < 16) {
                message += "0";
            }
            message += String(addr, HEX);
            logger::write(message.c_str(), "debug");
        }
    }
    if (nDevices == 0)
        logger::write("[nfcTask] No I2C devices found\n", "debug");
}

bool isLnurlw(void) {
    logger::write("[nfcTask] Checking if URL is lnurlw", "info");

    // Check if lnurlwNFC starts with "lnurlw:", "lnurl:", "lnurlp:", "lightning:", or is a bech32-encoded LNURL
    if (lnurlwNFC.startsWith("lnurlw:")) {
        String rest = lnurlwNFC.substring(7);
        if (rest.startsWith("http") || rest.startsWith("https")) {
            lnurlwNFC.replace("lnurlw:", "");
        } else if (rest.startsWith("//")) {
            lnurlwNFC.replace("lnurlw://", "https://");
        } else {
            if (rest.startsWith("lnurl") || rest.startsWith("LNURL")) {
                lnurlwNFC = String(Lnurl::decode(rest.c_str()).c_str());
            } else {
                return false;
            }
        }
    } else if (lnurlwNFC.startsWith("lnurl:")) {
        String rest = lnurlwNFC.substring(6);
        if (rest.startsWith("http") || rest.startsWith("https")) {
            lnurlwNFC.replace("lnurl:", "");
        } else if (rest.startsWith("//")) {
            lnurlwNFC.replace("lnurl://", "https://");
        } else {
            if (rest.startsWith("lnurl") || rest.startsWith("LNURL")) {
                lnurlwNFC = String(Lnurl::decode(rest.c_str()).c_str());
            } else {
                return false;
            }
        }
    } else if (lnurlwNFC.startsWith("lnurlp:")) {
        logger::write("[nfcTask] URL is not lnurlw, it's lnurlp", "info");
        return false;
    } else if (lnurlwNFC.startsWith("lightning:")) {
        String rest = lnurlwNFC.substring(10);
        if (rest.startsWith("http") || rest.startsWith("https")) {
            lnurlwNFC.replace("lightning:", "");
        } else if (rest.startsWith("//")) {
            lnurlwNFC.replace("lightning://", "https://");
        } else {
            if (rest.startsWith("lnurl") || rest.startsWith("LNURL")) {
                lnurlwNFC = String(Lnurl::decode(rest.c_str()).c_str());
            } else {
                // Handle bech32-encoded LNURL prefixed with "lightning:"
                lnurlwNFC = String(Lnurl::decode(rest.c_str()).c_str());
            }
        }
    } else if (lnurlwNFC.startsWith("LNURL")) {
        // Handle bech32-encoded LNURL without prefix
        lnurlwNFC = String(Lnurl::decode(lnurlwNFC.c_str()).c_str());
    } else if (lnurlwNFC.startsWith("http") && !lnurlwNFC.startsWith("https")) {
        lnurlwNFC.replace("http", "https");
    }

    // Check if the URL starts with "https://"
    if (lnurlwNFC.startsWith("https://")) {
        logger::write("[nfcTask] URL is lnurlw", "info");
        screen::showNFCsuccess();
        return true;
    } else {
        logger::write("[nfcTask] URL is not lnurlw", "info");
        screen::showNFCfailed();
        return false;
    }
}

void idleMode(PN532_I2C *pn532_i2c)
{
    setRFoff(true, pn532_i2c);
    while (!isRfOff) 
    {
        setRFoff(true, pn532_i2c);
        xEventGroupClearBits(appEventGroup, 0xFF); // Signal an empty app event group as long as RF is on
        vTaskDelay(pdMS_TO_TICKS(420)); 
    }
    while (isRfOff) 
    {
        logger::write("[nfcTask] NFC reader turned off, returning to NFC idling mode", "info");
        xEventGroupClearBits(appEventGroup, 0xFF);
        xEventGroupSetBits(appEventGroup, (1<<1)); // Signal bit 1 once RF is off
        vTaskDelay(pdMS_TO_TICKS(420)); 
        if (xEventGroupGetBits(nfcEventGroup) & (1 << 0)) 
        {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(420)); 
    }
}

bool sendQRCodeToPhone(PN532_I2C* pn532_i2c, const String& qrcodeData) {
    // Set the PN532 to card emulation mode
    uint8_t command[] = {0x8C, 0x00}; // TgInitAsTarget command
    uint8_t response[256]; // Buffer to hold the response
    int result = pn532_i2c->writeCommand(command, sizeof(command), response, (uint16_t)sizeof(response));

    if (result == 0) {
        logger::write("PN532 set to card emulation mode", "debug");

        // Prepare the NDEF message with the QR code data
        uint8_t ndefMessage[512]; // Increase the buffer size to accommodate longer data
        int ndefMessageLength = 0;

        // Add the NDEF message header
        ndefMessage[ndefMessageLength++] = 0x03; // NDEF message flag
        ndefMessage[ndefMessageLength++] = 0x00; // NDEF message length (placeholder)

        // Add the NDEF record header
        ndefMessage[ndefMessageLength++] = 0x01; // NDEF record header (TNF=0x01:Well-known record, SR=1:Short record)
        ndefMessage[ndefMessageLength++] = 0x01; // NDEF record type length
        ndefMessage[ndefMessageLength++] = 'T'; // NDEF record type (Text)
        ndefMessage[ndefMessageLength++] = qrcodeData.length(); // NDEF record payload length

        // Add the NDEF record payload (QR code data)
        memcpy(ndefMessage + ndefMessageLength, qrcodeData.c_str(), qrcodeData.length());
        ndefMessageLength += qrcodeData.length();

        // Update the NDEF message length
        ndefMessage[1] = ndefMessageLength - 2;

        // Set the NDEF message for the emulated tag
        uint8_t setDataCommand[512] = {0x8E, 0x00, 0x00, 0x00}; // TgSetData command
        memcpy(setDataCommand + 2, ndefMessage, ndefMessageLength);
        result = pn532_i2c->writeCommand(setDataCommand, ndefMessageLength + 2, response, (uint16_t)sizeof(response));

        if (result == 0) {
            logger::write("NDEF message set for emulated tag", "debug");

            // Set the emulated tag to be a generic NFC tag
            uint8_t tagTypeCommand[] = {0x8C, 0x02, 0x00, 0x00}; // TgInitAsTarget command with generic target parameters
            result = pn532_i2c->writeCommand(tagTypeCommand, sizeof(tagTypeCommand), response, (uint16_t)sizeof(response));

            if (result == 0) {
                logger::write("Emulated tag set as generic NFC tag", "debug");
                return true;
            } else {
                logger::write("Failed to set emulated tag as generic NFC tag", "debug");
                return false;
            }
        } else {
            logger::write("Failed to set NDEF message for emulated tag", "debug");
            return false;
        }
    }

    return false;
}

bool readAndProcessNFCData(PN532_I2C *pn532_i2c, PN532 *pn532, Adafruit_PN532 *nfc, NfcAdapter *nfcAdapter, int &readAttempts)
{
    NdefMessage message;
    int recordCount;
    uint8_t cardType = nfc->ntag424_isNTAG424();

    // Always try to read as NTAG424 first
    uint8_t data[256];
    uint8_t bytesread = 0;
    
    for (int i = 0; i < 6; i++) {
        bytesread = nfc->ntag424_ISOReadFile(data);
        if (bytesread > 0) {
            break;
        }
        readAttempts++;
        if (readAttempts >= 12) {
            setRFoff(true, pn532_i2c); // Switch off RF
            if (isRfOff) {
                return false;
            }
        }
    }

    if (bytesread > 0) {
        if (data[bytesread - 1] != '\0') { // Check if the last character is not a null terminator
            data[bytesread] = '\0'; // Manually add a null terminator at the end of the read data
        }
        // Extract URL from data
        lnurlwNFC = String((char*)data);
        Serial.println("URL from NTAG424: " + lnurlwNFC);
        if (isLnurlw()) {
            while (!isRfOff) {
                setRFoff(true, pn532_i2c); // Attempt to switch off RF
            }
            return true;
        }
    } else if (!cardType) {
        // If card type is not NTAG424, try the alternate process
        for (int i = 0; i < 3; i++) {
            NfcTag tag = nfcAdapter->read();
            String tagType = tag.getTagType();
            Serial.println("[nfcTask] Tag type read");
            Serial.println("Tag Type: " + tagType);
            if (tag.hasNdefMessage()) {
                message = tag.getNdefMessage();
                recordCount = message.getRecordCount();
            }
            bool lnurlwFound = false;
            for (int j = 0; j < recordCount && !lnurlwFound; j++) {
                NdefRecord record = message.getRecord(j);
                String recordType = record.getType();
                String logMessage = "Record Type: " + recordType;
                Serial.println(logMessage);
                if (recordType == "U" || recordType == "T") { 
                    uint8_t payload[record.getPayloadLength() + 1] = {0}; // Added +1 to the size and initialized to 0 to ensure null termination
                    record.getPayload(payload);
                    String recordPayload;
                    if (recordType == "U") {
                        switch (payload[0]) {
                            case 0x01:
                                recordPayload = "http://www.";
                                break;
                            case 0x02:
                                recordPayload = "https://www.";
                                break;
                            case 0x03:
                                recordPayload = "http://";
                                break;
                            case 0x04:
                                recordPayload = "https://";
                                break;
                            default:
                                recordPayload = String((char*)payload);
                                break;
                        }
                        recordPayload += String((char*)&payload[1]);
                    } else {
                        for (int k = 0; k < record.getPayloadLength(); k++) {
                            recordPayload += (char)payload[k];
                        }
                    }
                    lnurlwNFC = recordPayload;
                    String logMessage = "Record Payload: " + recordPayload;
                    Serial.println(logMessage);
                    if (isLnurlw()) {
                        return true;
                    }
                }
            }
            readAttempts++;
            if (readAttempts >= 6) {
                setRFoff(true, pn532_i2c); // Switch off RF
                if (isRfOff) {
                    return false;
                }
            }
        }
    }
    
    lnurlwNFC = ""; // Reset the global variable if it's not lnurlw
    return false;
}

// nfcTask
// The bits in the event groups represent the following states:
// appEventGroup:
// Bit 0 (1 << 0): Indicates nfcTask is actively processing.
// Bit 1 (1 << 1): Confirmation bit for the successful shutdown of the RF module and transition into idle mode in nfcTask.
// nfcEventGroup:
// Bit 0 (1 << 0): Instructs nfcTask to power up or remain active.
// Bit 1 (1 << 1): Commands nfcTask to turn off the RF functionality and transition to idle mode. This bit is used particularly after a successful payment is processed.

void nfcTask(void *args) 
{
    logger::write("[nfcTask] Starting NFC reader", "debug");
    uint8_t success;
    uint8_t uid[] = { 0, 0, 0, 0, 0, 0, 0 };
    uint8_t uidLength;
    Adafruit_PN532 *nfc = NULL;
    PN532_I2C *pn532_i2c = NULL;
    PN532 *pn532 = NULL;
    NfcAdapter *nfcAdapter = NULL;
    
    logger::write("[nfcTask] Initializing NFC", "debug");
    initFlagNFC = initNFC(&pn532_i2c, &nfc, &pn532, &nfcAdapter);
    if (!initFlagNFC) 
    {
        logger::write("[nfcTask] NFC initialization failed", "info");
    } else 
    {
        logger::write("[nfcTask] NFC initialization successful", "info");
    } 
    vTaskSuspend(NULL);
    
    
    EventBits_t uxBits;
    const EventBits_t uxAllBits = (1<<0) | (1<<1);
    int loopCounter = 0;
    while (1) 
    {
        while (!initFlagNFC) 
        {
            logger::write("[nfcTask] Attempting to initialize NFC", "debug");
            initFlagNFC = initNFC(&pn532_i2c, &nfc, &pn532, &nfcAdapter);
            if (!initFlagNFC) 
            {
                logger::write("[nfcTask] NFC initialization failed, retrying...", "debug");
                vTaskDelay(pdMS_TO_TICKS(1000)); // Delay for a second before retrying
            }
        }
        logger::write("[nfcTask] Starting main loop", "debug");
        uxBits = xEventGroupWaitBits(nfcEventGroup, uxAllBits, pdFALSE, pdFALSE, 0);
        logger::write(("[nfcTask] Received bits: " + String(uxBits, BIN)).c_str(), "debug");
        
        // NFC task is active
        xEventGroupClearBits(appEventGroup, 0xFF); // Clear all bits first

        if ((uxBits & (1 << 0)) != 0) 
        {
            logger::write("[nfcTask] Checking RF status", "debug");
            setRFoff(false, pn532_i2c);
            if (isRfOff) 
            {
                xEventGroupClearBits(appEventGroup, 0xFF); // Clear all bits
                xEventGroupSetBits(appEventGroup, (1<<1)); // NFC RF is off
                logger::write("[nfcTask] NFC task is idle and RF is off", "debug");
                taskYIELD();
                continue;
            }
            logger::write("[nfcTask] Waiting for NFC tag", "debug");
            int readAttempts = 0;
            while (1) 
            {
                uxBits = xEventGroupWaitBits(nfcEventGroup, uxAllBits, pdFALSE, pdFALSE, 0);
                logger::write(("[nfcTask] Received bits: " + String(uxBits, BIN)).c_str(), "debug");
                if ((uxBits & (1 << 1)) != 0) 
                {
                    idleMode(pn532_i2c);
                } 
                else 
                {
                    logger::write("[nfcTask] No shutdown signal", "debug");
                }
                if ((uxBits & (1 << 0)) == 0) 
                {
                    xEventGroupClearBits(appEventGroup, (1<<0)); // NFC task is not actively processing
                    break;
                }

                // Wait for an ISO14443A type cards (Mifare, etc.). When one is found
                // 'uid' will be populated with the UID, and uidLength will indicate
                // if the uid is 4 bytes (Mifare Classic) or 7 bytes (Mifare Ultralight)
                logger::write("[nfcTask] Checking for tag", "debug");
                success = nfc->readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 420);
                
                if (success) {
                    // We seem to have a tag present
                    logger::write("[nfcTask] Tag detected.", "debug");
                    screen::showNFC();
                    
                    // Reading and processing the NFC data
                    xEventGroupSetBits(appEventGroup, (1<<0)); // NFC task is actively processing
                    bool result = readAndProcessNFCData(pn532_i2c, pn532, nfc, nfcAdapter, readAttempts);
                    if (result) 
                    {
                        logger::write("[nfcTask] NFC card reading exited with success", "info");
                        // Try to withdraw from lnurlw
                        bool withdrawSuccess = withdrawFromLnurlw(lnurlwNFC, qrcodeData);
                        if (withdrawSuccess)
                        {
                            logger::write("[nfcTask] Withdraw from lnurlw exited with success", "debug");
                            screen::showSuccess();
                            idleMode(pn532_i2c); // Enter idle mode
                        }
                        else
                        {
                            logger::write("[nfcTask] Withdraw from lnurlw exited with failure", "debug");
                            screen::showSand();
                            vTaskDelay(pdMS_TO_TICKS(4200)); // Wait for 4.2 seconds
                            if (!paymentisMade) 
                            {
                                logger::write("[nfcTask] Invoice not paid", "info");
                                screen::showX();
                                xEventGroupClearBits(appEventGroup, (1<<0)); // NFC task is not actively processing
                                screen::showPaymentQRCodeScreen(qrcodeData);
                            }
                            else 
                            {
                                logger::write("[nfcTask] Invoice paid", "info");
                                screen::showSuccess();
                                idleMode(pn532_i2c); // Enter idle mode
                            }
                        }
                        lnurlwNFC = "";
                    } 
                    else 
                    {
                        logger::write("[nfcTask] NFC card reading exited with failure", "debug");
                        screen::showNFCfailed();
                        lnurlwNFC = "";
                        vTaskDelay(pdMS_TO_TICKS(1200));
                        xEventGroupClearBits(appEventGroup, (1<<0)); // NFC task is not actively processing
                    }
                    
                    break;
                }
                else 
                {
                    // No card detected, listen for phone commands and send QR code data
                    if (sendQRCodeToPhone(pn532_i2c, qrcodeData.c_str())) {
                        logger::write("QR code data sent to phone successfully", "debug");
                    } else {
                        logger::write("Failed to send QR code data to phone", "debug");
                    }
                    vTaskDelay(pdMS_TO_TICKS(100));
                }
            }     
        }
    }
}







