/* Generated from sodium/version.h.in (libsodium 1.0.20-RELEASE) with:
 *   VERSION=1.0.20, SODIUM_LIBRARY_VERSION_MAJOR=26,
 *   SODIUM_LIBRARY_VERSION_MINOR=2, not minimal.
 * Normally produced by ./configure; generated here by hand because only the
 * public headers are vendored (see third_party/libsodium/README.md).
 */
#ifndef sodium_version_H
#define sodium_version_H

#include "export.h"

#define SODIUM_VERSION_STRING "1.0.20"

#define SODIUM_LIBRARY_VERSION_MAJOR 26
#define SODIUM_LIBRARY_VERSION_MINOR 2


#ifdef __cplusplus
extern "C" {
#endif

SODIUM_EXPORT
const char *sodium_version_string(void);

SODIUM_EXPORT
int         sodium_library_version_major(void);

SODIUM_EXPORT
int         sodium_library_version_minor(void);

SODIUM_EXPORT
int         sodium_library_minimal(void);

#ifdef __cplusplus
}
#endif

#endif
