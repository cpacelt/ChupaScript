/* ChupaScript public C API.
 *
 * Placeholder: only the version query exists so the skeleton links and runs.
 * The real API goes here. */
#ifndef CHUPASCRIPT_H
#define CHUPASCRIPT_H

#ifdef __cplusplus
extern "C" {
#endif

#define CHUPASCRIPT_VERSION_MAJOR 0
#define CHUPASCRIPT_VERSION_MINOR 1
#define CHUPASCRIPT_VERSION_PATCH 0

/* Returns the library version as "major.minor.patch". Never null. */
const char *chupascript_version(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* CHUPASCRIPT_H */
