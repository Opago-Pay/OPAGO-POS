#ifndef OPAGO_I18N_H
#define OPAGO_I18N_H

#include "config.h"

#include <map>
#include <string>

namespace i18n {
	typedef std::map<const char*, const char*, util::MapCharPointerComparator> Locale;
	std::string t(const char* key);
	std::string t(const char* key, const char* locale);
	std::string getSupportedLocales();
}

#include "cs.h"
#include "da.h"
#include "de.h"
#include "en.h"
#include "es.h"
#include "fi.h"
#include "fr.h"
#include "hr.h"
#include "it.h"
#include "nl.h"
#include "no.h"
#include "pl.h"
#include "pt.h"
#include "ro.h"
#include "sl.h"
#include "sk.h"
#include "sv.h"

#endif
