#include "cap_touch.h"

std::string getTouch() {
    uint16_t currtouched = 0;
    char button_values[12] = {'1', '2', '3', '4', '5', '6', '7', '8', '9', '*', '0', '#'}; // Map buttons to their values
    unsigned long debounceTime = 50; // Increased debounce time in milliseconds
    unsigned long startTime = millis(); // Record the start time of the function

    const int numSamples = 210; 
    uint16_t touchSamples[numSamples] = {0};
    int sampleIndex = 0;

    while (millis() - startTime < numSamples) { // Loop for 100ms
        currtouched = cap.touched();
        touchSamples[sampleIndex] = currtouched;
        sampleIndex = (sampleIndex + 1) % numSamples;

        // Check if a button is released
        if (currtouched == 0 && sampleIndex > 0) {
            break;
        }

        vTaskDelay(1 / portTICK_PERIOD_MS / 2); 
    }

    // Calculate the most likely touched button
    int touchCounts[12] = {0};
    bool touchedButtons[12] = {false}; // Track which buttons were touched
    int differentButtonsTouched = 0; // Count of different buttons touched

    for (int i = 0; i < sampleIndex; i++) {
        for (uint8_t j = 0; j < 12; j++) {
            if (touchSamples[i] & _BV(j)) {
                touchCounts[j]++;
                if (!touchedButtons[j]) {
                    touchedButtons[j] = true;
                    differentButtonsTouched++;
                }
            }
        }
    }

    // If more than three different buttons were touched, ignore the output
    if (differentButtonsTouched > 3) {
        Serial.println("Interference detected, ignoring touch input");
        return "";
    }

    int maxCount = 0;
    int mostLikelyButton = -1;
    for (int i = 0; i < 12; i++) {
        if (touchCounts[i] > maxCount) {
            maxCount = touchCounts[i];
            mostLikelyButton = i;
        }
    }

    if (mostLikelyButton != -1) {
        Serial.print("Button "); Serial.print(button_values[mostLikelyButton]); Serial.println(" detected");
        return std::string(1, button_values[mostLikelyButton]);
    }

    return ""; // If no button is detected, return an empty string
}

bool getLongTouch(char targetButton, unsigned long longTouchTime) {
    uint16_t currtouched = 0;
    char button_values[12] = {'1', '2', '3', '4', '5', '6', '7', '8', '9', '*', '0', '#'}; // Map buttons to their values
    unsigned long startTime = millis(); // Record the start time of the function

    const int numSamples = longTouchTime / 10; // Number of samples to average over longTouchTime
    uint16_t touchSamples[numSamples] = {0};
    int sampleIndex = 0;

    while (millis() - startTime < longTouchTime) { // Loop for the duration of longTouchTime
        currtouched = cap.touched();
        touchSamples[sampleIndex] = currtouched;
        sampleIndex = (sampleIndex + 1) % numSamples;
        vTaskDelay(10 / portTICK_PERIOD_MS); // Delay for 10 ms
    }

    // Calculate the most likely touched button
    int touchCounts[12] = {0};
    bool touchedButtons[12] = {false}; // Track which buttons were touched
    int differentButtonsTouched = 0; // Count of different buttons touched

    for (int i = 0; i < numSamples; i++) {
        for (uint8_t j = 0; j < 12; j++) {
            if (touchSamples[i] & _BV(j)) {
                touchCounts[j]++;
                if (!touchedButtons[j]) {
                    touchedButtons[j] = true;
                    differentButtonsTouched++;
                }
            }
        }
    }

    // If more than three different buttons were touched, ignore the output
    if (differentButtonsTouched > 3) {
        Serial.println("Interference detected, ignoring long touch input");
        return false;
    }

    int maxCount = 0;
    int mostLikelyButton = -1;
    for (int i = 0; i < 12; i++) {
        if (touchCounts[i] > maxCount) {
            maxCount = touchCounts[i];
            mostLikelyButton = i;
        }
    }

    // Check if the most likely button is the target button and if it meets the 95% threshold
    if (mostLikelyButton != -1 && button_values[mostLikelyButton] == targetButton) {
        float requiredCount = numSamples * 0.9;
        if (touchCounts[mostLikelyButton] >= requiredCount) {
            Serial.print("Button "); Serial.print(button_values[mostLikelyButton]); Serial.println(" long touched");
            return true;
        }
    }

    return false; // If no button press is detected with high likelihood, return false
}