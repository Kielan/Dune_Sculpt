/* A platform-independent definition of [u]intXX_t
 * Plus the accompanying header include for htonl/ntohl
 *
 * This file includes <sys/types.h> to define [u]intXX_t types, where
 * XX can be 8, 16, 32 or 64. Unfortunately, not all systems have this
 * file.
 * - Windows uses __intXX compiler-builtin types. These are signed,
 *   so we have to flip the signs.
 * For these rogue platforms, we make the typedefs ourselves. */

#pragma once

#if defined(__linux__) || defined(__GNU__) || defined(__NetBSD__) || defined(__OpenBSD__) || \
    defined(__FreeBSD_kernel__) || defined(__HAIKU__)

/* Linux-i386, Linux-Alpha, Linux-PPC */
#  include <stdint.h>

#  ifndef UINT64_MAX
#    define UINT64_MAX 18446744073709551615
typedef uint8_t u_int8_t;
typedef uint16_t u_int16_t;
typedef uint32_t u_int32_t;
typedef uint64_t u_int64_t;
#  endif

#elif defined(__APPLE__)

#  include <inttypes.h>

/* MSVC >= 2010 */
#elif defined(_MSC_VER)
#  include <stdint.h>

#else

/* FreeBSD, Solaris */
#  include <stdint.h>
#  include <sys/types.h>

#endif /* ifdef platform for types */

#include <stdbool.h>
#include <stddef.h> /* size_t define */

/* The <uchar.h> standard header is missing on some systems. */
#  if defined(__APPLE__) || defined(__NetBSD__)
typedef unsigned int char32_t;
#  else
#    include <uchar.h>
#  endif
#endif

typedef unsigned int uint;
typedef unsigned short ushort;
typedef unsigned long ulong;
typedef unsigned char uchar;
#endif
