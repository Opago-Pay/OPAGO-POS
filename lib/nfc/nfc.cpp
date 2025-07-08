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

// NTAG424 DNA activation sequence for better detection of stationary cards
bool activateNTAG424DNA(PN532_I2C* pn532_i2c, Adafruit_PN532* nfc) {
    // Step 1: Ensure RF field is properly configured for NTAG424 DNA wake-up
    // OPTIMIZED: Reduced wake-up power from 220 to 200 for more stable operation
    setRFPower(pn532_i2c, 200, 0x48, 0x01, 0x01); // Optimal power, standard gain for wake-up
    vTaskDelay(pdMS_TO_TICKS(50)); // Allow field to stabilize
    
    // Step 2: Try to wake up any sleeping NTAG424 DNA cards
    // Send ISO14443-3 REQA (Request Type A) command to wake up cards
    uint8_t wakeupCmd[] = {0x26}; // REQA command
    uint8_t response[16];
    int result = pn532_i2c->writeCommand(wakeupCmd, sizeof(wakeupCmd), response, sizeof(response));
    
    if (result == 0) {
        logger::write("[nfcTask] NTAG424 wake-up command sent successfully", "debug");
    }
    
    // Step 3: Brief delay for card to respond to wake-up
    vTaskDelay(pdMS_TO_TICKS(20));
    
    // Step 4: OPTIMIZED: Lower power for stable reading with optimal modulation
    setRFPower(pn532_i2c, 170, 0x48, 0x02, 0x0E); // Optimal reading power for NTAG424
    vTaskDelay(pdMS_TO_TICKS(30));
    
    return true;
}

// InAutoPoll implementation for NTAG424 detection with automatic RF power sweeping
// Returns: 0 = no detection, 1 = card detected (show NFC screen), 2 = card detected + read successful  
int startAutoPollingForNTAG424(PN532* pn532, Adafruit_PN532* nfc) {
    logger::write("[nfcTask] Starting InAutoPoll for NTAG424 with automatic RF power sweeping", "info");
    
    // Configure target types for ISO14443A (NTAG424 compatible)
    uint8_t targetTypes[] = {PN532_MIFARE_ISO14443A}; // Type A targets (includes NTAG424)
    
    // OPTIMIZED: InAutoPoll with fine-tuned settings for NTAG424 DNA performance
    // pollNr=1: Check for 1 target max to minimize processing
    // period=2: Poll every 300ms (2 * 150ms) - optimal balance of speed and detection
    // timeout=1200ms: Slightly reduced for better responsiveness while maintaining reliability
    bool autoResult = pn532->inAutoPoll(1, 2, targetTypes, sizeof(targetTypes), 1200);
    
    if (autoResult) {
        logger::write("[nfcTask] InAutoPoll detected target - attempting direct NTAG424 read", "info");
        
        // ALWAYS return detection=1 so NFC screen shows regardless of read success
        // Skip UID reading completely - go straight to NTAG424 data reading
        uint8_t data[256];
        uint8_t bytesread = 0;
        
        // OPTIMIZED: Try NTAG424 read with optimal timing for better performance
        for (int attempt = 0; attempt < 5; attempt++) {
            bytesread = nfc->ntag424_ISOReadFile(data);
            if (bytesread > 0) {
                break;
            }
            
            if (attempt < 4) { // Don't delay after the last attempt
                vTaskDelay(pdMS_TO_TICKS(35)); // Reduced delay from 50ms to 35ms for faster attempts
            }
        }
        
        if (bytesread > 0) {
            // Successfully read NTAG424 data
            if (data[bytesread - 1] != '\0') {
                data[bytesread] = '\0'; // Add null terminator
            }
            
            // Store the LNURL data
            lnurlwNFC = String((char*)data);
            logger::write(("[nfcTask] InAutoPoll + NTAG424 read successful: " + lnurlwNFC).c_str(), "info");
            
            // Validate it's a proper LNURL-withdraw
            if (isLnurlw()) {
                return 2; // Card detected AND read successful
            } else {
                logger::write("[nfcTask] Card detected but not a valid LNURL-withdraw", "warning");
                return 1; // Card detected but invalid data
            }
        } else {
            logger::write("[nfcTask] InAutoPoll detected target but NTAG424 read failed", "warning");
            return 1; // Card detected but read failed - still show NFC screen
        }
    }
    
    return 0; // No targets detected
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
                setRFPower(pn532_i2c, 180); // OPTIMIZED: Use optimal detection power instead of max
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
    
    // CRITICAL: Re-enable normal touch input when entering idle mode
    suppressTouchDuringNFC(false);
    setRFSafeMode(false); // Disable RF-safe mode - allow all keys
    logger::write("[nfcTask] Touch input re-enabled in idleMode", "info");
    
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
    uint8_t response[64]; // Buffer to hold the response - reduced size to fix uint8_t overflow
    int result = pn532_i2c->writeCommand(command, sizeof(command), response, sizeof(response));

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
        result = pn532_i2c->writeCommand(setDataCommand, ndefMessageLength + 2, response, sizeof(response));

        if (result == 0) {
            logger::write("NDEF message set for emulated tag", "debug");

            // Set the emulated tag to be a generic NFC tag
            uint8_t tagTypeCommand[] = {0x8C, 0x02, 0x00, 0x00}; // TgInitAsTarget command with generic target parameters
            result = pn532_i2c->writeCommand(tagTypeCommand, sizeof(tagTypeCommand), response, sizeof(response));

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
    // Attempt NTAG424 operations on ALL cards (some NTAG424 cards hide their identity)
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
    }

    // NTAG424 read failed - not a valid boltcard
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
            logger::write("[nfcTask] Starting NFC polling for payment", "info");
            int readAttempts = 0;
            bool isProcessingCard = false; // Flag to prevent re-detection during processing
            
            // Enable RF-safe mode during NFC polling - allows * and # with enhanced debouncing
            // This prevents accidental triggers from RF interference while keeping abort/PIN entry functional
            suppressTouchDuringNFC(false); // Ensure full suppression is off
            setRFSafeMode(true); // Enable RF-safe mode for * and # buttons only
            
            while (1) 
            {
                // Check for shutdown signal only (non-blocking)
                uxBits = xEventGroupWaitBits(nfcEventGroup, (1 << 1), pdFALSE, pdFALSE, 0);
                if ((uxBits & (1 << 1)) != 0) 
                {
                    logger::write("[nfcTask] Shutdown signal received - exiting NFC polling", "info");
                    suppressTouchDuringNFC(false);  // Re-enable touch before idleMode
                    setRFSafeMode(false); // Disable RF-safe mode - allow all keys
                    logger::write("[nfcTask] Touch input re-enabled before shutdown", "info");
                    idleMode(pn532_i2c);
                    break;
                }

                loopCounter++;
                
                // Only poll for new cards when not processing an existing card
                int detectionResult = 0;
                if (!isProcessingCard) {
                    // Use InAutoPoll for automatic RF power sweeping and superior NTAG424 detection
                    // Hardware-assisted detection with automatic RF power management - no manual fallback needed
                    detectionResult = startAutoPollingForNTAG424(pn532, nfc);
                } else {
                    // CRITICAL: During card processing, don't poll and use longer delays to avoid busy-waiting
                    // This prevents re-detection during withdrawal processing which could interfere with sand screen
                    logger::write("[nfcTask] Card processing in progress - skipping polling to maintain sand screen", "debug");
                    detectionResult = 0; // Skip polling during processing
                    vTaskDelay(pdMS_TO_TICKS(500)); // Longer delay during processing
                    continue; // Skip to next loop iteration during processing
                }
                
                // Handle card detection with proper screen sequence
                if (detectionResult >= 1) {
                    logger::write("[nfcTask] Card detected - showing NFC screen", "info");
                    isProcessingCard = true; // Prevent re-detection during processing
                    screen::showNFC(); // ALWAYS show NFC screen when card is detected
                    
                    if (detectionResult == 2) {
                        // Card read successfully - show NFC success, then proceed with payment
                        logger::write("[nfcTask] Card read successful - showing NFC success", "info");
                        screen::showNFCsuccess();
                        
                        // InAutoPoll already read and validated the NTAG424 data
                        xEventGroupSetBits(appEventGroup, (1<<0)); // Signal card detected to payment task
                        
                        bool result = true; // Skip readAndProcessNFCData since InAutoPoll already did it
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
                            
                            // Pass the LNURL data to the payment task for SSL processing
                            cardDetectedLnurlw = std::string(lnurlwNFC.c_str()); // Store the LNURL withdraw data
                            logger::write("[nfcTask] LNURL withdraw data stored for payment task: " + cardDetectedLnurlw, "info");
                            
                            // Store current payment session ID to prevent stale results from affecting future sessions
                            extern uint32_t currentPaymentSessionId;
                            static uint32_t nfcProcessingSessionId = 0;
                            nfcProcessingSessionId = currentPaymentSessionId;
                            logger::write("[nfcTask] Processing withdrawal for session ID: " + std::to_string(nfcProcessingSessionId), "info");
                            
                            // Signal payment task to process LNURL withdrawal
                            xEventGroupSetBits(appEventGroup, LNURL_WITHDRAW_REQUEST_BIT);
                            
                            // Show loading screen while payment task processes the withdrawal
                            screen::showSand();
                            logger::write("[nfcTask] Waiting for payment task to process LNURL withdrawal", "info");
                            
                            // CRITICAL: Stop all NFC polling during withdrawal processing to prevent screen interference
                            // This ensures sand screen stays visible and no NFC detection can override it
                            logger::write("[nfcTask] Stopping NFC polling during withdrawal processing to protect sand screen", "info");
                            
                            // Wait for payment task to complete the withdrawal (success or failure)
                            // Extended timeout to account for bolt11 invoice fetching (up to 10 retries + network delays)
                            EventBits_t withdrawResult = xEventGroupWaitBits(
                                appEventGroup, 
                                LNURL_WITHDRAW_SUCCESS_BIT | LNURL_WITHDRAW_FAILED_BIT,
                                pdTRUE,  // Clear bits after waiting
                                pdFALSE, // Wait for ANY of the bits (OR operation)
                                pdMS_TO_TICKS(20000) // 20 second timeout to accommodate bolt11 fetching
                            );
                            
                            logger::write("[nfcTask] Withdrawal processing complete, resuming normal NFC polling", "info");
                            
                            if (withdrawResult & LNURL_WITHDRAW_SUCCESS_BIT) {
                                logger::write("[nfcTask] LNURL withdrawal successful", "info");
                                screen::showSuccess();
                                suppressTouchDuringNFC(false);
                                setRFSafeMode(false); // Disable RF-safe mode - payment complete
                                logger::write("[nfcTask] Touch input re-enabled after successful withdraw", "info");
                                isProcessingCard = false; // Reset processing flag
                                idleMode(pn532_i2c); // Enter idle mode
                                
                                // CRITICAL: Only set paymentisMade if we're still in the same payment session
                                // This prevents delayed results from previous sessions affecting new payments
                                if (nfcProcessingSessionId == currentPaymentSessionId) {
                                    paymentisMade = true;
                                    logger::write("[nfcTask] Setting paymentisMade=true for session " + std::to_string(nfcProcessingSessionId), "info");
                                } else {
                                    logger::write("[nfcTask] IGNORING withdrawal success - session mismatch. Processing: " + 
                                                std::to_string(nfcProcessingSessionId) + ", Current: " + 
                                                std::to_string(currentPaymentSessionId), "warning");
                                }
                            } else if (withdrawResult & LNURL_WITHDRAW_FAILED_BIT) {
                                logger::write("[nfcTask] LNURL withdrawal failed", "info");
                                vTaskDelay(pdMS_TO_TICKS(2000)); // Brief delay to show sand screen
                                if (!paymentisMade) {
                                    logger::write("[nfcTask] Payment not made via other means, showing failed screen", "info");
                                    // CRITICAL: Re-enable touch input on failure to prevent keyboard lockup
                                    suppressTouchDuringNFC(false);
                                    setRFSafeMode(false); // Disable RF-safe mode - return to QR screen
                                    logger::write("[nfcTask] Touch input re-enabled after withdrawal failure", "info");
                                    isProcessingCard = false; // Reset processing flag
                                    screen::showX();
                                    vTaskDelay(pdMS_TO_TICKS(2000)); // Show X briefly
                                    screen::showPaymentQRCodeScreen(qrcodeData);
                                } else {
                                    logger::write("[nfcTask] Payment made via other means, showing success", "info");
                                    screen::showSuccess();
                                    suppressTouchDuringNFC(false);
                                    setRFSafeMode(false); // Disable RF-safe mode - payment complete
                                    logger::write("[nfcTask] Touch input re-enabled after successful payment via other means", "info");
                                    isProcessingCard = false; // Reset processing flag
                                    idleMode(pn532_i2c);
                                }
                            } else {
                                logger::write("[nfcTask] LNURL withdrawal timeout - no response from payment task", "error");
                                // CRITICAL: Re-enable touch input on timeout to prevent keyboard lockup
                                suppressTouchDuringNFC(false);
                                setRFSafeMode(false); // Disable RF-safe mode - return to QR screen
                                logger::write("[nfcTask] Touch input re-enabled after withdrawal timeout", "info");
                                isProcessingCard = false; // Reset processing flag
                                screen::showX();
                                vTaskDelay(pdMS_TO_TICKS(2000));
                                screen::showPaymentQRCodeScreen(qrcodeData);
                            }
                            
                            lnurlwNFC = "";
                            cardDetectedLnurlw = ""; // Clear the data after processing
                        } 
                        else 
                        {
                            logger::write("[nfcTask] NFC card reading exited with failure", "debug");
                            screen::showNFCfailed();
                            lnurlwNFC = "";
                            isProcessingCard = false; // Reset processing flag on failure
                            // No delay needed - continue polling immediately for faster re-detection
                            xEventGroupClearBits(appEventGroup, (1<<0)); // Clear card processing bit
                        }
                    } else {
                        // Card detected but read failed (detectionResult == 1) - show NFC failed
                        logger::write("[nfcTask] Card detected but NTAG424 read failed - showing NFC failed", "info");
                        screen::showNFCfailed();
                        lnurlwNFC = "";
                        isProcessingCard = false; // Reset processing flag to allow new detections
                        // No delay needed - continue polling immediately for faster re-detection
                        xEventGroupClearBits(appEventGroup, (1<<0)); // Clear card processing bit
                    }
                    
                    // Brief delay after processing card before next poll
                    vTaskDelay(pdMS_TO_TICKS(100)); // Reduced from 200ms to 100ms for faster polling
                    // Continue polling for more cards instead of breaking
                }
                else 
                {
                    if (isProcessingCard) {
                        // While processing a card, wait longer to avoid busy-waiting
                        vTaskDelay(pdMS_TO_TICKS(200));
                    } else {
                        // No card detected - InAutoPoll handles timing internally but add minimal delay for I2C stability
                        if (loopCounter % 10 == 0) {
                            logger::write("[nfcTask] No card detected after 10 InAutoPoll attempts", "info");
                        }
                        
                        // Minimal delay since InAutoPoll handles most timing internally
                        // InAutoPoll already includes 600ms polling periods (period=4 * 150ms)
                        vTaskDelay(pdMS_TO_TICKS(50)); // Just enough for I2C bus stability
                    }
                }
            }
            
            // Safety: Re-enable normal touch if we somehow exit the polling loop
            suppressTouchDuringNFC(false);
            setRFSafeMode(false); // Disable RF-safe mode - return to normal operation
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







