#include "config.h"

namespace {

	const std::map<const char*, const char*> defaultValues = {
		{"apiKey.key", ""},
		{"callbackUrl", "https://opago-pay.com/getstarted"},
		{"fiatCurrency", "EUR"},
		{"fiatPrecision", "2"},
		{"tftRotation", "3"},
		{"batteryMaxVolts", "4.2"},
		{"batteryMinVolts", "3.2"},
		{"contrastLevel", "75"},
		{"logLevel", "info"},
		{"wifiSSID",""},
		{"wifiPwd",""}
	};

	// https://arduinojson.org/v6/api/dynamicjsondocument/
	DynamicJsonDocument configJsonDoc(8192);
	JsonObject values;// current configuration values

	// Using Preferences library as a wrapper to Non-Volatile Storage (flash memory):
	// https://github.com/espressif/arduino-esp32/tree/master/libraries/Preferences
	// https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/storage/nvs_flash.html
	const char* nvs_namespace = "OPAGO_Config";
	const bool nvs_readonly = false;
	Preferences nvs_prefs;
	bool nvs_available = false;

	bool isConfigKey(const char* key) {
		if (values.containsKey(key)) {
			return true;
		}
		const auto pos = defaultValues.find(key);
		return pos != defaultValues.end();
	}

	bool setConfigValue(const char* key, const std::string &value) {
		if (isConfigKey(key)) {
			values[key] = value;
			return true;
		}
		return false;
	}

	std::string getConfigValue(const char* key) {
		if (values.containsKey(key)) {
			const std::string value = values[key].as<const char*>();
			if (key == "coinValueIncrement") {
				const unsigned short fiatPrecision = (unsigned short) std::stoi(getConfigValue("fiatPrecision"));
				return util::doubleToStringWithPrecision((double) std::stod(value), fiatPrecision);
			}
			return value;
		}
		return "";
	}

	bool initNVS() {
		const bool result = nvs_prefs.begin(nvs_namespace, nvs_readonly);
		if (result) {
			nvs_available = true;
		}
		return result;
	}

	void endNVS() {
		nvs_prefs.end();
		nvs_available = false;
	}

	// Maximum NVS key length is 15 characters.
	const unsigned short nvsKeyMaxLength = 15;

	std::string truncateNVSKey(const char* key) {
		return std::string(key).substr(0, nvsKeyMaxLength);
	}

	bool keyExistsInNVS(const char* t_key) {
		const std::string key = truncateNVSKey(t_key);
		return nvs_prefs.isKey(key.c_str());
	}

	std::string readValueFromNVS(const char* t_key) {
		const std::string key = truncateNVSKey(t_key);
		return std::string(nvs_prefs.getString(key.c_str(), "").c_str());
	}

	void saveKeyValueToNVS(const char* t_key, const std::string &value) {
		if (!keyExistsInNVS(t_key) || readValueFromNVS(t_key) != value) {
			const std::string key = truncateNVSKey(t_key);
			nvs_prefs.putString(key.c_str(), value.c_str());
		}
	}

	bool readFromNVS() {
		if (!nvs_available && !initNVS()) {
			return false;
		}
		for (auto const& defaultValue : defaultValues) {
			const char* key = defaultValue.first;
			if (keyExistsInNVS(key)) {
				setConfigValue(key, readValueFromNVS(key));
			} else {
				setConfigValue(key, defaultValue.second);
			}
		}
		return true;
	}
}

namespace config {

	void init() {
		values = configJsonDoc.createNestedObject("values");
		if (initNVS()) {
			std::cout << "Non-volatile storage initialized" << std::endl;
			if (!readFromNVS()) {
				std::cout << "Failed to read configurations from non-volatile storage" << std::endl;
			}
		} else {
			std::cout << "Failed to initialize non-volatile storage" << std::endl;
		}
		endNVS();
	}

	std::string getString(const char* key) {
		return getConfigValue(key);
	}

	unsigned int getUnsignedInt(const char* key) {
		const std::string value = getConfigValue(key);
		if (value != "") {
			return (unsigned int) std::stoi(value);
		}
		return 0;
	}

	unsigned short getUnsignedShort(const char* key) {
		const std::string value = getConfigValue(key);
		if (value != "") {
			return (unsigned short) std::stoi(value);
		}
		return 0;
	}

	float getFloat(const char* key) {
		const std::string value = getConfigValue(key);
		if (value != "") {
			return std::atof(value.c_str());
		}
		return 0;
	}

	std::vector<float> getFloatVector(const char* key) {
		return util::stringListToFloatVector(getConfigValue(key));
	}

	bool getBool(const char* key) {
		const std::string value = getConfigValue(key);
		return (value == "true" || value == "1");
	}

	JsonObject getConfigurations() {
		DynamicJsonDocument docConfigs(20000);
		for (JsonPair kv : values) {
			const char* key = kv.key().c_str();
			const std::string value = kv.value().as<const char*>();
			if (key == "apiKey.key") {
				docConfigs[key] = "XXX";
			} else {
				docConfigs[key] = value;
			}
		}
		return docConfigs.as<JsonObject>();
	}

	std::string getConfigurationsAsString() {
		std::string str = "OPAGO configurations:\n";
		for (JsonPair kv : values) {
			const char* key = kv.key().c_str();
			const std::string value = kv.value().as<const char*>();
			str += "  " + std::string(key) + "=";
			if (value != "") {
				if (key == "apiKey.key") {
					str += "XXX";
				} else {
					str += value;
				}
			}
			str += "\n";
		}
		str.pop_back();// remove last line-break character
		return str;
	}

	bool saveConfigurations(const JsonObject &configurations) {
		logger::write("Entering saveConfigurations function");
		if (!nvs_available && !initNVS()) {
			logger::write("NVS not available or failed to initialize");
			return false;
		}
		for (JsonPair kv : configurations) {
			const char* key = kv.key().c_str();
			if (isConfigKey(key)) {
				const std::string value = kv.value().as<const char*>();
				saveKeyValueToNVS(key, value);
				setConfigValue(key, value);
			}
		}
		endNVS();
		logger::write("Configurations saved successfully");
		return true;
	}

	bool saveConfiguration(const char* key, const std::string &value) {
		if (!nvs_available && !initNVS()) {
			return false;
		}
		if (isConfigKey(key)) {
			saveKeyValueToNVS(key, value);
			setConfigValue(key, value);
		}
		endNVS();
		return true;
	}
}
