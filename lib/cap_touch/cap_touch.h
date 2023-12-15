#ifndef CAP_TOUCH_H
#define CAP_TOUCH_H

#include "Adafruit_MPR121.h"
#include <TFT_eSPI.h>

extern Adafruit_MPR121 cap;
extern TFT_eSPI tft;

std::string getTouch();
bool getLongTouch(char targetButton, unsigned long longTouchTime = 42);

#endif