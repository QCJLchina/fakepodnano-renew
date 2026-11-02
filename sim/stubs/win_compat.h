/**
 * win_compat.h — small POSIX→MSVC shims for the firmware sources.
 *
 * Force-included (MSVC /FI) into firmware/main/ui/*.cpp so views written for
 * ESP-IDF (newlib/POSIX) compile unmodified on Windows. Only mappings that are
 * 100% behaviour-equivalent are provided; nothing here changes rendering.
 */
#pragma once

#if defined(_MSC_VER)

#include <time.h>
#include <string.h>

/* ESP-IDF newlib has localtime_r/gmtime_r; MSVC only has the _s variants. */
static __inline struct tm *sim_localtime_r(const time_t *timer, struct tm *result)
{
    if (!timer || !result) return NULL;
    return (localtime_s(result, timer) == 0) ? result : NULL;
}

static __inline struct tm *sim_gmtime_r(const time_t *timer, struct tm *result)
{
    if (!timer || !result) return NULL;
    return (gmtime_s(result, timer) == 0) ? result : NULL;
}

#ifndef localtime_r
#define localtime_r(t, res) sim_localtime_r((t), (res))
#endif
#ifndef gmtime_r
#define gmtime_r(t, res) sim_gmtime_r((t), (res))
#endif

#ifndef strcasecmp
#define strcasecmp(a, b) _stricmp((a), (b))
#endif
#ifndef strncasecmp
#define strncasecmp(a, b, n) _strnicmp((a), (b), (n))
#endif
#ifndef strdup
#define strdup(s) _strdup((s))
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#endif /* _MSC_VER */
