#include "screen.h"
#include "screen_tft.h"
#include "logger.h"
#include <TFT_eSPI.h>

extern TFT_eSPI tft;
extern int rightMargin;

namespace screen {

QueueHandle_t screenQueue = NULL;
TaskHandle_t screenTaskHandle = NULL;
std::string currentScreen = "";

void init() {
	screen_tft::init();
	currentScreen = "";
	
	// Create screen message queue
	screenQueue = xQueueCreate(10, sizeof(ScreenMessage));
	if (screenQueue == NULL) {
		logger::write("Failed to create screen queue", "error");
		return;
	}
	
	// Create screen task
	BaseType_t result = xTaskCreate(
		screenTask,
		"ScreenTask",
		8192,
		NULL,
		3, // Higher priority than other tasks
		&screenTaskHandle
	);
	
	if (result != pdPASS) {
		logger::write("Failed to create screen task", "error");
	}
}

std::string getCurrentScreen() {
	return currentScreen;
}

// Screen task that processes queued screen updates
void screenTask(void* parameter) {
	ScreenMessage msg;
	std::string lastScreen = "";
	double lastAmount = 0;
	std::string lastQRCode = "";
	static unsigned long lastStatusUpdate = 0;
	const unsigned long STATUS_UPDATE_INTERVAL = 2100;
	bool lastOnlineStatus = onlineStatus;
	
	// Initial screen sequence
	screen_tft::renderJPEG("/home.jpg", 0, 0, 1);
	currentScreen = "home";
	vTaskDelay(pdMS_TO_TICKS(2100));
	screen_tft::showStatusSymbols(power::getBatteryPercent());
	screen_tft::showEnterAmountScreen(0);
	currentScreen = "enterAmount";
	
	while (true) {
		bool processedMessage = false;
		
		// First, process any non-status screen updates
		if (uxQueueMessagesWaiting(screenQueue) > 0) {
			if (xQueuePeek(screenQueue, &msg, 0) == pdTRUE) {
				if (msg.type != ScreenMessage::MessageType::STATUS_SYMBOLS) {
					xQueueReceive(screenQueue, &msg, 0);
					bool shouldProcess = true;
					
					// Check for duplicate screens
					if (msg.type == ScreenMessage::MessageType::ENTER_AMOUNT) {
						shouldProcess = (currentScreen != "enterAmount" || lastAmount != msg.amount);
					} else if (msg.type == ScreenMessage::MessageType::PAYMENT_QR) {
						shouldProcess = (currentScreen != "paymentQRCode" || lastQRCode != msg.text);
					} else if (msg.type == ScreenMessage::MessageType::PAYMENT_PIN) {
						// Always process PIN updates as they show progress
						shouldProcess = true;
					} else {
						// For all other screens, always process if it's different from current screen
						std::string newScreen;
						switch (msg.type) {
							case ScreenMessage::MessageType::HOME: newScreen = "home"; break;
							case ScreenMessage::MessageType::X: newScreen = "X"; break;
							case ScreenMessage::MessageType::NFC: newScreen = "NFC"; break;
							case ScreenMessage::MessageType::NFC_FAILED: newScreen = "NFCfailed"; break;
							case ScreenMessage::MessageType::NFC_SUCCESS: newScreen = "NFCsuccess"; break;
							case ScreenMessage::MessageType::SUCCESS: newScreen = "success"; break;
							case ScreenMessage::MessageType::SAND: newScreen = "sand"; break;
							case ScreenMessage::MessageType::NO_WIFI: newScreen = "nowifi"; break;
							case ScreenMessage::MessageType::MENU: newScreen = "menu"; break;
							case ScreenMessage::MessageType::PAYMENT_PIN: newScreen = "paymentPin"; break;
							case ScreenMessage::MessageType::ERROR: newScreen = "error"; break;
							default: newScreen = currentScreen; break;
						}
						shouldProcess = (currentScreen != newScreen);
					}
					
					if (shouldProcess) {
						processedMessage = true;
						// Process the message
						switch (msg.type) {
							case ScreenMessage::MessageType::HOME:
								logger::write("Show screen: Home", "debug");
								screen_tft::renderJPEG("/home.jpg", 0, 0, 1);
								currentScreen = "home";
								lastScreen = currentScreen;
								break;
								
							case ScreenMessage::MessageType::X:
								logger::write("Show screen: X", "debug");
								screen_tft::renderJPEG("/x.jpg", 0, 0, 1);
								currentScreen = "X";
								lastScreen = currentScreen;
								break;
								
							case ScreenMessage::MessageType::NFC:
								logger::write("Show screen: NFC", "debug");
								screen_tft::renderJPEG("/NFC.jpg", 0, 0, 1);
								currentScreen = "NFC";
								lastScreen = currentScreen;
								break;
								
							case ScreenMessage::MessageType::ENTER_AMOUNT:
								logger::write("Show screen: Enter Amount", "debug");
								screen_tft::showEnterAmountScreen(msg.amount);
								currentScreen = "enterAmount";
								lastScreen = currentScreen;
								lastAmount = msg.amount;
								break;
								
							case ScreenMessage::MessageType::PAYMENT_QR:
								logger::write("Show screen: Payment QR Code", "debug");
								screen_tft::showPaymentQRCodeScreen(msg.text);
								currentScreen = "paymentQRCode";
								lastScreen = currentScreen;
								lastQRCode = msg.text;
								break;
								
							case ScreenMessage::MessageType::NFC_FAILED:
								logger::write("Show screen: NFCfailed", "debug");
								screen_tft::renderJPEG("/NFCfailed.jpg", 0, 0, 1);
								currentScreen = "NFCfailed";
								lastScreen = currentScreen;
								break;
								
							case ScreenMessage::MessageType::NFC_SUCCESS:
								logger::write("Show screen: NFCsuccess", "debug");
								screen_tft::renderJPEG("/NFCsuccess.jpg", 0, 0, 1);
								currentScreen = "NFCsuccess";
								lastScreen = currentScreen;
								break;
								
							case ScreenMessage::MessageType::SUCCESS:
								logger::write("Show screen: Success", "debug");
								screen_tft::renderJPEG("/success.jpg", 0, 0, 1);
								currentScreen = "success";
								lastScreen = currentScreen;
								break;
								
							case ScreenMessage::MessageType::SAND:
								logger::write("Show screen: Sand", "debug");
								screen_tft::renderJPEG("/sand.jpg", 0, 0, 1);
								currentScreen = "sand";
								lastScreen = currentScreen;
								break;
								
							case ScreenMessage::MessageType::NO_WIFI:
								logger::write("Show screen: No WiFi", "debug");
								screen_tft::renderJPEG("/nowifi.jpg", 0, 0, 1);
								currentScreen = "nowifi";
								lastScreen = currentScreen;
								break;
								
							case ScreenMessage::MessageType::MENU:
								logger::write("Show screen: Menu", "debug");
								currentScreen = "menu";
								lastScreen = currentScreen;
								tft.fillScreen(TFT_BLACK);
								{
									int16_t center_x = (tft.width() - rightMargin) / 2;
									tft.setTextColor(TFT_WHITE);
									tft.setTextDatum(TC_DATUM);
									tft.setFreeFont(&Courier_Prime_Code16pt8b);
									tft.drawString("MENU", center_x, 40);
									
									std::string nfcStatus = config::getBool("nfcEnabled") ? "OFF" : "ON";
									std::string offlineStatus = config::getBool("offlineMode") ? "OFF" : "ON";
									
									tft.setFreeFont(&Courier_Prime_Code12pt8b);
									tft.drawString("1: NFC " + String(nfcStatus.c_str()), center_x, 80);
									tft.drawString("2: Offline Mode " + String(offlineStatus.c_str()), center_x, 120);
									tft.drawString("X: Exit", center_x, 160);
								}
								break;
								
							case ScreenMessage::MessageType::PAYMENT_PIN:
								logger::write("Show screen: Payment PIN", "debug");
								screen_tft::showPaymentPinScreen(msg.text);
								currentScreen = "paymentPin";
								lastScreen = currentScreen;
								break;
								
							case ScreenMessage::MessageType::ERROR:
								logger::write("Show screen: Error", "debug");
								screen_tft::showErrorScreen(msg.text);
								currentScreen = "error";
								lastScreen = currentScreen;
								break;
						}
						logger::write("Current Screen " + getCurrentScreen(), "debug");
					}
				}
			}
		}
		
		// Then handle status updates if no other screen was processed
		if (!processedMessage) {
			// Check for queued status updates first
			if (uxQueueMessagesWaiting(screenQueue) > 0) {
				if (xQueuePeek(screenQueue, &msg, 0) == pdTRUE) {
					if (msg.type == ScreenMessage::MessageType::STATUS_SYMBOLS) {
						xQueueReceive(screenQueue, &msg, 0);
						screen_tft::showStatusSymbols(msg.batteryPercent);
					}
				}
			}
			
			// Regular timed status updates
			unsigned long currentTime = millis();
			if (currentTime - lastStatusUpdate >= STATUS_UPDATE_INTERVAL) {
				screen_tft::showStatusSymbols(power::getBatteryPercent());
				lastStatusUpdate = currentTime;
			}
			
			// WiFi status changes
			if (lastOnlineStatus != onlineStatus) {
				screen_tft::showStatusSymbols(power::getBatteryPercent());
				lastOnlineStatus = onlineStatus;
			}
		}
		
		// Always yield to prevent watchdog issues
		vTaskDelay(pdMS_TO_TICKS(10));
	}
}

// Modified screen update functions to use queue
void showHomeScreen() {
	ScreenMessage msg(ScreenMessage::MessageType::HOME);
	xQueueSend(screenQueue, &msg, portMAX_DELAY);
}

void showX() {
	ScreenMessage msg(ScreenMessage::MessageType::X);
	xQueueSend(screenQueue, &msg, portMAX_DELAY);
}

void showEnterAmountScreen(const double &amount) {
	ScreenMessage msg{ScreenMessage::MessageType::ENTER_AMOUNT};
	msg.amount = amount;
	xQueueSend(screenQueue, &msg, portMAX_DELAY);
}

void showPaymentQRCodeScreen(const std::string &qrcodeData) {
	ScreenMessage msg(ScreenMessage::MessageType::PAYMENT_QR);
	strncpy(msg.text, qrcodeData.c_str(), sizeof(msg.text) - 1);
	msg.text[sizeof(msg.text) - 1] = '\0';
	xQueueSend(screenQueue, &msg, portMAX_DELAY);
}

// Implement other screen functions similarly...

void showNFC() {
    ScreenMessage msg{ScreenMessage::MessageType::NFC};
    xQueueSend(screenQueue, &msg, portMAX_DELAY);
}

void showNFCfailed() {
    ScreenMessage msg{ScreenMessage::MessageType::NFC_FAILED};
    xQueueSend(screenQueue, &msg, portMAX_DELAY);
}

void showNFCsuccess() {
    ScreenMessage msg{ScreenMessage::MessageType::NFC_SUCCESS};
    xQueueSend(screenQueue, &msg, portMAX_DELAY);
}

void showSuccess() {
    ScreenMessage msg{ScreenMessage::MessageType::SUCCESS};
    xQueueSend(screenQueue, &msg, portMAX_DELAY);
}

void showSand() {
    ScreenMessage msg{ScreenMessage::MessageType::SAND};
    xQueueSend(screenQueue, &msg, portMAX_DELAY);
}

void showNowifi() {
    ScreenMessage msg{ScreenMessage::MessageType::NO_WIFI};
    xQueueSend(screenQueue, &msg, portMAX_DELAY);
}

void showMenu() {
    ScreenMessage msg{ScreenMessage::MessageType::MENU};
    xQueueSend(screenQueue, &msg, portMAX_DELAY);
}

void showPaymentPinScreen(const std::string &pin) {
    ScreenMessage msg{ScreenMessage::MessageType::PAYMENT_PIN};
    strncpy(msg.text, pin.c_str(), sizeof(msg.text) - 1);
    msg.text[sizeof(msg.text) - 1] = '\0';
    xQueueSend(screenQueue, &msg, portMAX_DELAY);
}

void showErrorScreen(const std::string &error) {
    ScreenMessage msg{ScreenMessage::MessageType::ERROR};
    strncpy(msg.text, error.c_str(), sizeof(msg.text) - 1);
    msg.text[sizeof(msg.text) - 1] = '\0';
    xQueueSend(screenQueue, &msg, portMAX_DELAY);
}

void adjustContrast(const int &percentChange) {
    ScreenMessage msg{ScreenMessage::MessageType::ADJUST_CONTRAST};
    msg.contrastChange = percentChange;
    xQueueSend(screenQueue, &msg, portMAX_DELAY);
}

void showStatusSymbols(const int &batteryPercent) {
    ScreenMessage msg(ScreenMessage::MessageType::STATUS_SYMBOLS);
    msg.batteryPercent = batteryPercent;
    xQueueSend(screenQueue, &msg, 0);
}

void sleep() {
    ScreenMessage msg{ScreenMessage::MessageType::SLEEP};
    xQueueSend(screenQueue, &msg, portMAX_DELAY);
}

void wakeup() {
    ScreenMessage msg{ScreenMessage::MessageType::WAKEUP};
    xQueueSend(screenQueue, &msg, portMAX_DELAY);
}

}
