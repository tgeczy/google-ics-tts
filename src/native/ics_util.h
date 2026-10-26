/* ics_util.h -- file access by UTF-8 path. */
#ifndef ICS_UTIL_H
#define ICS_UTIL_H

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

FILE *ics_fopen(const char *utf8_path, const char *mode);
int ics_remove(const char *utf8_path);

#ifdef __cplusplus
}
#endif

#endif
