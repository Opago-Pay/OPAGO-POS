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

	void clearScreenBottom(const bool &reset = true) {
		// Clear the screen except for the status symbol boxes
		tft.fillRect(0, statusSymbolsBBox.y + statusSymbolsBBox.h, tft.width(), tft.height() - (statusSymbolsBBox.y + statusSymbolsBBox.h), bgColor);
		
		// Clear the areas around individual status symbols
		tft.fillRect(0, 0, wifiBBox.x, wifiBBox.y + wifiBBox.h, bgColor); // Left of WiFi symbol
		tft.fillRect(wifiBBox.x + wifiBBox.w, 0, usbBBox.x - (wifiBBox.x + wifiBBox.w), usbBBox.y + usbBBox.h, bgColor); // Between WiFi and USB symbols
		tft.fillRect(usbBBox.x + usbBBox.w, 0, batteryBBox.x - (usbBBox.x + usbBBox.w), batteryBBox.y + batteryBBox.h, bgColor); // Between USB and Battery symbols
		tft.fillRect(batteryBBox.x + batteryBBox.w, 0, tft.width() - (batteryBBox.x + batteryBBox.w), batteryBBox.y + batteryBBox.h, bgColor); // Right of Battery symbol

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

	uint16_t adjustColorByContrast(uint16_t originalColor) {
		// Extract RGB components
		uint8_t r = (originalColor >> 11) & 0x1F;
		uint8_t g = (originalColor >> 5) & 0x3F;
		uint8_t b = originalColor & 0x1F;
		
		// Calculate adjustment factor from contrast level (0.1 to 1.0)
		float contrastFactor = currentContrastPercent / 100.0;
		
		// Adjust each component
		r = (uint8_t)(r * contrastFactor);
		g = (uint8_t)(g * contrastFactor);
		b = (uint8_t)(b * contrastFactor);
		
		// Recombine into 16-bit color
		return (r << 11) | (g << 5) | b;
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
		clearScreen(false);
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
		clearScreenBottom(false);
		int16_t amount_y = center_y - 12;

		// Check for demo mode and show red text if enabled
		if (config::getString("callbackUrl") == "https://opago-pay.com/getstarted" || 
			config::getString("apiKey.key") == "BueokH4o3FmhWmbvqyqLKz") {
			renderText("DEMO MODE", Courier_Prime_Code12pt8b, 0xF800, center_x, 5, TC_DATUM);
		}

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

		if (config::getString("callbackUrl") == "https://opago-pay.com/getstarted" || 
			config::getString("apiKey.key") == "BueokH4o3FmhWmbvqyqLKz") {
			// Demo mode text at status icon height
			renderText("DEMO MODE", Courier_Prime_Code12pt8b, 0xF800, center_x, 5, TC_DATUM);
			
			// Demo mode QR code and instructions
			int qrCodeOffsetY = 8;
			int qrCodeHeightAdjustment = 48;
			const std::string demoUrl = "https://opago-pay.com/getstarted";
			int16_t demo_qr_max_w = screenWidth - 10;
			int16_t demo_qr_max_h = tft.height() - statusSymbolsBBox.h - 60;
			renderQRCode(demoUrl, center_x, center_y - qrCodeOffsetY, demo_qr_max_w, demo_qr_max_h - qrCodeHeightAdjustment);
			currentPaymentQRCodeData = demoUrl;
			int textOffsetY = 48;
			renderText("Scan for Setup", Courier_Prime_Code12pt8b, TFT_WHITE, center_x, tft.height() - textOffsetY, TC_DATUM);
			renderText("opago-pay.com/getstarted", Courier_Prime_Code10pt8b, TFT_WHITE, center_x, tft.height() - (textOffsetY - 17), TC_DATUM);
		} else {
			// Normal payment mode (not demo)
			std::string amountStr = getAmountFiatCurrencyString(amount);
			std::string currency = config::getString("fiatCurrency");
			
			// Position amount text at the very top with original font
			renderText(amountStr, Courier_Prime_Code12pt8b, TFT_WHITE, center_x, 5, TC_DATUM);
			
			// Calculate QR code area - absolutely maximize screen usage
			int16_t qr_top_margin = 25; // Minimal top margin
			int16_t qr_bottom_margin = 12; // Just enough for icons
			
			// Calculate maximum QR dimensions - use full width
			int16_t qr_max_width = screenWidth;
			int16_t qr_max_height = tft.height() - qr_top_margin - qr_bottom_margin;
			
			// Render QR code without automatic borders to maximize size
			// We'll render it manually to avoid the border reduction
			const char* data = qrcodeData.c_str();
			uint8_t qrVersion = 1;
			while (qrVersion <= 40) {
				const uint16_t bufferSize = qrcode_getBufferSize(qrVersion);
				QRCode qrcode;
				uint8_t qrcodeDataBuffer[bufferSize];
				const int8_t result = qrcode_initText(&qrcode, qrcodeDataBuffer, qrVersion, ECC_LOW, data);
				if (result == 0) {
					// Calculate scale to fill available space
					uint8_t scale = std::min(qr_max_width / qrcode.size, qr_max_height / qrcode.size);
					uint16_t qr_size = qrcode.size * scale;
					
					// Center the QR code
					int16_t qr_x = (screenWidth - qr_size) / 2;
					int16_t qr_y = qr_top_margin + ((qr_max_height - qr_size) / 2);
					
					// Draw white background
					tft.fillRect(qr_x, qr_y, qr_size, qr_size, textColor);
					
					// Draw QR modules
					for (uint8_t y = 0; y < qrcode.size; y++) {
						for (uint8_t x = 0; x < qrcode.size; x++) {
							if (qrcode_getModule(&qrcode, x, y)) {
								tft.fillRect(qr_x + (x * scale), qr_y + (y * scale), scale, scale, bgColor);
							}
						}
					}
					break;
				} else if (result == -2) {
					qrVersion++;
				} else {
					logger::write("QR code generation failed", "error");
					break;
				}
			}
			currentPaymentQRCodeData = qrcodeData;
			
			// Log the displayed LNURL with amount and currency
			logger::write("[screen] Displaying payment QR - Amount: " + 
			              util::doubleToStringWithPrecision(amount, config::getUnsignedShort("fiatPrecision")) + 
			              " " + currency + ", LNURL: " + qrcodeData, "info");
			
			// Show PIN icon at bottom right corner
			renderText("\uE06A", MaterialIcons_Regular_24pt_chare06a24pt8b, TFT_WHITE, screenWidth - 5, tft.height() - 2, BR_DATUM);
			
			// Show NFC status icon at bottom left if enabled
			if (config::getBool("nfcEnabled")) {
				if (initFlagNFC) {
					renderText("\uEA71", MaterialIcons_Regular_24pt_charea7124pt8b, 0x07E0, 5, tft.height() - 2, BL_DATUM);
				} else {
					renderText("\uEA71", MaterialIcons_Regular_24pt_charea7124pt8b, 0xF800, 5, tft.height() - 2, BL_DATUM);
				}
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

	void showStatusSymbols(const int& batteryPercent) {
		uint16_t color;
		if (onlineStatus) {
			color = adjustColorByContrast(TFT_WHITE);
			tft.fillRect(5, 5, 24, 24, bgColor);
			wifiBBox = renderText("\uE63E", MaterialIcons_Regular_12pt_chare63e12pt8b, color, 5, 30, TL_DATUM);
		} else {
			color = adjustColorByContrast(0xF800);  // Red
			tft.fillRect(5, 5, 24, 24, bgColor);
			wifiBBox = renderText("\uE648", MaterialIcons_Regular_12pt_chare64812pt8b, color, 5, 30, TL_DATUM);
		}

		if (power::isUSBPowered()) {
			if (power::isCharging()) {
				color = adjustColorByContrast(0x07E0);  // Green
			} else {
				color = adjustColorByContrast(TFT_WHITE);
			}
			tft.fillRect(screenWidth - 35, 5, 24, 24, bgColor);
			batteryBBox = renderText(power::isCharging() ? "\uE1A3" : "\uE1A4",
				power::isCharging() ? MaterialIcons_Regular_12pt_chare1a312pt8b : MaterialIcons_Regular_12pt_chare1a412pt8b,
				color, screenWidth - 35, 30, TL_DATUM);
			
			color = adjustColorByContrast(TFT_WHITE);
			tft.fillRect(screenWidth - 35, 35, 24, 24, bgColor);
			usbBBox = renderText("\uE1E0", MaterialIcons_Regular_12pt_chare1e012pt8b, color, screenWidth - 35, 60, TL_DATUM);
		} else {
			tft.fillRect(screenWidth - 35, 35, 24, 24, bgColor);
			if (batteryPercent >= 20) {
				if (batteryPercent >= 40) {
					color = adjustColorByContrast(0x07E0);  // Green
				} else if (batteryPercent >= 20) {
					color = adjustColorByContrast(0xFD20);  // Orange
				} else {
					color = adjustColorByContrast(0xFD60);  // Light red
				}
				tft.fillRect(screenWidth - 35, 5, 24, 24, bgColor);
				batteryBBox = renderText("\uE1A4", MaterialIcons_Regular_12pt_chare1a412pt8b, color, screenWidth - 35, 30, TL_DATUM);
			} else {
				color = adjustColorByContrast(0xF800);  // Red
				tft.fillRect(screenWidth - 35, 5, 24, 24, bgColor);
				batteryBBox = renderText("\uE19C", MaterialIcons_Regular_12pt_chare19c12pt8b, color, screenWidth - 35, 30, TL_DATUM);
			}
		}
		
		statusSymbolsBBox.x = 0;
		statusSymbolsBBox.y = 0;
		statusSymbolsBBox.w = screenWidth;
		statusSymbolsBBox.h = 36;
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

	void showContrastInputScreen(const std::string &contrastInput) {
		clearScreen(false);
		showStatusSymbols(power::getBatteryPercent());
		
		// Show entered digits or underscores for empty spots
		std::string displayInput = "___";  // 3 digits for contrast
		for (size_t i = 0; i < contrastInput.length() && i < 3; i++) {
			displayInput[i] = contrastInput[i];
		}
		
		// Show instruction text at the top with fixed font size
		const std::string instructionText1 = "QR Contrast";
		const std::string instructionText2 = "(1-100)";
		
		renderText(instructionText1, Courier_Prime_Code12pt8b, textColor, center_x, 5, TC_DATUM);
		renderText(instructionText2, Courier_Prime_Code12pt8b, textColor, center_x, 35, TC_DATUM);
		
		// Show input digits in the center
		const GFXfont inputFont = getBestFitFont(displayInput, monospaceFonts);
		renderText(displayInput, inputFont, textColor, center_x, center_y - 2, TC_DATUM);
	}

	void showSensitivityInputScreen(const std::string &sensitivityInput) {
		clearScreenBottom(false);  // Only clear bottom portion to preserve status
		showStatusSymbols(power::getBatteryPercent());
		
		std::string displayInput = "___";  // 3 digits for sensitivity
		for (size_t i = 0; i < sensitivityInput.length() && i < 3; i++) {
			displayInput[i] = sensitivityInput[i];
		}
		
		const std::string instructionText1 = "Touch Sensitivity";
		const std::string instructionText2 = "(1-100)";
		
		renderText(instructionText1, Courier_Prime_Code12pt8b, textColor, center_x, 5, TC_DATUM);
		renderText(instructionText2, Courier_Prime_Code12pt8b, textColor, center_x, 35, TC_DATUM);
		
		const GFXfont inputFont = getBestFitFont(displayInput, monospaceFonts);
		renderText(displayInput, inputFont, textColor, center_x, center_y - 2, TC_DATUM);
	}
}
