#include "Keypad.h"
#include "keypad.h"

namespace {
	enum class State {
		uninitialized,
		initialized,
		failed
	};
	State state = State::uninitialized;

	std::string keypadCharList;
	Keypad* keypadInstance;
	char* userKeymap;
}

namespace keypad {

	void init() {
		state = State::initialized;  // we assume the keypad is always available
	}

	void loop() {
		if (state == State::uninitialized) {
			logger::write("Initializing keypad...");
			state = State::initialized;  // we assume the keypad is always available
		}
	}

	std::string getPressedKey() {
		// Use getTouch() to get the currently pressed key. 
		std::string key = getTouch();
		return key;
	}
}
