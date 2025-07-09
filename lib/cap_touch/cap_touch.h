#ifndef CAP_TOUCH_H
#define CAP_TOUCH_H

#include <Arduino.h>
#include <algorithm>
#include "config.h"
#include "logger.h"
#include "Adafruit_MPR121.h"

extern Adafruit_MPR121 cap;

// Structure for rolling window analysis in RF-safe mode
struct RollingWindowData {
    static const int WINDOW_SIZE = 30;  // Number of samples in 300ms window (10ms sampling)
    char keyHistory[WINDOW_SIZE];       // History of detected keys
    unsigned long timeHistory[WINDOW_SIZE];  // Timestamps of detections
    int currentIndex;                   // Current position in circular buffer
    bool windowFull;                    // Whether we have a full window of data
    
    RollingWindowData() : currentIndex(0), windowFull(false) {
        for (int i = 0; i < WINDOW_SIZE; i++) {
            keyHistory[i] = 0;
            timeHistory[i] = 0;
        }
    }
    
    void addSample(char key, unsigned long timestamp) {
        keyHistory[currentIndex] = key;
        timeHistory[currentIndex] = timestamp;
        currentIndex = (currentIndex + 1) % WINDOW_SIZE;
        if (currentIndex == 0) windowFull = true;
    }
    
    float getKeyProbability(char targetKey, unsigned long currentTime) {
        const unsigned long WINDOW_DURATION_MS = 300;  // 300ms window
        int validSamples = 0;
        int targetKeyCount = 0;
        
        // Count samples within the time window
        for (int i = 0; i < (windowFull ? WINDOW_SIZE : currentIndex); i++) {
            if (currentTime - timeHistory[i] <= WINDOW_DURATION_MS) {
                validSamples++;
                if (keyHistory[i] == targetKey) {
                    targetKeyCount++;
                }
            }
        }
        
        if (validSamples == 0) return 0.0f;
        return (float)targetKeyCount / (float)validSamples;
    }
};

// Function declarations
void cap_touch_init();
void updateSensitivity(int sensitivityPercent);
std::string getTouch();
bool getLongTouch(char targetButton, unsigned long longTouchTime = 210);
bool getDebouncedLongTouch(char targetButton, unsigned long longTouchTime = 210);
void suppressTouchDuringNFC(bool suppress);
void setRFSafeMode(bool enable);
void setPinEntryMode(bool enable);

#endif // CAP_TOUCH_H