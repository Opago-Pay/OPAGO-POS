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

#ifndef LNURL_H
#define LNURL_H

#include "bech32.h"
#include "crypto/hmac_sha256.h"
#include "util/strencodings.h"

#include <cmath>
#include <iomanip>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>

namespace Lnurl {

	std::string encode(const std::string &unencoded);
	std::string decode(const std::string &encoded);

	typedef std::map<std::string, std::string> Query;

	struct SignerApiKey {
		std::string id;
		std::string key;
		std::string encoding = "";
	};

	struct SignerConfig {
		struct SignerApiKey apiKey;
		std::string callbackUrl;
		bool shorten = false;
	};

	struct Params {
		std::string nonce = "";
		Query custom;
	};

	struct WithdrawParams : Params {
		std::string minWithdrawable;
		std::string maxWithdrawable;
		std::string defaultDescription = "";
	};

	class Signer {
		private:
			Lnurl::SignerConfig config;
			void set_config(Lnurl::SignerConfig t_config);
			bool is_reserved_param_key(const std::string &paramKey);
			Lnurl::Query shorten_query(const Lnurl::Query &query);
			std::string stringify_query(const Lnurl::Query &query);
			void sign_query(Lnurl::Query &query);
		public:
			Signer(Lnurl::SignerConfig t_config);
			std::string url_encode(const std::string &value);
			std::string create_signature(const std::string &t_data);
			std::string create_url(const Lnurl::Query &t_query);
			std::string create_url(const Lnurl::WithdrawParams &params, const std::string &nonce = "");
	};
}

#endif
