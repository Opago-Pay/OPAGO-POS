#ifndef OPAGO_KEYPAD_H
#define OPAGO_KEYPAD_H

#include "config.h"
#include "cap_touch.h"
#include <string>
#include <vector>

class Keypad;  // Forward declaration

namespace keypad {
	void init();
	void loop();
	std::string getPressedKey();
}

#endif
