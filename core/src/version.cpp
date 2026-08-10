// Placeholder translation unit: gives the core library something to compile
// until real sources land. Safe to delete then.
#include "chupascript/chupascript.h"

#define CHUPASCRIPT_STR_(x) #x
#define CHUPASCRIPT_STR(x) CHUPASCRIPT_STR_(x)

const char *chupascript_version(void) {
    return CHUPASCRIPT_STR(CHUPASCRIPT_VERSION_MAJOR) "."
           CHUPASCRIPT_STR(CHUPASCRIPT_VERSION_MINOR) "."
           CHUPASCRIPT_STR(CHUPASCRIPT_VERSION_PATCH);
}
