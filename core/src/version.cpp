// Provides the chupa_version() C API function backed by the version macros
// in the public header.
#include "chupascript/chupascript.h"

#define CHUPASCRIPT_STR_(x) #x
#define CHUPASCRIPT_STR(x) CHUPASCRIPT_STR_(x)

const char *chupa_version(void) {
    return CHUPASCRIPT_STR(CHUPASCRIPT_VERSION_MAJOR) "."
           CHUPASCRIPT_STR(CHUPASCRIPT_VERSION_MINOR) "."
           CHUPASCRIPT_STR(CHUPASCRIPT_VERSION_PATCH);
}
