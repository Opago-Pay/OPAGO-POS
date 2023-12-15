#ifndef LNPOSMAIN_H
#define LNPOSMAIN_H

#include "Adafruit_MPR121.h"
#include <TFT_eSPI.h>

extern Adafruit_MPR121 cap;
extern TFT_eSPI tft;

char getTouch();

#endif