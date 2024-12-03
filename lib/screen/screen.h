#ifndef SCREEN_H
#define SCREEN_H

#include "config.h"
#include "logger.h"
#include "screen_tft.h"
#include <string>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

extern TFT_eSPI tft;
extern std::string currentScreen;

namespace screen {

// Define screen update message structure
struct ScreenMessage {
    enum class MessageType {
        HOME,
        X,
        NFC,
        NFC_FAILED,
        NFC_SUCCESS,
        SUCCESS,
        SAND,
        NO_WIFI,
        MENU,
        ENTER_AMOUNT,
        PAYMENT_QR,
        PAYMENT_PIN,
        ERROR,
        STATUS_SYMBOLS,
        SLEEP,
        WAKEUP,
        ADJUST_CONTRAST
    };
    
    MessageType type;
    union {
        double amount;
        int batteryPercent;
        int contrastChange;
    };
    char text[512];  // Fixed size buffer instead of std::string
    
    // Default constructor
    ScreenMessage() : type(MessageType::HOME), amount(0) {
        text[0] = '\0';
    }
    
    // Constructor with type
    explicit ScreenMessage(MessageType t) : type(t), amount(0) {
        text[0] = '\0';
    }
};

extern QueueHandle_t screenQueue;
extern TaskHandle_t screenTaskHandle;

void init();
std::string getCurrentScreen();

// These functions now queue screen updates instead of directly updating
void showHomeScreen();
void showX();
void showNFC();
void showNFCfailed();
void showNFCsuccess();
void showSuccess();
void showSand();
void showNowifi();
void showMenu();
void showEnterAmountScreen(const double &amount);
void showPaymentQRCodeScreen(const std::string &qrcodeData);
void showPaymentPinScreen(const std::string &pin);
void showErrorScreen(const std::string &error);
void adjustContrast(const int &percentChange);
void showStatusSymbols(const int &batteryPercent);
void sleep();
void wakeup();

// New screen task function
void screenTask(void* parameter);

}

#endif
