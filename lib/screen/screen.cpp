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
	
	// Screen timing tracking
	static unsigned long nfcScreenStartTime = 0;
	static unsigned long nfcFailedScreenStartTime = 0;
	static unsigned long nfcSuccessScreenStartTime = 0;
	static unsigned long successScreenStartTime = 0;
	static unsigned long xScreenStartTime = 0;
	static unsigned long xScreen2StartTime = 0;
	
	// Minimum display durations (in milliseconds) - enforced before any transition
	const unsigned long NFC_MIN_DISPLAY_TIME = 500;
	const unsigned long NFC_FAILED_MIN_DISPLAY_TIME = 1000;
	const unsigned long NFC_SUCCESS_MIN_DISPLAY_TIME = 2000;  // Increased to ensure proper visibility
	const unsigned long SUCCESS_MIN_DISPLAY_TIME = 2100;  // Minimum before skip allowed
	const unsigned long X_MIN_DISPLAY_TIME = 500;  // Brief minimum for X screen
	
	// Intended display durations (in milliseconds) - normal auto-transition time
	const unsigned long SUCCESS_INTENDED_DISPLAY_TIME = 4200;  // Normal display time
	const unsigned long X_INTENDED_DISPLAY_TIME = 2100;  // Normal X screen time
	
	// Early transition trigger flag
	static bool earlyTransitionRequested = false;
	
	// Helper function to check if minimum display time has elapsed
	auto canTransitionFromScreen = [&](const std::string& fromScreen) -> bool {
		unsigned long currentTime = millis();
		
		if (fromScreen == "NFC") {
			return (currentTime - nfcScreenStartTime) >= NFC_MIN_DISPLAY_TIME;
		} else if (fromScreen == "NFCfailed") {
			return (currentTime - nfcFailedScreenStartTime) >= NFC_FAILED_MIN_DISPLAY_TIME;
		} else if (fromScreen == "NFCsuccess") {
			return (currentTime - nfcSuccessScreenStartTime) >= NFC_SUCCESS_MIN_DISPLAY_TIME;
		} else if (fromScreen == "success") {
			return (currentTime - successScreenStartTime) >= SUCCESS_MIN_DISPLAY_TIME;
		} else if (fromScreen == "X") {
			return (currentTime - xScreenStartTime) >= X_MIN_DISPLAY_TIME;
		}
		
		return true; // No minimum time restriction for other screens
	};
	
	// Helper function to check if intended display time has elapsed for auto-transitions
	auto shouldAutoTransition = [&](const std::string& fromScreen) -> bool {
		unsigned long currentTime = millis();
		
		if (fromScreen == "success") {
			return (currentTime - successScreenStartTime) >= SUCCESS_INTENDED_DISPLAY_TIME;
		} else if (fromScreen == "X") {
			return (currentTime - xScreenStartTime) >= X_INTENDED_DISPLAY_TIME;
		}
		
		return false; // No auto-transition for other screens
	};
	
	// Connection status display variables
	static unsigned long lastConnectionStatusCheck = 0;
	const unsigned long CONNECTION_STATUS_CHECK_INTERVAL = 100; // Check every 100ms for responsiveness
	static bool lastWifiLostScreenState = false;
	static bool lastPinSymbolState = false;  // Track PIN symbol state to avoid unnecessary refreshes
	
	// Initial screen sequence
	screen_tft::renderJPEG("/home.jpg", 0, 0, 1);
	currentScreen = "home";
	vTaskDelay(pdMS_TO_TICKS(2100));
	screen_tft::showStatusSymbols(power::getBatteryPercent());
	screen_tft::showEnterAmountScreen(0);
	currentScreen = "enterAmount";
	lastOnlineStatus = onlineStatus;
	lastPinSymbolState = screen_tft::shouldShowPinSymbol();  // Initialize PIN symbol state
	
	while (true) {
		bool processedMessage = false;
		unsigned long currentTime = millis();
		
		// Handle auto-transitions for screens with intended display times
		if (shouldAutoTransition(currentScreen)) {
			if (currentScreen == "success") {
				logger::write("[screen] Success screen auto-transition after 4.2s", "debug");
				// Auto-transition after success screen - app task will handle cleanup
				showEnterAmountScreen(0);
			} else if (currentScreen == "X") {
				logger::write("[screen] X screen auto-transition after 2.1s", "debug");
				// Auto-transition after X screen
				showEnterAmountScreen(0);
			}
		}
		
		// Handle early transition triggers (like pressing * to skip)
		if (earlyTransitionRequested && canTransitionFromScreen(currentScreen)) {
			earlyTransitionRequested = false;
			logger::write("[screen] Early transition triggered for " + currentScreen, "debug");
			
			if (currentScreen == "success") {
				// Handle early success screen transition - app task will handle cleanup
				showEnterAmountScreen(0);
			}
		}
		
		// Check connection status regularly when showing payment QR code
		if (currentScreen == "paymentQRCode" && 
			currentTime - lastConnectionStatusCheck >= CONNECTION_STATUS_CHECK_INTERVAL) {
			
			// Update connection status timing
			screen_tft::updateConnectionStatus();
			
			// Check if WiFi lost screen state has changed
			bool currentWifiLostScreenState = screen_tft::shouldShowWifiLostScreen();
			bool currentPinSymbolState = screen_tft::shouldShowPinSymbol();
			
			// Refresh screen only if there's an actual state change
			if (currentWifiLostScreenState != lastWifiLostScreenState) {
				// WiFi lost screen state changed, refresh the screen
				screen_tft::showPaymentQRCodeScreen(lastQRCode);
				lastWifiLostScreenState = currentWifiLostScreenState;
				logger::write("[screen] WiFi lost screen state changed: " + 
				              std::string(currentWifiLostScreenState ? "showing" : "hiding"), "info");
			} else if (currentPinSymbolState != lastPinSymbolState) {
				// PIN symbol state changed, refresh the screen
				screen_tft::showPaymentQRCodeScreen(lastQRCode);
				lastPinSymbolState = currentPinSymbolState;
				logger::write("[screen] PIN symbol state changed: " + 
				              std::string(currentPinSymbolState ? "showing" : "hiding"), "info");
			}
			
			lastConnectionStatusCheck = currentTime;
		}
		
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
					} else if (msg.type == ScreenMessage::MessageType::PAYMENT_PIN || 
							   msg.type == ScreenMessage::MessageType::CONTRAST_INPUT ||
							   msg.type == ScreenMessage::MessageType::SENSITIVITY_INPUT) {
						// Always process PIN, contrast and sensitivity input updates as they show progress
						shouldProcess = true;
					} else {
						// For all other screens, check minimum display time before transitioning
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
							case ScreenMessage::MessageType::CONTRAST_INPUT: newScreen = "contrastInput"; break;
							case ScreenMessage::MessageType::SENSITIVITY_INPUT: newScreen = "sensitivityInput"; break;
							default: newScreen = currentScreen; break;
						}
						
						// Validate screen transition logic to prevent invalid flows
						bool isValidTransition = true;
						if (currentScreen != newScreen) {
							// CRITICAL: Prevent NFC screen from following NFC success or sand
							if (newScreen == "NFC" && (currentScreen == "NFCsuccess" || currentScreen == "sand")) {
								logger::write("[screen] BLOCKED invalid transition: " + currentScreen + " → " + newScreen + 
								             " (NFC screen cannot follow NFC success or sand)", "warning");
								isValidTransition = false;
							}
							
							// CRITICAL: Ensure correct NFC flow
							// NFC success should be followed by sand (not back to NFC)
							// Sand should be followed by X or success (not back to NFC)
							if (currentScreen == "NFCsuccess" && newScreen != "sand") {
								logger::write("[screen] BLOCKED invalid transition: NFCsuccess → " + newScreen + 
								             " (should transition to sand)", "warning");
								isValidTransition = false;
							}
							
							if (currentScreen == "sand" && newScreen != "X" && newScreen != "success") {
								logger::write("[screen] BLOCKED invalid transition: sand → " + newScreen + 
								             " (should transition to X or success)", "warning");
								isValidTransition = false;
							}
						}
						
						// Check if screen is different and if we can transition from current screen
						if (currentScreen != newScreen && isValidTransition) {
							if (canTransitionFromScreen(currentScreen)) {
								shouldProcess = true;
							} else {
								// Re-queue the message for later processing if minimum time hasn't elapsed
								shouldProcess = false;
								xQueueSend(screenQueue, &msg, 0); // Re-queue without blocking
								logger::write("[screen] Minimum display time not met for " + currentScreen + 
								             ", re-queuing transition to " + newScreen, "debug");
							}
						} else {
							shouldProcess = false;
						}
					}
					
					if (shouldProcess) {
						processedMessage = true;
						// Process the message
						switch (msg.type) {
							case ScreenMessage::MessageType::HOME:
								screen_tft::renderJPEG("/home.jpg", 0, 0, 1);
								currentScreen = "home";
								lastScreen = currentScreen;
								break;
								
							case ScreenMessage::MessageType::X:
								screen_tft::renderJPEG("/x.jpg", 0, 0, 1);
								currentScreen = "X";
								lastScreen = currentScreen;
								xScreenStartTime = millis(); // Record start time for timing
								break;
								
							case ScreenMessage::MessageType::NFC:
								screen_tft::renderJPEG("/NFC.jpg", 0, 0, 1);
								currentScreen = "NFC";
								lastScreen = currentScreen;
								nfcScreenStartTime = millis(); // Record start time for minimum display duration
								break;
								
							case ScreenMessage::MessageType::ENTER_AMOUNT:
								screen_tft::showEnterAmountScreen(msg.amount);
								currentScreen = "enterAmount";
								lastScreen = currentScreen;
								lastAmount = msg.amount;
								break;
								
							case ScreenMessage::MessageType::PAYMENT_QR:
								screen_tft::showPaymentQRCodeScreen(msg.text);
								currentScreen = "paymentQRCode";
								lastScreen = currentScreen;
								lastQRCode = msg.text;
								// Reset connection timers when starting to show payment QR
								screen_tft::resetConnectionTimers();
								lastWifiLostScreenState = false;
								lastPinSymbolState = screen_tft::shouldShowPinSymbol();  // Initialize PIN symbol state
								break;
								
							case ScreenMessage::MessageType::NFC_FAILED:
								screen_tft::renderJPEG("/NFCfailed.jpg", 0, 0, 1);
								currentScreen = "NFCfailed";
								lastScreen = currentScreen;
								nfcFailedScreenStartTime = millis(); // Record start time for minimum display duration
								break;
								
							case ScreenMessage::MessageType::NFC_SUCCESS:
								screen_tft::renderJPEG("/NFCsuccess.jpg", 0, 0, 1);
								currentScreen = "NFCsuccess";
								lastScreen = currentScreen;
								nfcSuccessScreenStartTime = millis(); // Record start time for minimum display duration
								break;
								
							case ScreenMessage::MessageType::SUCCESS:
								screen_tft::renderJPEG("/success.jpg", 0, 0, 1);
								currentScreen = "success";
								lastScreen = currentScreen;
								successScreenStartTime = millis(); // Record start time for minimum display duration
								break;
								
							case ScreenMessage::MessageType::SAND:
								screen_tft::renderJPEG("/sand.jpg", 0, 0, 1);
								currentScreen = "sand";
								lastScreen = currentScreen;
								break;
								
							case ScreenMessage::MessageType::NO_WIFI:
								screen_tft::renderJPEG("/nowifi.jpg", 0, 0, 1);
								currentScreen = "nowifi";
								lastScreen = currentScreen;
								break;
								
							// In the menu case of showPaymentQRCodeScreen:
							case ScreenMessage::MessageType::MENU:
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
									tft.drawString("3: QR Contrast", center_x, 160);
									tft.drawString("4: Touch Sensitivity", center_x, 200);
									tft.drawString("X: Exit", center_x, 240);
								}
								break;
								
							case ScreenMessage::MessageType::PAYMENT_PIN:
								screen_tft::showPaymentPinScreen(msg.text);
								currentScreen = "paymentPin";
								lastScreen = currentScreen;
								break;
								
							case ScreenMessage::MessageType::ERROR:
								screen_tft::showErrorScreen(msg.text);
								currentScreen = "error";
								lastScreen = currentScreen;
								break;
								
							case ScreenMessage::MessageType::CONTRAST_INPUT:
								screen_tft::showContrastInputScreen(msg.text);
								currentScreen = "contrastInput";
								lastScreen = currentScreen;
								break;
								
							case ScreenMessage::MessageType::SENSITIVITY_INPUT:
								screen_tft::showSensitivityInputScreen(msg.text);
								currentScreen = "sensitivityInput";
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
			// Check for queued status updates and early transition triggers
			if (uxQueueMessagesWaiting(screenQueue) > 0) {
				if (xQueuePeek(screenQueue, &msg, 0) == pdTRUE) {
					if (msg.type == ScreenMessage::MessageType::EARLY_TRANSITION_TRIGGER) {
						xQueueReceive(screenQueue, &msg, 0);
						earlyTransitionRequested = true;
						processedMessage = true;
					} else if (msg.type == ScreenMessage::MessageType::STATUS_SYMBOLS) {
						xQueueReceive(screenQueue, &msg, 0);
						screen_tft::showStatusSymbols(msg.batteryPercent);
					}
				}
			}
			
			// Regular timed status updates
			if (currentTime - lastStatusUpdate >= STATUS_UPDATE_INTERVAL) {
				screen_tft::showStatusSymbols(power::getBatteryPercent());
				lastStatusUpdate = currentTime;
			}
			
			// WiFi status changes
			if (lastOnlineStatus != onlineStatus) {
				screen_tft::showStatusSymbols(power::getBatteryPercent());
				lastOnlineStatus = onlineStatus;
				
				// If we're showing payment QR code and connection status changed, refresh the screen
				if (currentScreen == "paymentQRCode") {
					screen_tft::showPaymentQRCodeScreen(lastQRCode);
					logger::write("[screen] Connection status changed, refreshing payment QR screen", "info");
				}
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

void showContrastInputScreen(const std::string &contrastInput) {
    ScreenMessage msg(ScreenMessage::MessageType::CONTRAST_INPUT);
    strncpy(msg.text, contrastInput.c_str(), sizeof(msg.text) - 1);
    msg.text[sizeof(msg.text) - 1] = '\0';
    xQueueSend(screenQueue, &msg, portMAX_DELAY);
}

void showSensitivityInputScreen(const std::string &sensitivityInput) {
    ScreenMessage msg(ScreenMessage::MessageType::SENSITIVITY_INPUT);
    strncpy(msg.text, sensitivityInput.c_str(), sizeof(msg.text) - 1);
    msg.text[sizeof(msg.text) - 1] = '\0';
    xQueueSend(screenQueue, &msg, portMAX_DELAY);
}

void triggerEarlyTransition() {
    ScreenMessage msg(ScreenMessage::MessageType::EARLY_TRANSITION_TRIGGER);
    xQueueSend(screenQueue, &msg, 0); // Don't block
}

}
