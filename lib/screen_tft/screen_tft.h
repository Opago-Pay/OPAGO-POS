#ifndef SCREEN_TFT_H
#define SCRENN_TFT_H

#include "i18n.h"
#include "logger.h"
#include "util.h"

#include "Arduino.h"
#include "qrcode.h"
#include "SPI.h"
#include "TFT_eSPI.h"
#include "TJpg_Decoder.h"

#include "opago_wifi.h"

#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

#include "checkbooklightning_12pt.h"
#include "checkbooklightning_14pt.h"
#include "checkbooklightning_16pt.h"
#include "checkbooklightning_18pt.h"
#include "checkbooklightning_20pt.h"
#include "checkbooklightning_22pt.h"
#include "courier_prime_code_6pt.h"
#include "courier_prime_code_7pt.h"
#include "courier_prime_code_8pt.h"
#include "courier_prime_code_9pt.h"
#include "courier_prime_code_10pt.h"
#include "courier_prime_code_12pt.h"
#include "courier_prime_code_14pt.h"
#include "courier_prime_code_16pt.h"
#include "courier_prime_code_18pt.h"
#include "courier_prime_code_20pt.h"
#include "courier_prime_code_22pt.h"
#include "courier_prime_code_24pt.h"
#include "courier_prime_code_28pt.h"
#include "courier_prime_code_32pt.h"
#include "courier_prime_code_34pt.h"
#include "courier_prime_code_36pt.h"
#include "courier_prime_code_38pt.h"
#include "courier_prime_code_42pt.h"
#include "courier_prime_code_44pt.h"

extern TFT_eSPI tft;
extern int screenWidth;
extern bool onlineStatus;

namespace screen_tft {
	void init();
	bool jpgRender(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t *bitmap);
	void renderJPEG(const char *filename, int xpos, int ypos, uint8_t scale);
	void showEnterAmountScreen(const double &amount);
	void showPaymentQRCodeScreen(const std::string &qrcodeData);
	void showPaymentPinScreen(const std::string &pin);
	void showErrorScreen(const std::string &error);
	void adjustContrast(const int &percentChange);
	void showBatteryPercent(const int &percent);
	void hideBatteryPercent();
	void tellBacklightStatus();
	void sleep();
	void wakeup();
}

#endif
