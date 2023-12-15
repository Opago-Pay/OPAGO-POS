#ifndef SCREEN_H
#define SCREEN_H

#include "config.h"
#include "logger.h"
#include "screen_tft.h"

extern TFT_eSPI tft;

namespace screen {
	void init();
	std::string getCurrentScreen();
	void showHomeScreen();
	void showX();
	void showNFC();
	void showNFCfailed();
	void showNFCsuccess();
	void showSuccess();
	void showSand();
	void showEnterAmountScreen(const double &amount);
	void showPaymentQRCodeScreen(const std::string &qrcodeData);
	void showPaymentPinScreen(const std::string &pin);
	void showErrorScreen(const std::string &error);
	void adjustContrast(const int &percentChange);
	void showBatteryPercent(const int &percent);
	void hideBatteryPercent();
	void sleep();
	void wakeup();
}

#endif
