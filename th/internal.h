/* th's helpers shared between its files; not for programs. */
#ifndef TH_INTERNAL_H
#define TH_INTERNAL_H

#include "th.h"

namespace th {

void trim(char* s);
void copy_str(char* dst, const char* src, int size);
/* Every file ending in 'ext' in the folders for 'sub' ("themes": the
 * user's ~/.config/ttr/themes, then data/schemes/ttr and irix; "schemes":
 * ~/.config/ttr/schemes, then data/schemes/text), added to 'out' under
 * the name of their "name:" line or file name, skipping names already
 * listed. */
void scan(const char* kind, List* out);

} /* namespace th */

#endif
