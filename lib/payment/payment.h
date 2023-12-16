#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "freertos/event_groups.h"
#include "FreeRTOSConfig.h"
#include "logger.h"
#include "keypad.h"
#include "nfc.h"

extern String lnurlwNFC;
extern EventGroupHandle_t nfcEventGroup;
extern EventGroupHandle_t appEventGroup;
extern SemaphoreHandle_t wifiSemaphore;
extern std::string qrcodeData;
extern bool isRfOff;
extern Adafruit_MPR121 cap;
extern bool paymentisMade;

std::string parseCallbackUrl(const std::string &response);
std::string parseInvoice(const std::string &json);
std::string requestInvoice(const std::string &url);
std::string fetchPaymentHash(const std::string &lnurl);
bool isPaymentMade(const std::string &paymentHash, const std::string &apiKey);
bool waitForPaymentOrCancel(const std::string &paymentHash, const std::string &apiKey, const std::string &invoice);