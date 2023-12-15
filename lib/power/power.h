#ifndef POWER_H
#define POWER_H

#include "logger.h"
#include "driver/rtc_io.h"
#include <cmath>
#include <string>

namespace power {
	void init();
	void loop();
	bool isUSBPowered();
	void sleep();
	int getBatteryPercent(const bool &force = false);
}

#endif
