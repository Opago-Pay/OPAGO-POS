#include "screen_tft.h"

namespace {

	const uint16_t bgColor = TFT_BLACK;
	const int minContrastPercent = 10;
	int currentContrastPercent = 100;
	uint16_t textColor = TFT_WHITE;
	int16_t center_x;
	int16_t center_y;
	std::string currentPaymentQRCodeData;
	bool backlightOff = false;

	typedef std::vector<GFXfont> FontList;

	FontList monospaceFonts = {
		// Ordered from largest (top) to smallest (bottom).
		Courier_Prime_Code44pt7b,
		Courier_Prime_Code42pt7b,
		Courier_Prime_Code38pt7b,
		Courier_Prime_Code36pt7b,
		Courier_Prime_Code34pt7b,
		Courier_Prime_Code32pt7b,
		Courier_Prime_Code28pt8b,
		Courier_Prime_Code24pt8b,
		Courier_Prime_Code22pt8b,
		Courier_Prime_Code20pt8b,
		Courier_Prime_Code18pt8b,
		Courier_Prime_Code16pt8b,
		Courier_Prime_Code14pt8b,
		Courier_Prime_Code12pt8b,
		Courier_Prime_Code10pt8b,
		Courier_Prime_Code9pt8b,
		Courier_Prime_Code8pt8b,
		Courier_Prime_Code7pt8b,
		Courier_Prime_Code6pt8b
	};

	FontList brandFonts = {
		// Ordered from largest (top) to smallest (bottom).
		CheckbookLightning22pt7b,
		CheckbookLightning20pt7b,
		CheckbookLightning18pt7b,
		CheckbookLightning16pt7b,
		CheckbookLightning14pt7b,
		CheckbookLightning12pt7b
	};

	struct BoundingBox {
		int16_t x = 0;
		int16_t y = 0;
		uint16_t w = 0;
		uint16_t h = 0;
	};

	BoundingBox amountTextBBox;
	BoundingBox statusSymbolsBBox;
	BoundingBox wifiBBox;
	BoundingBox usbBBox;
	BoundingBox batteryBBox;

	std::string getAmountFiatCurrencyString(const double &amount) {
		return util::doubleToStringWithPrecision(amount, config::getUnsignedShort("fiatPrecision")) + " " + config::getString("fiatCurrency");
	}

	BoundingBox calculateTextDimensions(const std::string &t_text, const GFXfont font) {
		BoundingBox bbox;
		const char* text = t_text.c_str();
		tft.setTextSize(1);
		tft.setFreeFont(&font);
		const uint16_t textWidth = tft.textWidth(text);
		const uint16_t textHeight = tft.fontHeight();
		bbox.w = textWidth;
		bbox.h = textHeight;
		return bbox;
	}

	GFXfont getBestFitFont(const std::string &text, const FontList fonts, uint16_t max_w = 0, uint16_t max_h = 0) {
		if (max_w == 0) {
			max_w = screenWidth;
		}
		if (max_h == 0) {
			max_h = tft.height();
		}
		uint8_t chosenFontIndex = 0;
		for (uint8_t index = 0; index < fonts.size(); index++) {
			const GFXfont font = fonts.at(index);
			const BoundingBox bbox = calculateTextDimensions(text, font);
			if (bbox.w <= max_w && bbox.h <= max_h) {
				// Best fit font found.
				chosenFontIndex = index;
				break;
			}
		}
		return fonts[chosenFontIndex];
	}

	BoundingBox renderText(
		const std::string &t_text,
		const GFXfont font,
		const uint16_t &color,
		const int16_t x,
		const int16_t y,
		const uint8_t &alignment = MC_DATUM
	) {
		const char* text = t_text.c_str();
		tft.setTextColor(color);
		tft.setTextSize(1);
		tft.setFreeFont(&font);
		BoundingBox bbox = calculateTextDimensions(text, font);
		int16_t cursor_x = x;
		int16_t cursor_y = y;
		tft.setTextDatum(alignment);
		tft.drawString(text, cursor_x, cursor_y);
		bbox.x = cursor_x;
		bbox.y = cursor_y;
		return bbox;
	}

	BoundingBox renderQRCode(
		const std::string &t_data,
		const int16_t x,
		const int16_t y,
		const uint16_t &max_w,
		const uint16_t &max_h,
		const bool &center = true
	) {
		BoundingBox bbox;
		try {
			const char* data = t_data.c_str();
			uint8_t version = 1;
			uint8_t scale = 1;
			while (version <= 40) {
				const uint16_t bufferSize = qrcode_getBufferSize(version);
				QRCode qrcode;
				uint8_t qrcodeData[bufferSize];
				const int8_t result = qrcode_initText(&qrcode, qrcodeData, version, ECC_LOW, data);
				if (result == 0) {
					// QR encoding successful.
					scale = std::min(std::floor(max_w / qrcode.size), std::floor(max_h / qrcode.size));
					const uint16_t w = qrcode.size * scale;
					const uint16_t h = w;
					int16_t box_x = x;
					int16_t box_y = y;
					if (center) {
						box_x -= (w / 2);
						box_y -= (h / 2);
					}
					tft.fillRect(box_x, box_y, w, h, textColor);
					for (uint8_t y = 0; y < qrcode.size; y++) {
						for (uint8_t x = 0; x < qrcode.size; x++) {
							const auto color = qrcode_getModule(&qrcode, x, y) ? bgColor : textColor;
							tft.fillRect(box_x + scale*x, box_y + scale*y, scale, scale, color);
						}
					}
					bbox.x = box_x;
					bbox.y = box_y;
					bbox.w = w;
					bbox.h = h;
					break;
				} else if (result == -2) {
					// Data was too long for the QR code version.
					version++;
				} else if (result == -1) {
					throw std::runtime_error("Unable to detect mode");
				} else {
					throw std::runtime_error("Unknown failure case");
				}
			}
			// Draw a border around the QR code - to improve readability.
			const uint8_t margin_x = std::min(scale, (uint8_t)std::floor((screenWidth - bbox.w) / 2));
			const uint8_t margin_y = std::min(scale, (uint8_t)std::floor((tft.height() - bbox.h) / 2));
			const uint16_t border_x = bbox.x - margin_x;
			const uint16_t border_y = bbox.y - margin_y;
			tft.fillRect(border_x, border_y, margin_x, bbox.h + (margin_y * 2), textColor);// left
			tft.fillRect(bbox.x + bbox.w, border_y, margin_x, bbox.h + (margin_y * 2), textColor);// right
			tft.fillRect(bbox.x, border_y, bbox.w, margin_y, textColor);// top
			tft.fillRect(bbox.x, bbox.y + bbox.h, bbox.w, margin_y, textColor);// bottom
		} catch (const std::exception &e) {
			std::cerr << e.what() << std::endl;
			logger::write("Error while rendering QR code: " + std::string(e.what()), "error");
		}
		return bbox;
	}

	void clearScreen(const bool &reset = true) {
		tft.fillScreen(bgColor);
		if (reset && currentPaymentQRCodeData != "") {
			currentPaymentQRCodeData = "";
		}
	}

	void setContrastLevel(const int &percent) {
		currentContrastPercent = percent;
		const int value = std::ceil((percent * 255) / 100);
		textColor = tft.color565(value, value, value);
		const std::string percentStr = std::to_string(percent);
		config::saveConfiguration("contrastLevel", percentStr);
		logger::write("Set contrast level to " + percentStr + " %");
		if (currentPaymentQRCodeData != "") {
			screen_tft::showPaymentQRCodeScreen(currentPaymentQRCodeData);
		}
	}
}

namespace screen_tft {

	void init() {
		logger::write("Initializing TFT tft...");
		tft.begin();
		tft.setRotation(config::getUnsignedShort("tftRotation"));
		logger::write("TFT display width = " + std::to_string(screenWidth));
		logger::write("TFT display height = " + std::to_string(tft.height()));
		center_x = screenWidth / 2;
		center_y = tft.height() / 2;
		//Serial.println("Center x = " + center_x);
		//Serial.println("Center y = " + center_y);
		setContrastLevel(config::getUnsignedInt("contrastLevel"));
	}

	// This function will be called during decoding of the jpeg file
	bool jpgRender(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t *bitmap)
	{
		// Display jpeg chunk
		tft.pushImage(x, y, w, h, bitmap);
		return true; // Continue decoding
	}

	void renderJPEG(const char *filename, int xpos, int ypos, uint8_t scale) {
		// Open the named file (the file must have been opened for read)
		fs::File jpgFile = SPIFFS.open(filename, "r");

		if (jpgFile) {
			TJpgDec.setJpgScale(scale); // Set the scale factor
			TJpgDec.setCallback(jpgRender);
			TJpgDec.drawFsJpg(xpos, ypos, jpgFile);
			jpgFile.close();
		} else {
			// If the jpg file cannot be loaded, display the filename in the middle of the screen
			if (filename != NULL && strlen(filename) > 0) {
				renderText(filename, Courier_Prime_Code18pt8b, TFT_WHITE, center_x, center_y);
				Serial.println("Displaying alternate text: " + String(filename));
			} else {
				renderText("Filesystem error.", Courier_Prime_Code18pt8b, TFT_WHITE, center_x, center_y);
				Serial.println("Displaying alternate text: Error");
			}
		}
	}

	void showEnterAmountScreen(const double &amount) {
		clearScreen(false);
		int16_t amount_y = center_y - 12;

		std::string amountStr = util::doubleToStringWithPrecision(amount, config::getUnsignedShort("fiatPrecision"));
		std::string currencyStr = config::getString("fiatCurrency");

		const GFXfont amountFont = getBestFitFont(amountStr, monospaceFonts);
		//const GFXfont currencyFont = getBestFitFont(currencyStr, monospaceFonts);

		// Render amount and currency on separate lines
		renderText(amountStr, amountFont, textColor, center_x, amount_y);
		renderText(currencyStr, Courier_Prime_Code18pt8b, textColor, center_x, amount_y + amountFont.yAdvance);
	}

	void showPaymentQRCodeScreen(const std::string &qrcodeData) {
		clearScreen(false);
		showStatusSymbols(power::getBatteryPercent());
		const int16_t qr_max_w = screenWidth; 
		const int16_t qr_max_h = tft.height() - statusSymbolsBBox.h; 
		if (config::getString("callbackUrl") == "https://opago-pay.com/getstarted") {
			// Render the QR code with the default callback URL
			renderQRCode("https://opago-pay.com/getstarted", center_x, center_y + 18, qr_max_w, qr_max_h - 18);
			currentPaymentQRCodeData = "https://opago-pay.com/getstarted";
		}
		else if (!onlineStatus) {
			renderQRCode(qrcodeData, center_x, center_y, qr_max_w, qr_max_h); 
			currentPaymentQRCodeData = qrcodeData;
			//Serial.println(("Payment QR code data: " + currentPaymentQRCodeData).c_str());
			renderText("\uE06A", MaterialIcons_Regular_24pt_chare06a24pt8b, TFT_WHITE, screenWidth - 10, tft.height() - 10, BR_DATUM); 
		}
		else {
			renderQRCode(qrcodeData, center_x, center_y, qr_max_w, qr_max_h); 
			currentPaymentQRCodeData = qrcodeData;
			if (initFlagNFC) {
				renderText("\uEA71", MaterialIcons_Regular_24pt_charea7124pt8b, 0x07E0, 0, tft.height() - 10, BL_DATUM); // Bright green
			} else {
				renderText("\uEA71", MaterialIcons_Regular_24pt_charea7124pt8b, 0xF800, 0, tft.height() - 10, BL_DATUM); // Bright red
			}
		}
	}

	void showPaymentPinScreen(const std::string &pin) {
		clearScreen(false);
		std::string displayPin = "____";
		for (int i = 0; i < pin.length(); i++) {
			displayPin[i] = pin[i];
		}
		const GFXfont pinFont = getBestFitFont(displayPin, monospaceFonts);
		const BoundingBox pinBBox = renderText(displayPin, pinFont, textColor, center_x, center_y - 2);
		const std::string instructionText1 = i18n::t("PIN") + ":";
		const GFXfont instructionFont1 = getBestFitFont(instructionText1, monospaceFonts, screenWidth, pinBBox.h / 2);
		renderText(instructionText1, instructionFont1, textColor, center_x, 0, TC_DATUM);
		renderText("\uE00A", MaterialIcons_Regular_24pt_chare00a24pt8b, TFT_WHITE, 35, tft.height() - 10, BR_DATUM); 
	}

	void showErrorScreen(const std::string &error) {
		clearScreen(false);
		const GFXfont pinFont = getBestFitFont(error, monospaceFonts);
		const BoundingBox pinBBox = renderText(error, pinFont, textColor, center_x, center_y - 2);
		const std::string instructionText1 = i18n::t("ERROR") + ":";
		const GFXfont instructionFont1 = getBestFitFont(instructionText1, monospaceFonts, screenWidth, pinBBox.h / 2);
		const std::string instructionText2 = "X " + i18n::t("reset");
		const GFXfont instructionFont2 = getBestFitFont(instructionText2, monospaceFonts, screenWidth / 2, pinBBox.h / 2);
		renderText(instructionText1, instructionFont1, textColor, center_x, 0, TC_DATUM);
		renderText(instructionText2, instructionFont2, textColor, 0, tft.height(), BL_DATUM);
	}

	void adjustContrast(const int &percentChange) {
		const int newContrastPercent = std::max(minContrastPercent, std::min(100, currentContrastPercent + percentChange));
		if (newContrastPercent != currentContrastPercent) {
			setContrastLevel(newContrastPercent);
		}
	}

	void showStatusSymbols(const int &batteryPercent) {
		// Check if battery is below 15%
		if (batteryPercent < 5 && amount == 0) {
			// Render E19C in 56pt in the middle of the screen in bright red
			tft.fillRect(0, 0, screenWidth, tft.height(), bgColor); // Fill the screen with bgColor
			renderText("\uE19C", MaterialIcons_Regular_56pt_chare19c56pt8b, 0xF800, center_x - 15, center_y + 50, TC_DATUM);
		} else {
			// Show wifi status
			uint16_t color;
			if (onlineStatus) {
				color = TFT_WHITE;
				tft.fillRect(5, 5, 24, 24, bgColor); // Clear the area before rendering 
				wifiBBox = renderText("\uE63E", MaterialIcons_Regular_12pt_chare63e12pt8b, color, 5, 30, TL_DATUM);
			} else {
				color = 0xF800; // Bright red
				tft.fillRect(5, 5, 24, 24, bgColor); // Clear the area before rendering 
				wifiBBox = renderText("\uE648", MaterialIcons_Regular_12pt_chare64812pt8b, color, 5, 30, TL_DATUM);
			}

			// Show USB symbol if connected to USB
			if (power::isUSBPowered()) {
				color = TFT_WHITE;
				tft.fillRect(30, 5, 24, 24, bgColor); // Clear the area before rendering 
				usbBBox = renderText("\uE1E0", MaterialIcons_Regular_12pt_chare1e012pt8b, color, 30, 30, TL_DATUM);
			}

			// Show battery status
			if (power::isUSBPowered()) {
				if (power::isCharging()) {
					color = 0x07E0; // Bright green
					tft.fillRect(screenWidth - 35, 5, 24, 24, bgColor); // Clear the area before rendering 
					batteryBBox = renderText("\uE1A3", MaterialIcons_Regular_12pt_chare1a312pt8b, color, screenWidth - 35, 30, TL_DATUM);
				} else {
					color = TFT_WHITE;
					tft.fillRect(screenWidth - 35, 5, 24, 24, bgColor); // Clear the area before rendering
					batteryBBox = renderText("\uE1A4", MaterialIcons_Regular_12pt_chare1a412pt8b, color, screenWidth - 35, 30, TL_DATUM);
				}
			} else {
				tft.fillRect(30, 5, 24, 24, bgColor); // Clear the USB area before rendering 
				if (batteryPercent >= 20) {
					if (batteryPercent >= 80) {
						color = 0x07E0; // Bright green
					} else if (batteryPercent >= 60) {
						color = 0x9E66; // Light green
					} else if (batteryPercent >= 40) {
						color = 0xFD20; // Bright orange
					} else {
						color = 0xFD60; // Light red
					}
					tft.fillRect(screenWidth - 35, 5, 24, 24, bgColor); // Clear the area before rendering
					batteryBBox = renderText("\uE1A4", MaterialIcons_Regular_12pt_chare1a412pt8b, color, screenWidth - 35, 30, TL_DATUM);
				} else {
					color = 0xF800; // Bright red
					tft.fillRect(screenWidth - 35, 5, 24, 24, bgColor); // Clear the area before rendering
					batteryBBox = renderText("\uE19C", MaterialIcons_Regular_12pt_chare19c12pt8b, color, screenWidth - 35, 30, TL_DATUM);
				}
			}
			
			//Serial.println(currentScreen.c_str());
			if (config::getString("callbackUrl") == "https://opago-pay.com/getstarted") {
				// Render "DEMO MODE" in the top center of the screen in bright red
				renderText("DEMO MODE", Courier_Prime_Code16pt8b, 0xF800, center_x, 0, TC_DATUM);
			} else if (currentScreen == "paymentQRCode") {
				// Check the currency and adjust the amount accordingly
				std::string currencyStr = config::getString("fiatCurrency");
				// Use the utility function to format the amount with the desired precision
				// If the currency is 'sat', multiply the amount by 100
				std::string amountStr = currencyStr == "sat" 
					? util::doubleToStringWithPrecision(amount * 100, config::getUnsignedShort("fiatPrecision"))
					: util::doubleToStringWithPrecision(amount, config::getUnsignedShort("fiatPrecision"));

				// Combine the formatted amount and currency into one string
				std::string amountText = amountStr + " " + currencyStr;

				// Render the combined amount and currency string on the screen
				renderText(amountText.c_str(), Courier_Prime_Code12pt8b, TFT_WHITE, center_x, 0, TC_DATUM);
			}

			// Update the global bounding box for status symbols
			statusSymbolsBBox.x = 0;
			statusSymbolsBBox.y = 0;
			statusSymbolsBBox.w = screenWidth;
			statusSymbolsBBox.h = 36;
		}
	}

	void tellBacklightStatus() {
		if (digitalRead(TFT_BL) == HIGH) {
			logger::write("Backlight is on");
		} else {
			logger::write("Backlight is off");
		}
	}

	void sleep() {
		if (TFT_BL && !backlightOff) {
			logger::write("Turning off TFT backlight");
			digitalWrite(TFT_BL, LOW);
			backlightOff = true;
		} else {
			clearScreen();
		}
	}

	void wakeup() {
		if (TFT_BL && backlightOff) {
			logger::write("Turning on TFT backlight");
			digitalWrite(TFT_BL, HIGH);
			backlightOff = false;
		} else {
			clearScreen();
		}
	}
}
