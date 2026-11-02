/* emulibc.h - native build shim. The native reference compiles the same cinterface.c as the
 * guest. Here the waterbox allocation and visibility API becomes plain C. The guest build
 * never sees this file: its include path has miniBox's emulibc first.
 */
#ifndef _EMULIBC_H
#define _EMULIBC_H

#include <stdlib.h>

#define ECL_ENTRY
#define ECL_EXPORT __attribute__((used))
#define ECL_SEALED
#define ECL_INVISIBLE

static inline void *alloc_sealed(size_t size) { return calloc(size, 1); }
static inline void *alloc_invisible(size_t size) { return calloc(size, 1); }
static inline void *alloc_plain(size_t size) { return calloc(size, 1); }

#endif