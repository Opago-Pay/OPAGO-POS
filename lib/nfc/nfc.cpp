#include "nfc.h"

bool isRfOff = false;

void printTaskState(TaskHandle_t taskHandle) {
    eTaskState taskState = eTaskGetState(taskHandle);

    Serial.print("Task state: ");

    switch(taskState) {
        case eReady:
            logger::write("Task is ready to run");
            break;
        case eRunning:
            logger::write("Task is currently running");
            break;
        case eBlocked:
            logger::write("Task is blocked");
            break;
        case eSuspended:
            logger::write("Task is suspended");
            break;
        case eDeleted:
            logger::write("Task is being deleted");
            break;
        default:
            logger::write("Unknown state");
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
    Wire.setClock(20000);
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
        logger::write("[nfcTask] Powering down RF");
        // Try to turn off RF
        if (pn532_i2c->writeCommand(commandRFoff, sizeof(commandRFoff)) == 0) {
            // If RF is successfully turned off, set the flag
            logger::write("[nfcTask] RF is off");
            isRfOff = true;
            logger::write("[nfcTask] RF is off - Flag set");
        } else {
            logger::write("[nfcTask] Error powering down RF");
        }
    } else if (!turnOff && isRfOff) {
        logger::write("[nfcTask] Powering up RF");
        // Try to turn on RF
        vTaskDelay(21);
        if (pn532_i2c->writeCommand(commandRFon, sizeof(commandRFon)) == 0) {
            // If RF is successfully turned on, clear the flag
            logger::write("[nfcTask] RF is on");
            isRfOff = false;
        } else {
            logger::write("[nfcTask] Error powering up RF");
            ESP.restart();
        }
    } else {
        logger::write("[nfcTask] RF already in desired state");
    }
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
            logger::write(message.c_str());
            break;

        } else if (err == 4) {
            String message = "[nfcTask] Unknown error at address 0x";
            if (addr < 16) {
                message += "0";
            }
            message += String(addr, HEX);
            logger::write(message.c_str());
        }
    }
    if (nDevices == 0)
        logger::write("[nfcTask] No I2C devices found\n");
}

bool isLnurlw(void) {
    logger::write("[nfcTask] Checking if URL is lnurlw", "info");
    // Check if lnurlwNFC starts with "lnurlw", "lnurl", "lnurlp", "lightning"
    if (lnurlwNFC.startsWith("lnurlw:")) {
        String rest = lnurlwNFC.substring(7);
        if (rest.startsWith("http") || rest.startsWith("https")) {
            lnurlwNFC.replace("lnurlw:", "");
        } else if (rest.startsWith("//")) {
            lnurlwNFC.replace("lnurlw://", "https://");
        } else {
            if (rest.startsWith("lnurl")) {
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
            if (rest.startsWith("lnurl")) {
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
            if (rest.startsWith("lnurl")) {
                lnurlwNFC = String(Lnurl::decode(rest.c_str()).c_str());
            } else {
                return false;
            }
        }
    } else if (lnurlwNFC.startsWith("lnurl") && !lnurlwNFC.startsWith("lnurl:")) {
        lnurlwNFC = String(Lnurl::decode(lnurlwNFC.c_str()).c_str());
    } else if (lnurlwNFC.startsWith("http") && !lnurlwNFC.startsWith("https")) {
        lnurlwNFC.replace("http", "https");
    }
    // Check if the URL starts with "https://" 
    if (lnurlwNFC.startsWith("https://")) {
        logger::write("[nfcTask] URL is lnurlw", "info");
        return true;
    } else {
        logger::write("[nfcTask] URL is not lnurlw", "info");
        return false;
    }
}

void idleMode(PN532_I2C *pn532_i2c)
{
    // logger::write("[nfcTask] Bit 2 is set. Going into idle mode 2.", "debug");
    // logger::write("[nfcTask] Checking RF status", "debug");
    setRFoff(true, pn532_i2c);
    if (!isRfOff) 
    {
        xEventGroupClearBits(appEventGroup, 0xFF);
        xEventGroupSetBits(appEventGroup, (1<<0)); // NFC task is active
        // logger::write("[nfcTask] NFC task is active", "debug");
        taskYIELD();
        return;
    }
    logger::write("[nfcTask] NFC reader turned off, returning to NFC idling mode", "info");
    xEventGroupClearBits(appEventGroup, 0xFF);
    xEventGroupSetBits(appEventGroup, (1<<2) | (1<<4)); // NFC task is idle, RF is off and transition to idle mode 2 is complete
    taskYIELD();
}

bool readAndProcessNFCData(PN532_I2C *pn532_i2c, PN532 *pn532, Adafruit_PN532 *nfc, NfcAdapter *nfcAdapter, int &readAttempts)
{
    NdefMessage message;
    int recordCount;
    uint8_t cardType = nfc->ntag424_isNTAG424();
    if (cardType) 
    {
        uint8_t data[256];
        uint8_t bytesread = nfc->ntag424_ISOReadFile(data);
        if (data[bytesread - 1] != '\0') { // Check if the last character is not a null terminator
            data[bytesread] = '\0'; // Manually add a null terminator at the end of the read data
        }
        // Extract URL from data
        lnurlwNFC = String((char*)data);
        Serial.println("URL from NTAG424: " + lnurlwNFC);
        if (isLnurlw()) 
        {
            while (!isRfOff) 
            {
                setRFoff(true, pn532_i2c); // Attempt to switch off RF
            }
            return true;
        }
    }
    else
    {
        NfcTag tag = nfcAdapter->read();
        String tagType = tag.getTagType();
        Serial.println("[nfcTask] Tag type read");
        Serial.println("Tag Type: " + tagType);
        if (tag.hasNdefMessage()) 
        {
            message = tag.getNdefMessage();
            recordCount = message.getRecordCount();
        }
        bool lnurlwFound = false;
        for (int i = 0; i < recordCount && !lnurlwFound; i++) 
        {
            NdefRecord record = message.getRecord(i);
            String recordType = record.getType();
            String logMessage = "Record Type: " + recordType;
            Serial.println(logMessage);
            if (recordType == "U" || recordType == "T") 
            { 
                uint8_t payload[record.getPayloadLength() + 1] = {0}; // Added +1 to the size and initialized to 0 to ensure null termination
                record.getPayload(payload);
                String recordPayload;
                if (recordType == "U") 
                {
                    switch (payload[0]) 
                    {
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
                } 
                else 
                {
                    for (int i = 0; i < record.getPayloadLength(); i++) 
                    {
                        recordPayload += (char)payload[i];
                    }
                }
                lnurlwNFC = recordPayload;
                String logMessage = "Record Payload: " + recordPayload;
                Serial.println(logMessage);
                if (isLnurlw()) 
                {
                    screen::showNFCsuccess();
                    return true;
                }
            }
        }
    }
    lnurlwNFC = ""; // Reset the global variable if it's not lnurlw
    readAttempts++;
    if (readAttempts >= 12) 
    {
        setRFoff(true, pn532_i2c); // Switch off RF
        if (isRfOff) 
        {
            return false;
        }
    }
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
    bool initFlag = false;
    uint8_t success;
    uint8_t uid[] = { 0, 0, 0, 0, 0, 0, 0 };
    uint8_t uidLength;
    Adafruit_PN532 *nfc = NULL;
    PN532_I2C *pn532_i2c = NULL;
    PN532 *pn532 = NULL;
    NfcAdapter *nfcAdapter = NULL;
    
    if (!initFlag) 
    {
        logger::write("[nfcTask] Initializing NFC", "debug");
        initNFC(&pn532_i2c, &nfc, &pn532, &nfcAdapter);
        vTaskSuspend(NULL);
    }
    EventBits_t uxBits;
    const EventBits_t uxAllBits = (1<<0) | (1<<1);
    int loopCounter = 0;
    while (1) 
    {
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
                logger::write("[nfcTask] Checking for tag", "info");
                success = nfc->readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 420);
                
                if (success) {
                    // We seem to have a tag present
                    logger::write("[nfcTask] Tag detected.", "info");
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
                            logger::write("[nfcTask] Withdraw from lnurlw exited with success", "info");
                            screen::showSuccess();
                            idleMode(pn532_i2c); // Enter idle mode
                        }
                        else
                        {
                            logger::write("[nfcTask] Withdraw from lnurlw exited with failure", "info");
                            screen::showX();
                            xEventGroupClearBits(appEventGroup, (1<<0)); // NFC task is not actively processing
                            vTaskDelay(1200);
                            screen::showPaymentQRCodeScreen(qrcodeData);
                        }
                        lnurlwNFC = "";
                    } 
                    else 
                    {
                        logger::write("[nfcTask] NFC card reading exited with failure", "info");
                        screen::showNFCfailed();
                        lnurlwNFC = "";
                        xEventGroupClearBits(appEventGroup, (1<<0)); // NFC task is not actively processing
                    }
                    
                    break;
                }
                else 
                {
                    vTaskDelay(pdMS_TO_TICKS(100));
                }
            }     
        }
    }
}







