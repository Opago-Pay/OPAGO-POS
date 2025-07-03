#ifndef APP_H
#define APP_H

#include <Arduino.h>
#include "power.h"
#include "logger.h"
#include "screen.h"
#include "keypad.h"
#include "util.h"
#include "config.h"
#include "cache.h"
#include "payment.h"
#include "lnurl.h"
#include "withdraw_lnurlw.h"
#include "json-rpc.h"
#include "opago_wifi.h"

extern bool lnurlwNFCFound;
extern String lnurlwNFC;
extern QueueHandle_t appQueue;
extern double amount;
extern uint16_t amountCentsDivisor;
extern unsigned short maxNumKeysPressed;
extern std::string qrcodeData;
extern unsigned int sleepModeDelay;
extern unsigned long lastActivityTime;
extern bool isFakeSleeping;
extern bool offlineMode;
extern bool offlineOnly;

// Payment flow state variables
extern PaymentState currentPaymentState;
extern std::string currentPaymentLNURL;
extern std::string currentPaymentPin;
extern bool isInPaymentFlow;

void appendToKeyBuffer(const std::string &key);
std::string leftTrimZeros(const std::string &keys);
double keysToAmount(const std::string &t_keys);
void handleSleepMode();
void appTask(void* pvParameters);
void handlePaymentFlow();

#endif // APP_H