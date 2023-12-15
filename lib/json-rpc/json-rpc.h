#ifndef JSON_RPC_H
#define JSON_RPC_H

#include "config.h"
#include "i18n.h"
#include "logger.h"
#include "opago_main.h"
#include "spiffs.h"

#include "ArduinoJson.h"
#include <stdexcept>
#include <string>

namespace jsonRpc {
	void init();
	void loop();
	bool inUse();
	bool hasPinConflict();
}

#endif
