#ifndef TTR_CORE_TEXT_H
#define TTR_CORE_TEXT_H

#include "core/str.h"

namespace ttr {

/* Convert UTF-8 to ISO 8859-1 for core X fonts. Characters outside
 * Latin-1 and malformed sequences become '?'. */
Str utf8_to_latin1(const char* s, int len);

}

#endif
