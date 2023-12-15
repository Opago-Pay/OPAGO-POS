#ifndef CONFIG_H
#define CONFIG_H

//#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>

#include "logger.h"
#include "util.h"
#include "lnurl.h"

#include <algorithm>
#include <string>
#include <fstream>
#include <sstream>
#include <vector>

namespace config {
	void init();
	std::string getString(const char* key);
	unsigned int getUnsignedInt(const char* key);
	unsigned short getUnsignedShort(const char* key);
	float getFloat(const char* key);
	std::vector<float> getFloatVector(const char* key);
	bool getBool(const char* key);
	JsonObject getConfigurations();
	std::string getConfigurationsAsString();
	bool saveConfigurations(const JsonObject &configurations);
	bool saveConfiguration(const char* key, const std::string &value);
}

#endif
