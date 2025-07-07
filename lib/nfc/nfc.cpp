#include "nfc.h"
#include "cap_touch.h"

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
    
    // Error 263 mitigation strategies - balanced for reliability and performance
    Wire.setClock(40000);           // 40kHz - proven reliable speed
    Wire.setTimeOut(1000);          // 1 second timeout
    
    logger::write("[nfcTask] I2C initialized: 40kHz, 1000ms timeout", "info");
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

void recoverI2CBus() {
    logger::write("[nfcTask] Attempting I2C bus recovery for Error 263 mitigation", "info");
    
    // Method 1: Reset I2C peripheral
    Wire.end();
    vTaskDelay(100);
    Wire.begin(NFC_SDA, NFC_SCL);
    Wire.setClock(40000);  // Match main initialization
    Wire.setTimeOut(1000); // Match main initialization
    
    logger::write("[nfcTask] I2C bus recovery completed", "info");
}

void setRFoff(bool turnOff, PN532_I2C* pn532_i2c) {
    uint8_t commandRFoff[3] = {0x32, 0x01, 0x00}; // RFConfiguration command to turn off the RF field
    uint8_t commandRFon[7] = { 0x02, 0x02, 0x00, 0xD4, 0x02, 0x2A, 0x00 };
    // Check the desired state
    if (turnOff && !isRfOff) {
        logger::write("[nfcTask] Powering down RF", "debug");
        vTaskDelay(21);
        // Try to turn off RF
        int rfOffResult = pn532_i2c->writeCommand(commandRFoff, sizeof(commandRFoff));
        if (rfOffResult == 0) {
            // If RF is successfully turned off, set the flag
            logger::write("[nfcTask] RF is off", "debug");
            isRfOff = true;
            logger::write("[nfcTask] RF is off - Flag set", "debug");
        } else {
            logger::write(("[nfcTask] Error powering down RF, error: " + String(rfOffResult)).c_str(), "info");
        }
    } else if (!turnOff && isRfOff) {
        logger::write("[nfcTask] Powering up RF", "debug");
        
        // Try RF power-on with Error 263 tolerance (only 2 attempts, then proceed)
        bool rfSuccess = false;
        for (int attempt = 1; attempt <= 2; attempt++) {
            logger::write(("[nfcTask] RF power-on attempt " + String(attempt) + "/2").c_str(), "debug");
            vTaskDelay(50); // Shorter delay
            
            int rfResult = pn532_i2c->writeCommand(commandRFon, sizeof(commandRFon));
            if (rfResult == 0) {
                logger::write("[nfcTask] RF power-on successful", "debug");
                setRFPower(pn532_i2c, 190); //increase power to max
                rfSuccess = true;
                break;
            } else {
                logger::write(("[nfcTask] RF power-on attempt " + String(attempt) + " error: " + String(rfResult) + " (tolerating Error 263)").c_str(), "debug");
            }
        }
        
        // Always assume RF is functional after attempts - Error 263 tolerance
        logger::write("[nfcTask] Setting RF as active (Error 263 tolerant)", "info");
        isRfOff = false;
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
        
        // Add delay between NTAG424 read attempts to reduce I2C stress
        if (i < 5) { // Don't delay after the last attempt
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        
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
    
    
    EventBits_t uxBits;
    const EventBits_t uxAllBits = (1<<0) | (1<<1);
    int loopCounter = 0;
    bool loggedWaiting = false;
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
        
        // Log waiting message only once
        if (!loggedWaiting) {
            logger::write("[nfcTask] Starting main loop - waiting for payment activation", "info");
            loggedWaiting = true;
        }
        
        // Wait for payment activation signal before starting NFC polling
        uxBits = xEventGroupWaitBits(nfcEventGroup, uxAllBits, pdFALSE, pdFALSE, pdMS_TO_TICKS(1000));
        logger::write(("[nfcTask] Received bits: " + String(uxBits, BIN) + " (decimal: " + String(uxBits) + ")").c_str(), "debug");
        
        // Only proceed if NFC is enabled and we have activation signal
        if (!config::getBool("nfcEnabled")) {
            logger::write("[nfcTask] NFC disabled in config, task will idle", "info");
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }
        
        logger::write(("[nfcTask] Checking activation bit: uxBits=" + String(uxBits) + ", bit0=" + String((uxBits & (1 << 0)))).c_str(), "debug");
        if ((uxBits & (1 << 0)) != 0) {
            loggedWaiting = false; // Reset waiting flag when entering payment mode
            logger::write("[nfcTask] Payment mode activated - starting NFC polling", "info");
            
            // Reset hybrid payment state for new payment session
            hybridBolt11Invoice = "";
            hybridInvoiceFetched = false;
            logger::write("[nfcTask] Hybrid payment state reset for new payment session", "info");
            
            logger::write("[nfcTask] Checking RF status", "debug");
            setRFoff(false, pn532_i2c);
            logger::write(("[nfcTask] After setRFoff(false), isRfOff=" + String(isRfOff)).c_str(), "debug");
            if (isRfOff) 
            {
                xEventGroupClearBits(appEventGroup, 0xFF); // Clear all bits
                xEventGroupSetBits(appEventGroup, (1<<1)); // NFC RF is off
                logger::write("[nfcTask] NFC task is idle and RF is off - continuing outer loop", "info");
                taskYIELD();
                continue;
            }
            logger::write("[nfcTask] RF is active, starting payment mode polling", "info");
            logger::write("[nfcTask] *** CHECKPOINT 1: About to initialize readAttempts ***", "info");
            int readAttempts = 0;
            logger::write("[nfcTask] *** CHECKPOINT 2: readAttempts initialized ***", "info");
            
            // Payment mode polling loop - only active during payment
            logger::write("[nfcTask] *** CHECKPOINT 3: About to enter while loop ***", "info");
            
            // Suppress all touch input during NFC polling to prevent RF interference
            suppressTouchDuringNFC(true);
            logger::write("[nfcTask] Touch input suppressed for entire NFC polling period", "info");
            
            while (1) 
            {
                logger::write("[nfcTask] *** CHECKPOINT 4: Inside polling loop iteration ***", "info");
                
                // Check for shutdown signal only (non-blocking)
                logger::write("[nfcTask] Checking for shutdown signal", "debug");
                uxBits = xEventGroupWaitBits(nfcEventGroup, (1 << 1), pdFALSE, pdFALSE, 0);
                logger::write(("[nfcTask] Event bits received: " + String(uxBits)).c_str(), "debug");
                if ((uxBits & (1 << 1)) != 0) 
                {
                    logger::write("[nfcTask] *** SHUTDOWN SIGNAL RECEIVED - EXITING LOOP ***", "info");
                    suppressTouchDuringNFC(false);
                    logger::write("[nfcTask] Touch input re-enabled before shutdown", "info");
                    idleMode(pn532_i2c);
                    break;
                }
                logger::write("[nfcTask] No shutdown signal, continuing to poll", "debug");

                // Wait for an ISO14443A type cards (Mifare, etc.). When one is found
                // 'uid' will be populated with the UID, and uidLength will indicate
                // if the uid is 4 bytes (Mifare Classic) or 7 bytes (Mifare Ultralight)
                loopCounter++;
                logger::write(("[nfcTask] *** POLLING ATTEMPT " + String(loopCounter) + " ***").c_str(), "info");
                
                // Simple card detection - no complex checks
                logger::write("[nfcTask] About to call readPassiveTargetID", "info");
                
                // Direct polling with adaptive timeout - longer after RF cycling
                unsigned long startTime = millis();
                // Use longer timeout right after RF cycling (when loopCounter % 3 == 1), shorter for normal polls
                uint16_t timeout = ((loopCounter % 3) == 1) ? 700 : 400; // Longer timeout right after RF cycle
                success = nfc->readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, timeout);
                unsigned long duration = millis() - startTime;
                
                logger::write(("[nfcTask] *** readPassiveTargetID COMPLETED in " + String(duration) + "ms, success=" + String(success) + " ***").c_str(), "info");
                
                // Cycle RF more frequently to wake up stationary NTAG424 cards
                if (!success && (loopCounter % 3 == 0)) {
                    logger::write("[nfcTask] No card detected after 3 attempts - cycling RF aggressively", "info");
                    
                    // More complete RF cycle - turn off for longer
                    setRFoff(true, pn532_i2c);
                    logger::write(("[nfcTask] RF off, isRfOff=" + String(isRfOff)).c_str(), "debug");
                    vTaskDelay(pdMS_TO_TICKS(150)); // Longer RF off period for complete power cycle
                    
                    setRFoff(false, pn532_i2c);
                    logger::write(("[nfcTask] RF on requested, isRfOff=" + String(isRfOff)).c_str(), "debug");
                    
                    // Verify RF is actually back on
                    if (isRfOff) {
                        logger::write("[nfcTask] RF cycling failed - forcing RF on", "info");
                        setRFoff(false, pn532_i2c); // Force retry
                        vTaskDelay(pdMS_TO_TICKS(50));
                    }
                    
                    vTaskDelay(pdMS_TO_TICKS(100)); // Longer stabilization time
                    
                    // Complete RF reinitialization every 10th attempt for stubborn cards
                    if (loopCounter % 10 == 0) {
                        logger::write("[nfcTask] Performing complete RF reinitialization", "info");
                        
                        // Reinitialize SAM configuration for clean RF field
                        if (pn532->SAMConfig()) {
                            logger::write("[nfcTask] SAM reconfiguration successful", "debug");
                        } else {
                            logger::write("[nfcTask] SAM reconfiguration failed", "info");
                        }
                        
                        vTaskDelay(pdMS_TO_TICKS(100)); // Extra stabilization after full reset
                    }
                    
                    logger::write("[nfcTask] RF cycling complete", "debug");
                }
                
                // Even if Error 263 occurred internally, don't let it stop the polling loop
                // The PN532 library might return 0 (false) due to I2C timeouts, but RF is still functional
                
                logger::write("[nfcTask] Checking if card was detected...", "info");
                if (success) {
                    // We seem to have a tag present
                    logger::write(("[nfcTask] *** CARD DETECTED! UID Length: " + String(uidLength) + " bytes ***").c_str(), "info");
                    String uidStr = "[nfcTask] UID Value: ";
                    for (uint8_t i = 0; i < uidLength; i++) {
                        if (uid[i] < 0x10) uidStr += "0";
                        uidStr += String(uid[i], HEX);
                        if (i < uidLength - 1) uidStr += " ";
                    }
                    logger::write(uidStr.c_str(), "info");
                    screen::showNFC();
                    
                    // HYBRID PAYMENT: Fetch bolt11 invoice from LNURL-pay on first card detection
                    if (!hybridInvoiceFetched && (qrcodeData.find("LNURL") == 0 || qrcodeData.find("lightning:") == 0)) {
                        logger::write("[nfcTask] Card detected - fetching bolt11 invoice from LNURL-pay for hybrid payment", "info");
                        
                        // Extract the LNURL from the qrcodeData  
                        std::string lnurlPayUrl;
                        if (qrcodeData.find("lightning:") == 0) {
                            lnurlPayUrl = qrcodeData.substr(10); // Remove "lightning:" prefix
                        } else {
                            lnurlPayUrl = qrcodeData; // Already just the LNURL
                        }
                        
                        // Decode the LNURL to get the actual URL  
                        std::string decodedUrl = Lnurl::decode(lnurlPayUrl);
                        logger::write((std::string("[nfcTask] Decoded LNURL-pay URL: ") + decodedUrl).c_str(), "info");
                        
                        // Fetch the bolt11 invoice
                        hybridBolt11Invoice = requestInvoice(decodedUrl);
                        
                        if (!hybridBolt11Invoice.empty()) {
                            logger::write("[nfcTask] Successfully fetched bolt11 invoice for hybrid payment", "info");
                            logger::write((std::string("[nfcTask] Invoice: ") + hybridBolt11Invoice.substr(0, 50) + "...").c_str(), "debug");
                            hybridInvoiceFetched = true;
                        } else {
                            logger::write("[nfcTask] Failed to fetch bolt11 invoice - will use LNURL-pay directly", "info");
                        }
                    }
                    
                    // Reading and processing the NFC data
                    xEventGroupSetBits(appEventGroup, (1<<0)); // NFC task is actively processing
                    bool result = readAndProcessNFCData(pn532_i2c, pn532, nfc, nfcAdapter, readAttempts);
                    if (result) 
                    {
                        logger::write("[nfcTask] NFC card reading exited with success", "info");
                        
                        // Use bolt11 invoice for hybrid payment if available, otherwise fallback to original qrcodeData
                        std::string paymentData = hybridInvoiceFetched && !hybridBolt11Invoice.empty() ? 
                                                hybridBolt11Invoice : qrcodeData;
                        
                        if (hybridInvoiceFetched && !hybridBolt11Invoice.empty()) {
                            logger::write("[nfcTask] Using payment data: bolt11 invoice", "info");
                        } else {
                            logger::write("[nfcTask] Using payment data: LNURL-pay", "info");
                        }
                        
                        // Try to withdraw from lnurlw
                        bool withdrawSuccess = withdrawFromLnurlw(lnurlwNFC, paymentData);
                        if (withdrawSuccess)
                        {
                            logger::write("[nfcTask] Withdraw from lnurlw exited with success", "debug");
                            screen::showSuccess();
                            suppressTouchDuringNFC(false);
                            logger::write("[nfcTask] Touch input re-enabled after successful withdraw", "info");
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
                                suppressTouchDuringNFC(false);
                                logger::write("[nfcTask] Touch input re-enabled after invoice paid", "info");
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
                    
                    // Brief delay after processing card before next poll
                    vTaskDelay(pdMS_TO_TICKS(200));
                    // Continue polling for more cards instead of breaking
                }
                else 
                {
                    // No card detected - just continue polling
                    logger::write(("[nfcTask] *** NO CARD DETECTED (attempt " + String(loopCounter) + ") ***").c_str(), "info");
                    
                    // Balanced delay for I2C recovery and responsiveness 
                    vTaskDelay(pdMS_TO_TICKS(150)); // Balanced timing
                    logger::write("[nfcTask] *** END OF LOOP ITERATION - CONTINUING ***", "info");
                }
            }
            
            // Safety: Re-enable touch if we somehow exit the polling loop
            suppressTouchDuringNFC(false);
            logger::write("[nfcTask] Touch input re-enabled (safety fallback)", "info");
        } else {
            // Not in payment mode - ensure RF is OFF to prevent keyboard disruption
            logger::write("[nfcTask] Not in payment mode - ensuring RF is OFF", "debug");
            setRFoff(true, pn532_i2c);
            if (isRfOff) {
                xEventGroupClearBits(appEventGroup, 0xFF);
                xEventGroupSetBits(appEventGroup, (1<<1)); // Signal RF is off
            }
            logger::write("[nfcTask] Completed non-payment mode handling", "debug");
        }
    }
}







