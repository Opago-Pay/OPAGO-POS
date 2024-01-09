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
		logger::write("Show screen: Home", "debug");
		screen_tft::renderJPEG("/home.jpg", 0, 0, 1);
		currentScreen = "home";
		logger::write("Current Screen " + getCurrentScreen(), "debug");
	}

	void showX() {
		logger::write("Show screen: X", "debug");
		screen_tft::renderJPEG("/x.jpg", 0, 0, 1);
		currentScreen = "X";
		logger::write("Current Screen " + getCurrentScreen(), "debug");
	}

	void showNFC() {
		logger::write("Show screen: NFC", "debug");
		screen_tft::renderJPEG("/NFC.jpg", 0, 0, 1);
		currentScreen = "NFC";
		logger::write("Current Screen " + getCurrentScreen(), "debug");
	}

	void showNFCfailed() {
		logger::write("Show screen: NFCfailed", "debug");
		screen_tft::renderJPEG("/NFCfailed.jpg", 0, 0, 1);
		currentScreen = "NFCfailed";
		logger::write("Current Screen " + getCurrentScreen(), "debug");
	}

	void showNFCsuccess() {
		logger::write("Show screen: NFCsuccess", "debug");
		screen_tft::renderJPEG("/NFCsuccess.jpg", 0, 0, 1);
		currentScreen = "NFCsuccess";
		logger::write("Current Screen " + getCurrentScreen(), "debug");
	}

	void showSuccess() {
		logger::write("Show screen: Success", "debug");
		screen_tft::renderJPEG("/success.jpg", 0, 0, 1);
		currentScreen = "success";
		logger::write("Current Screen " + getCurrentScreen(), "debug");
	}

	void showSand() {
		logger::write("Show screen: Sand", "debug");
		screen_tft::renderJPEG("/sand.jpg", 0, 0, 1);
		currentScreen = "sand";
		logger::write("Current Screen " + getCurrentScreen(), "debug");
	}

	void showEnterAmountScreen(const double &amount) {
		logger::write("Show screen: Enter Amount", "debug");
		screen_tft::showEnterAmountScreen(amount);
		currentScreen = "enterAmount";
		logger::write("Current Screen " + getCurrentScreen(), "debug");
	}

	void showPaymentQRCodeScreen(const std::string &qrcodeData) {
		logger::write("Show screen: Payment QR Code", "debug");
		screen_tft::showPaymentQRCodeScreen(qrcodeData);
		currentScreen = "paymentQRCode";
		logger::write("Current Screen " + getCurrentScreen(), "debug");
	}

	void showPaymentPinScreen(const std::string &pin) {
		logger::write("Show screen: Payment PIN", "debug");
		screen_tft::showPaymentPinScreen(pin);
		currentScreen = "paymentPin";
		logger::write("Current Screen " + getCurrentScreen(), "debug");
	}

	void showErrorScreen(const std::string &error) {
		logger::write("Show screen: Error", "debug");
		screen_tft::showErrorScreen(error);
		currentScreen = "error";
		logger::write("Current Screen " + getCurrentScreen(), "debug");
	}

	void adjustContrast(const int &percentChange) {
		screen_tft::adjustContrast(percentChange);
	}

	void showStatusSymbols(const int &batteryPercent) {
		screen_tft::showStatusSymbols(batteryPercent);
	}

	void sleep() {
		screen_tft::sleep();
	}

	void wakeup() {
		screen_tft::wakeup();
	}
}
