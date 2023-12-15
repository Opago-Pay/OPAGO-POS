#include "screen.h"

namespace {
	std::string currentScreen = "";
}

namespace screen {

	void init() {
		screen_tft::init();
		currentScreen = "";
	}

	std::string getCurrentScreen() {
		return currentScreen;
	}

	void showHomeScreen() {
		logger::write("Show screen: Home");
		screen_tft::renderJPEG("/home.jpg", 0, 0, 1);
		currentScreen = "home";
		logger::write("Current Screen " + getCurrentScreen());
	}

	void showX() {
		logger::write("Show screen: X");
		screen_tft::renderJPEG("/x.jpg", 0, 0, 1);
		currentScreen = "X";
		logger::write("Current Screen " + getCurrentScreen());
	}

	void showNFC() {
		logger::write("Show screen: NFC");
		screen_tft::renderJPEG("/NFC.jpg", 0, 0, 1);
		currentScreen = "NFC";
		logger::write("Current Screen " + getCurrentScreen());
	}

	void showNFCfailed() {
		logger::write("Show screen: NFCfailed");
		screen_tft::renderJPEG("/NFCfailed.jpg", 0, 0, 1);
		currentScreen = "NFCfailed";
		logger::write("Current Screen " + getCurrentScreen());
	}

	void showNFCsuccess() {
		logger::write("Show screen: NFCsuccess");
		screen_tft::renderJPEG("/NFCsuccess.jpg", 0, 0, 1);
		currentScreen = "NFCsuccess";
		logger::write("Current Screen " + getCurrentScreen());
	}

	void showSuccess() {
		logger::write("Show screen: Success");
		screen_tft::renderJPEG("/success.jpg", 0, 0, 1);
		currentScreen = "success";
		logger::write("Current Screen " + getCurrentScreen());
	}

	void showSand() {
		logger::write("Show screen: Sand");
		screen_tft::renderJPEG("/sand.jpg", 0, 0, 1);
		currentScreen = "sand";
		logger::write("Current Screen " + getCurrentScreen());
	}

	void showEnterAmountScreen(const double &amount) {
		logger::write("Show screen: Enter Amount");
		screen_tft::showEnterAmountScreen(amount);
		currentScreen = "enterAmount";
		getCurrentScreen();
	}

	void showPaymentQRCodeScreen(const std::string &qrcodeData) {
		logger::write("Show screen: Payment QR Code");
		screen_tft::showPaymentQRCodeScreen(qrcodeData);
		currentScreen = "paymentQRCode";
		logger::write("Current Screen " + getCurrentScreen());
	}

	void showPaymentPinScreen(const std::string &pin) {
		logger::write("Show screen: Payment PIN");
		screen_tft::showPaymentPinScreen(pin);
		currentScreen = "paymentPin";
		logger::write("Current Screen " + getCurrentScreen());
	}

	void showErrorScreen(const std::string &error) {
		logger::write("Show screen: Error");
		screen_tft::showErrorScreen(error);
		currentScreen = "error";
		logger::write("Current Screen " + getCurrentScreen());
	}

	void adjustContrast(const int &percentChange) {
		screen_tft::adjustContrast(percentChange);
	}

	void showBatteryPercent(const int &percent) {
		screen_tft::showBatteryPercent(percent);
	}

	void hideBatteryPercent() {
		screen_tft::hideBatteryPercent();
	}

	void sleep() {
		screen_tft::sleep();
	}

	void wakeup() {
		screen_tft::wakeup();
	}
}
