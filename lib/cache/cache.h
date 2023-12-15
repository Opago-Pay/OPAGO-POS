#ifndef CACHE_H
#define CACHE_H

#include <Preferences.h>
#include <string>

namespace cache {
	void init();
	void end();
	std::string getString(const char* key);
	void save(const char* key, const std::string &value);
}

#endif
