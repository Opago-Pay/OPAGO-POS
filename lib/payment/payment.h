#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "freertos/event_groups.h"
#include "FreeRTOSConfig.h"
#include "logger.h"
#include "config.h"
#include "keypad.h"
#include "nfc.h"

// Payment flow states
enum class PaymentState {
    SHOWING_QR,
    MONITORING_PAYMENT,
    PAYMENT_SUCCESS,
    PAYMENT_CANCELLED,
    SWITCH_TO_PIN,
    ERROR
};

extern String lnurlwNFC;
extern EventGroupHandle_t nfcEventGroup;
extern EventGroupHandle_t appEventGroup;
extern SemaphoreHandle_t wifiSemaphore;
extern std::string qrcodeData;
extern bool isRfOff;
extern Adafruit_MPR121 cap;
extern bool paymentisMade;
extern bool connectionLoss; 
extern TaskHandle_t onlineMonitorTaskHandle;
extern bool onlineMonitorActive;
extern std::string onlinePaymentHash;

std::string parseCallbackUrl(const std::string &response);
std::string parseInvoice(const std::string &json);
std::string requestInvoice(const std::string &url);
std::string fetchPaymentHash(const std::string &lnurl);
bool isPaymentMade(const std::string &paymentHash, const std::string &apiKey);
bool waitForPaymentOrCancel(const std::string &paymentHash, const std::string &apiKey, const std::string &invoice);

void onlinePaymentMonitorTask(void* pvParameters);
bool startUnifiedPaymentFlow(const double &amount, const std::string &pin);
bool waitForPaymentWithFallback(const std::string &lnurlQR, const std::string &pin);

// New functions for refactored payment flow
PaymentState initializePaymentFlow(const double &amount, const std::string &pin, std::string &lnurlQR);
PaymentState checkPaymentStatus(const std::string &lnurlQR, const std::string &pin);
void cleanupPaymentFlow();
bool isPaymentFlowActive();