#include "cap_touch.h"

std::string getTouch() {
    uint16_t lasttouched = 0;
    uint16_t currtouched = 0;
    int8_t touched = -1; // Initialize to -1 to indicate no button is touched
    char button_values[12] = {'1', '2', '3', '4', '5', '6', '7', '8', '9', '*', '0', '#'}; // Map buttons to their values
    unsigned long debounceTime = 21; // Debounce time in milliseconds
    unsigned long lastDebounceTime = 0; // the last time the output pin was toggled

    int counter = 0;  // Counter to track the number of cycles without a button press

    while (counter < 1000) { // Loop until a button is pressed and released or the counter reaches the threshold
        lasttouched = currtouched;
        currtouched = cap.touched();
        if (currtouched) {
            counter = 0;  // Reset the counter if a button is touched
        }

        for (uint8_t i = 0; i < 12; i++) {
            // if it *is* touched and *wasn't* touched before
            if ((currtouched & _BV(i)) && !(lasttouched & _BV(i))) {
                if (touched == -1) { // If no button is currently being tracked
                    if ((millis() - lastDebounceTime) > debounceTime) { // If the button has been pressed for longer than the debounce time
                        touched = i;
                        Serial.print("Button "); Serial.print(button_values[i]); Serial.println(" touched");
                        lastDebounceTime = millis();  // Update the last debounce time
                    }
                } else { // Another button was touched before the first one was released
                    touched = -1; // Reset the tracking
                    break; // Exit the loop
                }
            }
        }
        
        // If no button is currently touched and we have a tracked button
        if (currtouched == 0 && touched != -1) {
            if ((millis() - lastDebounceTime) > debounceTime) { // If the button has been released for longer than the debounce time
                Serial.print("Button "); Serial.print(button_values[touched]); Serial.println(" released");
                return std::string(1, button_values[touched]);
            }
        }
        
        counter++;  // Increment the counter
        delay(1); // Delay for 1 ms to match the counter threshold with the debounce time
    }
    // If the counter reaches the threshold, return an empty string
    return "";  
}

bool getLongTouch(char targetButton, unsigned long longTouchTime) {
    uint16_t lasttouched = 0;
    uint16_t currtouched = 0;
    int8_t touched = -1; // Initialize to -1 to indicate no button is touched
    char button_values[12] = {'1', '2', '3', '4', '5', '6', '7', '8', '9', '*', '0', '#'}; // Map buttons to their values
    unsigned long lastDebounceTime = 0; // the last time the output pin was toggled
    unsigned long startTime = millis(); // Record the start time of the function

    currtouched = cap.touched(); // Check if a button is already pressed when the function is called
    for (uint8_t i = 0; i < 12; i++) {
        if (currtouched & _BV(i)) { // If a button is already pressed
            touched = i;
            Serial.print("Button "); Serial.print(button_values[i]); Serial.println(" touched");
            lastDebounceTime = millis();  // Update the last debounce time
            break;
        }
    }

    while (millis() - startTime <= 420) { // Loop to detect a button press
        lasttouched = currtouched;
        currtouched = cap.touched();

        for (uint8_t i = 0; i < 12; i++) {
            // if it *is* touched and *wasn't* touched before
            if ((currtouched & _BV(i)) && !(lasttouched & _BV(i))) {
                if (touched == -1) { // If no button is currently being tracked
                    touched = i;
                    Serial.print("Button "); Serial.print(button_values[i]); Serial.println(" touched");
                    lastDebounceTime = millis();  // Update the last debounce time
                } else { // Another button was touched before the first one was released
                    touched = -1; // Reset the tracking
                    break; // Exit the loop
                }
            }
        }
        
        // If a button is currently touched and we have a tracked button
        if (currtouched != 0 && touched != -1) {
            if ((millis() - lastDebounceTime) >= longTouchTime) { // If the button has been pressed for longer than the long touch time
                Serial.print("Button "); Serial.print(button_values[touched]); Serial.println(" long touched");
                return button_values[touched] == targetButton;
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(1)); // Delay for 1 ms to match the counter threshold with the debounce time
    }
    return false; // If no button press is detected within the first 210ms, return false
}