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

extern std::string qrcodeData;
extern bool paymentisMade;

bool initNFC(PN532_I2C** pn532_i2c, Adafruit_PN532** nfc, PN532** pn532, NfcAdapter** nfcAdapter);
void printTaskState(TaskHandle_t taskHandle);
void setRFoff(bool turnOff, PN532_I2C* pn532_i2c);
bool setRFPower(PN532_I2C* pn532_i2c, int power);
void nfcTask(void *args);
void scanDevices(TwoWire *w);
void printRecordPayload(const uint8_t* payload, size_t len);
std::string decodeUriPrefix(uint8_t prefixCode);
bool isLnurlw(String url);
void idleMode(PN532_I2C *pn532_i2c);
bool readAndProcessNFCData(PN532_I2C *pn532_i2c, PN532 *pn532, Adafruit_PN532 *nfc, NfcAdapter *nfcAdapter, int &readAttempts);

#endif