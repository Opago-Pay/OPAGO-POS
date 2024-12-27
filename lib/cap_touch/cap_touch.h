#ifndef CAP_TOUCH_H
#define CAP_TOUCH_H

#include <Arduino.h>
#include <Wire.h>
#include "Adafruit_MPR121.h"

extern Adafruit_MPR121 cap;

// Function declarations
void cap_touch_init();
void updateSensitivity(int sensitivityPercent);
std::string getTouch();
bool getLongTouch(char targetButton, unsigned long longTouchTime);

#endif // CAP_TOUCH_H