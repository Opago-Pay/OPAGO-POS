//  lnurl-platformio - https://github.com/chill117/lnurl-platformio
//
//  Copyright (C) 2020 Charles Hill
//
//  This program is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program.  If not, see <https://www.gnu.org/licenses/>.

#include "lnurl.h"

namespace {

	const std::string encodeUriComponentSafeChars = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_.!~*'()";

	const std::map<std::string, std::string> shortenKeysLookupTable {
		{ "id", "id" },
		{ "nonce", "n" },
		{ "signature", "s" },
		{ "tag", "t" },
		{ "minWithdrawable", "pn" },
		{ "maxWithdrawable", "px" },
		{ "defaultDescription", "pd" }
	};

	const std::map<std::string, std::string> shortenTagsLookupTable {
		{ "withdrawRequest", "w" }
	};

	std::string string_to_hex(const std::string& in) {
		std::stringstream ss;
		ss << std::hex << std::setfill('0');
		for (size_t i = 0; in.length() > i; ++i) {
			ss << std::setw(2) << static_cast<unsigned int>(static_cast<unsigned char>(in[i]));
		}
		return ss.str();
	}
}

namespace Lnurl {

	const std::string hrp = "lnurl";

	std::string encode(const std::string &unencoded) {
		std::vector<uint8_t> v = ParseHex(string_to_hex(unencoded));
		std::vector<unsigned char> tmp = {};
		tmp.reserve(1 + 32 * 8 / 5);
		ConvertBits<8, 5, true>([&](unsigned char c) { tmp.push_back(c); }, v.begin(), v.end());
		return bech32::Encode(hrp, tmp);
	}

	std::string decode(const std::string &encoded) {
		const std::pair<std::string, std::vector<uint8_t>> decoded = bech32::Decode(encoded);
		std::vector<unsigned char> ret;
		ret.reserve((decoded.second.size() * 5) / 8);
		ConvertBits<5, 8, false>([&](unsigned char c) { ret.push_back(c); }, decoded.second.begin(), decoded.second.end());
		return std::string(ret.begin(), ret.end());
	}

	Signer::Signer(SignerConfig t_config) {
		set_config(t_config);
	};

	void Signer::set_config(SignerConfig t_config) {
		// Encoding equal to "hex", "base64", or "" (unspecified) are permitted.
		if (
			t_config.apiKey.encoding != "hex" &&
			t_config.apiKey.encoding != "base64" &&
			t_config.apiKey.encoding != ""
		) {
			throw std::invalid_argument("Invalid config (\"apiKey.encoding\"): Unsupported encoding - \"hex\", \"base64\", or \"\" are permitted.");
		}
		config = t_config;
	};

	// Same functionality as JavaScript's encodeURIComponent.
	// Keeps alphanumeric (and a few other) characters intact (NOT encoded). See:
	// https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/encodeURIComponent#description
	std::string Signer::url_encode(const std::string &value) {
		std::ostringstream escaped;
		escaped.fill('0');
		escaped << std::hex;
		for (std::string::const_iterator i = value.begin(), n = value.end(); i != n; ++i) {
			std::string::value_type c = (*i);
			// Keep alphanumeric and other accepted characters intact.
			if (encodeUriComponentSafeChars.find(c) != std::string::npos) {
				escaped << c;
				continue;
			}
			// Any other characters are percent-encoded.
			escaped << std::uppercase;
			escaped << '%' << std::setw(2) << int((unsigned char) c);
			escaped << std::nouppercase;
		}
		return escaped.str();
	};

	bool Signer::is_reserved_param_key(const std::string &paramKey) {
		for (auto const &it : shortenKeysLookupTable) {
			std::string key = it.first;
			std::string value = it.second;
			if (paramKey == key || paramKey == value) {
				return true;
			}
		}
		return false;
	};

	std::string Signer::create_signature(const std::string &t_data) {
		std::vector<unsigned char> data = ParseHex(string_to_hex(t_data));
		std::vector<unsigned char> key;
		if (config.apiKey.encoding == "hex") {
			// Hex-encoded key.
			key = ParseHex(config.apiKey.key);
		} else if (config.apiKey.encoding == "base64") {
			// Base64 encoded key.
			key = ParseHex(string_to_hex(DecodeBase64(config.apiKey.key)));
		} else if (config.apiKey.encoding == "") {
			// Unspecified encoding.
			key = ParseHex(string_to_hex(config.apiKey.key));
		} else {
			// This should never happen.
			throw std::logic_error("Invalid config (\"apiKey.encoding\"): Unsupported encoding - \"hex\", \"base64\", or \"\" are permitted.");
		}
		std::vector<unsigned char> signature;
		signature.resize(32);
		CHMAC_SHA256 hasher = CHMAC_SHA256(key.data(), key.size());
		hasher.Write((unsigned char*)&data[0], data.size());
		hasher.Finalize(&signature[0]);
		return HexStr(signature);
	};

	Lnurl::Query Signer::shorten_query(const Lnurl::Query &query) {
		Lnurl::Query shortenedQuery;
		for (auto const &it : query) {
			std::string key = it.first;
			std::string value = it.second;
			if (key == "tag") {
				const auto shortTag = shortenTagsLookupTable.find(value);
				if (shortTag != shortenTagsLookupTable.end()) {
					value = shortTag->second;
				}
			}
			const auto shortKey = shortenKeysLookupTable.find(key);
			if (shortKey != shortenKeysLookupTable.end()) {
				key = shortKey->second;
			}
			shortenedQuery[key] = value;
		}
		return shortenedQuery;
	};

	std::string Signer::stringify_query(const Lnurl::Query &query) {
		std::string str = "";
		// The default behavior of std::map is to sort alphabetically on the first element.
		// In this case the first element is the query parameter's key.
		// No extra sorting is required here.
		for (auto const &it : query) {
			std::string key = it.first;
			std::string value = it.second;
			str += "&";
			str += url_encode(key);
			str += "=";
			str += url_encode(value);
		}
		// Erase the first delimiter.
		if (str != "") {
			str.erase(0, 1);
		}
		return str;
	};

	void Signer::sign_query(Lnurl::Query &query) {
		// The query object should be stringified in a standardized way.
		// This is needed to ensure consistent signing between device and server.
		const std::string payload = stringify_query(query);
		const std::string signature = create_signature(payload);
		query["signature"] = signature;
	};

	std::string Signer::create_url(const Lnurl::Query &t_query) {
		std::string url = "";
		url += config.callbackUrl;
		Lnurl::Query query = t_query;
		sign_query(query);
		if (config.shorten) {
			query = shorten_query(query);
		}
		url += "?" + stringify_query(query);
		return url;
	};

	std::string Signer::create_url(const Lnurl::WithdrawParams &params, const std::string &nonce) {
		if (nonce == "") {
			throw std::invalid_argument("Missing required argument: \"nonce\"");
		}
		if (params.minWithdrawable == "") {
			throw std::invalid_argument("Missing required parameter: \"minWithdrawable\"");
		}
		if (params.maxWithdrawable == "") {
			throw std::invalid_argument("Missing required parameter: \"maxWithdrawable\"");
		}
		const double minWithdrawable = std::stod(params.minWithdrawable);
		const double maxWithdrawable = std::stod(params.maxWithdrawable);
		if (std::isnan(minWithdrawable)) {
			throw std::invalid_argument("\"minWithdrawable\" must be a number");
		}
		if (std::isnan(maxWithdrawable)) {
			throw std::invalid_argument("\"maxWithdrawable\" must be a number");
		}
		if (minWithdrawable < 0) {
			throw std::invalid_argument("\"minWithdrawable\" must be greater than zero");
		}
		if (maxWithdrawable < minWithdrawable) {
			throw std::invalid_argument("\"maxWithdrawable\" must be greater than or equal to \"minWithdrawable\"");
		}
		Lnurl::Query query;
		query["id"] = config.apiKey.id;
		query["tag"] = "withdrawRequest";
		query["nonce"] = nonce;
		query["minWithdrawable"] = params.minWithdrawable;
		query["maxWithdrawable"] = params.maxWithdrawable;
		query["defaultDescription"] = params.defaultDescription;
		for (auto const &it : params.custom) {
			std::string customKey = it.first;
			std::string customValue = it.second;
			if (is_reserved_param_key(customKey)) {
				throw std::invalid_argument("Invalid custom parameter key (\"" + customKey + "\"): Reserved");
			}
			query[customKey] = customValue;
		}
		return create_url(query);
	};
}
