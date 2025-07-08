#ifndef NFC_H
#define NFC_H

#include <Wire.h>
#include "Adafruit_PN532_NTAG424.h"
#include <PN532_I2C.h>
#include <PN532.h>
#include <NfcAdapter.h>
#include <TFT_eSPI.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include "FreeRTOSConfig.h"
#include "screen.h"
#include "logger.h"
#include "withdraw_lnurlw.h"
#include "lnurl.h"

#define NFC_SDA    13
#define NFC_SCL    15
#define NFC_IRQ    26
#define NFC_RST    25

extern TFT_eSPI tft;  
extern TaskHandle_t nfcTaskHandle;
extern EventGroupHandle_t nfcEventGroup;
extern EventGroupHandle_t appEventGroup;

extern String lnurlwNFC;
extern std::string hybridBolt11Invoice; // Store bolt11 invoice for hybrid payments
extern bool hybridInvoiceFetched; // Track if we've already fetched the invoice for this payment
extern bool initFlagNFC;

extern std::string qrcodeData;
extern bool paymentisMade;
extern std::string cardDetectedLnurlw; // LNURL withdraw data detected by NFC task for payment task to process

// Event bits for NFC-Payment task coordination
#define NFC_CARD_DETECTED_BIT      (1 << 0)  // Card detected by NFC task
#define NFC_RF_OFF_CONFIRMED_BIT   (1 << 1)  // RF shutdown confirmed
#define LNURL_WITHDRAW_REQUEST_BIT (1 << 2)  // NFC task requests payment task to process LNURL withdraw
#define LNURL_WITHDRAW_SUCCESS_BIT (1 << 3)  // Payment task signals successful LNURL withdraw
#define LNURL_WITHDRAW_FAILED_BIT  (1 << 4)  // Payment task signals failed LNURL withdraw

bool initNFC(PN532_I2C** pn532_i2c, Adafruit_PN532** nfc, PN532** pn532, NfcAdapter** nfcAdapter);
void printTaskState(TaskHandle_t taskHandle);
void recoverI2CBus();
void setRFoff(bool turnOff, PN532_I2C* pn532_i2c);
bool setRFPower(PN532_I2C* pn532_i2c, int power, int gain = 0x40, int modulation = 0x03, int miller = 0x0E);
bool activateNTAG424DNA(PN532_I2C* pn532_i2c, Adafruit_PN532* nfc);
void nfcTask(void *args);
void scanDevices(TwoWire *w);
void printRecordPayload(const uint8_t* payload, size_t len);
std::string decodeUriPrefix(uint8_t prefixCode);
bool isLnurlw(String url);
void idleMode(PN532_I2C *pn532_i2c);
bool sendQRCodeToPhone(PN532_I2C* pn532_i2c, const String& qrcodeData);
bool readAndProcessNFCData(PN532_I2C *pn532_i2c, PN532 *pn532, Adafruit_PN532 *nfc, NfcAdapter *nfcAdapter, int &readAttempts);

// Function to request bolt11 invoice from LNURL-pay (from payment.cpp)
std::string requestInvoice(const std::string &url);

#endif